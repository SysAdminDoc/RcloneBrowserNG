#pragma once

#include "pch.h"
#include "ui_mount_widget.h"

class MountWidget : public QWidget {
  Q_OBJECT

public:
  MountWidget(QProcess *process, const QString &remote, const QString &folder,
              const QString &rcAddr = QString(),
              const QString &rcUser = QString(),
              const QString &rcPass = QString(), QWidget *parent = nullptr);
  ~MountWidget();

public slots:
  void cancel();

signals:
  void finished();
  void closed();

private:
  Ui::MountWidget ui;

  bool mRunning = true;
  QProcess *mProcess;

  // Windows remote-control endpoint + per-mount credentials used to issue an
  // authenticated unmount (empty on other platforms)
  QString mRcAddr;
  QString mRcUser;
  QString mRcPass;
};
