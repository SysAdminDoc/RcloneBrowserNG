#include "mount_backend.h"

namespace {

bool versionAtLeast(const QString &version, const QString &minimum) {
  const auto parse = [](const QString &value) {
    QVector<int> parts;
    const QStringList raw = value.split('.');
    parts.reserve(raw.size());
    for (const QString &part : raw) {
      int number = 0;
      for (const QChar ch : part) {
        if (!ch.isDigit()) {
          break;
        }
        number = number * 10 + ch.digitValue();
      }
      parts.append(number);
    }
    return parts;
  };

  QVector<int> left = parse(version);
  QVector<int> right = parse(minimum);
  const int size = qMax(left.size(), right.size());
  left.resize(size);
  right.resize(size);
  for (int i = 0; i < size; ++i) {
    if (left[i] > right[i]) {
      return true;
    }
    if (left[i] < right[i]) {
      return false;
    }
  }
  return true;
}

QString macFuseOldWarningVersion(const QString &version) {
  return version.isEmpty() ? QString("missing") : version;
}

} // namespace

QString DetectMacFuseVersion() {
#if defined(Q_OS_MACOS)
  const QStringList plists = {
      "/Library/Filesystems/macfuse.fs/Contents/Info.plist",
      "/Library/Filesystems/osxfuse.fs/Contents/Info.plist",
  };

  for (const QString &path : plists) {
    if (!QFileInfo::exists(path)) {
      continue;
    }
    QSettings plist(path, QSettings::NativeFormat);
    const QString version =
        plist.value("CFBundleShortVersionString").toString().trimmed();
    if (!version.isEmpty()) {
      return version;
    }
  }
#endif
  return QString();
}

bool DetectFuseTInstalled() {
#if defined(Q_OS_MACOS)
  const QStringList candidates = {
      "/Applications/fuse-t.app",
      "/Library/Application Support/fuse-t",
      "/Library/Filesystems/fuse-t.fs",
      "/usr/local/lib/libfuse-t.dylib",
      "/opt/homebrew/lib/libfuse-t.dylib",
  };

  for (const QString &path : candidates) {
    if (QFileInfo::exists(path)) {
      return true;
    }
  }
#endif
  return false;
}

bool IsMacOs26OrNewer() {
#if defined(Q_OS_MACOS)
  return QOperatingSystemVersion::current().majorVersion() >= 26;
#else
  return false;
#endif
}

bool RcloneCommandSupported(const QString &rclone, const QString &command) {
  QProcess process;
  process.start(rclone, QStringList() << command << "--help",
                QIODevice::ReadOnly);
  if (!process.waitForStarted(3000)) {
    return false;
  }
  if (!process.waitForFinished(5000)) {
    process.kill();
    process.waitForFinished(2000);
    return false;
  }
  return process.exitStatus() == QProcess::NormalExit &&
         process.exitCode() == 0;
}

bool MountOptionsContainFuseBackend(const QStringList &options) {
  for (int i = 0; i < options.size(); ++i) {
    const QString option = options[i].trimmed();
    if ((option == "-o" || option == "--fuse-flag") &&
        i + 1 < options.size() &&
        options[i + 1].contains("backend=", Qt::CaseInsensitive)) {
      return true;
    }
    if ((option.startsWith("-o") || option.startsWith("--fuse-flag=")) &&
        option.contains("backend=", Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

MountBackendPlan PlanMacMountBackend(const MacMountBackendFacts &facts) {
  MountBackendPlan plan;
  plan.backendName = "rclone mount";

  if (!facts.macFuseVersion.isEmpty() &&
      versionAtLeast(facts.macFuseVersion, "5.2")) {
    plan.backendName = "macFUSE FSKit-capable mount";
    return plan;
  }

  if (facts.fuseTInstalled) {
    plan.backendName = "fuse-t mount";
    if (facts.macOsMajorVersion >= 26 &&
        !MountOptionsContainFuseBackend(facts.userMountOptions)) {
      plan.argsBeforeRemote << "-o" << "backend=fskit";
      plan.backendName = "fuse-t FSKit mount";
    }
    return plan;
  }

  if (facts.nfsMountSupported) {
    plan.command = "nfsmount";
    plan.backendName = "rclone nfsmount";
    return plan;
  }

  if (facts.macFuseVersion.isEmpty() ||
      !versionAtLeast(facts.macFuseVersion, "5.2")) {
    plan.warningKey = "Settings/macFuseWarnedVersion";
    plan.warningVersion = macFuseOldWarningVersion(facts.macFuseVersion);
    plan.warningTitle = "macOS mount backend recommended";
    if (facts.macFuseVersion.isEmpty()) {
      plan.warningText =
          "No FSKit-capable macOS mount backend was detected.\n\n"
          "Install macFUSE 5.2 or newer, install fuse-t, or update rclone to a "
          "build that includes nfsmount before relying on long-running mounts.";
    } else {
      plan.warningText =
          QString("Detected macFUSE %1.\n\nmacFUSE versions before 5.2 do "
                  "not provide the current FSKit mount API. Install macFUSE "
                  "5.2 or newer, use fuse-t, or use an rclone build with "
                  "nfsmount before relying on long-running mounts.")
              .arg(facts.macFuseVersion);
    }
  }

  return plan;
}
