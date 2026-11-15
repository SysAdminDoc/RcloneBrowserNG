#pragma once

#include "pch.h"
#include "adaptive_bandwidth.h"

#include <functional>

class RcloneRcEngine;

// Runs adaptive bandwidth against one rclone daemon. The daemon's bandwidth
// limit is shared by every transfer on it, so there is one driver per daemon,
// and every job card on that daemon shows the same state.
class AdaptiveBandwidthDriver : public QObject {
  Q_OBJECT

public:
  // The engine's driver, made on first use and owned by the engine.
  static AdaptiveBandwidthDriver *forEngine(RcloneRcEngine *engine);

  bool isActive() const { return mActive; }
  bool isThrottled() const { return mController.throttled(); }
  QString ceiling() const { return mCeiling; }
  QString floor() const { return mFloor; }
  // Why adaptive mode stopped on its own, or what last went wrong.
  QString problem() const { return mProblem; }

  // ceiling is the limit while the network is free ("off" for none), floor
  // the limit while other apps use it. Both take --bwlimit's form.
  void start(const QString &ceiling, const QString &floor);
  void setCeiling(const QString &ceiling);
  void setFloor(const QString &floor);
  // Puts the ceiling back and stops watching.
  void stop();

  // It keeps watching while nothing transfers, so the next transfer on the
  // daemon starts at the right limit. It stops when switched off or when
  // the daemon stops answering.
  static constexpr int kIntervalMs = 2000;

  // Where interface counters come from. ReadInterfaceCounters unless a test
  // feeds its own traffic.
  using CounterSource =
      std::function<QList<AdaptiveBandwidth::InterfaceCounter>(bool *ok)>;
  void setCounterSource(CounterSource source) {
    mReadCounters = std::move(source);
  }
  void setInterval(int msecs) { mTimer.setInterval(msecs); }

signals:
  void stateChanged();

private:
  explicit AdaptiveBandwidthDriver(RcloneRcEngine *engine);

  void tick();
  void applyLimit(const QString &rate);
  void stopWithProblem(const QString &problem);

  RcloneRcEngine *mEngine;
  CounterSource mReadCounters = AdaptiveBandwidth::ReadInterfaceCounters;
  QTimer mTimer;
  QElapsedTimer mSinceLastReading;
  AdaptiveBandwidth::Controller mController;
  QList<AdaptiveBandwidth::InterfaceCounter> mLastCounters;
  quint64 mLastRcloneBytes = 0;
  bool mHaveBaseline = false;
  bool mTickInFlight = false;
  bool mActive = false;
  QString mCeiling;
  QString mFloor;
  QString mProblem;
};
