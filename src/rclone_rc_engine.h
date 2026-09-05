#pragma once

#include "pch.h"
#include "rc_sync_request.h"
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
  };

  using JobCallback = std::function<void(const StartedJob &job)>;

  void runCommand(const QStringList &args, QObject *context,
                  JobCallback callback);
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

  // Read from the daemon's own rc/list and options/info once it is up. Empty
  // means every transfer takes the core/command route, which is what the app
  // always did and is always correct.
  QSet<QString> mEndpoints;
  RcSyncRequest::OptionIndex mOptionIndex;
  int mGroupCounter = 0;

  void startCommand(const QStringList &args, const QString &group,
                    QObject *context, JobCallback callback);
  void startTransfer(const QStringList &args, QObject *context,
                     JobCallback callback);
  void startSync(const QStringList &args,
                 const RcSyncRequest::Request &request, QObject *context,
                 JobCallback callback);
  // sync/* takes a directory Fs. The CLI splits a single file into a
  // directory plus a leaf and copies just that file, while sync/copy fails
  // the job with "is a file not a directory", so a file source has to stay
  // on the core/command route.
  void resolveSourceIsDirectory(const QString &source, QObject *context,
                                std::function<void(bool)> callback);
  void loadCapabilities(std::function<void()> done);
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
