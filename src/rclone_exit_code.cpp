#include "rclone_exit_code.h"

namespace RcloneExitCode {

// Verified against rclone v1.75.0 on 2026-09-05 rather than taken from the
// docs table, which is stale in two places the v1.69.0 changelog warned
// about. Observed:
//   copy of a present file .................... 0
//   copy with an unknown flag ................. 2   (docs say 1)
//   lsd of a missing directory ................ 3
//   deletefile on a missing file .............. 4
//   moveto with a missing source .............. 1
//   lsd against an unreachable sftp host ...... 1
//   bisync without --resync ................... 7   (docs say 2)
//   copy --max-transfer 100k over 800k ........ 8
//   copy --error-on-no-transfer, nothing to do  9
//   copy --max-duration 1ms --cutoff-mode hard  10
// So 1 is a general operation failure in practice, and 2 is the usage
// error; the docs have those the other way round.
Meaning Describe(int exitCode) {
  switch (exitCode) {
  case 0:
    return {Outcome::Success, "Completed",
            "Everything transferred without errors."};
  case 1:
    return {Outcome::Failed, "Failed",
            "rclone could not complete the operation. The job output says "
            "why."};
  case 2:
    return {Outcome::Failed, "Bad command",
            "rclone rejected the command. Check the flags in the transfer "
            "options."};
  case 3:
    return {Outcome::Retryable, "Directory not found",
            "The source or destination directory does not exist. Check the "
            "paths, then retry."};
  case 4:
    return {Outcome::Retryable, "File not found",
            "A file the job expected is not there. It may have been moved or "
            "deleted since the listing."};
  case 5:
    return {Outcome::Retryable, "Temporary error",
            "The remote failed in a way rclone considers temporary. Retrying "
            "often succeeds."};
  case 6:
    return {Outcome::Failed, "Finished with errors",
            "Some files did not transfer. The job output lists them."};
  case 7:
    return {Outcome::Failed, "Stopped to avoid damage",
            "rclone aborted rather than risk the data. A bisync run needs "
            "--resync after its state is lost."};
  case 8:
    return {Outcome::CompletedWithLimit, "Transfer limit reached",
            "Stopped at the --max-transfer limit. What transferred before "
            "the limit is complete; run again to continue."};
  case 9:
    return {Outcome::CompletedWithLimit, "Nothing to transfer",
            "No files needed transferring, and --error-on-no-transfer asked "
            "to be told about it."};
  case 10:
    return {Outcome::CompletedWithLimit, "Time limit reached",
            "Stopped at the --max-duration limit. What transferred before "
            "the limit is complete; run again to continue."};
  default:
    break;
  }

  return {Outcome::Failed, QString("Exit code %1").arg(exitCode),
          "rclone exited with a code this version of Rclone Browser NG does "
          "not recognise. The job output says more."};
}

bool IsRetryable(int exitCode) {
  return Describe(exitCode).outcome == Outcome::Retryable;
}

bool IsSuccessful(int exitCode) {
  const Outcome outcome = Describe(exitCode).outcome;
  return outcome == Outcome::Success || outcome == Outcome::CompletedWithLimit;
}

} // namespace RcloneExitCode
