#pragma once

#include "pch.h"
#include "ui_mount_widget.h"
#include <functional>

class MountWidget : public QWidget {
  Q_OBJECT

public:
  MountWidget(QProcess *process, const QString &remote, const QString &folder,
              const QString &rcAddr = QString(),
              const QString &rcUser = QString(),
              const QString &rcPass = QString(), bool keepMounted = true,
              QWidget *parent = nullptr);
  ~MountWidget();

  bool keepMounted() const;
  void setRemountScheduled(int delayMs, int attempt);

public slots:
  void cancel();

signals:
  void finished();
  void stopped(bool requestedUnmount, bool cleanExit);
  void closed();

private:
  Ui::MountWidget ui;

  bool mRunning = true;
  bool mStopping = false;
  bool mUserRequestedUnmount = false;
  QProcess *mProcess;

  // Windows remote-control endpoint + per-mount credentials used to issue an
  // authenticated unmount (empty on other platforms)
  QString mRcAddr;
  QString mRcUser;
  QString mRcPass;

  QString rcAddr() const;
  using RcCommandCallback =
      std::function<void(bool, const QByteArray &, const QString &)>;
  void runRcCommandAsync(const QString &command, RcCommandCallback callback);
  void confirmNoPendingVfsUploads(std::function<void(bool)> callback);
  void beginUnmount();
};
