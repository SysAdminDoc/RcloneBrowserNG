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

// True when the job card should offer Retry rather than only Restart.
bool IsRetryable(int exitCode);

// True when the job finished the work it was allowed to do.
bool IsSuccessful(int exitCode);

} // namespace RcloneExitCode
