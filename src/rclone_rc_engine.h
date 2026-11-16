#pragma once

#include "pch.h"
#include "rc_delete_batch.h"
#include "rc_sync_request.h"
#include <functional>

class RcloneRcEngine : public QObject {
  Q_OBJECT

public:
  using RcCallback =
      std::function<void(const QJsonObject &result, const QString &error)>;

  explicit RcloneRcEngine(QObject *parent = nullptr);
  ~RcloneRcEngine() override;

  using StartCallback = std::function<void(bool ok, const QString &error)>;

  // Brings the rcd daemon up, or confirms the running one still answers, then
  // calls back. Never blocks: this runs on the click that starts a transfer,
  // and the old synchronous version could hold the window for fifteen seconds
  // with no progress and nothing to cancel.
  void ensureStartedAsync(QObject *context, StartCallback callback);

  // Drives an rcd that is already running somewhere else instead of starting
  // one. An empty url goes back to starting a local daemon. A local daemon
  // this engine started is stopped, because its jobs couldn't be reached
  // through the new address anyway. certificateSha256 is a TLS certificate
  // fingerprint the user trusted (RcCertificate), and an https daemon with any
  // other certificate is refused.
  void useRemoteDaemon(const QString &url, const QString &user,
                       const QString &password,
                       const QString &certificateSha256 = QString());
  bool isRemote() const { return !mRemoteUrl.isEmpty(); }

  struct StartedJob {
    int jobId = -1;
    // The stats group to poll. The two routes name their groups differently:
    // a sync/* call is given an explicit one, while core/command inherits
    // rclone's automatic "job/<id>".
    QString group;
    // What was actually posted, for the job card's details pane. Built here
    // rather than by the caller so it cannot describe the route that was not
    // taken.
    QStringList displayCommand;
    QString error;
    // For a job/batch delete, one label per input in input order, so the
    // job card can name the items rclone couldn't delete.
    QStringList batchLabels;
    // A sync/* call doesn't retry the way the CLI does, so the job card
    // reposts endpoint and payload until attempts runs out, waiting
    // retrySleepMs between runs. Empty on core/command, where the CLI does
    // its own retrying.
    QString endpoint;
    QJsonObject payload;
    int attempts = 1;
    qint64 retrySleepMs = 0;
    // Why a transfer went to core/command, where the card has no live
    // progress to show. Empty when it took sync/*.
    QString routeNote;
    // The daemon's own settings (options/get's "main"), which a sync/* call
    // takes for anything its _config leaves out, the caps included. Empty
    // when the daemon didn't answer options/get.
    QJsonObject daemonMain;
  };

  using JobCallback = std::function<void(const StartedJob &job)>;
  using ResubmitCallback =
      std::function<void(int jobId, const QString &error)>;

  void runCommand(const QStringList &args, QObject *context,
                  JobCallback callback);
  // Deletes every item in the plan through one job/batch call. A job id of
  // -1 means the batch couldn't be used (an older rclone, or an option it
  // can't carry), and error says why, so the caller can fall back to one
  // job per item.
  void runDeleteBatch(const RcDeleteBatch::Plan &plan, QObject *context,
                      JobCallback callback);
  // Posts a StartedJob's endpoint and a payload for the next attempt, which
  // the job card builds (RcJobResult::PlanNextAttempt).
  void resubmit(const QString &endpoint, const QJsonObject &payload,
                QObject *context, const ResubmitCallback &callback);
  // A job that finished with an error comes back as a status with an empty
  // call error; the job's failure is in status["success"] and ["error"].
  void jobStatus(int jobId, QObject *context, const RcCallback &callback);
  void coreStats(const QString &group, QObject *context,
                 const RcCallback &callback);

  // Reads the daemon's bandwidth limit, or sets it when rate is not empty.
  // This is daemon-wide rather than per job: every transfer running on this
  // daemon shares one limit, and rclone normalises what it is given, so "1M"
  // comes back as "1Mi". Callers should show what came back rather than what
  // they sent.
  void bandwidthLimit(const QString &rate, QObject *context,
                      const RcCallback &callback);
  void stopJob(int jobId, QObject *context,
               const std::function<void(bool ok, const QString &error)> &callback);

  QStringList rcCommandForDisplay(const QStringList &args) const;

private:
  QProcess *mProcess = nullptr;
  QNetworkAccessManager mNetwork;
  QString mUrl;
  QString mUser;
  QString mPass;
  QString mRemoteUrl;
  QString mRemoteUser;
  QString mRemotePass;
  QString mRemoteTrusted;
  // The config file the remote daemon reads, from config/paths.
  QString mRemoteConfigPath;
  // Callers that arrive while the daemon is still coming up wait here rather
  // than each starting one of their own.
  QVector<StartCallback> mPendingStarts;
  bool mStarting = false;

  // Read from the daemon's own rc/list and options/info once it is up. Empty
  // means every transfer takes the core/command route, which is what the app
  // always did and is always correct.
  QSet<QString> mEndpoints;
  RcSyncRequest::OptionIndex mOptionIndex;
  QJsonObject mDaemonMain;
  // rclone keeps a stats group for as long as the daemon runs, and a daemon
  // can outlive this engine or serve two copies of the app. A tag of this
  // engine's own keeps its group names from meeting another's.
  QString mGroupTag;
  int mGroupCounter = 0;
  // Bumped whenever the tables are cleared for a different daemon, so the
  // answers of one that's gone can't fill them.
  int mCapabilityGeneration = 0;

  QString nextGroup();
  void startCommand(const QStringList &args, const QString &routeNote,
                    QObject *context, const JobCallback &callback);
  void startTransfer(const QStringList &args, QObject *context,
                     JobCallback callback);
  void startSync(const QStringList &args,
                 const RcSyncRequest::Request &request, QObject *context,
                 const JobCallback &callback);
  // sync/* takes a directory Fs. The CLI splits a single file into a
  // directory plus a leaf and copies just that file, while sync/copy fails
  // the job with "is a file not a directory", so a file source has to stay
  // on the core/command route.
  void resolveSourceIsDirectory(const QString &source, QObject *context,
                                const std::function<void(bool)> &callback);
  // The start itself, for the local or remote daemon that's current.
  void beginStart();
  // True when a start answer is to be dropped: the start already ended, or
  // the daemon changed since generation, in which case the start begins
  // again against the current one.
  bool restartIfStale(int generation);
  // Reads rc/list and the options tables, then ends the start.
  void loadCapabilities();
  void connectRemote();
  void finishRemoteConnect();
  // The --config pair a core/command run needs to see the daemon's remotes.
  QStringList commandConfig() const;
  void spawnDaemon();
  void pollUntilReady(const QElapsedTimer &deadline, int generation);
  void finishStart(bool ok, const QString &error);

  // Only for destructor teardown, where there is no event loop left to
  // return to. Never call this from a path a user click can reach.
  QJsonObject postSync(const QString &path, const QJsonObject &payload,
                       QString *error);
  void postAsync(const QString &path, const QJsonObject &payload,
                 QObject *context, const RcCallback &callback);
  // The rclone rc command line equivalent to posting payload to endpoint,
  // with the password left out.
  QStringList rcCallForDisplay(const QString &endpoint,
                               const QJsonObject &payload) const;
  QNetworkRequest request(const QString &path) const;
};
