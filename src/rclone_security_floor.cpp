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

bool IsBelowFloor(const QString &version) {
  const QString trimmed = version.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }
  return compareVersion(trimmed.toStdString(), kMinimumVersion) == 2;
}

QString AdvisorySummary() {
  return QString(
             "Versions before %1 are affected by rclone security advisories "
             "that reach features this app uses. Serve WebDAV lets anyone "
             "create folders above the shared one (GHSA-44pm-2q5p-fm63) and "
             "skips the login for OPTIONS requests (GHSA-gj73-fh6v-92fj). "
             "Serve S3 lets a presigned upload link read any bucket "
             "(GHSA-w3h7-jxxq-vfrp) and can run past its memory limit "
             "(GHSA-qjw8-8hmp-c3m6). rclone selfupdate trusts a download "
             "hash nobody signed (GHSA-w33h-3qgq-8mv8), and the "
             "remote-control daemon reads a request of any size before "
             "checking the login (GHSA-f8p4-8g4j-q3m2).")
      .arg(kMinimumVersion);
}

} // namespace RcloneSecurityFloor
