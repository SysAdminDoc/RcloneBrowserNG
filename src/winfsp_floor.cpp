#include "winfsp_floor.h"
#include "utils.h"

namespace WinFspFloor {

bool IsBelowFloor(const QString &fileVersion) {
  const QString trimmed = fileVersion.trimmed();
  if (trimmed.isEmpty() || !trimmed.at(0).isDigit()) {
    return false;
  }
  // A zeroed version block reads as "0.0.0.0", which says nothing about the
  // build. compareVersion pads with zeros, so this matches any all-zero form.
  if (compareVersion(trimmed.toStdString(), "0") == 0) {
    return false;
  }
  return compareVersion(trimmed.toStdString(), kMinimumVersion) == 2;
}

QString Warning(const QString &fileVersion) {
  // Beta1 2.2.26112 fixed CVE-2026-3006, Beta2 2.2.26183 CVE-2026-7162, Beta4
  // 2.2.26215 two more reported vulnerabilities and Beta5 2.2.26286 four
  // more. The six later ones have no CVE IDs in the release notes.
  return QString("Detected WinFsp %1.\n\n"
                 "WinFsp builds before %2 are missing security fixes. "
                 "CVE-2026-3006 is a local privilege escalation in the kernel "
                 "driver, fixed in 2026 Beta1. CVE-2026-7162 was fixed in "
                 "Beta2, and six more reported vulnerabilities were fixed in "
                 "Beta4 and Beta5. The latest stable release, WinFsp 2025, "
                 "has none of these fixes.\n\n"
                 "Update to %3 or newer from %4 before using mounts on a "
                 "shared or untrusted computer. It's marked as a beta, but "
                 "it's the only build with every fix. The download on "
                 "winfsp.dev is still WinFsp 2025.")
      .arg(fileVersion.trimmed(), kMinimumVersion, kRecommendedRelease,
           kDownloadUrl);
}

QString InstallAdvice() {
  return QString("Mounting requires WinFsp, which is not installed.\n\n"
                 "Install %1 or newer from %2. It's marked as a beta, but "
                 "the stable WinFsp 2025 on winfsp.dev and in winget is "
                 "missing the 2026 security fixes.")
      .arg(kRecommendedRelease, kDownloadUrl);
}

QString WarnedKey(const QString &fileVersion) {
  return QString("%1 < %2").arg(fileVersion.trimmed(), kMinimumVersion);
}

} // namespace WinFspFloor
