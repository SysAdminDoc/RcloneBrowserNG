#include "rc_job_result.h"

#include "job_stats.h"
#include "rc_delete_batch.h"
#include "rclone_exit_code.h"
#include "rclone_number.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QTimeZone>

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

// cmd.Run's lines for a try it won't retry, and its last line, which names
// the error the run exits on ("Failed to copy: directory not found", or
// "Failed to sync with 3 errors: last error was: ..."), both measured on
// v1.76.0 (2026-11-16).
constexpr char kFatalLine[] = "Fatal error received - not attempting retries";
constexpr char kCantRetryLine[] =
    "Can't retry any of the errors - not attempting retries";

struct FinalError {
  bool found = false;
  QString error;
};

FinalError LastFailedLine(const QString &log) {
  // The command's name is one word. A file's own "Failed to copy" line
  // starts with the file's name, and "Failed to create file system for"
  // is the line a remote that can't be opened ends with.
  static const QRegularExpression kFailed(QStringLiteral(
      "^Failed to \\S+(?: with \\d+ errors: last error was)?: (.*)$"),
      QRegularExpression::DotMatchesEverythingOption);
  FinalError out;
  for (const QString &line : log.split(QChar('\n'))) {
    const LogText entry = Parse(line);
    if (entry.stats || (!entry.level.isEmpty() &&
                        entry.level != QLatin1String("notice"))) {
      continue;
    }
    const QRegularExpressionMatch match = kFailed.match(entry.text);
    if (match.hasMatch()) {
      out.found = true;
      out.error = match.captured(1);
    }
  }
  return out;
}

bool HasLine(const QString &log, const char *text) {
  for (const QString &line : log.split(QChar('\n'))) {
    if (Parse(line).text == QLatin1String(text)) {
      return true;
    }
  }
  return false;
}

// True when text is cause, or ends with ": " and cause.
bool EndsWithCause(const QString &text, const QString &cause) {
  return text == cause || (text.endsWith(cause) &&
                           text.left(text.size() - cause.size())
                               .endsWith(QLatin1String(": ")));
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
  // resolveExitCode reads only the error cmd.Run's last line names: a
  // missing directory or file first, then a --max-transfer or
  // --max-duration stop, then the rest. A --max-delete stop ends a sync on
  // a fatal "failed to delete N files", whose text doesn't say why.
  const FinalError last = LastFailedLine(log);
  const int cause = last.found ? CauseExitCode(last.error) : -1;
  if (cause == 3 || cause == 4) {
    return cause;
  }
  if (!DeleteCapLine(log).isEmpty()) {
    return 7;
  }
  if (last.found) {
    // A limit stop's error is no-retry, so a sync returns a plain error
    // ahead of it, and the run then exits on that one (measured on v1.76.0).
    if (EndsWithCause(last.error, QLatin1String(kTransferLimit))) {
      return 8;
    }
    if (EndsWithCause(last.error, QLatin1String(kDurationLimit))) {
      return 10;
    }
    // bisync's cmd.Run has Retry off, so it prints neither retry line, and
    // its command wraps "bisync aborted" in a fatal error
    // (cmd/bisync/cmd.go).
    return ExitCodeForError(
        last.error,
        HasLine(log, kFatalLine) || last.error == QLatin1String("bisync aborted"),
        HasLine(log, kCantRetryLine));
  }
  // A sync/* job's error, which is the only text there is.
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
      // newLess (fs/sync/pipe.go), which newPipe wraps as fatal. The pattern
      // ones are for --order-by pattern,<globs>.
      QStringLiteral("pattern requires at least one glob"),
      QStringLiteral("empty pattern glob"),
      QStringLiteral("bad pattern glob "),
      QStringLiteral("unknown --order-by comparison "),
      QStringLiteral("unknown --order-by sort direction "),
      QStringLiteral("bad mixed fraction --order-by "),
      QStringLiteral("bad --order-by string "),
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

bool IsNoRetryError(const QString &error) {
  // rclone v1.76.0: fs.ErrorImmutableModified (fs/sync/sync.go and
  // fs/operations), and the deadline processError marks as no-retry.
  return error == QLatin1String("immutable file modified") ||
         error == QLatin1String("context deadline exceeded");
}

namespace {

// fserrors.ShouldRetry (fs/fserrors/error.go) walks the cause chain. It
// retries an error whose Timeout() or Temporary() says so, one whose root
// is io.EOF, io.ErrUnexpectedEOF or an errno retriable_errors*.go lists,
// and one whose root's text holds a phrase from retriableErrorStrings.
// The roots are Go's texts for those errors on Linux, macOS and Windows,
// where the message is the system's English one.
bool IsRetriableText(const QString &error) {
  static const QStringList kRoots{
      // io.EOF and io.ErrUnexpectedEOF.
      QStringLiteral("EOF"),
      QStringLiteral("unexpected EOF"),
      // Timeout() true: a deadline on a connection, context.DeadlineExceeded
      // and net/http's own timeouts.
      QStringLiteral("i/o timeout"),
      QStringLiteral("context deadline exceeded"),
      QStringLiteral("net/http: TLS handshake timeout"),
      QStringLiteral("net/http: timeout awaiting response headers"),
      // Temporary() true: a DNS server that failed to answer, and the errnos
      // syscall.Errno calls temporary.
      QStringLiteral("server misbehaving"),
      QStringLiteral("interrupted system call"),
      QStringLiteral("too many open files"),
      QStringLiteral("too many open files in system"),
      // EPIPE, ETIMEDOUT, ECONNREFUSED, EHOSTDOWN, EHOSTUNREACH, ENETDOWN,
      // ENETUNREACH, ECONNABORTED, EAGAIN and ECONNRESET.
      QStringLiteral("broken pipe"),
      QStringLiteral("connection timed out"),
      QStringLiteral("operation timed out"),
      QStringLiteral("connection refused"),
      QStringLiteral("host is down"),
      QStringLiteral("no route to host"),
      QStringLiteral("network is down"),
      QStringLiteral("network is unreachable"),
      QStringLiteral("software caused connection abort"),
      QStringLiteral("resource temporarily unavailable"),
      QStringLiteral("connection reset by peer"),
      // The Windows socket and file errors retriable_errors_windows.go adds.
      QStringLiteral("A socket operation encountered a dead network."),
      QStringLiteral(
          "A socket operation was attempted to an unreachable network."),
      QStringLiteral("The connection has been broken due to keep-alive "
                     "activity detecting a failure while the operation was "
                     "in progress."),
      QStringLiteral("An established connection was aborted by the software "
                     "in your host machine."),
      QStringLiteral(
          "An existing connection was forcibly closed by the remote host."),
      QStringLiteral("An operation on a socket could not be performed "
                     "because the system lacked sufficient buffer space or "
                     "because a queue was full."),
      QStringLiteral("A request to send or receive data was disallowed "
                     "because the socket is not connected and (when sending "
                     "on a datagram socket using a sendto call) no address "
                     "was supplied."),
      QStringLiteral("A request to send or receive data was disallowed "
                     "because the socket had already been shut down in that "
                     "direction with a previous shutdown call."),
      QStringLiteral("A connection attempt failed because the connected "
                     "party did not properly respond after a period of "
                     "time, or established connection failed because "
                     "connected host has failed to respond."),
      QStringLiteral("No connection could be made because the target "
                     "machine actively refused it."),
      QStringLiteral(
          "A socket operation failed because the destination host was down."),
      QStringLiteral(
          "A socket operation was attempted to an unreachable host."),
      QStringLiteral("Returned by WSARecv or WSARecvFrom to indicate the "
                     "remote party has initiated a graceful shutdown "
                     "sequence."),
      QStringLiteral(
          "A database query failed because it was actively refused."),
      QStringLiteral("No such host is known."),
      QStringLiteral("This is usually a temporary error during hostname "
                     "resolution and means that the local server did not "
                     "receive a response from an authoritative server."),
      QStringLiteral("Reached the end of the file."),
      QStringLiteral("The specified network name is no longer available."),
      QStringLiteral("The pipe has been ended."),
  };
  static const QStringList kPhrases{
      // retriableErrorStrings.
      QStringLiteral("use of closed network connection"),
      QStringLiteral("unexpected EOF reading trailer"),
      QStringLiteral("transport connection broken"),
      QStringLiteral("http: ContentLength="),
      QStringLiteral("server closed idle connection"),
      QStringLiteral("bad record MAC"),
      QStringLiteral("stream error:"),
      QStringLiteral("http2: server sent GOAWAY"),
      QStringLiteral("tls: use of closed connection"),
      // net/http's client timeout, which it adds to the request's error in
      // parentheses ("(Client.Timeout exceeded while awaiting headers)").
      QStringLiteral("(Client.Timeout "),
  };
  for (const QString &root : kRoots) {
    if (EndsWithCause(error, root)) {
      return true;
    }
  }
  for (const QString &phrase : kPhrases) {
    if (error.contains(phrase)) {
      return true;
    }
  }
  return false;
}

// Milliseconds since the epoch in a time written in Go's time.RFC3339Nano
// layout ("2026-11-16T06:29:30.6725768-05:00", "2026-11-16T11:00:00Z"),
// which is how rclone writes a retry-after time and a job's endTime.
bool Rfc3339Ms(const QString &text, qint64 *ms) {
  static const QRegularExpression layout(QStringLiteral(
      "^(\\d{4})-(\\d{2})-(\\d{2})T(\\d{2}):(\\d{2}):(\\d{2})(?:\\.(\\d{1,9}))?"
      "(?:Z|([+-])(\\d{2}):(\\d{2}))$"));
  const QRegularExpressionMatch m = layout.match(text);
  if (!m.hasMatch()) {
    return false;
  }
  const QDate date(m.captured(1).toInt(), m.captured(2).toInt(),
                   m.captured(3).toInt());
  const QTime time(m.captured(4).toInt(), m.captured(5).toInt(),
                   m.captured(6).toInt());
  if (!date.isValid() || !time.isValid()) {
    return false;
  }
  qint64 value = QDateTime(date, time, QTimeZone::UTC).toMSecsSinceEpoch();
  value += (m.captured(7) + QStringLiteral("00")).left(3).toInt();
  if (!m.captured(8).isEmpty()) {
    const qint64 offset =
        (m.captured(9).toInt() * 60 + m.captured(10).toInt()) * 60000LL;
    value += m.captured(8) == QLatin1String("-") ? offset : -offset;
  }
  *ms = value;
  return true;
}

// A wait the way time.Duration.String() writes it, to the millisecond:
// "750ms", "1.5s", "1m1s", "1h0m0s".
QString GoDurationText(qint64 ms) {
  if (ms == 0) {
    return QStringLiteral("0s");
  }
  QString out;
  if (ms < 0) {
    out = QStringLiteral("-");
    ms = -ms;
  }
  if (ms < 1000) {
    return out + QString::number(ms) + QStringLiteral("ms");
  }
  const qint64 hours = ms / 3600000;
  const qint64 minutes = ms / 60000 % 60;
  const qint64 secondsMs = ms % 60000;
  QString seconds = QString::number(secondsMs / 1000);
  if (secondsMs % 1000 != 0) {
    QString fraction =
        QString("%1").arg(secondsMs % 1000, 3, 10, QLatin1Char('0'));
    while (fraction.endsWith(QLatin1Char('0'))) {
      fraction.chop(1);
    }
    seconds += QLatin1Char('.') + fraction;
  }
  if (hours > 0) {
    out += QString::number(hours) + QLatin1Char('h');
  }
  if (hours > 0 || minutes > 0) {
    out += QString::number(minutes) + QLatin1Char('m');
  }
  return out + seconds + QLatin1Char('s');
}

} // namespace

bool IsNoLowLevelRetryError(const QString &error) {
  // rclone v1.76.0's own fserrors.NoLowLevelRetryError texts:
  // backend/local/local.go for a file that changed while it was read, and
  // backend/sftp/sftp.go's host key checks. The sftp mark survives the
  // "failed to make fs for --compare-dest" wrapping, so that run exits 6
  // (measured 2026-11-16).
  static const QStringList kMarked{
      QStringLiteral("can't copy - source file is being updated ("),
      QStringLiteral("sftp: server presented an SSH certificate ("),
      QStringLiteral("sftp: host key mismatch for "),
      QStringLiteral(
          " but no key is pinned for this algorithm in host_keys; "),
      QStringLiteral("sftp: host_keys already contains "),
  };
  for (const QString &text : kMarked) {
    if (error.contains(text)) {
      return true;
    }
  }
  // backend/webdav/chunking.go marks a chunk's error once its low-level
  // retries run out, and only an error worth a retry gets that far.
  const QString chunk = QStringLiteral("uploading chunk failed: ");
  const qsizetype at = error.indexOf(chunk);
  return at >= 0 && IsRetriableText(error.mid(at + chunk.size()));
}

int CauseExitCode(const QString &error) {
  // fs.ErrorDirNotFound and fs.ErrorObjectNotFound, which resolveExitCode
  // finds with errors.Is. Measured on v1.76.0 (2026-11-16): a copy, move
  // or sync from a missing folder ends on "directory not found" and exits 3
  // on the CLI, and the same sync/* call over rc fails with that text.
  if (EndsWithCause(error, QStringLiteral("directory not found"))) {
    return 3;
  }
  if (EndsWithCause(error, QStringLiteral("object not found"))) {
    return 4;
  }
  // ShouldRetry says no to a NoLowLevelRetryError before it looks at the
  // cause.
  if (IsNoLowLevelRetryError(error)) {
    return -1;
  }
  return IsRetriableText(error) ? 5 : -1;
}

int ExitCodeForError(const QString &error, bool fatal, bool noRetry) {
  const int cause = CauseExitCode(error);
  if (cause >= 0) {
    return cause;
  }
  if (noRetry || IsNoRetryError(error) || IsNoLowLevelRetryError(error)) {
    return 6;
  }
  return fatal ? 7 : 1;
}

qint64 GoDurationMs(const QString &text) {
  // time.ParseDuration's grammar: an optional sign, then one or more
  // decimal numbers, each with a unit.
  static const QRegularExpression part(
      QStringLiteral("(\\d+(?:\\.\\d*)?|\\.\\d+)(ns|us|\\x{00B5}s|\\x{03BC}s|"
                     "ms|s|m|h)"));
  QString rest = text.trimmed();
  double sign = 1.0;
  if (rest.startsWith(QLatin1Char('-')) || rest.startsWith(QLatin1Char('+'))) {
    sign = rest.startsWith(QLatin1Char('-')) ? -1.0 : 1.0;
    rest = rest.mid(1);
  }
  if (rest == QLatin1String("0")) {
    return 0;
  }
  if (rest.isEmpty()) {
    return -1;
  }
  double ms = 0.0;
  qsizetype at = 0;
  while (at < rest.size()) {
    const QRegularExpressionMatch m = part.match(
        rest, at, QRegularExpression::NormalMatch,
        QRegularExpression::AnchorAtOffsetMatchOption);
    if (!m.hasMatch()) {
      return -1;
    }
    const double value = m.captured(1).toDouble();
    const QString unit = m.captured(2);
    double perUnit = 0.0;
    if (unit == QLatin1String("h")) {
      perUnit = 3600000.0;
    } else if (unit == QLatin1String("m")) {
      perUnit = 60000.0;
    } else if (unit == QLatin1String("s")) {
      perUnit = 1000.0;
    } else if (unit == QLatin1String("ms")) {
      perUnit = 1.0;
    } else if (unit == QLatin1String("ns")) {
      perUnit = 0.000001;
    } else {
      perUnit = 0.001;
    }
    ms += value * perUnit;
    at = m.capturedEnd(0);
  }
  return static_cast<qint64>(sign * ms + (sign < 0 ? -0.5 : 0.5));
}

RetryAfter RetryAfterWait(const QStringList &texts, const QString &tryEnd) {
  // fserrors.ErrorRetryAfter.Error() in v1.76.0.
  static const QRegularExpression said(
      QStringLiteral("try again after (\\S+) \\(([^()]*)\\)"));
  qint64 endMs = 0;
  const bool fromEnd = Rfc3339Ms(tryEnd, &endMs);
  RetryAfter longest;
  for (const QString &text : texts) {
    QRegularExpressionMatchIterator it = said.globalMatch(text);
    while (it.hasNext()) {
      const QRegularExpressionMatch m = it.next();
      qint64 untilMs = 0;
      if (!Rfc3339Ms(m.captured(1), &untilMs)) {
        continue; // not a time, so not that error's text
      }
      const qint64 waitMs =
          fromEnd ? untilMs - endMs : GoDurationMs(m.captured(2));
      if (waitMs > longest.waitMs) {
        longest.waitMs = waitMs;
        longest.until = m.captured(1);
        longest.left = fromEnd ? GoDurationText(waitMs) : m.captured(2);
      }
    }
  }
  return longest;
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

// What a cap in a sync/* call comes to: -1 when it's off, kUnknownCap when
// it can't be told.
constexpr qint64 kUnknownCap = -2;

// A cap _config leaves out is the daemon's own, which options/get gives as
// a number (a SizeSuffix is its byte count). A daemon whose settings have
// no such key is an rclone without that cap.
qint64 DaemonCap(const QJsonObject &daemonMain, const QString &key) {
  if (daemonMain.isEmpty()) {
    return kUnknownCap;
  }
  if (!daemonMain.contains(key)) {
    return -1;
  }
  const QJsonValue value = daemonMain.value(key);
  if (!value.isDouble()) {
    return kUnknownCap;
  }
  const double number = value.toDouble();
  if (number < 0) {
    return -1;
  }
  // Beyond 2^53 a double no longer holds every integer.
  if (number > 9007199254740992.0) {
    return kUnknownCap;
  }
  return static_cast<qint64>(number);
}

qint64 CountCap(const QJsonObject &config, const QString &key,
                const QJsonObject &daemonMain) {
  if (!config.contains(key)) {
    return DaemonCap(daemonMain, key);
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
               const QJsonObject &daemonMain) {
  if (!config.contains(key)) {
    return DaemonCap(daemonMain, key);
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
                            qint64 bytesSoFar, qint64 deletesSoFar,
                            const QJsonObject &daemonMain,
                            qint64 deletedBytesSoFar,
                            bool deletedBytesLost) {
  static const QString kGroup = QStringLiteral("_group");
  static const QString kConfig = QStringLiteral("_config");
  static const QString kMaxDelete = QStringLiteral("MaxDelete");
  static const QString kMaxDeleteSize = QStringLiteral("MaxDeleteSize");
  static const QString kMaxTransfer = QStringLiteral("MaxTransfer");
  const QJsonObject config = original.value(kConfig).toObject();
  const qint64 deleteCount = CountCap(config, kMaxDelete, daemonMain);
  const qint64 deleteBytes = SizeCap(config, kMaxDeleteSize, daemonMain);
  const qint64 transferBytes = SizeCap(config, kMaxTransfer, daemonMain);
  // A size cap that came after the deleted bytes were lost leaves nothing
  // to delete that has a size. A cap the daemon won't say still keeps the
  // run where it is, as below.
  const bool noSizeLeft =
      deletedBytesLost && deletesSoFar > 0 && deleteBytes >= 0;
  // With nothing deleted or moved so far there's nothing to carry.
  const bool deleteBytesCarry =
      deleteBytes == -1 || noSizeLeft ||
      (deleteBytes >= 0 && deletedBytesSoFar >= 0);
  const bool deletesCarry =
      deletesSoFar <= 0 || (deleteCount != kUnknownCap && deleteBytesCarry);
  const bool bytesCarry = bytesSoFar <= 0 || transferBytes != kUnknownCap;

  NextAttempt next;
  if ((!deletesCarry || !bytesCarry) && current.contains(kGroup)) {
    next.payload = current;
    next.sameGroup = true;
    if (noSizeLeft) {
      QJsonObject stays = current.value(kConfig).toObject();
      stays.insert(kMaxDeleteSize, QStringLiteral("0b"));
      next.payload.insert(kConfig, stays);
    }
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
  if (noSizeLeft) {
    lowered.insert(kMaxDeleteSize, QStringLiteral("0b"));
  } else if (deleteBytes >= 0 && deletesSoFar > 0 && deletedBytesSoFar > 0) {
    // rclone refuses a delete once the group's deleted bytes plus the
    // file's size would pass the cap, so this is the same test.
    lowered.insert(kMaxDeleteSize,
                   QString("%1b").arg(qMax<qint64>(
                       0, deleteBytes - deletedBytesSoFar)));
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

void AddDeletes(const QJsonObject &transferred, ListedDeletes *deletes) {
  ListedDeletes listed;
  const QJsonArray entries = transferred.value("transferred").toArray();
  for (const QJsonValue &value : entries) {
    const QJsonObject entry = value.toObject();
    if (entry.value("what").toString() != QLatin1String("deleting") ||
        RcloneExitCode::IsDeleteCapError(entry.value("error").toString())) {
      continue;
    }
    // DeleteFile counts a size it can't tell as 0.
    const qint64 size = qMax<qint64>(0, entry.value("size").toInteger(0));
    const QString key = entry.value("name").toString() + QChar('\n') +
                        QString::number(size) + QChar('\n') +
                        entry.value("started_at").toString() + QChar('\n') +
                        entry.value("completed_at").toString();
    ListedDelete &one = listed[key];
    one.size = size;
    one.count++;
  }
  for (auto it = listed.cbegin(); it != listed.cend(); ++it) {
    ListedDelete &kept = (*deletes)[it.key()];
    kept.size = it.value().size;
    kept.count = qMax(kept.count, it.value().count);
  }
}

qint64 DeleteCount(const ListedDeletes &deletes) {
  qint64 count = 0;
  for (const ListedDelete &one : deletes) {
    count += one.count;
  }
  return count;
}

qint64 DeletedBytes(const ListedDeletes &deletes) {
  qint64 bytes = 0;
  for (const ListedDelete &one : deletes) {
    bytes += one.size * one.count;
  }
  return bytes;
}

} // namespace RcJobResult
