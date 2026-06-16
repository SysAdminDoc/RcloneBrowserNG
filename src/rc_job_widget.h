#pragma once

#include "pch.h"
#include "job_history.h"
#include "ui_job_widget.h"

class RcloneRcEngine;

class RcJobWidget : public QWidget {
  Q_OBJECT

public:
  RcJobWidget(RcloneRcEngine *engine, int jobId, const QString &info,
              const QStringList &displayArgs, const QString &source,
              const QString &dest, QWidget *parent = nullptr);
  ~RcJobWidget();

  void showDetails();
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
  QString mInfo;
  QString mSource;
  QString mDest;
  qint64 mBytes = 0;
  int mFiles = 0;
  int mErrors = 0;
  int mExitCode = 0;
  QTimer mPollTimer;

  void poll();
  void applyStats(const QJsonObject &stats);
  void finish(bool success, const QString &error);
};
