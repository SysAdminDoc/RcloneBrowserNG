#include "task_notify.h"
#include "app_log.h"
#include "utils.h"

#include <QtNetwork>

namespace TaskNotify {

QUrl HeartbeatEndpoint(QUrl endpoint, bool success) {
  if (!success) {
    QString path = endpoint.path();
    if (!path.endsWith('/')) {
      path += '/';
    }
    path += "fail";
    endpoint.setPath(path);
  }
  return endpoint;
}

QByteArray WebhookBody(const QString &taskName, bool success,
                       const QString &error) {
  QJsonObject payload;
  payload.insert("app", QStringLiteral("Rclone Browser NG"));
  payload.insert("task", taskName);
  payload.insert("status", success ? "success" : "failed");
  if (!error.isEmpty()) {
    payload.insert("error", error);
  }
  const QString summary =
      success ? QString("Task \"%1\" completed successfully.").arg(taskName)
              : QString("Task \"%1\" failed: %2").arg(taskName, error);
  payload.insert("content", summary);
  return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

void SendAndWait(const QString &heartbeatUrl, const QString &webhookUrl,
                 const QString &taskName, bool success, const QString &error) {
  QNetworkAccessManager network;
  QEventLoop wait;
  int pending = 0;
  const auto track = [&](QNetworkReply *reply, const QString &what) {
    ++pending;
    QObject::connect(reply, &QNetworkReply::finished, &wait, [&, reply, what]() {
      if (reply->error() != QNetworkReply::NoError) {
        AppLog::Write(AppLog::Level::Warning, "schedule",
                      QString("The %1 for \"%2\" didn't go through: %3")
                          .arg(what, taskName, reply->errorString()));
      }
      reply->deleteLater();
      if (--pending == 0) {
        wait.quit();
      }
    });
  };

  QUrl endpoint;
  QString urlError;
  if (!heartbeatUrl.isEmpty()) {
    if (ParseHttpUrl(heartbeatUrl, &endpoint, &urlError)) {
      track(network.get(QNetworkRequest(HeartbeatEndpoint(endpoint, success))),
            "heartbeat");
    } else {
      AppLog::Write(AppLog::Level::Warning, "schedule",
                    "Heartbeat skipped: " + urlError);
    }
  }
  if (!webhookUrl.isEmpty()) {
    if (ParseHttpUrl(webhookUrl, &endpoint, &urlError)) {
      QNetworkRequest request(endpoint);
      request.setHeader(QNetworkRequest::ContentTypeHeader,
                        "application/json");
      track(network.post(request, WebhookBody(taskName, success, error)),
            "webhook");
    } else {
      AppLog::Write(AppLog::Level::Warning, "schedule",
                    "Webhook skipped: " + urlError);
    }
  }
  if (pending == 0) {
    return;
  }
  // A reply that's still running when the timer fires is aborted, which
  // finishes it, so the loop always ends.
  QTimer::singleShot(15000, &wait, [&network]() {
    const auto replies = network.findChildren<QNetworkReply *>();
    for (QNetworkReply *reply : replies) {
      if (reply->isRunning()) {
        reply->abort();
      }
    }
  });
  wait.exec();
}

} // namespace TaskNotify
