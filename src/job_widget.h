#pragma once

#include "pch.h"
#include "job_history.h"
#include "ui_job_widget.h"

class JobWidget : public QWidget {
  Q_OBJECT

public:
  JobWidget(QProcess *process, const QString &info, const QStringList &args,
            const QString &source, const QString &dest,
            QWidget *parent = nullptr);
  ~JobWidget();

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

  bool mRunning = true;
  bool mSuccess = false;
  QProcess *mProcess;
  QDateTime mStartedAt;
  QDateTime mFinishedAt;

  QStringList mArgs;
  QString mInfo;
  QString mSource;
  QString mDest;
  qint64 mBytes = 0;
  int mFiles = 0;
  int mErrors = 0;
  int mExitCode = 0;
  QHash<QString, QLabel *> mActive;
  QLabel *mOverflowLabel = nullptr;

  void setProgressOverflow(int hiddenCount);
  void clearFileProgress();
};
