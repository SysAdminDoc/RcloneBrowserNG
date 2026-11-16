#include "job_options.h"
#include "bwlimit_timetable.h"
#include "rclone_number.h"
#include "utils.h"
#include <QDebug>
#include <QSet>
#include <qexception.h>
#include <qlogging.h>

#include <utility>
#ifdef _WIN32
#pragma warning(disable : 4505)
#endif

namespace {
// The values Go's strconv.ParseBool reads as false, which pflag uses for a
// bool flag's =value.
bool IsGoFalse(QStringView value) {
  return value == u"0" || value == u"f" || value == u"F" ||
         value == u"false" || value == u"FALSE" || value == u"False";
}

// Renames each ",global.<key>" option in arg to ",ignored.global.<key>".
// rclone's fspath.Parse ends a key only at = , or :, config.Get matches the
// key exactly, and a key rclone doesn't know is ignored (measured on
// v1.76.0), so the renamed option does nothing. An option always follows a
// comma, and a colon always comes after it, since Parse refuses a config
// string that ends before its path. Text with no colon after it is part of a
// folder name or a pattern ("D:/x/dst,global.dry_run=1"), and renaming it
// would point the dry run at another folder. Every remote inside a value is
// covered too, a union's list of upstreams included, which a parse of one
// path at a time would miss.
void IgnoreGlobalOption(QString &arg, const QString &key) {
  const QString needle = QStringLiteral(",global.") + key;
  for (qsizetype at = arg.indexOf(needle); at >= 0;
       at = arg.indexOf(needle, at + 1)) {
    const qsizetype end = at + needle.size();
    if (end < arg.size() &&
        (arg.at(end) == QLatin1Char('=') || arg.at(end) == QLatin1Char(',') ||
         arg.at(end) == QLatin1Char(':')) &&
        arg.indexOf(QLatin1Char(':'), end) >= 0) {
      arg.insert(at + 1, QStringLiteral("ignored."));
    }
  }
}

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
  // These take the next argument as a pattern, and a pattern can look like
  // a flag without being one.
  static const QStringList kPatternFlags{QStringLiteral("--exclude"),
                                         QStringLiteral("--include"),
                                         QStringLiteral("--filter")};
  // A path's global.max_delete or global.max_delete_size caps the run the
  // same way (addConfigToContext in fs/newfs.go), so the check renames
  // those too. A pattern is never opened as a path, so it keeps its text.
  static const QStringList kGlobalCaps{QStringLiteral("max_delete"),
                                       QStringLiteral("max_delete_size")};
  // A download of several selected items saves its --filter rules ahead of
  // the command, where the --dry-run added below split the first rule from
  // its flag and rclone refused the run. rclone reads a flag anywhere, so the
  // command moves in front of them and the rules keep their order.
  qsizetype command = 0;
  while (command < args.size()) {
    const QString &arg = args.at(command);
    const qsizetype equals = arg.indexOf(QLatin1Char('='));
    if (kPatternFlags.contains(arg)) {
      command += 2;
    } else if (equals > 0 && kPatternFlags.contains(arg.left(equals))) {
      command += 1;
    } else {
      break;
    }
  }
  if (command > 0 && command < args.size()) {
    args.move(command, 0);
  }
  args = KeepDryRunOn(args);
  const bool dropCaps = args.value(0) != QLatin1String("bisync");
  for (qsizetype i = 0; dropCaps && i < args.size();) {
    QString arg = args.at(i);
    const qsizetype equals = arg.indexOf(QLatin1Char('='));
    const QString name = equals > 0 ? arg.left(equals) : arg;
    if ((i > 0 && kPatternFlags.contains(args.at(i - 1))) ||
        (equals > 0 && kPatternFlags.contains(name))) {
      i++; // a pattern; a cap left in only stops the preview early
    } else if (kCaps.contains(arg)) {
      args.remove(i, qMin<qsizetype>(2, args.size() - i));
    } else if (equals > 0 && kCaps.contains(name)) {
      args.removeAt(i);
    } else {
      for (const QString &key : kGlobalCaps) {
        IgnoreGlobalOption(arg, key);
      }
      args[i] = arg;
      i++;
    }
  }
  // Only the argument right after the command is surely a flag. Anywhere
  // else a --dry-run or -n can be the value of the option before it
  // (`--exclude -n` excludes a file named -n and changes everything), so
  // the preview adds its own. A second --dry-run is harmless.
  if (args.value(1) != QLatin1String("--dry-run") &&
      args.value(1) != QLatin1String("-n")) {
    args.insert(args.isEmpty() ? 0 : 1, QStringLiteral("--dry-run"));
  }
  return args;
}

QStringList JobOptions::KeepDryRunOn(QStringList args) {
  // rclone reads a global.dry_run option in every path it opens, a remote
  // nested in another's option too (":alias,remote=':local,global.dry_run=
  // false:dir':"), and sets it for the whole run after the flags were read
  // (addConfigToContext in fs/newfs.go), so global.dry_run=false made a dry
  // run delete for real. Renaming it wherever it stands keeps the dry run on
  // (measured on v1.76.0, 2026-11-16).
  static const QString kDryRunKey = QStringLiteral("dry_run");
  for (qsizetype i = 0; i < args.size(); i++) {
    QString arg = args.at(i);
    const qsizetype equals = arg.indexOf(QLatin1Char('='));
    const QString name = equals > 0 ? arg.left(equals) : arg;
    // A short flag cluster gives its = value to the letter just before it,
    // so -vn=false sets -v and turns the dry run off. pflag reads every
    // character as a flag letter; this only takes letters, so "- run=*"
    // isn't read as one.
    bool shortDryRun = equals >= 2 && arg.startsWith(QLatin1Char('-')) &&
                       !arg.startsWith(QLatin1String("--")) &&
                       arg.at(equals - 1) == QLatin1Char('n');
    for (qsizetype k = 1; shortDryRun && k < equals; k++) {
      const char16_t c = arg.at(k).unicode();
      shortDryRun = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
    }
    if ((shortDryRun || name == QLatin1String("--dry-run")) &&
        IsGoFalse(QStringView(arg).mid(equals + 1))) {
      // Any argument can be the value of the option before it
      // (`--user-agent --exclude --dry-run=false` gives --exclude to
      // --user-agent and turns the dry run off), so a false value is turned
      // into the bare flag wherever it stands. Dropping it instead would
      // hand that option the next argument. A true value keeps the dry run,
      // and anything else is refused by rclone for the real run too.
      args[i] = arg.left(equals);
      continue;
    }
    // Every argument, for the same reason. A pattern that looks like a
    // connection string up to its colon only changes what a dry run lists.
    IgnoreGlobalOption(arg, kDryRunKey);
    args[i] = arg;
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
  }

  // Right after the command, where DryRunArguments looks for it.
  if (dryRun) {
    list << "--dry-run";
  }

  if (operation == Bisync && !conflictResolve.isEmpty() &&
      conflictResolve != "none") {
    list << "--conflict-resolve" << conflictResolve;
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
    // A global limit saved before rclone 1.76.0 can hold a time it now
    // refuses (0800), and rclone would stop every run at the start over
    // it. The status bar marks that one, and runs go without it.
    const QStringList global = GetGlobalBandwidthLimit();
    const QString problem =
        global.size() == 2 ? BwlimitTimetable::Problem(global.at(1))
                           : QString();
    if (global.size() == 2 && problem.isEmpty()) {
      list << global;
    } else if (!problem.isEmpty()) {
      // Into the app log once per value, since the options are built for
      // previews and command text too. Every value is remembered, so going
      // back to an earlier one doesn't log it again.
      static QSet<QString> warned;
      if (!warned.contains(global.at(1))) {
        warned.insert(global.at(1));
        qWarning().noquote()
            << QString("Runs leave out the global bandwidth limit \"%1\", "
                       "which rclone won't accept. %2")
                   .arg(global.at(1), problem);
      }
    }
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

  // The extra and default options come after the app's --dry-run, and the
  // last value rclone is given wins.
  return dryRun ? KeepDryRunOn(list) : list;
}

SerializationException::SerializationException(QString msg)
    : QException(), Message(std::move(msg)) {}
