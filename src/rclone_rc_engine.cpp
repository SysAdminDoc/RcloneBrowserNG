#include "rclone_rc_engine.h"
#include "rc_certificate.h"
#include "rclone_capabilities.h"
#include "utils.h"

namespace {
constexpr int kRcStartTimeoutMs = 5000;
constexpr int kRcRequestTimeoutMs = 10000;

QString makeRcUrl(quint16 port) {
  return QString("http://localhost:%1/").arg(port);
}

// The command and arg array for core/command, which runs them as a second
// rclone process with only these arguments ("command" first, then "arg").
// The daemon's own --config and --password-command don't reach it, so a
// remote that only exists in the config chosen in Preferences fails with
// "didn't find section in config file". conf is flag/value pairs, the way
// GetRcloneConf() returns them. They go ahead of everything, as the process
// route runs `rclone --config <conf> <args>`: the task's own --config or
// --password-command later in the arguments still wins, since pflag keeps a
// flag's last value, and a pair after the arguments landed behind a "--",
// where rclone took it for two more paths. A download of several selected
// items opens with --filter rules, and a pair right after the first one
// split it from its rule.
QJsonObject coreCommandPayload(const QStringList &args,
                               const QStringList &conf) {
  QStringList all = conf;
  all << args;
  QJsonObject payload;
  if (!all.isEmpty()) {
    payload.insert("command", all.first());
  }
  QJsonArray arg;
  for (qsizetype i = 1; i < all.size(); i++) {
    arg.append(all.at(i));
  }
  payload.insert("arg", arg);
  return payload;
}

} // namespace

RcloneRcEngine::RcloneRcEngine(QObject *parent)
    : QObject(parent),
      mGroupTag(QString::number(QRandomGenerator::system()->generate(), 16)) {}

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
  beginStart();
}

void RcloneRcEngine::beginStart() {
  if (isRemote()) {
    connectRemote();
    return;
  }

  if (mProcess && mProcess->state() != QProcess::NotRunning) {
    // The daemon looks alive. Confirm it still answers before reusing it,
    // and only respawn if the ping fails.
    const int generation = mCapabilityGeneration;
    postAsync("rc/noopauth", QJsonObject(), this,
              [this, generation](const QJsonObject &,
                                 const QString &pingError) {
                if (restartIfStale(generation)) {
                  return;
                }
                if (pingError.isEmpty()) {
                  if (mOptionIndex.isEmpty() || mEndpoints.isEmpty()) {
                    loadCapabilities();
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

bool RcloneRcEngine::restartIfStale(int generation) {
  if (!mStarting) {
    return true;
  }
  if (generation == mCapabilityGeneration) {
    return false;
  }
  // The daemon was switched or respawned while this start was talking to
  // the old one. Its answers say nothing about the rclone that's current
  // now, so the start begins again there and the waiters get that answer.
  beginStart();
  return true;
}

void RcloneRcEngine::useRemoteDaemon(const QString &url, const QString &user,
                                     const QString &password,
                                     const QString &certificateSha256) {
  if (url == mRemoteUrl && user == mRemoteUser && password == mRemotePass &&
      certificateSha256 == mRemoteTrusted) {
    return;
  }
  // Qt sends a request down a connection it already has open, with no
  // handshake for the certificate check to judge, so connections opened
  // under the old fingerprint must not carry requests made under the new.
  if (RcCertificate::Normalize(certificateSha256) !=
      RcCertificate::Normalize(mRemoteTrusted)) {
    mNetwork.clearConnectionCache();
  }
  mRemoteUrl = url;
  mRemoteUser = user;
  mRemotePass = password;
  mRemoteTrusted = certificateSha256;
  mRemoteConfigPath.clear();
  // Whatever answers next is a different rclone with its own tables.
  mEndpoints.clear();
  mOptionIndex = RcSyncRequest::OptionIndex();
  mDaemonMain = QJsonObject();
  mCapabilityGeneration++;
  mUrl.clear();
  if (mProcess) {
    if (mProcess->state() != QProcess::NotRunning) {
      mProcess->kill();
      mProcess->waitForFinished(1000);
    }
    mProcess->deleteLater();
    mProcess = nullptr;
  }
}

void RcloneRcEngine::connectRemote() {
  mUrl = mRemoteUrl;
  mUser = mRemoteUser;
  mPass = mRemotePass;
  const int generation = mCapabilityGeneration;
  // rc/noopauth needs a login, as core/command does, so this fails for the
  // same reasons a transfer would rather than after it's been posted.
  postAsync("rc/noopauth", QJsonObject(), this,
            [this, generation](const QJsonObject &, const QString &pingError) {
              if (restartIfStale(generation)) {
                return;
              }
              if (!pingError.isEmpty()) {
                finishStart(false,
                            QString("Couldn't reach the remote rclone at "
                                    "%1: %2")
                                .arg(mRemoteUrl, pingError));
                return;
              }
              if (!mRemoteConfigPath.isEmpty()) {
                finishRemoteConnect();
                return;
              }
              // core/command starts a second rclone on that machine, and it
              // reads the default config unless told which one the daemon
              // has. An rclone too old for config/paths gets the default.
              postAsync("config/paths", QJsonObject(), this,
                        [this, generation](const QJsonObject &paths,
                                           const QString &) {
                          // The old daemon's config path would send the new
                          // one's core/command runs to the wrong config.
                          if (restartIfStale(generation)) {
                            return;
                          }
                          mRemoteConfigPath =
                              paths.value("config").toString();
                          finishRemoteConnect();
                        });
            });
}

void RcloneRcEngine::finishRemoteConnect() {
  if (!mStarting) {
    return;
  }
  if (mEndpoints.isEmpty() || mOptionIndex.isEmpty()) {
    loadCapabilities();
    return;
  }
  finishStart(true, QString());
}

QStringList RcloneRcEngine::commandConfig() const {
  if (!isRemote()) {
    return GetRcloneConf();
  }
  if (mRemoteConfigPath.isEmpty()) {
    return QStringList();
  }
  return QStringList() << "--config" << mRemoteConfigPath;
}

void RcloneRcEngine::spawnDaemon() {
  // A new daemon may be a different rclone, so its own tables have to be read
  // again rather than inherited from the one that died.
  mEndpoints.clear();
  mOptionIndex = RcSyncRequest::OptionIndex();
  mDaemonMain = QJsonObject();
  mCapabilityGeneration++;

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
  pollUntilReady(deadline, mCapabilityGeneration);
}

void RcloneRcEngine::pollUntilReady(const QElapsedTimer &deadline,
                                    int generation) {
  // A switch to a remote daemon kills the one this waits for, and pinging
  // it until the deadline would only end in a false "didn't start".
  if (restartIfStale(generation)) {
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
            [this, deadline, generation](const QJsonObject &,
                                         const QString &pingError) {
              if (restartIfStale(generation)) {
                return;
              }
              if (pingError.isEmpty()) {
                loadCapabilities();
                return;
              }
              // Not up yet. Come back on the event loop rather than sleeping
              // on the thread that has to paint the window.
              QTimer::singleShot(50, this, [this, deadline, generation]() {
                pollUntilReady(deadline, generation);
              });
            });
}

void RcloneRcEngine::loadCapabilities() {
  // Reads the daemon's tables, then ends the start. A daemon switch or
  // respawn while these are in flight clears the tables for the new rclone,
  // so the old one's answers are dropped and the start begins again there.
  // Ending it as a success would tell the waiters a daemon nobody has
  // reached yet is up.
  const int generation = mCapabilityGeneration;
  postAsync("rc/list", QJsonObject(), this,
            [this, generation](const QJsonObject &listed,
                               const QString &listError) {
              if (restartIfStale(generation)) {
                return;
              }
              if (listError.isEmpty()) {
                const QJsonArray commands =
                    listed.value("commands").toArray();
                for (const auto &entry : commands) {
                  mEndpoints.insert(entry.toObject().value("Path").toString());
                }
              }
              postAsync("options/info", QJsonObject(), this,
                        [this, generation](const QJsonObject &info,
                                           const QString &infoError) {
                          if (restartIfStale(generation)) {
                            return;
                          }
                          if (infoError.isEmpty()) {
                            mOptionIndex = RcSyncRequest::IndexOptions(info);
                          }
                          // Either failure just leaves the tables empty, and
                          // an empty table means every transfer keeps the old
                          // core/command route.
                          if (mOptionIndex.isEmpty()) {
                            finishStart(true, QString());
                            return;
                          }
                          // The delete timings are only in options/get.
                          postAsync("options/get", QJsonObject(), this,
                                    [this, generation](
                                        const QJsonObject &values,
                                        const QString &getError) {
                                      if (restartIfStale(generation)) {
                                        return;
                                      }
                                      if (getError.isEmpty()) {
                                        RcSyncRequest::AddDeleteModes(
                                            &mOptionIndex, values);
                                        mDaemonMain =
                                            values.value("main").toObject();
                                      }
                                      finishStart(true, QString());
                                    });
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
  const int generation = mCapabilityGeneration;
  QTimer::singleShot(
      0, this, [this, waiting = std::move(waiting), ok, error, generation]() {
        for (qsizetype i = 0; i < waiting.size(); i++) {
          if (generation != mCapabilityGeneration) {
            // The daemon was switched before this waiter heard, by an
            // earlier waiter or anything else on the way here. The answer
            // was the old daemon's, so the rest wait for a start on the
            // new one, as a switch in the middle of a start does.
            for (qsizetype rest = i; rest < waiting.size(); rest++) {
              mPendingStarts.append(waiting.at(rest));
            }
            if (!mStarting) {
              mStarting = true;
              beginStart();
            }
            return;
          }
          if (waiting.at(i)) {
            waiting.at(i)(ok, error);
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
  const QString group = nextGroup();
  const RcSyncRequest::Request request =
      RcSyncRequest::Build(args, group, mOptionIndex);

  if (!request.usable) {
    // Deleting, purging and anything carrying a flag this rclone does not
    // expose over the API all land here. Running the CLI inside the daemon
    // still works; it just has no stats group of its own.
    startCommandRoute(args, request.reason, context, callback);
    return;
  }
  if (!mEndpoints.contains(request.endpoint)) {
    const QString reason =
        QString("this rclone has no %1").arg(request.endpoint);
    startCommandRoute(args, reason, context, callback);
    return;
  }
  // The daemon ran the pair with its --compare-dest and dropped the task's
  // --copy-dest. The CLI inherits the same setting and refuses the pair.
  const QString conflict = RcSyncRequest::DaemonConflictReason(
      request.payload.value(QLatin1String("_config")).toObject(), mDaemonMain);
  if (!conflict.isEmpty()) {
    startCommandRoute(args, conflict, context, callback);
    return;
  }

  const QPointer<QObject> contextGuard(context);
  resolveSourceIsDirectory(
      request.source, context,
      [this, args, request, contextGuard,
       callback = std::move(callback)](bool isDirectory) mutable {
        if (!isDirectory) {
          const QString reason = QStringLiteral(
              "the source is a single file, which sync/* cannot take");
          startCommandRoute(args, reason, contextGuard, callback);
          return;
        }
        // A dry run stays on sync/*, which skips every global. option,
        // global.dry_run among them. The CLI in the daemon applies a
        // section's global.dry_run = false over --dry-run (measured on
        // v1.76.0: core/command deleted the files a sync/sync dry run kept),
        // so the dry run shows the run without the config's other global.
        // options rather than risk becoming a real one. One that can't take
        // sync/* is checked in startCommandRoute.
        if (request.payload.value(QLatin1String("_config"))
                .toObject()
                .value(QLatin1String("DryRun"))
                .toBool()) {
          startSync(args, request, contextGuard, callback);
          return;
        }
        // A global. option in rclone.conf or the daemon's environment
        // applies to the CLI and not to sync/*, so read the config the daemon
        // has before committing. Only this PC's environment is known; a
        // remote daemon's own variables can't be read over rc.
        const QStringList environment =
            isRemote() ? QStringList()
                       : QProcessEnvironment::systemEnvironment().toStringList();
        QObject *owner = contextGuard ? contextGuard.data()
                                      : static_cast<QObject *>(this);
        postAsync(
            "config/dump", QJsonObject(), owner,
            [this, args, request, contextGuard, environment,
             callback = std::move(callback)](const QJsonObject &dump,
                                             const QString &error) {
              if (!error.isEmpty()) {
                // The config can't be read, so the run goes where a global.
                // option in it still applies.
                const QString reason =
                    QString("config/dump failed (%1), so rclone.conf can't be "
                            "checked for global. options")
                        .arg(error);
                startCommandRoute(args, reason, contextGuard, callback);
                return;
              }
              const QString reason =
                  RcSyncRequest::ConfigGlobalsReason(dump, environment);
              if (!reason.isEmpty()) {
                startCommandRoute(args, reason, contextGuard, callback);
                return;
              }
              startSync(args, request, contextGuard, callback);
            });
      });
}

void RcloneRcEngine::resolveSourceIsDirectory(
    const QString &source, QObject *context,
    const std::function<void(bool)> &callback) {
  const QString trimmed = source.trimmed();
  const qsizetype colon = RcSyncRequest::RemoteColon(trimmed);
  const bool looksRemote = colon > 0;

  if (!looksRemote && !isRemote()) {
    // A local path can be answered without troubling the daemon.
    if (callback) {
      callback(QFileInfo(trimmed).isDir());
    }
    return;
  }
  if (looksRemote && trimmed.endsWith(QChar(':'))) {
    // The root of a remote is a directory by definition.
    if (callback) {
      callback(true);
    }
    return;
  }

  // A local path on a remote daemon is a path on that machine, so only the
  // daemon can say what it is. ":local:C:/a.txt" and ":local:C:\dir\a.txt"
  // are local paths too, looked at from "C:/" and "C:\dir".
  const RcSyncRequest::StatTarget target =
      RcSyncRequest::StatTargetFor(trimmed);
  if (!looksRemote && target.fs.isEmpty()) {
    // No folder to look in, so take the route that copes with either.
    if (callback) {
      callback(false);
    }
    return;
  }
  const QString &fs = target.fs;
  const QString &remote = target.remote;
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
                               QObject *context, const JobCallback &callback) {
  Q_UNUSED(args);
  const QString group = request.payload.value("_group").toString();
  const QString endpoint = request.endpoint;
  // What's posted is the sync/* call, so that's what the card shows.
  const QStringList display = rcCallForDisplay(endpoint, request.payload);
  const QJsonObject daemonMain = mDaemonMain;

  postAsync(endpoint, request.payload, context,
            [callback, group, request, display,
             daemonMain](const QJsonObject &response, const QString &error) {
              if (!callback) {
                return;
              }
              StartedJob job;
              job.group = group;
              job.displayCommand = display;
              job.endpoint = request.endpoint;
              job.payload = request.payload;
              job.attempts = request.retries;
              job.retrySleepMs = request.retriesSleepMs;
              job.daemonMain = daemonMain;
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

QString RcloneRcEngine::nextGroup() {
  return QString("rclonebrowser-%1-%2").arg(mGroupTag).arg(++mGroupCounter);
}

void RcloneRcEngine::startCommandRoute(const QStringList &args,
                                       const QString &routeNote,
                                       QObject *context,
                                       const JobCallback &callback) {
  Diagnostics::appendLog("rc-engine",
                         QString("core/command route: %1").arg(routeNote));
  if (!RcSyncRequest::IsDryRun(args)) {
    startCommand(args, routeNote, context, callback);
    return;
  }
  // The CLI writes a section's global.dry_run into its global config when
  // the path opens, after --dry-run was read (addConfigToContext in
  // fs/newfs.go, v1.76.0), so with global.dry_run = false a dry run here
  // deleted the destination. Nothing on the command line outranks it, so
  // such a dry run doesn't start. `rclone config dump` run through
  // core/command the way the task runs reads the rclone.conf the task reads,
  // its own --config included, which config/dump can't. Only this PC's
  // environment is known.
  const QStringList environment =
      isRemote() ? QStringList()
                 : QProcessEnvironment::systemEnvironment().toStringList();
  const QPointer<QObject> contextGuard(context);
  QObject *owner = context ? context : static_cast<QObject *>(this);
  QJsonObject payload = coreCommandPayload(
      RcSyncRequest::ConfigDumpArguments(args), commandConfig());
  payload.insert("returnType", "COMBINED_OUTPUT");
  postAsync(
      "core/command", payload, owner,
      [this, args, routeNote, contextGuard, environment,
       callback](const QJsonObject &response, const QString &rcError) {
        const QByteArray output =
            response.value("result").toString().toUtf8();
        QJsonObject dump;
        QString error = rcError;
        if (error.isEmpty() &&
            (response.value("error").toBool() ||
             !RcSyncRequest::ParseConfigDump(output, &dump))) {
          error = QString::fromUtf8(output).trimmed().section(QChar('\n'), -1);
          if (error.isEmpty()) {
            error = QStringLiteral("rclone config dump printed nothing");
          }
        }
        QString refusal;
        if (!error.isEmpty()) {
          refusal = QString("This dry run has to run as a separate rclone "
                            "(%1), and rclone.conf couldn't be read (%2) to "
                            "check for a global.dry_run that would make it a "
                            "real run. Nothing was run.")
                        .arg(routeNote, error);
        } else {
          const QString setter =
              RcSyncRequest::DryRunGlobalReason(dump, environment);
          if (!setter.isEmpty()) {
            refusal = QString("This dry run has to run as a separate rclone "
                              "(%1), where %2 would turn --dry-run off and "
                              "make it a real run. Nothing was run.")
                          .arg(routeNote, setter);
          }
        }
        if (refusal.isEmpty()) {
          startCommand(args, routeNote, contextGuard, callback);
          return;
        }
        Diagnostics::appendLog("rc-engine", refusal);
        if (!callback) {
          return;
        }
        StartedJob refused;
        refused.error = refusal;
        refused.routeNote = routeNote;
        refused.displayCommand = rcCommandForDisplay(args);
        refused.refused = true;
        callback(refused);
      });
}

void RcloneRcEngine::startCommand(const QStringList &args,
                                  const QString &routeNote, QObject *context,
                                  const JobCallback &callback) {
  QJsonObject payload = coreCommandPayload(args, commandConfig());
  payload.insert("returnType", "COMBINED_OUTPUT");
  payload.insert("_async", true);

  const QStringList display = rcCommandForDisplay(args);
  postAsync("core/command", payload, context,
            [callback, display, routeNote](const QJsonObject &response,
                                           const QString &error) {
              if (!callback) {
                return;
              }
              StartedJob job;
              job.displayCommand = display;
              job.routeNote = routeNote;
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

void RcloneRcEngine::runDeleteBatch(const RcDeleteBatch::Plan &plan,
                                    QObject *context, JobCallback callback) {
  const QPointer<QObject> contextGuard(context);
  ensureStartedAsync(context, [this, plan, contextGuard,
                               callback = std::move(callback)](
                                  bool ok, const QString &startError) {
    StartedJob refused;
    if (!ok) {
      refused.error = startError;
    } else if (!mEndpoints.contains(QStringLiteral("job/batch"))) {
      refused.error = QStringLiteral("this rclone has no job/batch");
    }
    RcDeleteBatch::Request request;
    if (refused.error.isEmpty()) {
      request = RcDeleteBatch::Build(plan, mOptionIndex);
      if (!request.usable) {
        refused.error = request.reason;
      }
    }
    if (!refused.error.isEmpty()) {
      Diagnostics::appendLog(
          "rc-engine",
          QString("one delete job per item: %1").arg(refused.error));
      if (callback) {
        callback(refused);
      }
      return;
    }

    const QString group = nextGroup();
    QJsonObject payload = request.payload;
    payload.insert("_group", group);
    payload.insert("_async", true);
    const QStringList display = rcCallForDisplay("job/batch", payload);
    const QStringList labels = request.labels;

    postAsync("job/batch", payload, contextGuard,
              [callback, group, display, labels](const QJsonObject &response,
                                                 const QString &error) {
                if (!callback) {
                  return;
                }
                StartedJob job;
                job.group = group;
                job.displayCommand = display;
                job.batchLabels = labels;
                if (!error.isEmpty()) {
                  job.error = error;
                } else if (!response.contains("jobid")) {
                  job.error =
                      QStringLiteral("rclone rc did not return a job id");
                } else {
                  job.jobId = response.value("jobid").toInt(-1);
                }
                callback(job);
              });
  });
}

void RcloneRcEngine::resubmit(const QString &endpoint,
                              const QJsonObject &payload, QObject *context,
                              const ResubmitCallback &callback) {
  postAsync(endpoint, payload, context,
            [callback](const QJsonObject &response, const QString &error) {
              if (!callback) {
                return;
              }
              if (!error.isEmpty()) {
                callback(-1, error);
                return;
              }
              if (!response.contains("jobid")) {
                callback(-1, QStringLiteral("rclone rc did not return a job id"));
                return;
              }
              callback(response.value("jobid").toInt(-1), QString());
            });
}

void RcloneRcEngine::jobStatus(int jobId, QObject *context,
                               const RcCallback &callback) {
  QJsonObject payload;
  payload.insert("jobid", jobId);
  // A failed job's status is still a good answer: rclone sends it with
  // HTTP 200 and the job's own "error" text, which postAsync would report
  // as a failed call. The caller then never saw "finished" and polled the
  // job forever. Only an answer without the job's fields (an unknown job id,
  // a dead daemon) is a failed call.
  postAsync("job/status", payload, context,
            [callback](const QJsonObject &status, const QString &error) {
              if (!callback) {
                return;
              }
              if (!error.isEmpty() && status.contains("finished") &&
                  status.contains("id")) {
                callback(status, QString());
                return;
              }
              callback(status, error);
            });
}

void RcloneRcEngine::coreStats(const QString &group, QObject *context,
                               const RcCallback &callback) {
  QJsonObject payload;
  if (!group.isEmpty()) {
    payload.insert("group", group);
  }
  postAsync("core/stats", payload, context, callback);
}

void RcloneRcEngine::transferred(const QString &group, QObject *context,
                                 const RcCallback &callback) {
  QJsonObject payload;
  if (!group.isEmpty()) {
    payload.insert("group", group);
  }
  postAsync("core/transferred", payload, context, callback);
}

void RcloneRcEngine::daemonSettings(QObject *context,
                                    const RcCallback &callback) {
  postAsync("options/get", QJsonObject(), context, callback);
}

void RcloneRcEngine::bandwidthLimit(const QString &rate, QObject *context,
                                    const RcCallback &callback) {
  QJsonObject payload;
  if (!rate.isEmpty()) {
    payload.insert("rate", rate);
  }
  postAsync("core/bwlimit", payload, context, callback);
}

void RcloneRcEngine::stopJob(
    int jobId, QObject *context,
    const std::function<void(bool ok, const QString &error)> &callback) {
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
  QJsonObject payload = coreCommandPayload(args, commandConfig());
  payload.insert("returnType", "COMBINED_OUTPUT");
  payload.insert("_async", true);

  return rcCallForDisplay("core/command", payload);
}

QStringList RcloneRcEngine::rcCallForDisplay(const QString &endpoint,
                                             const QJsonObject &payload) const {
  return QStringList() << QDir::toNativeSeparators(GetRclone()) << "rc"
                       << "--url" << mUrl << "--user" << mUser << "--pass"
                       << "<redacted>" << "--json"
                       << QString::fromUtf8(QJsonDocument(payload).toJson(
                              QJsonDocument::Compact))
                       << endpoint;
}

void RcloneRcEngine::postAsync(const QString &path, const QJsonObject &payload,
                               QObject *context, const RcCallback &callback) {
  if (mUrl.isEmpty()) {
    // No daemon yet, so the request would go to a relative URL and fail in a
    // way that reads like a server error. Answer on the event loop instead,
    // which is where every other reply arrives.
    if (callback) {
      const QPointer<QObject> contextGuard(context);
      QTimer::singleShot(0, this, [contextGuard, context, callback]() {
        if (context && contextGuard.isNull()) {
          return;
        }
        callback(QJsonObject(),
                 QStringLiteral("the rclone remote control daemon is not "
                                "running"));
      });
    }
    return;
  }

  QNetworkRequest req = request(path);
  QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  QNetworkReply *reply = mNetwork.post(req, body);
  const QString trusted = mRemoteTrusted;
  RcCertificate::Guard(reply, trusted);

  auto *timer = new QTimer(reply);
  timer->setSingleShot(true);
  QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
  timer->start(kRcRequestTimeoutMs);

  // A null context means the reply isn't tied to any object, as in the early
  // answer above. Qt refuses a connection with a null receiver, so the engine
  // receives it then.
  QObject *receiver = context ? context : static_cast<QObject *>(this);
  if (context) {
    QObject::connect(context, &QObject::destroyed, reply,
                     &QNetworkReply::deleteLater);
  }

  QObject::connect(
      reply, &QNetworkReply::finished, receiver,
      [reply, callback, trusted, url = mUrl]() {
        reply->deleteLater();

        const QByteArray raw = reply->readAll();
        const auto networkError = reply->error();
        const QString networkErrorText = reply->errorString();

        // Before the timeout case, since refusing a certificate aborts too.
        const QString certificateError =
            RcCertificate::Problem(reply, url, trusted);
        if (!certificateError.isEmpty()) {
          if (callback) {
            callback(QJsonObject(), certificateError);
          }
          return;
        }

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

        // rclone's own failures carry "error" as a string. A synchronous
        // core/command answers 200 with "error" as a bool that only says
        // how the command exited, false included.
        const QJsonObject object = doc.object();
        if (networkError != QNetworkReply::NoError ||
            object.value("error").isString()) {
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
  RcCertificate::Guard(reply, mRemoteTrusted);

  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(kRcRequestTimeoutMs);
  loop.exec();

  if (reply->isFinished()) {
    const QString certificateError =
        RcCertificate::Problem(reply, mUrl, mRemoteTrusted);
    if (!certificateError.isEmpty()) {
      reply->deleteLater();
      if (error) {
        *error = certificateError;
      }
      return QJsonObject();
    }
  }

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
  if (networkError != QNetworkReply::NoError ||
      object.value("error").isString()) {
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
  RcCertificate::Prepare(req, mRemoteTrusted);
  return req;
}
