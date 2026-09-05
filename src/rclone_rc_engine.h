#pragma once

#include "pch.h"
#include <functional>

class RcloneRcEngine : public QObject {
  Q_OBJECT

public:
  using RcCallback =
      std::function<void(const QJsonObject &result, const QString &error)>;

  explicit RcloneRcEngine(QObject *parent = nullptr);
  ~RcloneRcEngine();

  using StartCallback = std::function<void(bool ok, const QString &error)>;

  // Brings the rcd daemon up, or confirms the running one still answers, then
  // calls back. Never blocks: this runs on the click that starts a transfer,
  // and the old synchronous version could hold the window for fifteen seconds
  // with no progress and nothing to cancel.
  void ensureStartedAsync(QObject *context, StartCallback callback);

  void runCommand(
      const QStringList &args, QObject *context,
      std::function<void(int jobId, const QString &error)> callback);
  void jobStatus(int jobId, QObject *context, RcCallback callback);
  void coreStats(const QString &group, QObject *context, RcCallback callback);
  void stopJob(int jobId, QObject *context,
               std::function<void(bool ok, const QString &error)> callback);

  QStringList rcCommandForDisplay(const QStringList &args) const;

private:
  QProcess *mProcess = nullptr;
  QNetworkAccessManager mNetwork;
  QString mUrl;
  QString mUser;
  QString mPass;
  // Callers that arrive while the daemon is still coming up wait here rather
  // than each starting one of their own.
  QVector<StartCallback> mPendingStarts;
  bool mStarting = false;

  void startCommand(const QStringList &args, QObject *context,
                    std::function<void(int jobId, const QString &error)>
                        callback);
  void spawnDaemon();
  void pollUntilReady(const QElapsedTimer &deadline);
  void finishStart(bool ok, const QString &error);

  // Only for destructor teardown, where there is no event loop left to
  // return to. Never call this from a path a user click can reach.
  QJsonObject postSync(const QString &path, const QJsonObject &payload,
                       QString *error);
  void postAsync(const QString &path, const QJsonObject &payload,
                 QObject *context, RcCallback callback);
  QNetworkRequest request(const QString &path) const;
};
