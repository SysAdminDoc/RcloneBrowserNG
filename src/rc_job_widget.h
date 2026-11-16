#pragma once

#include "pch.h"
#include "job_history.h"
#include "rc_job_result.h"
#include "rclone_rc_engine.h"
#include "ui_job_widget.h"

class AdaptiveBandwidthDriver;

class RcJobWidget : public QWidget {
  Q_OBJECT

public:
  // The group is supplied rather than derived: a sync/* job is given an
  // explicit one, so "job/<id>" is only right for the core/command route.
  RcJobWidget(RcloneRcEngine *engine, int jobId, const QString &group,
              const QString &info, const QStringList &displayArgs,
              const QString &source, const QString &dest,
              QWidget *parent = nullptr);
  ~RcJobWidget() override;

  void showDetails();
  // Marks this card as a job/batch delete. rclone reports such a job as a
  // success even when items in it failed, so the card reads the per-item
  // results instead and names each failure.
  void setBatchLabels(const QStringList &labels) { mBatchLabels = labels; }
  // The rclone arguments history keeps for Restart and Dry Run, the same
  // list the process route records. Without them the row has no command to
  // rerun: the card's own rc call is for display only.
  void setHistoryArgs(const QStringList &args) { mHistoryArgs = args; }
  // What the engine said about the start: how to run the job again when an
  // attempt fails (sync/* doesn't retry, the CLI does), and why a transfer
  // went to core/command instead, which the output pane then explains.
  void setStartedJob(const RcloneRcEngine::StartedJob &job);
  bool wasSuccessful() const { return mSuccess; }
  // Stricter than wasSuccessful(): false for a run that stopped at
  // --max-transfer, --max-duration or --error-on-no-transfer.
  bool completedFully() const { return mSuccess && mExitCode == 0; }
  JobHistoryEntry historyEntry() const;

public slots:
  void cancel();

signals:
  void finished(const QString &info);
  void closed();

private:
  Ui::JobWidget ui;

  RcloneRcEngine *mEngine;
  int mJobId;
  bool mRunning = true;
  bool mStopping = false;
  bool mUserCancelled = false;
  bool mPollInFlight = false;
  bool mSuccess = false;
  QDateTime mStartedAt;
  QDateTime mFinishedAt;
  QString mGroup;
  QStringList mDisplayArgs;
  QStringList mHistoryArgs;
  QStringList mBatchLabels;
  QString mInfo;
  QString mSource;
  QString mDest;
  qint64 mBytes = 0;
  int mFiles = 0;
  int mErrors = 0;
  int mExitCode = 0;
  // A --max-delete stop, seen in core/stats lastError (sync/sync) or in
  // core/command's output. job/status alone only says "failed to delete N
  // files". Sticky, since a later lastError can replace the cap's.
  bool mDeleteCapHit = false;
  QString mDeleteCapLine;
  // The label history and notifications use, matching the CLI route's.
  QString mStatusLabel;
  bool mFinished = false;
  // job/status said finished. finish() can still be a stats read away.
  bool mJobEnded = false;
  // The job ran through core/command, as a child rclone process.
  bool mFromCommand = false;
  // A sync/bisync job's answer has its session, so bisync itself ran. One
  // that failed opening a path has none, and the CLI fails that before
  // cmd.Run, exiting 1.
  bool mBisyncRan = false;
  // The stats read after the job ended worked, so mFiles is final.
  bool mFinalStats = false;
  // The retry loop cmd.Run gives the CLI. cmd.Run clears the error count
  // and the fatal and retry flags between attempts, and a stats group never
  // clears them, so every attempt gets a group of its own. Transfers, bytes
  // and checks still add up across attempts, as they do in one CLI run:
  // mCarried holds the earlier groups' share, mThisAttempt the current
  // group's latest numbers.
  //
  // rclone checks --max-delete, --max-delete-size and --max-transfer
  // against the group's own counts, where the CLI's carry across attempts.
  // A new group gets those caps lowered by what the earlier ones used, the
  // caps read from the daemon again before each attempt. When that can't
  // be worked out (a --max-delete-size run whose deleted bytes
  // core/transferred no longer lists in full, a daemon that doesn't say
  // what its caps are), the next attempt stays in the current group, and
  // mErrorBase is that group's error count when the attempt started. The
  // group's retry flag can then be an earlier attempt's (mRetryFlagCarried),
  // and the attempt is judged by the error its job returned.
  QString mRetryEndpoint;
  QJsonObject mRetryPayload;
  // What the current attempt posted.
  QJsonObject mAttemptPayload;
  // The daemon's settings, for the caps _config leaves out.
  QJsonObject mDaemonMain;
  int mErrorBase = 0;
  int mGroupErrors = 0;
  bool mGroupRetryFlag = false;
  bool mRetryFlagCarried = false;
  int mAttempts = 1;
  int mAttempt = 1;
  qint64 mRetrySleepMs = 0;
  // A backend's retry-after wait ahead of the next attempt's --retries-sleep.
  qint64 mRetryAfterMs = 0;
  // When the current job ended, in the daemon's clock (job/status endTime),
  // which is where cmd.Run starts its retry-after wait. Empty until then.
  QString mJobEndTime;
  bool mRetryPending = false;
  // Cancel came after the job ended, while its last stats were read.
  bool mCancelBeforeRetry = false;
  struct Totals {
    double bytes = 0;
    double totalBytes = 0;
    int transfers = 0;
    int checks = 0;
    qint64 deletes = 0;
  };
  Totals mCarried;
  Totals mThisAttempt;
  // The files the current group deleted and their sizes, from
  // core/transferred (RcJobResult::AddDeletes), which a --max-delete-size
  // run needs for a group of its own. rclone lists only about the last 100
  // files a group finished or checked, so the card reads it on every poll,
  // and the sizes only count once there are as many as the group's deletes.
  // mCarriedDeletedBytes is the earlier groups' share, -1 once one of them
  // couldn't be told.
  RcJobResult::ListedDeletes mGroupDeletes;
  qint64 mCarriedDeletedBytes = 0;
  bool mTrackDeletes = false;
  // The time cmd.Run would count for the attempts that ended: each job's
  // own duration, which a group's elapsedTime overshoots by however late
  // the poll saw the end, and the --retries-sleep before each retry.
  // mAttemptClock runs from the current attempt's start, for the time shown
  // while it runs.
  double mElapsedDone = 0;
  QElapsedTimer mAttemptClock;
  // The exit code the CLI gives the error a failed run ended on, as
  // resolveExitCode in cmd/cmd.go reads it: 3, 4 or 5 for its cause, 6 when
  // no error could be retried, 7 after a fatal one. -1 leaves the code to
  // the job's result.
  int mStopExit = -1;
  QTimer mPollTimer;
  QLineEdit *mBandwidth = nullptr;
  QPushButton *mBandwidthApply = nullptr;
  QLabel *mBandwidthNote = nullptr;
  QCheckBox *mAdaptive = nullptr;
  QLineEdit *mAdaptiveFloor = nullptr;
  AdaptiveBandwidthDriver *mAdaptiveDriver = nullptr;

  void poll();
  void buildBandwidthControl();
  void applyBandwidthLimit();
  void showBandwidthLimit(const QJsonObject &response, const QString &error);
  QString floorRate() const;
  void setAdaptive(bool on);
  void showAdaptiveState();
  void applyStats(const QJsonObject &stats);
  void showElapsed();
  // Reads the group's core/stats, and its deletes when the run tracks them,
  // then calls done with the stats answer.
  void readGroup(
      const std::function<void(const QJsonObject &, const QString &)> &done);
  // What every group so far deleted in bytes, -1 when that can't be told.
  qint64 deletedBytesSoFar() const;
  // Remembers the first --max-delete stop found in text.
  void noteDeleteCap(const QString &text);
  // Called with the stats read after a failed attempt. True when another
  // attempt has been scheduled, using cmd.Run's rules and log lines.
  // uncountedStop: jobError is a limit stop the group never counted, which
  // cmd.Run counts as the try's last error.
  bool retryAfterFailure(const QJsonObject &stats, bool haveStats,
                         const QString &jobError, bool uncountedStop = false);
  void startNextAttempt();
  // Posts the next attempt once the daemon's caps are read again.
  void submitNextAttempt();
  // The line cmd.Run prints when a limit stop (exit 8 or 10) ends the run.
  void noteLimitStop(const QJsonObject &stats, bool haveStats, int exitCode);
  // exitCode: the child rclone's, read from a core/command log; -1 takes 0
  // or 1 from success.
  void finish(bool success, const QString &error, int exitCode = -1);
};
