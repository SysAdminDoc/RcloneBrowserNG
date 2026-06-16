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

  bool ensureStarted(QString *error);

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

  QJsonObject postSync(const QString &path, const QJsonObject &payload,
                       QString *error);
  void postAsync(const QString &path, const QJsonObject &payload,
                 QObject *context, RcCallback callback);
  QNetworkRequest request(const QString &path) const;
};
