#include "rclone_rc_engine.h"
#include "utils.h"

namespace {
constexpr int kRcStartTimeoutMs = 5000;
constexpr int kRcRequestTimeoutMs = 10000;

QString makeRcUrl(quint16 port) {
  return QString("http://localhost:%1/").arg(port);
}
} // namespace

RcloneRcEngine::RcloneRcEngine(QObject *parent) : QObject(parent) {}

RcloneRcEngine::~RcloneRcEngine() {
  if (mProcess && mProcess->state() != QProcess::NotRunning) {
    QString ignored;
    postSync("core/quit", QJsonObject(), &ignored);
    if (!mProcess->waitForFinished(3000)) {
      mProcess->kill();
      // Destructor teardown remains bounded even if rclone ignores kill.
      mProcess->waitForFinished(1000);
    }
  }
}

void RcloneRcEngine::ensureStartedAsync(QObject *context,
                                        StartCallback callback) {
  // Track the context only when the caller gave us one. A null context is the
  // Qt idiom for "no lifetime to follow, always call me", and guarding on a
  // null QPointer would drop that caller's callback forever.
  const bool tracksContext = context != nullptr;
  const QPointer<QObject> contextGuard(context);
  auto guarded = [tracksContext, contextGuard,
                  callback = std::move(callback)](bool ok,
                                                  const QString &error) mutable {
    if (!callback) {
      return;
    }
    if (tracksContext && contextGuard.isNull()) {
      return; // the caller went away while the daemon was coming up
    }
    callback(ok, error);
  };

  if (mStarting) {
    // A start is already in flight; ride along with it.
    mPendingStarts.append(std::move(guarded));
    return;
  }

  mPendingStarts.append(std::move(guarded));
  mStarting = true;

  if (mProcess && mProcess->state() != QProcess::NotRunning) {
    // The daemon looks alive. Confirm it still answers before reusing it,
    // and only respawn if the ping fails.
    postAsync("rc/noopauth", QJsonObject(), this,
              [this](const QJsonObject &, const QString &pingError) {
                if (pingError.isEmpty()) {
                  finishStart(true, QString());
                  return;
                }
                spawnDaemon();
              });
    return;
  }

  spawnDaemon();
}

void RcloneRcEngine::spawnDaemon() {
  if (mProcess) {
    mProcess->deleteLater();
    mProcess = nullptr;
  }

  const quint16 port = static_cast<quint16>(
      29000 + (QRandomGenerator::system()->bounded(20000)));
  mUrl = makeRcUrl(port);
  mUser = "rclonebrowser";
  mPass = MakeRcPassword();

  mProcess = new QProcess(this);
  mProcess->setProcessChannelMode(QProcess::MergedChannels);
  QStringList args;
  args << "rcd" << GetRcloneConf() << "--rc-addr"
       << QString("localhost:%1").arg(port) << "--rc-user" << mUser
       << "--rc-pass" << mPass << "--rc-job-expire-duration" << "1h";
  UseRclonePassword(mProcess);

  QPointer<QProcess> processGuard(mProcess);
  QObject::connect(mProcess, &QProcess::errorOccurred, this,
                   [this, processGuard](QProcess::ProcessError processError) {
                     if (processError != QProcess::FailedToStart) {
                       return;
                     }
                     if (processGuard != mProcess) {
                       return; // a later spawn has replaced this one
                     }
                     finishStart(false, "failed to start rclone rcd");
                   });

  mProcess->start(GetRclone(), args, QIODevice::ReadOnly);

  QElapsedTimer deadline;
  deadline.start();
  pollUntilReady(deadline);
}

void RcloneRcEngine::pollUntilReady(const QElapsedTimer &deadline) {
  if (!mStarting) {
    return;
  }
  if (deadline.elapsed() >= kRcStartTimeoutMs) {
    QString output;
    if (mProcess) {
      output = QString::fromUtf8(mProcess->readAll()).trimmed();
    }
    finishStart(false, output.isEmpty() ? QString("rclone rcd did not become "
                                                  "ready")
                                        : output);
    return;
  }

  postAsync("rc/noopauth", QJsonObject(), this,
            [this, deadline](const QJsonObject &, const QString &pingError) {
              if (!mStarting) {
                return;
              }
              if (pingError.isEmpty()) {
                finishStart(true, QString());
                return;
              }
              // Not up yet. Come back on the event loop rather than sleeping
              // on the thread that has to paint the window.
              QTimer::singleShot(50, this,
                                 [this, deadline]() { pollUntilReady(deadline); });
            });
}

void RcloneRcEngine::finishStart(bool ok, const QString &error) {
  if (!mStarting) {
    return;
  }
  mStarting = false;
  QVector<StartCallback> waiting;
  waiting.swap(mPendingStarts);

  // QProcess::start emits errorOccurred synchronously when the program cannot
  // be launched at all, so without this a caller could be re-entered from
  // inside its own call to ensureStartedAsync. Callers always resume on the
  // event loop instead.
  QTimer::singleShot(0, this, [waiting = std::move(waiting), ok, error]() {
    for (const StartCallback &callback : waiting) {
      if (callback) {
        callback(ok, error);
      }
    }
  });
}

void RcloneRcEngine::runCommand(
    const QStringList &args, QObject *context,
    std::function<void(int jobId, const QString &error)> callback) {
  if (args.isEmpty()) {
    if (callback) {
      callback(-1, QStringLiteral("empty rclone command"));
    }
    return;
  }

  const QPointer<QObject> contextGuard(context);
  ensureStartedAsync(
      context, [this, args, contextGuard, callback = std::move(callback)](
                   bool ok, const QString &startError) mutable {
        if (!ok) {
          if (callback) {
            callback(-1, startError);
          }
          return;
        }
        startCommand(args, contextGuard, std::move(callback));
      });
}

void RcloneRcEngine::startCommand(
    const QStringList &args, QObject *context,
    std::function<void(int jobId, const QString &error)> callback) {
  QJsonArray commandArgs;
  for (int i = 1; i < args.size(); i++) {
    commandArgs.append(args[i]);
  }

  QJsonObject payload;
  payload.insert("command", args.first());
  payload.insert("arg", commandArgs);
  payload.insert("returnType", "COMBINED_OUTPUT");
  payload.insert("_async", true);

  postAsync("core/command", payload, context,
            [callback](const QJsonObject &response, const QString &error) {
              if (!error.isEmpty()) {
                if (callback) {
                  callback(-1, error);
                }
                return;
              }
              if (response.contains("jobid")) {
                if (callback) {
                  callback(response.value("jobid").toInt(-1), QString());
                }
                return;
              }
              if (callback) {
                callback(-1,
                         QStringLiteral("rclone rc did not return a job id"));
              }
            });
}

void RcloneRcEngine::jobStatus(int jobId, QObject *context,
                               RcCallback callback) {
  QJsonObject payload;
  payload.insert("jobid", jobId);
  postAsync("job/status", payload, context, std::move(callback));
}

void RcloneRcEngine::coreStats(const QString &group, QObject *context,
                               RcCallback callback) {
  QJsonObject payload;
  if (!group.isEmpty()) {
    payload.insert("group", group);
  }
  postAsync("core/stats", payload, context, std::move(callback));
}

void RcloneRcEngine::stopJob(
    int jobId, QObject *context,
    std::function<void(bool ok, const QString &error)> callback) {
  QJsonObject payload;
  payload.insert("jobid", jobId);
  postAsync("job/stop", payload, context,
            [callback](const QJsonObject &, const QString &error) {
              if (callback) {
                callback(error.isEmpty(), error);
              }
            });
}

QStringList RcloneRcEngine::rcCommandForDisplay(const QStringList &args) const {
  QJsonArray commandArgs;
  for (int i = 1; i < args.size(); i++) {
    commandArgs.append(args[i]);
  }

  QJsonObject payload;
  if (!args.isEmpty()) {
    payload.insert("command", args.first());
  }
  payload.insert("arg", commandArgs);
  payload.insert("returnType", "COMBINED_OUTPUT");
  payload.insert("_async", true);

  return QStringList() << QDir::toNativeSeparators(GetRclone()) << "rc"
                       << "--url" << mUrl << "--user" << mUser << "--pass"
                       << "<redacted>" << "--json"
                       << QString::fromUtf8(QJsonDocument(payload).toJson(
                              QJsonDocument::Compact))
                       << "core/command";
}

void RcloneRcEngine::postAsync(const QString &path, const QJsonObject &payload,
                               QObject *context, RcCallback callback) {
  QNetworkRequest req = request(path);
  QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  QNetworkReply *reply = mNetwork.post(req, body);

  auto *timer = new QTimer(reply);
  timer->setSingleShot(true);
  QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
  timer->start(kRcRequestTimeoutMs);

  QObject::connect(context, &QObject::destroyed, reply,
                   &QNetworkReply::deleteLater);

  QObject::connect(
      reply, &QNetworkReply::finished, context, [reply, callback]() {
        reply->deleteLater();

        const QByteArray raw = reply->readAll();
        const auto networkError = reply->error();
        const QString networkErrorText = reply->errorString();

        if (networkError == QNetworkReply::OperationCanceledError) {
          if (callback) {
            callback(QJsonObject(),
                     QStringLiteral("rclone rc request timed out"));
          }
          return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
          if (callback) {
            callback(QJsonObject(),
                     raw.isEmpty() ? networkErrorText
                                   : QString::fromUtf8(raw));
          }
          return;
        }

        const QJsonObject object = doc.object();
        if (networkError != QNetworkReply::NoError ||
            object.contains("error")) {
          if (callback) {
            callback(object,
                     object.value("error").toString(networkErrorText));
          }
          return;
        }

        if (callback) {
          callback(object, QString());
        }
      });
}

QJsonObject RcloneRcEngine::postSync(const QString &path,
                                     const QJsonObject &payload,
                                     QString *error) {
  if (error) {
    error->clear();
  }

  QNetworkRequest req = request(path);
  QNetworkReply *reply =
      mNetwork.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));

  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(kRcRequestTimeoutMs);
  loop.exec();

  if (!timer.isActive()) {
    reply->abort();
    reply->deleteLater();
    if (error) {
      *error = "rclone rc request timed out";
    }
    return QJsonObject();
  }

  const QByteArray raw = reply->readAll();
  const QNetworkReply::NetworkError networkError = reply->error();
  const QString networkErrorText = reply->errorString();
  reply->deleteLater();

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) {
      *error = raw.isEmpty() ? networkErrorText : QString::fromUtf8(raw);
    }
    return QJsonObject();
  }

  const QJsonObject object = doc.object();
  if (networkError != QNetworkReply::NoError || object.contains("error")) {
    if (error) {
      *error = object.value("error").toString(networkErrorText);
    }
    return object;
  }

  return object;
}

QNetworkRequest RcloneRcEngine::request(const QString &path) const {
  QUrl url(mUrl + path);
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  const QByteArray credential =
      QString("%1:%2").arg(mUser, mPass).toUtf8().toBase64();
  req.setRawHeader("Authorization", "Basic " + credential);
  return req;
}
