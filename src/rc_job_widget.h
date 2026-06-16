#pragma once

#include "pch.h"
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
  bool mPollInFlight = false;
  QString mGroup;
  QStringList mDisplayArgs;
  QTimer mPollTimer;

  void poll();
  void applyStats(const QJsonObject &stats);
  void finish(bool success, const QString &error);
};
