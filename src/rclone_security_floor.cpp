#include "rclone_security_floor.h"
#include "utils.h"

namespace RcloneSecurityFloor {

// rclone 1.75.2 (2026-10-09) and 1.76.0 (2026-11-06) closed 18 advisories,
// and 17 of them list 1.75.1, the previous floor, as affected. Several reach
// code this app runs itself: serve webdav, serve s3, selfupdate and the rc
// daemon.
const char kMinimumVersion[] = "1.76.0";
const char kReviewedDate[] = "2026-11-15";
const int kReviewIntervalDays = 180;

namespace {

// "rclone v1.76.0-DEV" and "  1.76.0 " both become "1.76.0-DEV" or
// "1.76.0". Empty when there is no version number to read.
QString Normalized(const QString &version) {
  QString trimmed = version.trimmed();
  if (trimmed.startsWith("rclone ")) {
    trimmed.remove(0, 7);
  }
  if (trimmed.startsWith('v')) {
    trimmed.remove(0, 1);
  }
  if (trimmed.isEmpty() || !trimmed.at(0).isDigit()) {
    return QString();
  }
  return trimmed;
}

} // namespace

bool IsBelowFloor(const QString &version) {
  const QString trimmed = Normalized(version);
  if (trimmed.isEmpty()) {
    return false;
  }
  const qsizetype suffix = trimmed.indexOf('-');
  if (suffix != -1) {
    // A dev or beta build of the floor release may predate the fixes.
    return compareVersion(trimmed.left(suffix).toStdString(),
                          kMinimumVersion) != 1;
  }
  return compareVersion(trimmed.toStdString(), kMinimumVersion) == 2;
}

QString AdvisorySummary() {
  // 1.75.2 fixed the serve and selfupdate advisories, so only the rc one
  // still applies to it. The text must not tell a 1.75.2 user otherwise.
  return QString(
             "rclone %1 fixes the remote-control daemon this app starts, "
             "which reads a request of any size before checking the login "
             "(GHSA-f8p4-8g4j-q3m2). Versions before 1.75.2 also have holes "
             "in other features this app uses. Serve WebDAV can create, "
             "overwrite and delete files outside the shared folder "
             "(GHSA-44pm-2q5p-fm63) and skips the login for OPTIONS requests "
             "(GHSA-gj73-fh6v-92fj). Serve S3 lets a presigned upload link "
             "read any bucket (GHSA-w3h7-jxxq-vfrp) and can run past its "
             "memory limit (GHSA-qjw8-8hmp-c3m6). rclone selfupdate trusts a "
             "download hash nobody signed (GHSA-w33h-3qgq-8mv8).")
      .arg(kMinimumVersion);
}

bool SelfUpdateIsSafe(const QString &version) {
  const QString trimmed = Normalized(version);
  if (trimmed.isEmpty()) {
    return false;
  }
  const qsizetype suffix = trimmed.indexOf('-');
  if (suffix != -1) {
    // A pre-release of 1.76.0 or earlier may have been built before the
    // fix landed. Anything leading up to a later release was built after
    // 1.76.0 shipped with it.
    return compareVersion(trimmed.left(suffix).toStdString(), "1.76.0") == 1;
  }
  return compareVersion(trimmed.toStdString(), kSelfUpdateFixedVersion) != 2;
}

QString SelfUpdateUnsafeReason() {
  return QString("The updater in this rclone can be tricked into installing "
                 "a tampered download (GHSA-w33h-3qgq-8mv8), so get the new "
                 "version from rclone.org instead of running selfupdate.");
}

} // namespace RcloneSecurityFloor
