#include "mount_health.h"

#include <algorithm>

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

QList<LocalDiskInfo> ListLocalDisks() {
  static const QSet<QByteArray> pseudoFileSystems = {
      "autofs",  "binfmt_misc", "bpf",       "cgroup",     "cgroup2",
      "configfs", "debugfs",    "devfs",     "devpts",     "devtmpfs",
      "efivarfs", "fusectl",    "hugetlbfs", "mqueue",     "nsfs",
      "nullfs",  "overlay",     "proc",      "pstore",     "ramfs",
      "rpc_pipefs", "securityfs", "squashfs", "sysfs",     "tmpfs",
      "tracefs"};
  QList<LocalDiskInfo> disks;
  const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
  for (const QStorageInfo &volume : volumes) {
    if (!volume.isValid() || !volume.isReady() || volume.bytesTotal() <= 0) {
      continue;
    }
    if (pseudoFileSystems.contains(volume.fileSystemType().toLower())) {
      continue;
    }
    // macOS mounts its sealed system, VM and preboot volumes here. They share
    // the startup disk's space, so they only repeat the "/" row.
    if (volume.rootPath().startsWith(QStringLiteral("/System/Volumes/"))) {
      continue;
    }
    LocalDiskInfo disk;
    disk.path = QDir::toNativeSeparators(volume.rootPath());
    disk.fileSystem = QString::fromLatin1(volume.fileSystemType());
    disk.total = volume.bytesTotal();
    disk.used = (std::max)(qint64(0), volume.bytesTotal() - volume.bytesFree());
    disks << disk;
  }
  return disks;
}
