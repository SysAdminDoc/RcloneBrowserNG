#include "rc_job_result.h"

#include "job_stats.h"
#include "rc_delete_batch.h"
#include "rclone_exit_code.h"

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
  const bool flagOn =
      args.contains(QLatin1String("--error-on-no-transfer")) ||
      args.contains(QLatin1String("--error-on-no-transfer=true"));
  return exitCode == 0 && transfers == 0 && flagOn ? 9 : exitCode;
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

} // namespace RcJobResult
