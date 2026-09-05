#include "rclone_capabilities.h"
#include "utils.h"

namespace {
bool versionAtLeast(const QString &ver, const char *min) {
  if (ver.isEmpty()) {
    return false;
  }
  return compareVersion(ver.toStdString(), min) != 2;
}

bool jsonBool(const QJsonObject &obj, const char *key, bool fallback) {
  if (obj.contains(QLatin1String(key))) {
    return obj.value(QLatin1String(key)).toBool(fallback);
  }
  return fallback;
}
} // namespace

BackendFeatures BackendFeatures::fromJson(const QByteArray &json) {
  BackendFeatures f;
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return f;
  }

  QJsonObject root = doc.object();
  QJsonObject features;
  if (root.contains("Features")) {
    features = root.value("Features").toObject();
  } else {
    features = root;
  }

  f.queried = true;
  f.copy = jsonBool(features, "Copy", true);
  f.move = jsonBool(features, "Move", true);
  f.dirMove = jsonBool(features, "DirMove", true);
  f.purge = jsonBool(features, "Purge", true);
  f.publicLink = jsonBool(features, "PublicLink", false);
  f.about = jsonBool(features, "About", false);
  f.cleanUp = jsonBool(features, "CleanUp", false);
  f.serverSideAcrossConfigs =
      jsonBool(features, "ServerSideAcrossConfigs", false);
  f.canHaveEmptyDirectories =
      jsonBool(features, "CanHaveEmptyDirectories", true);

  return f;
}

BackendFeatures BackendFeatures::defaultForType(const QString &remoteType) {
  BackendFeatures f;
  f.queried = false;

  if (remoteType == "drive") {
    f.publicLink = true;
    f.about = true;
    f.cleanUp = true;
    f.trashSupported = true;
    f.trashFlag = "--drive-use-trash";
  } else if (remoteType == "onedrive") {
    f.about = true;
    f.cleanUp = true;
    f.trashSupported = true;
    f.trashFlag = "--onedrive-no-trash=false"; // inverted flag
  } else if (remoteType == "dropbox") {
    f.about = true;
    f.trashSupported = true;
  } else if (remoteType == "s3" || remoteType == "b2") {
    f.publicLink = true;
    f.about = true;
  } else if (remoteType == "sftp" || remoteType == "ftp") {
    f.move = true;
    f.dirMove = true;
    f.publicLink = false;
    f.about = true;
  } else if (remoteType == "local") {
    f.move = true;
    f.dirMove = true;
    f.about = true;
    f.canHaveEmptyDirectories = true;
  }

  return f;
}

BackendFeatureCache &BackendFeatureCache::instance() {
  static BackendFeatureCache cache;
  return cache;
}

BackendFeatures BackendFeatureCache::get(const QString &remote) const {
  QMutexLocker lock(&mMutex);
  return mCache.value(remote, BackendFeatures());
}

void BackendFeatureCache::put(const QString &remote,
                              const BackendFeatures &features) {
  QMutexLocker lock(&mMutex);
  mCache.insert(remote, features);
}

bool BackendFeatureCache::has(const QString &remote) const {
  QMutexLocker lock(&mMutex);
  return mCache.contains(remote);
}

void BackendFeatureCache::queryAsync(
    const QString &remote,
    std::function<void(const BackendFeatures &)> callback) {
  auto &cache = instance();
  if (cache.has(remote)) {
    if (callback)
      callback(cache.get(remote));
    return;
  }

  auto *proc = new QProcess();
  UseRclonePassword(proc);
  proc->setProgram(GetRclone());
  proc->setArguments(QStringList() << "backend" << "features"
                                   << GetRcloneConf() << remote + ":");
  proc->setProcessChannelMode(QProcess::SeparateChannels);
  auto completed = QSharedPointer<bool>::create(false);

  QObject::connect(
      proc,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      [proc, remote, callback, completed](int code, QProcess::ExitStatus) {
        if (*completed) {
          proc->deleteLater();
          return;
        }
        *completed = true;
        BackendFeatures features;
        if (code == 0) {
          features = BackendFeatures::fromJson(proc->readAllStandardOutput());
        }
        if (!features.queried) {
          features.queried = false;
        }
        instance().put(remote, features);
        if (callback)
          callback(features);
        proc->deleteLater();
      });

  QTimer::singleShot(10000, proc, [proc, remote, callback, completed]() {
    if (*completed) {
      return;
    }
    if (proc->state() != QProcess::NotRunning) {
      *completed = true;
      proc->kill();
      BackendFeatures fallback;
      instance().put(remote, fallback);
      if (callback)
        callback(fallback);
    }
  });

  proc->start();
}

bool RcloneCapabilities::hasNameTransform() const {
  return versionAtLeast(rcloneVersion, "1.74");
}

bool RcloneCapabilities::hasListCutoff() const {
  return versionAtLeast(rcloneVersion, "1.74");
}

bool RcloneCapabilities::hasJsonLog() const {
  return versionAtLeast(rcloneVersion, "1.56");
}

bool RcloneCapabilities::hasBisync() const {
  return versionAtLeast(rcloneVersion, "1.58");
}

bool RcloneCapabilities::hasJobBatch() const {
  return versionAtLeast(rcloneVersion, "1.72");
}

bool RcloneCapabilities::hasListRemotesJson() const {
  return versionAtLeast(rcloneVersion, "1.68");
}

bool RcloneCapabilities::hasCoreDisks() const {
  return versionAtLeast(rcloneVersion, "1.74");
}

QString RcloneCapabilities::summary() const {
  QStringList lines;
  lines << QString("Rclone Browser NG v%1").arg(RCLONE_BROWSER_VERSION);
  lines << QString("Qt %1 (built against %2)")
               .arg(qVersion(), QT_VERSION_STR);
  lines << QString("OS: %1 %2 (%3)")
               .arg(QSysInfo::productType(), QSysInfo::productVersion(),
                    QSysInfo::currentCpuArchitecture());
  lines << QString("rclone: %1").arg(
      rcloneVersion.isEmpty() ? "not detected" : "v" + rcloneVersion);
  lines << QString("rclone path: %1").arg(
      rclonePath.isEmpty() ? "(default PATH lookup)" : rclonePath);
  lines << QString("Config: %1").arg(
      configPath.isEmpty() ? "(rclone default)" : configPath);

  if (!mountBackend.isEmpty()) {
    lines << QString("Mount backend: %1").arg(mountBackend);
  }

  lines << "";
  lines << "Feature support:";
  lines << QString("  --use-json-log: %1").arg(hasJsonLog() ? "yes" : "no");
  lines << QString("  --name-transform: %1").arg(
      hasNameTransform() ? "yes" : "needs rclone >= 1.74");
  lines << QString("  --list-cutoff: %1").arg(
      hasListCutoff() ? "yes" : "needs rclone >= 1.74");
  lines << QString("  bisync: %1").arg(
      hasBisync() ? "yes" : "needs rclone >= 1.58");
  lines << QString("  job/batch RC: %1").arg(
      hasJobBatch() ? "yes" : "needs rclone >= 1.72");

  return lines.join('\n');
}

namespace Diagnostics {

namespace {
struct LogEntry {
  QString source;
  QString line;
};
QList<LogEntry> &logBuffer() {
  static QList<LogEntry> buf;
  return buf;
}
constexpr int kMaxLogEntries = 200;
} // namespace

QString redactSecrets(const QString &text) {
  QString out = text;
  static const QRegularExpression patterns[] = {
      QRegularExpression(R"((--rc-pass)\s+\S+)"),
      QRegularExpression(
          R"(((?:api[-_]?key|client[-_]?secret|access[-_]?key|pass(?:word)?|token|secret|credential|key))\s*[:=]\s*\S+)",
                         QRegularExpression::CaseInsensitiveOption),
      QRegularExpression(R"((RCLONE_CONFIG_PASS)=\S+)"),
      QRegularExpression(R"((Authorization:\s*(?:Basic|Bearer))\s+\S+)",
                         QRegularExpression::CaseInsensitiveOption),
  };
  for (const auto &re : patterns) {
    out.replace(re, "\\1 <redacted>");
  }
  return out;
}

static LogCallback &logCallback() {
  static LogCallback cb;
  return cb;
}

void setLogCallback(LogCallback cb) { logCallback() = std::move(cb); }

void appendLog(const QString &source, const QString &line) {
  auto &buf = logBuffer();
  if (buf.size() >= kMaxLogEntries) {
    buf.removeFirst();
  }
  buf.append({source, line});
  if (logCallback())
    logCallback()(source, line);
}

QString recentLog() {
  QStringList lines;
  for (const auto &entry : logBuffer()) {
    lines << QString("[%1] %2").arg(entry.source, entry.line);
  }
  return lines.join('\n');
}

} // namespace Diagnostics

RcloneCapabilities RcloneCapabilities::detect() {
  RcloneCapabilities caps;

  auto settings = GetSettings();
  caps.rcloneVersion =
      settings->value("Settings/rcloneVersion").toString();
  caps.rclonePath = settings->value("Settings/rclone").toString();
  caps.configPath = settings->value("Settings/rcloneConf").toString();
  caps.qtVersion = QString(qVersion());

#if defined(Q_OS_WIN32)
  caps.mountBackend = "WinFsp";
#elif defined(Q_OS_MACOS)
  caps.mountBackend = "macFUSE / fuse-t / nfsmount";
#elif defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
  caps.mountBackend = "not supported";
#else
  caps.mountBackend = "FUSE";
#endif

  return caps;
}
