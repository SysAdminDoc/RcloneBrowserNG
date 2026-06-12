#pragma once

#include "pch.h"

class RcloneRcEngine : public QObject {
  Q_OBJECT

public:
  explicit RcloneRcEngine(QObject *parent = nullptr);
  ~RcloneRcEngine();

  bool ensureStarted(QString *error);
  int runCommandAsync(const QStringList &args, QString *error);
  QJsonObject jobStatus(int jobId, QString *error);
  QJsonObject coreStats(const QString &group, QString *error);
  bool stopJob(int jobId, QString *error);
  QStringList rcCommandForDisplay(const QStringList &args) const;

private:
  QProcess *mProcess = nullptr;
  QNetworkAccessManager mNetwork;
  QString mUrl;
  QString mUser;
  QString mPass;

  QJsonObject post(const QString &path, const QJsonObject &payload,
                   QString *error);
  QNetworkRequest request(const QString &path) const;
};
