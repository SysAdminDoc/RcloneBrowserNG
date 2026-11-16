#pragma once

#include "pch.h"

// A job that stopped because it hit a transfer or duration cap is not a
// failure, but every non-zero exit was reported the same way: red, with the
// bare number and no explanation.
namespace RcloneExitCode {

enum class Outcome {
  Success,
  // Stopped on purpose at a limit the user asked for. Not an error.
  CompletedWithLimit,
  // Might work on another attempt: a missing path or a temporary fault.
  Retryable,
  Failed,
};

struct Meaning {
  Outcome outcome = Outcome::Failed;
  // Short label for the job card, e.g. "Transfer limit reached".
  QString name;
  // One line saying what happened and what to do about it.
  QString explanation;
};

Meaning Describe(int exitCode);

// What a finished QProcess actually means. A killed or crashed process does
// not report an rclone exit code: Windows gives back whatever the kill used
// (62097 here), and a POSIX kill reports the signal number, so 9 would come
// straight back through the table above as "Nothing to transfer" and count
// as a success. Both the job card and the job history go through this, so
// they cannot disagree about the same run.
struct ProcessOutcome {
  Outcome outcome = Outcome::Failed;
  QString name;
  QString explanation;
  // Ended in a way the user asked for, including a deliberate limit.
  bool success = false;
  // Did all the work it was asked to do. Only this may gate anything
  // destructive or anything that assumes the copy is complete.
  bool completedFully = false;
};

// deleteCapHit: rclone said it stopped at --max-delete. That exit is 7, the
// same code a bisync with lost state gets, so only its message can tell.
ProcessOutcome DescribeProcess(int exitCode, bool crashed, bool userCancelled,
                               bool deleteCapHit = false);

// True for rclone's "--max-delete threshold reached" (and the
// --max-delete-size form), from the CLI's log or rc core/stats lastError.
bool IsDeleteCapError(const QString &text);

// The job card and history label for a run the delete cap stopped.
Meaning DeleteCap();

// A history row: the label its job card recorded, with the explanation the
// card gave for it. Exit 7 is a cap stop or a bisync needing --resync, and
// a cancel or crash has no rclone exit code at all, so the label decides.
// Rows saved before labels existed fall back to the exit-code table.
Meaning DescribeHistory(int exitCode, const QString &statusLabel);

// True when the job card should offer Retry rather than only Restart.
bool IsRetryable(int exitCode);

// True when the job finished the work it was allowed to do.
bool IsSuccessful(int exitCode);

} // namespace RcloneExitCode
