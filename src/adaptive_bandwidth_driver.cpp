#include "adaptive_bandwidth_driver.h"

#include "app_log.h"
#include "rclone_rc_engine.h"

using AdaptiveBandwidth::Controller;

AdaptiveBandwidthDriver *
AdaptiveBandwidthDriver::forEngine(RcloneRcEngine *engine) {
  auto *driver = engine->findChild<AdaptiveBandwidthDriver *>(
      QString(), Qt::FindDirectChildrenOnly);
  if (!driver) {
    driver = new AdaptiveBandwidthDriver(engine);
  }
  return driver;
}

AdaptiveBandwidthDriver::AdaptiveBandwidthDriver(RcloneRcEngine *engine)
    : QObject(engine), mEngine(engine) {
  mTimer.setInterval(kIntervalMs);
  QObject::connect(&mTimer, &QTimer::timeout, this,
                   &AdaptiveBandwidthDriver::tick);
}

void AdaptiveBandwidthDriver::start(const QString &ceiling,
                                    const QString &floor) {
  bool countersOk = false;
  mLastCounters = mReadCounters(&countersOk);
  if (!countersOk) {
    stopWithProblem("This system doesn't report network traffic, so the app "
                    "can't tell when other apps use the network.");
    return;
  }
  mCeiling = ceiling;
  mFloor = floor;
  mProblem.clear();
  mController.reset();
  mHaveBaseline = false;
  mActive = true;
  mSinceLastReading.start();
  mTimer.start();
  applyLimit(mCeiling);
  AppLog::Write(AppLog::Level::Info, "bandwidth",
                QString("Adaptive bandwidth on: %1 while the network is free, "
                        "%2 while other apps use it.")
                    .arg(mCeiling, mFloor));
  emit stateChanged();
  tick();
}

void AdaptiveBandwidthDriver::setCeiling(const QString &ceiling) {
  mCeiling = ceiling;
  if (mActive && !mController.throttled()) {
    applyLimit(mCeiling);
  }
  emit stateChanged();
}

void AdaptiveBandwidthDriver::setFloor(const QString &floor) {
  mFloor = floor;
  if (mActive && mController.throttled()) {
    applyLimit(mFloor);
  }
  emit stateChanged();
}

void AdaptiveBandwidthDriver::stop() {
  if (!mActive) {
    return;
  }
  mTimer.stop();
  mActive = false;
  const bool wasThrottled = mController.throttled();
  mController.reset();
  if (wasThrottled) {
    applyLimit(mCeiling);
  }
  AppLog::Write(AppLog::Level::Info, "bandwidth", "Adaptive bandwidth off.");
  emit stateChanged();
}

void AdaptiveBandwidthDriver::stopWithProblem(const QString &problem) {
  mProblem = problem;
  AppLog::Write(AppLog::Level::Warning, "bandwidth", problem);
  if (mActive) {
    stop();
  } else {
    emit stateChanged();
  }
}

void AdaptiveBandwidthDriver::applyLimit(const QString &rate) {
  mEngine->bandwidthLimit(
      rate.isEmpty() ? QStringLiteral("off") : rate, this,
      [this](const QJsonObject &, const QString &error) {
        if (!error.isEmpty()) {
          stopWithProblem(error);
        }
      });
}

void AdaptiveBandwidthDriver::tick() {
  if (!mActive || mTickInFlight) {
    return;
  }
  mTickInFlight = true;
  mEngine->coreStats(
      QString(), this, [this](const QJsonObject &stats, const QString &error) {
        mTickInFlight = false;
        if (!mActive) {
          return;
        }
        if (!error.isEmpty()) {
          stopWithProblem(error);
          return;
        }

        bool countersOk = false;
        const QList<AdaptiveBandwidth::InterfaceCounter> counters =
            mReadCounters(&countersOk);
        const auto rcloneBytes =
            static_cast<quint64>(qMax(0.0, stats.value("bytes").toDouble()));
        const qint64 msecs = mSinceLastReading.restart();
        const bool comparable =
            mHaveBaseline && countersOk && rcloneBytes >= mLastRcloneBytes;
        const quint64 onWire =
            AdaptiveBandwidth::BytesBetween(mLastCounters, counters);
        const quint64 byRclone = rcloneBytes - qMin(rcloneBytes,
                                                    mLastRcloneBytes);
        mLastCounters = counters;
        mLastRcloneBytes = rcloneBytes;
        mHaveBaseline = countersOk;
        // rclone rewinds its byte count when it retries a file, which would
        // read as a burst of someone else's traffic. Skip that reading.
        if (!comparable) {
          return;
        }

        switch (mController.sample(onWire, byRclone, msecs)) {
        case Controller::Action::Throttle:
          applyLimit(mFloor);
          AppLog::Write(
              AppLog::Level::Info, "bandwidth",
              QString("Other apps are using about %1/s, so transfers are held "
                      "to %2.")
                  .arg(QLocale().formattedDataSize(static_cast<qint64>(
                           mController.otherBytesPerSecond())),
                       mFloor));
          emit stateChanged();
          break;
        case Controller::Action::Release:
          applyLimit(mCeiling);
          AppLog::Write(AppLog::Level::Info, "bandwidth",
                        QString("The network is free again, so transfers are "
                                "back to %1.")
                            .arg(mCeiling));
          emit stateChanged();
          break;
        case Controller::Action::None:
          break;
        }
      });
}
