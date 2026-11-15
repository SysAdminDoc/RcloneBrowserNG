#pragma once

#include "pch.h"
#include "job_history.h"
#include "ui_job_widget.h"

class AdaptiveBandwidthDriver;
class RcloneRcEngine;

class RcJobWidget : public QWidget {
  Q_OBJECT

public:
  // The group is supplied rather than derived: a sync/* job is given an
  // explicit one, so "job/<id>" is only right for the core/command route.
  RcJobWidget(RcloneRcEngine *engine, int jobId, const QString &group,
              const QString &info, const QStringList &displayArgs,
              const QString &source, const QString &dest,
              QWidget *parent = nullptr);
  ~RcJobWidget();

  void showDetails();
  // Marks this card as a job/batch delete. rclone reports such a job as a
  // success even when items in it failed, so the card reads the per-item
  // results instead and names each failure.
  void setBatchLabels(const QStringList &labels) { mBatchLabels = labels; }
  bool wasSuccessful() const { return mSuccess; }
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
  QStringList mBatchLabels;
  QString mInfo;
  QString mSource;
  QString mDest;
  qint64 mBytes = 0;
  int mFiles = 0;
  int mErrors = 0;
  int mExitCode = 0;
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
  void finish(bool success, const QString &error);
};
