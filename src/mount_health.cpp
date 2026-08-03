#include "mount_health.h"

MountHealthProbeResult ProbeMountPoint(const QString &folder) {
  const QString path = folder.trimmed();
  if (path.isEmpty()) {
    return {false, QStringLiteral("The mount point is empty.")};
  }

  const QFileInfo mountPoint(path);
  if (!mountPoint.exists() || !mountPoint.isDir()) {
    return {false, QStringLiteral("The mount point is no longer available.")};
  }

  QStorageInfo storage(path);
  storage.refresh();
  if (!storage.isValid() || !storage.isReady()) {
    return {false,
            QStringLiteral("The mounted volume is not ready for filesystem "
                           "operations.")};
  }

  QDir directory(path);
  if (!directory.exists()) {
    return {false, QStringLiteral("The mount point could not be opened.")};
  }

  return {true, QString()};
}
