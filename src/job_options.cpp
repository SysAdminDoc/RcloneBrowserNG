#include "job_options.h"
#include "rclone_number.h"
#include "utils.h"
#include <qexception.h>
#include <qlogging.h>

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
    const qsizetype equals = arg.indexOf(QLatin1Char('='));
    // A short flag cluster gives its = value to the letter just before it,
    // so -vn=false sets -v and turns the dry run off.
    const bool shortDryRunValue =
        equals >= 2 && arg.startsWith(QLatin1Char('-')) &&
        !arg.startsWith(QLatin1String("--")) &&
        arg.at(equals - 1) == QLatin1Char('n');
    if (dropCaps && kCaps.contains(arg)) {
      args.remove(i, qMin<qsizetype>(2, args.size() - i));
    } else if (equals > 0 && ((dropCaps && kCaps.contains(name)) ||
                              name == QLatin1String("--dry-run"))) {
      // --dry-run=false would turn the flag added below back off.
      args.removeAt(i);
    } else if (shortDryRunValue && equals == 2) {
      args.removeAt(i); // -n=<value>
    } else if (shortDryRunValue) {
      args[i] = arg.left(equals - 1); // keep -v from -vn=<value>
      i++;
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
      // rclone reads it with Go's fmt.Sscanln (" 5" is 5, 010 is 8), and a
      // negative count means no cap (measured on v1.76.0).
      qint64 count = -1;
      caps.count = RcloneNumber::ScanInt(value, &count) && count >= 0 ? count
                                                                      : -1;
    } else if (name == kBytes) {
      caps.bytes = SizeSuffixBytes(value);
    }
  }
  return caps;
}

qint64 JobOptions::SizeSuffixBytes(const QString &text) {
  // fs/sizesuffix.go: 3k is 3072, 10 is 10240, 2b is 2, 10Ki, 10KI and
  // 10MiB work, 1_000 is 1024000, and 10MB, " 5k", NaNk and 8Ei are refused.
  qint64 bytes = -1;
  return RcloneNumber::ParseSize(text, &bytes) == RcloneNumber::SizeError::None
             ? bytes
             : -1;
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
