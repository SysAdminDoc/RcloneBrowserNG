#include "rclone_security_floor.h"

#include "utils.h"

namespace RcloneSecurityFloor {

// rclone 1.75.1 (2026-09-04) closed eleven advisories at once, two of them
// critical. The previous floor of 1.74.3 was wrong in both directions: it was
// three releases behind, and 1.74.3 itself is affected by GHSA-fqj9-69pf-6pjg.
const char kMinimumVersion[] = "1.75.1";
const char kReviewedDate[] = "2026-09-04";
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
      "Versions before %1 are affected by rclone security advisories, "
      "including two rated critical: GHSA-p569-5gjg-9cmj, where a configured "
      "remote-control auth proxy is silently ignored, and "
      "GHSA-xwwr-4h3p-r22c, an authentication bypass in serve s3. "
      "GHSA-mfvx-7rcj-9m5g matters here in particular, because it exposes the "
      "remote-control daemon's full command line - including the credentials "
      "Rclone Browser NG passes it - to anything that can reach the port.")
      .arg(kMinimumVersion);
}

} // namespace RcloneSecurityFloor
