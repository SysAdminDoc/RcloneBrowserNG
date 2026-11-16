#include "job_options.h"
#include "utils.h"
#include <qexception.h>
#include <qlogging.h>

#include <limits>
#include <utility>
#ifdef _WIN32
#pragma warning(disable : 4505)
#endif

namespace {
QString backupDateToken() {
  static QAtomicInteger<quint64> sequence = 0;
  const quint64 seq = sequence.fetchAndAddRelaxed(1);
  const QString pid =
      QString::number(static_cast<quint64>(QCoreApplication::applicationPid()),
                      36);
  const QString seqPart = QString::number(seq, 36).rightJustified(4, '0');
  return QString("%1_%2_%3")
      .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss_zzz"),
           pid, seqPart);
}
} // namespace

JobOptions::JobOptions(bool isDownload) : JobOptions() {
  setJobType(isDownload);
  uniqueId = QUuid::createUuid();
}

JobOptions::JobOptions()
    : jobType(UnknownJobType), operation(UnknownOp), dryRun(false), sync(false),
      syncTiming(UnknownTiming), skipNewer(false), skipExisting(false),
      compare(false), compareOption(), verbose(false), sameFilesystem(false),
      dontUpdateModified(false), maxDepth(0), deleteExcluded(false),
      serverSideAcrossConfigs(false),
      isFolder(false), watchFolder(false) {}

const qint32 JobOptions::classVersion = 9;

JobOptions::~JobOptions() {}

QStringList JobOptions::getPreviewOptions() const {
  JobOptions preview(*this);
  preview.dryRun = true;
  preview.maxDelete = -1;
  // Extra options and the default options can carry a cap of their own.
  return DryRunArguments(preview.getOptions());
}

QStringList JobOptions::DryRunArguments(QStringList args) {
  // rclone checks both caps before it skips a dry run's delete, so either
  // one stops a sync preview partway; the deletion summary warns about the
  // cap instead. Bisync is different (measured on v1.76.0): its
  // --max-delete is a percentage that aborts before anything changes, and
  // its dry run stops at --max-delete-size on the same file the real run
  // does, so a bisync preview keeps both to show what the run will do.
  static const QStringList kCaps{QStringLiteral("--max-delete"),
                                 QStringLiteral("--max-delete-size")};
  const bool dropCaps = args.value(0) != QLatin1String("bisync");
  for (qsizetype i = 0; i < args.size();) {
    const QString &arg = args.at(i);
    const QString name = arg.section(QLatin1Char('='), 0, 0);
    if (dropCaps && kCaps.contains(arg)) {
      args.remove(i, qMin<qsizetype>(2, args.size() - i));
    } else if (arg.contains(QLatin1Char('=')) &&
               ((dropCaps && kCaps.contains(name)) ||
                name == QLatin1String("--dry-run") ||
                name == QLatin1String("-n"))) {
      // --dry-run=false would turn the flag added below back off.
      args.removeAt(i);
    } else {
      i++;
    }
  }
  if (!args.contains(QLatin1String("--dry-run")) &&
      !args.contains(QLatin1String("-n"))) {
    args.insert(args.isEmpty() ? 0 : 1, QStringLiteral("--dry-run"));
  }
  return args;
}

JobOptions::DeleteCaps JobOptions::DeleteCapsIn(const QStringList &args) {
  static const QString kCount = QStringLiteral("--max-delete");
  static const QString kBytes = QStringLiteral("--max-delete-size");
  DeleteCaps caps;
  for (qsizetype i = 0; i < args.size(); i++) {
    const QString &arg = args.at(i);
    QString name = arg;
    QString value;
    const qsizetype equals = arg.indexOf(QLatin1Char('='));
    if (equals > 0) {
      name = arg.left(equals);
      value = arg.mid(equals + 1);
    } else if ((arg == kCount || arg == kBytes) && i + 1 < args.size()) {
      value = args.at(++i);
    } else {
      continue;
    }
    if (name == kCount) {
      bool ok = false;
      const qint64 count = value.toLongLong(&ok);
      caps.count = ok && count >= 0 ? count : -1;
    } else if (name == kBytes) {
      caps.bytes = SizeSuffixBytes(value);
    }
  }
  return caps;
}

qint64 JobOptions::SizeSuffixBytes(const QString &text) {
  // fs/sizesuffix.go, checked on v1.76.0 through options/get: 3k is 3072,
  // 10 is 10240, 2b is 2, 10Ki, 10KI and 10MiB work, and 10MB and " 5k"
  // are refused.
  if (text.isEmpty() ||
      text.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0) {
    return -1;
  }
  auto bitsFor = [](QChar symbol) {
    const qsizetype at = QStringLiteral("kmgtpe").indexOf(symbol.toLower());
    return at < 0 ? -1 : 10 * static_cast<int>(at + 1);
  };
  const qsizetype size = text.size();
  const QChar last = text.back();
  int bits = 10;
  qsizetype suffix = 0;
  if ((last >= QLatin1Char('0') && last <= QLatin1Char('9')) ||
      last == QLatin1Char('.')) {
    // A bare number is KiB.
  } else if (last == QLatin1Char('b') || last == QLatin1Char('B')) {
    if (size > 2 && text.at(size - 2) == QLatin1Char('i')) {
      bits = bitsFor(text.at(size - 3));
      suffix = 3;
    } else {
      bits = 0;
      suffix = 1;
    }
  } else if (last == QLatin1Char('i') || last == QLatin1Char('I')) {
    bits = size > 1 ? bitsFor(text.at(size - 2)) : -1;
    suffix = 2;
  } else {
    bits = bitsFor(last);
    suffix = 1;
  }
  const QString number = text.left(size - suffix);
  bool ok = false;
  const double value = number.toDouble(&ok);
  if (bits < 0 || !ok || value < 0 || number.trimmed() != number) {
    return -1;
  }
  const double bytes = value * static_cast<double>(Q_INT64_C(1) << bits);
  return bytes >= 9.0e18 ? std::numeric_limits<qint64>::max()
                         : static_cast<qint64>(bytes);
}

/*
 * Turn the options held here into a string list for
 * use in the rclone command.
 *
 * This logic was originally in transfer_dialog.cpp.
 *
 * This needs to change whenever e.g. new options are
 * added to the dialog.
 */
QStringList JobOptions::getOptions() const {
  QStringList list;

  if (operation == Copy) {
    list << "copy";
  } else if (operation == Move) {
    list << "move";
  } else if (operation == Sync) {
    list << "sync";
  } else if (operation == Bisync) {
    list << "bisync";
    if (!conflictResolve.isEmpty() && conflictResolve != "none") {
      list << "--conflict-resolve" << conflictResolve;
    }
  }

  if (dryRun) {
    list << "--dry-run";
  }

  if (sync) {
    switch (syncTiming) {
    case During:
      list << "--delete-during";
      break;
    case After:
      list << "--delete-after";
      break;
    case Before:
      list << "--delete-before";
      break;
    default:
      break;
      ;
    }
  }

  if (operation == Sync && maxDelete >= 0) {
    list << "--max-delete" << QString::number(maxDelete);
  }

  if (skipNewer) {
    list << "--update";
  }
  if (skipExisting) {
    list << "--ignore-existing";
  }

  if (compare) {
    switch (compareOption) {
    case Checksum:
      list << "--checksum";
      break;
    case IgnoreSize:
      list << "--ignore-size";
      break;
    case SizeOnly:
      list << "--size-only";
      break;
    case ChecksumIgnoreSize:
      list << "--checksum"
           << "--ignore-size";
      break;
    default:
      break;
    }
  }

  // always verbose
  list << "--verbose";
  if (sameFilesystem) {
    list << "--one-file-system";
  }
  if (dontUpdateModified) {
    list << "--no-update-modtime";
  }

  // A task saved without these counts and timeouts (a hand-written
  // tasks.json) falls back to rclone's defaults, since an empty value or a
  // bare "s" stops rclone before it transfers anything.
  if (!transfers.isEmpty()) {
    list << "--transfers" << transfers;
  }
  if (!checkers.isEmpty()) {
    list << "--checkers" << checkers;
  }

  if (!bandwidth.isEmpty()) {
    list << "--bwlimit" << bandwidth;
  } else {
    list << GetGlobalBandwidthLimit();
  }
  if (!minSize.isEmpty()) {
    list << "--min-size" << minSize;
  }
  if (!minAge.isEmpty()) {
    list << "--min-age" << minAge;
  }
  if (!maxAge.isEmpty()) {
    list << "--max-age" << maxAge;
  }

  if (maxDepth != 0) {
    list << "--max-depth" << QString::number(maxDepth);
  }

  if (!connectTimeout.isEmpty()) {
    list << "--contimeout" << (connectTimeout + "s");
  }
  if (!idleTimeout.isEmpty()) {
    list << "--timeout" << (idleTimeout + "s");
  }
  if (!retries.isEmpty()) {
    list << "--retries" << retries;
  }
  if (!lowLevelRetries.isEmpty()) {
    list << "--low-level-retries" << lowLevelRetries;
  }

  if (deleteExcluded) {
    list << "--delete-excluded";
  }

  if (serverSideAcrossConfigs) {
    list << "--server-side-across-configs";
  }

  if (!excluded.isEmpty()) {
    for (const auto &line : excluded.split('\n')) {
      QString trimmed = line.trimmed();
      if (!trimmed.isEmpty()) {
        list << "--exclude" << trimmed;
      }
    }
  }

  if (!extra.isEmpty()) {
    list << SplitRcloneOptions(extra);
  }

  if (DriveSharedWithMe) {
    list << "--drive-shared-with-me";
  }

  if (!nameTransform.isEmpty()) {
    list << "--name-transform" << nameTransform;
  }

  if (!backupDir.isEmpty()) {
    QString dated = backupDir;
    if (dated.contains("{date}")) {
      dated.replace("{date}", backupDateToken());
    }
    list << "--backup-dir" << dated;
  }

  list << "--use-json-log";
  list << "--stats" << "1s";
  list << "--stats-file-name-length" << "0";

  QStringList defaultExcludes = GetDefaultExcludeList();
  if (!defaultExcludes.isEmpty()) {
    list << defaultExcludes;
  }

  QStringList defaultRcloneOptionsList = GetDefaultRcloneOptionsList();
  if (!defaultRcloneOptionsList.isEmpty()) {
    list << defaultRcloneOptionsList;
  }

  list << source;
  list << dest;

  return list;
}

SerializationException::SerializationException(QString msg)
    : QException(), Message(std::move(msg)) {}
