#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Reads a finished rc job's job/status the same way for every route. A
// sync/* or operations/* job reports failure in "success" and "error". A
// core/command job "succeeds" whenever the daemon ran the command, so the
// child rclone's own failure is only in output.error, with its log in
// output.result and no exit code. A job/batch delete lists each item's
// result in "output".
namespace RcJobResult {

// The card's output pane keeps this many lines, so a log is cut to its
// last ones the same way rather than in one oversized append.
constexpr qsizetype kMaxShownLines = 10000;
// The app's log panel keeps 500 lines.
constexpr qsizetype kMaxProblems = 500;

struct Result {
  // As the CLI route counts it: exits 8, 9 and 10 (a --max-transfer,
  // --error-on-no-transfer or --max-duration stop) are successes.
  bool success = false;
  // The child rclone's exit code, read from its log, for a core/command job;
  // 0 or 1 for the rest.
  int exitCode = 1;
  // Why it failed, for the job card. Empty on success.
  QString error;
  // Lines for the card's output pane: the job's text output, the child
  // rclone's notices, warnings and errors (its per-file and stats lines
  // left out, and only the last of a long log), the batch items that
  // couldn't be deleted.
  QStringList output;
  // The lines worth the app's log panel: the child rclone's warnings and
  // errors, and the batch items that couldn't be deleted.
  QStringList problems;
  // The line where rclone said it stopped at --max-delete, if it did.
  QString deleteCapLine;
  // A core/command job: a child rclone process, outside the daemon's stats.
  bool fromCommand = false;
};

// batchLabels: one label per job/batch input, empty for other jobs.
Result Read(const QJsonObject &status, const QStringList &batchLabels);

// The first line of text that reports a --max-delete stop, as a person
// reads it (a JSON log line gives its message), else empty.
QString DeleteCapLine(const QString &text);

// The exit code a child rclone that failed left, read from its combined
// log: 7 for the delete cap, 8 for --max-transfer, 10 for --max-duration,
// 9 when it only said there was nothing to transfer, else 1. Errors before
// rclone's "Attempt N/M succeeded" were retried away and don't count.
int ExitCodeFromLog(const QString &log);

// A sync/* job run with --error-on-no-transfer that copied nothing reports
// plain success, where the process route exits 9. Returns 9 for that case
// (exit 0, no transfers, the flag on in args), else exitCode.
int WithNoTransferExit(int exitCode, qint64 transfers, const QStringList &args);

// What cmd.Run makes of an error a sync/* job returned without counting it.
// rclone counts an error in the stats where it happens, but the checks a
// sync makes before it starts return theirs uncounted. cmd.Run counts the
// returned error itself (fs.CountError) before it decides on a retry, and
// the rc route never does, so the job card asks here when a failed job
// counted nothing. Fatal for the ones rclone wraps in fserrors.FatalError,
// Retry for a plain error, Unknown for the rest, such as a remote that
// can't be opened, which the CLI fails before its retry loop starts.
enum class Uncounted { Unknown, Retry, Fatal };
Uncounted ClassifyUncounted(const QString &error);

// The sync/* call for attempt `attempt` of a run the job card retries.
// rclone checks --max-delete, --max-delete-size and --max-transfer against
// the stats group's own counts, and the CLI's one group carries them across
// attempts. A new group starts at zero, so it gets those caps lowered by
// what the earlier attempts used: bytesSoFar and deletesSoFar, read from
// core/stats. A cap that can't be lowered exactly keeps the attempt in the
// current group instead (sameGroup), with current's caps: core/stats has no
// deleted-bytes count for --max-delete-size, and a cap set through the
// daemon's environment (RCLONE_MAX_DELETE and the rest) isn't in _config.
struct NextAttempt {
  QJsonObject payload;
  bool sameGroup = false;
};
NextAttempt PlanNextAttempt(const QJsonObject &original,
                            const QJsonObject &current, int attempt,
                            qint64 bytesSoFar, qint64 deletesSoFar);

} // namespace RcJobResult
