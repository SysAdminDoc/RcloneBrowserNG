#pragma once

#include "pch.h"

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
