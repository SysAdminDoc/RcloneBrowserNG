#pragma once

#include "pch.h"

struct BackendFeatures {
  bool queried = false;
  bool copy = true;
  bool move = true;
  bool dirMove = true;
  bool purge = true;
  bool publicLink = false;
  bool about = false;
  bool cleanUp = false;
  bool serverSideAcrossConfigs = false;
  bool canHaveEmptyDirectories = true;
  bool trashSupported = false;
  QString trashFlag;

  static BackendFeatures fromJson(const QByteArray &json);
  static BackendFeatures defaultForType(const QString &remoteType);
};

class BackendFeatureCache {
public:
  static BackendFeatureCache &instance();
  BackendFeatures get(const QString &remote) const;
  void put(const QString &remote, const BackendFeatures &features);
  bool has(const QString &remote) const;

  static void queryAsync(const QString &remote,
                         std::function<void(const BackendFeatures &)> callback);

private:
  BackendFeatureCache() = default;
  QHash<QString, BackendFeatures> mCache;
  mutable QMutex mMutex;
};

struct RcloneCapabilities {
  QString rcloneVersion;
  QString rclonePath;
  QString configPath;
  QString qtVersion;
  QString osInfo;
  QString mountBackend;

  bool hasNameTransform() const;
  bool hasListCutoff() const;
  bool hasJsonLog() const;
  bool hasBisync() const;
  bool hasJobBatch() const;

  QString summary() const;

  static RcloneCapabilities detect();
};

namespace Diagnostics {

using LogCallback = std::function<void(const QString &source, const QString &line)>;

QString redactSecrets(const QString &text);
void appendLog(const QString &source, const QString &line);
QString recentLog();
void setLogCallback(LogCallback cb);

} // namespace Diagnostics
