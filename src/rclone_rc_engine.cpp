#include "rclone_rc_engine.h"
#include "rclone_capabilities.h"
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
                  if (mOptionIndex.isEmpty() || mEndpoints.isEmpty()) {
                    loadCapabilities([this]() { finishStart(true, QString()); });
                    return;
                  }
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
  // A new daemon may be a different rclone, so its own tables have to be read
  // again rather than inherited from the one that died.
  mEndpoints.clear();
  mOptionIndex = RcSyncRequest::OptionIndex();

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
                loadCapabilities([this]() { finishStart(true, QString()); });
                return;
              }
              // Not up yet. Come back on the event loop rather than sleeping
              // on the thread that has to paint the window.
              QTimer::singleShot(50, this,
                                 [this, deadline]() { pollUntilReady(deadline); });
            });
}

void RcloneRcEngine::loadCapabilities(std::function<void()> done) {
  postAsync("rc/list", QJsonObject(), this,
            [this, done](const QJsonObject &listed, const QString &listError) {
              if (listError.isEmpty()) {
                const QJsonArray commands =
                    listed.value("commands").toArray();
                for (const QJsonValue &entry : commands) {
                  mEndpoints.insert(entry.toObject().value("Path").toString());
                }
              }
              postAsync("options/info", QJsonObject(), this,
                        [this, done](const QJsonObject &info,
                                     const QString &infoError) {
                          if (infoError.isEmpty()) {
                            mOptionIndex = RcSyncRequest::IndexOptions(info);
                          }
                          // Either failure just leaves the tables empty, and
                          // an empty table means every transfer keeps the old
                          // core/command route.
                          if (done) {
                            done();
                          }
                        });
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

void RcloneRcEngine::runCommand(const QStringList &args, QObject *context,
                                JobCallback callback) {
  if (args.isEmpty()) {
    if (callback) {
      StartedJob failed;
      failed.error = QStringLiteral("empty rclone command");
      callback(failed);
    }
    return;
  }

  const QPointer<QObject> contextGuard(context);
  ensureStartedAsync(
      context, [this, args, contextGuard, callback = std::move(callback)](
                   bool ok, const QString &startError) mutable {
        if (!ok) {
          if (callback) {
            StartedJob failed;
            failed.error = startError;
            callback(failed);
          }
          return;
        }
        startTransfer(args, contextGuard, std::move(callback));
      });
}

void RcloneRcEngine::startTransfer(const QStringList &args, QObject *context,
                                   JobCallback callback) {
  const QString group = QString("rclonebrowser-%1").arg(++mGroupCounter);
  const RcSyncRequest::Request request =
      RcSyncRequest::Build(args, group, mOptionIndex);

  if (!request.usable) {
    // Deleting, purging and anything carrying a flag this rclone does not
    // expose over the API all land here. Running the CLI inside the daemon
    // still works; it just has no stats group of its own.
    Diagnostics::appendLog("rc-engine",
                           QString("core/command route: %1").arg(request.reason));
    startCommand(args, QString(), context, std::move(callback));
    return;
  }
  if (!mEndpoints.contains(request.endpoint)) {
    Diagnostics::appendLog(
        "rc-engine",
        QString("core/command route: this rclone has no %1")
            .arg(request.endpoint));
    startCommand(args, QString(), context, std::move(callback));
    return;
  }

  const QPointer<QObject> contextGuard(context);
  resolveSourceIsDirectory(
      request.source, context,
      [this, args, request, contextGuard,
       callback = std::move(callback)](bool isDirectory) mutable {
        if (!isDirectory) {
          Diagnostics::appendLog(
              "rc-engine",
              QStringLiteral("core/command route: the source is a single "
                             "file, which sync/* cannot take"));
          startCommand(args, QString(), contextGuard, std::move(callback));
          return;
        }
        startSync(args, request, contextGuard, std::move(callback));
      });
}

void RcloneRcEngine::resolveSourceIsDirectory(
    const QString &source, QObject *context,
    std::function<void(bool)> callback) {
  const QString trimmed = source.trimmed();
  const int colon = trimmed.indexOf(QChar(':'));
  const bool looksRemote = colon > 1;

  if (!looksRemote) {
    // A local path can be answered without troubling the daemon.
    if (callback) {
      callback(QFileInfo(trimmed).isDir());
    }
    return;
  }
  if (trimmed.endsWith(QChar(':'))) {
    // The root of a remote is a directory by definition.
    if (callback) {
      callback(true);
    }
    return;
  }

  const int lastSlash = trimmed.lastIndexOf(QChar('/'));
  QString fs;
  QString remote;
  if (lastSlash > colon) {
    fs = trimmed.left(lastSlash);
    remote = trimmed.mid(lastSlash + 1);
  } else {
    fs = trimmed.left(colon + 1);
    remote = trimmed.mid(colon + 1);
  }
  if (remote.isEmpty()) {
    if (callback) {
      callback(true);
    }
    return;
  }

  QJsonObject payload;
  payload.insert("fs", fs);
  payload.insert("remote", remote);
  postAsync("operations/stat", payload, context,
            [callback](const QJsonObject &response, const QString &error) {
              if (!callback) {
                return;
              }
              if (!error.isEmpty()) {
                // Unknown means take the route that copes with either.
                callback(false);
                return;
              }
              const QJsonValue item = response.value("item");
              if (!item.isObject()) {
                // Nothing there yet. rclone creates a missing destination and
                // reports a missing source itself, so let sync/* say so.
                callback(true);
                return;
              }
              callback(item.toObject().value("IsDir").toBool());
            });
}

void RcloneRcEngine::startSync(const QStringList &args,
                               const RcSyncRequest::Request &request,
                               QObject *context, JobCallback callback) {
  const QStringList display = rcCommandForDisplay(args);
  const QString group = request.payload.value("_group").toString();
  const QString endpoint = request.endpoint;

  postAsync(endpoint, request.payload, context,
            [callback, group, endpoint, display](const QJsonObject &response,
                                                 const QString &error) {
              if (!callback) {
                return;
              }
              StartedJob job;
              job.group = group;
              job.displayCommand = display;
              if (!error.isEmpty()) {
                job.error = error;
                callback(job);
                return;
              }
              if (!response.contains("jobid")) {
                job.error = QStringLiteral("rclone rc did not return a job id");
                callback(job);
                return;
              }
              job.jobId = response.value("jobid").toInt(-1);
              callback(job);
            });
}

void RcloneRcEngine::startCommand(const QStringList &args,
                                  const QString &group, QObject *context,
                                  JobCallback callback) {
  Q_UNUSED(group);
  QJsonArray commandArgs;
  for (int i = 1; i < args.size(); i++) {
    commandArgs.append(args[i]);
  }

  QJsonObject payload;
  payload.insert("command", args.first());
  payload.insert("arg", commandArgs);
  payload.insert("returnType", "COMBINED_OUTPUT");
  payload.insert("_async", true);

  const QStringList display = rcCommandForDisplay(args);
  postAsync("core/command", payload, context,
            [callback, display](const QJsonObject &response,
                                const QString &error) {
              if (!callback) {
                return;
              }
              StartedJob job;
              job.displayCommand = display;
              if (!error.isEmpty()) {
                job.error = error;
                callback(job);
                return;
              }
              if (!response.contains("jobid")) {
                job.error = QStringLiteral("rclone rc did not return a job id");
                callback(job);
                return;
              }
              job.jobId = response.value("jobid").toInt(-1);
              // rclone names an async core/command job's stats group after
              // its id, which is what the job card has always polled.
              job.group = QString("job/%1").arg(job.jobId);
              callback(job);
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
