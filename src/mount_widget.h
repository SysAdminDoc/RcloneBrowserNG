#pragma once

#include "pch.h"
#include "mount_health.h"
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
  bool remountRequested() const;
  void setRemountScheduled(int delayMs, int attempt);

public slots:
  void cancel();
  void requestRemount();

signals:
  void finished();
  void stopped(bool requestedUnmount, bool cleanExit);
  void staleDetected(const QString &detail);
  void closed();

private:
  Ui::MountWidget ui;

  bool mRunning = true;
  bool mStopping = false;
  bool mUserRequestedUnmount = false;
  bool mRemountRequested = false;
  bool mHealthProbeInFlight = false;
  bool mStaleNotified = false;
  int mHealthFailures = 0;
  QProcess *mProcess;
  QTimer *mHealthTimer = nullptr;

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
  void startHealthProbe();
  void finishHealthProbe(const MountHealthProbeResult &result);
};
