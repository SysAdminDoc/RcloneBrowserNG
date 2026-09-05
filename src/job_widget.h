#pragma once

#include "pch.h"
#include "job_history.h"
#include "ui_job_widget.h"

class SparklineWidget : public QWidget {
  Q_OBJECT
public:
  explicit SparklineWidget(QWidget *parent = nullptr);
  void addSample(double value);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  QVector<double> mSamples;
  static constexpr int kMaxSamples = 60;
};

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
  int serverSideCopies() const { return mServerSideCopies; }

public slots:
  void cancel();
  void togglePause();

  QStringList retryArgs() const { return mTransferArgs; }
  QString retrySource() const { return mSource; }
  QString retryDest() const { return mDest; }
  QString retryInfo() const { return mInfo; }

signals:
  void finished(const QString &info);
  void retryRequested();
  void resyncRequested();
  void closed();

private:
  Ui::JobWidget ui;

  bool mRunning = true;
  bool mStopping = false;
  bool mUserCancelled = false;
  bool mPaused = false;
  bool mSuccess = false;
  QProcess *mProcess;
  QDateTime mStartedAt;
  QDateTime mFinishedAt;

  QStringList mArgs;
  QStringList mTransferArgs;
  QString mInfo;
  QString mSource;
  QString mDest;
  qint64 mBytes = 0;
  int mFiles = 0;
  int mErrors = 0;
  int mServerSideCopies = 0;
  int mExitCode = 0;
  QStringList mTransferDetail;
  QHash<QString, QLabel *> mActive;
  QLabel *mOverflowLabel = nullptr;
  SparklineWidget *mSparkline = nullptr;

  void setProgressOverflow(int hiddenCount);
  void clearFileProgress();
};
