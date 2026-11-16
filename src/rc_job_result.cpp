#include "rc_job_result.h"

#include "job_stats.h"
#include "rc_delete_batch.h"
#include "rclone_exit_code.h"
#include "rclone_number.h"

#include <QJsonValue>
#include <QRegularExpression>

namespace RcJobResult {

namespace {

struct LogText {
  QString text;
  QString level;
  bool stats = false;
};

// rclone logs JSON (the app always passes --use-json-log), and a person
// reads "object: message", the way rclone's text log puts it.
LogText Parse(const QString &line) {
  LogText out;
  const JobStats::LogLine parsed = JobStats::ParseLogLine(line.toUtf8());
  if (!parsed.isJson) {
    out.text = line.trimmed();
    return out;
  }
  out.level = parsed.level;
  out.stats = parsed.stats.present;
  out.text = parsed.object.isEmpty()
                 ? parsed.message
                 : QStringLiteral("%1: %2").arg(parsed.object, parsed.message);
  return out;
}

bool IsProblem(const QString &level) {
  return level == QLatin1String("error") || level == QLatin1String("warning") ||
         level == QLatin1String("critical") || level == QLatin1String("fatal");
}

// Measured on rclone v1.76.0 with --use-json-log (2026-11-15): exit 8 logs
// "max transfer limit reached as set by --max-transfer", exit 10 "max
// transfer duration reached as set by --max-duration", and exit 9 only the
// info line "There was nothing to transfer", which a run without
// --error-on-no-transfer logs as well.
constexpr char kTransferLimit[] =
    "max transfer limit reached as set by --max-transfer";
constexpr char kDurationLimit[] =
    "max transfer duration reached as set by --max-duration";
constexpr char kNothingToTransfer[] = "There was nothing to transfer";

// Measured on rclone v1.76.0 (2026-11-15): a retry that works logs
// "Attempt 2/3 succeeded" at error level, after attempt 1's errors.
bool IsRetrySuccess(const QString &text) {
  static const QRegularExpression kAttempt(
      QStringLiteral("^Attempt \\d+/\\d+ succeeded$"));
  return kAttempt.match(text).hasMatch();
}

// Keeps the last max lines, with a first line saying how many went.
QStringList KeepLast(QStringList lines, qsizetype max, const QString &note) {
  if (lines.size() <= max) {
    return lines;
  }
  const qsizetype dropped = lines.size() - max;
  lines = lines.mid(dropped);
  lines.prepend(note.arg(dropped));
  return lines;
}

void AddLog(Result &result, const QString &log) {
  QStringList shown;
  QStringList problems;
  for (const QString &line : log.split(QChar('\n'))) {
    const LogText entry = Parse(line);
    if (entry.text.isEmpty() || entry.stats) {
      continue;
    }
    if (entry.level.isEmpty() || IsProblem(entry.level)) {
      problems << entry.text;
    }
    if (entry.level.isEmpty() || entry.level == QLatin1String("notice") ||
        IsProblem(entry.level)) {
      shown << entry.text;
    }
  }
  result.output << KeepLast(shown, kMaxShownLines,
                            QStringLiteral("(%1 earlier lines left out)"));
  result.problems << KeepLast(problems, kMaxProblems,
                              QStringLiteral("(%1 earlier problems left out)"));
}

} // namespace

QString DeleteCapLine(const QString &text) {
  for (const QString &line : text.split(QChar('\n'))) {
    if (RcloneExitCode::IsDeleteCapError(line)) {
      return Parse(line).text;
    }
  }
  return QString();
}

int ExitCodeFromLog(const QString &log) {
  if (!DeleteCapLine(log).isEmpty()) {
    return 7;
  }
  if (log.contains(QLatin1String(kTransferLimit))) {
    return 8;
  }
  if (log.contains(QLatin1String(kDurationLimit))) {
    return 10;
  }
  bool problem = false;
  bool nothingToTransfer = false;
  for (const QString &line : log.split(QChar('\n'))) {
    const LogText entry = Parse(line);
    if (entry.text.isEmpty() || entry.stats) {
      continue;
    }
    if (IsRetrySuccess(entry.text)) {
      // What failed before this attempt was retried and came good.
      problem = false;
      continue;
    }
    problem = problem || entry.level.isEmpty() || IsProblem(entry.level);
    nothingToTransfer =
        nothingToTransfer || entry.text == QLatin1String(kNothingToTransfer);
  }
  return !problem && nothingToTransfer ? 9 : 1;
}

int WithNoTransferExit(int exitCode, qint64 transfers,
                       const QStringList &args) {
  // A bool flag takes its value only after =, read with Go's
  // strconv.ParseBool, and the last one given wins: --error-on-no-transfer=1
  // turns it on, and a later --error-on-no-transfer=False turns it off.
  static const QStringList kTrue{"1", "t", "T", "TRUE", "true", "True"};
  static const QStringList kFalse{"0", "f", "F", "FALSE", "false", "False"};
  const QString flag = QStringLiteral("--error-on-no-transfer");
  bool flagOn = false;
  for (const QString &arg : args) {
    if (arg == flag) {
      flagOn = true;
    } else if (arg.startsWith(flag + QChar('='))) {
      const QString value = arg.mid(flag.size() + 1);
      if (kTrue.contains(value)) {
        flagOn = true;
      } else if (kFalse.contains(value)) {
        flagOn = false;
      }
    }
  }
  return exitCode == 0 && transfers == 0 && flagOn ? 9 : exitCode;
}

Uncounted ClassifyUncounted(const QString &error) {
  // rclone v1.76.0's newSyncCopyMove and runSyncCopyMove (fs/sync/sync.go),
  // with BackupDir, GetCompareDest and GetCopyDest (fs/operations), which
  // they call before any file is looked at. The --no-check-dest ones come
  // first, since one of them names --backup-dir too.
  static const QStringList kRetry{
      QStringLiteral("can't use --no-check-dest with "),
      QStringLiteral("unknown track renames strategy "),
  };
  static const QStringList kFatal{
      QStringLiteral("can't sync or move files on overlapping remotes"),
      QStringLiteral("can't delete and move at the same time"),
      QStringLiteral("can't use --delete-before with --track-renames"),
      QStringLiteral("failed to make fs for --compare-dest "),
      QStringLiteral("failed to make fs for --copy-dest "),
      QStringLiteral("parameter to --copy-dest has to be on the same remote "
                     "as destination"),
      QStringLiteral("can't use --copy-dest on a remote which doesn't "
                     "support server side copy"),
      QStringLiteral("failed to make fs for --backup-dir "),
      QStringLiteral("parameter to --backup-dir has to be on the same remote "
                     "as destination"),
      QStringLiteral("destination and parameter to --backup-dir mustn't "
                     "overlap"),
      QStringLiteral("source and parameter to --backup-dir mustn't overlap"),
      QStringLiteral("destination and parameter to --backup-dir mustn't be "
                     "the same"),
      QStringLiteral("source and parameter to --backup-dir mustn't be the "
                     "same"),
      QStringLiteral("can't use --backup-dir on a remote which doesn't "
                     "support server-side move or copy"),
  };
  for (const QString &start : kRetry) {
    if (error.startsWith(start)) {
      return Uncounted::Retry;
    }
  }
  for (const QString &start : kFatal) {
    if (error.startsWith(start)) {
      return Uncounted::Fatal;
    }
  }
  return Uncounted::Unknown;
}

Result Read(const QJsonObject &status, const QStringList &batchLabels) {
  Result result;
  result.success = status.value("success").toBool();
  result.error = status.value("error").toString();
  result.deleteCapLine = DeleteCapLine(result.error);
  int exitCode = -1;

  const QJsonValue output = status.value("output");
  if (output.isString()) {
    const QString text = output.toString().trimmed();
    if (!text.isEmpty()) {
      result.output << text;
    }
  }

  const QJsonObject command = output.toObject();
  const bool isCommand =
      command.contains("result") && command.value("error").isBool();
  result.fromCommand = isCommand;
  if (!isCommand && !result.success && batchLabels.isEmpty()) {
    // A sync/* job takes --max-transfer and --max-duration in its _config
    // and reports the stop as its error, as the CLI would exit 8 or 10.
    const int fromError = ExitCodeFromLog(result.error);
    if (RcloneExitCode::IsSuccessful(fromError)) {
      exitCode = fromError;
      result.success = true;
    }
  }
  if (isCommand) {
    const QString log = command.value("result").toString();
    AddLog(result, log);
    if (command.value("error").toBool()) {
      exitCode = ExitCodeFromLog(log);
      result.success = RcloneExitCode::IsSuccessful(exitCode);
      if (result.deleteCapLine.isEmpty()) {
        result.deleteCapLine = DeleteCapLine(log);
      }
      if (!result.success && result.error.isEmpty()) {
        result.error = QStringLiteral(
            "rclone stopped with an error. The output above says why.");
      }
    }
  }

  if (!batchLabels.isEmpty()) {
    const QStringList failed = RcDeleteBatch::Failures(output, batchLabels);
    result.output << failed;
    result.problems << failed;
    if (!failed.isEmpty()) {
      result.success = false;
      exitCode = -1;
      if (result.error.isEmpty()) {
        result.error = QStringLiteral("%1 of %2 items couldn't be deleted.")
                           .arg(failed.size())
                           .arg(batchLabels.size());
      }
    }
  }

  if (result.success) {
    result.error.clear();
  }
  result.exitCode = exitCode >= 0 ? exitCode : (result.success ? 0 : 1);
  return result;
}

namespace {

// What a cap in a sync/* call's _config comes to: -1 when it's off,
// kUnknownCap when it can't be told. With no flag for it, rclone takes the
// environment variable, and the daemon runs with this app's environment.
constexpr qint64 kUnknownCap = -2;

qint64 CountCap(const QJsonObject &config, const QString &key,
                const char *variable) {
  if (!config.contains(key)) {
    return qEnvironmentVariableIsSet(variable) ? kUnknownCap : -1;
  }
  const QJsonValue value = config.value(key);
  if (!value.isDouble()) {
    return kUnknownCap;
  }
  const qint64 count = value.toInteger(-1);
  return count < 0 ? -1 : count;
}

// A SizeSuffix goes into _config as the CLI token (rc_sync_request.cpp).
qint64 SizeCap(const QJsonObject &config, const QString &key,
               const char *variable) {
  if (!config.contains(key)) {
    return qEnvironmentVariableIsSet(variable) ? kUnknownCap : -1;
  }
  const QJsonValue value = config.value(key);
  qint64 bytes = -1;
  if (!value.isString() ||
      RcloneNumber::ParseSize(value.toString(), &bytes) !=
          RcloneNumber::SizeError::None) {
    return kUnknownCap;
  }
  return bytes < 0 ? -1 : bytes;
}

} // namespace

NextAttempt PlanNextAttempt(const QJsonObject &original,
                            const QJsonObject &current, int attempt,
                            qint64 bytesSoFar, qint64 deletesSoFar) {
  static const QString kGroup = QStringLiteral("_group");
  static const QString kConfig = QStringLiteral("_config");
  static const QString kMaxDelete = QStringLiteral("MaxDelete");
  static const QString kMaxTransfer = QStringLiteral("MaxTransfer");
  const QJsonObject config = original.value(kConfig).toObject();
  const qint64 deleteCount =
      CountCap(config, kMaxDelete, "RCLONE_MAX_DELETE");
  const qint64 deleteBytes = SizeCap(config, QStringLiteral("MaxDeleteSize"),
                                     "RCLONE_MAX_DELETE_SIZE");
  const qint64 transferBytes =
      SizeCap(config, kMaxTransfer, "RCLONE_MAX_TRANSFER");
  // With nothing deleted or moved so far there's nothing to carry.
  const bool deletesCarry =
      deletesSoFar <= 0 || (deleteCount != kUnknownCap && deleteBytes == -1);
  const bool bytesCarry = bytesSoFar <= 0 || transferBytes != kUnknownCap;

  NextAttempt next;
  if ((!deletesCarry || !bytesCarry) && current.contains(kGroup)) {
    next.payload = current;
    next.sameGroup = true;
    return next;
  }
  // A payload with no _group puts each job in a job/<id> group of its own.
  next.payload = original;
  if (original.contains(kGroup)) {
    next.payload.insert(kGroup, QString("%1-attempt-%2")
                                    .arg(original.value(kGroup).toString())
                                    .arg(attempt));
  }
  QJsonObject lowered = config;
  if (deleteCount >= 0 && deletesSoFar > 0) {
    lowered.insert(kMaxDelete, qMax<qint64>(0, deleteCount - deletesSoFar));
  }
  if (transferBytes >= 0 && bytesSoFar > 0) {
    // A b suffix is bytes, where a bare number would be KiB.
    lowered.insert(kMaxTransfer,
                   QString("%1b").arg(qMax<qint64>(0, transferBytes -
                                                          bytesSoFar)));
  }
  if (lowered != config) {
    next.payload.insert(kConfig, lowered);
  }
  return next;
}

} // namespace RcJobResult
