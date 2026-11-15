#include "adaptive_bandwidth.h"
#include "adaptive_bandwidth_driver.h"
#include "rclone_rc_engine.h"
#include "test_rclone.h"
#include "utils.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using AdaptiveBandwidth::BytesBetween;
using AdaptiveBandwidth::Controller;
using AdaptiveBandwidth::InterfaceCounter;

namespace {
InterfaceCounter counter(const QString &name, quint64 received, quint64 sent,
                         bool wraps32 = false) {
  InterfaceCounter c;
  c.name = name;
  c.received = received;
  c.sent = sent;
  c.wraps32 = wraps32;
  return c;
}

constexpr quint64 kMiB = 1024 * 1024;
} // namespace

class AdaptiveBandwidthTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mDir;

  // What the daemon itself says its limit is.
  QString daemonRate(RcloneRcEngine &engine) {
    QString rate;
    bool done = false;
    engine.bandwidthLimit(QString(), this,
                          [&](const QJsonObject &response, const QString &) {
                            rate = response.value("rate").toString();
                            done = true;
                          });
    if (!QTest::qWaitFor([&]() { return done; }, 30000)) {
      return "no answer";
    }
    return rate;
  }

private slots:
  void initTestCase() {
    // The driver logs what it does. Keep that out of the real profile.
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(mDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mDir.path());
    QCoreApplication::setOrganizationName("rclone-browser-adaptive-test");
    QCoreApplication::setApplicationName("rclone-browser-adaptive-test");
  }

  void readsProcNetDev() {
    // The real layout, including a receive count long enough to run into
    // the interface name.
    const QString text =
        "Inter-|   Receive                                                |  "
        "Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|"
        "bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo:  123456     100    0    0    0     0          0         0"
        "   123456     100    0    0    0     0       0          0\n"
        "  eth0:98765432109   5000    0    0    0     0          0        12"
        "  4321000    3000    0    0    0     0       0          0\n"
        "wlan0: 1000 10 0 0 0 0 0 0 2000 20 0 0 0 0 0 0\n"
        "broken: 1 2 3\n";
    const QList<InterfaceCounter> counters =
        AdaptiveBandwidth::ParseProcNetDev(text);
    QCOMPARE(counters.size(), 3);
    QCOMPARE(counters.at(0).name, QStringLiteral("lo"));
    QCOMPARE(counters.at(1).name, QStringLiteral("eth0"));
    QCOMPARE(counters.at(1).received, Q_UINT64_C(98765432109));
    QCOMPARE(counters.at(1).sent, Q_UINT64_C(4321000));
    QCOMPARE(counters.at(2).name, QStringLiteral("wlan0"));
    QCOMPARE(counters.at(2).received, Q_UINT64_C(1000));
    QCOMPARE(counters.at(2).sent, Q_UINT64_C(2000));
  }

  void countsBothDirectionsOnEveryInterface() {
    const QList<InterfaceCounter> before{counter("eth0", 1000, 500),
                                         counter("wlan0", 10, 10)};
    const QList<InterfaceCounter> after{counter("eth0", 4000, 1500),
                                        counter("wlan0", 110, 60),
                                        // Plugged in between readings.
                                        counter("usb0", 9000, 9000)};
    QCOMPARE(BytesBetween(before, after), Q_UINT64_C(3000 + 1000 + 100 + 50));
  }

  void aWrappedCounterCountsOnlyTheWrap() {
    // macOS counters are 32 bits and wrap at 4 GiB.
    const QList<InterfaceCounter> before{
        counter("en0", 0xFFFFFF00ull, 0, true)};
    const QList<InterfaceCounter> after{counter("en0", 0x100ull, 0, true)};
    QCOMPARE(BytesBetween(before, after), Q_UINT64_C(0x200));
  }

  void aResetCounterCountsNothing() {
    // A card that was re-plugged starts again from zero. That isn't a burst
    // of traffic.
    const QList<InterfaceCounter> before{counter("eth0", 5000000, 5000000)};
    const QList<InterfaceCounter> after{counter("eth0", 10, 10)};
    QCOMPARE(BytesBetween(before, after), Q_UINT64_C(0));
  }

  void oneBusyReadingIsNotEnoughToThrottle() {
    Controller controller;
    QCOMPARE(controller.sample(10 * kMiB, 0, 2000), Controller::Action::None);
    QCOMPARE(controller.sample(0, 0, 2000), Controller::Action::None);
    QCOMPARE(controller.sample(10 * kMiB, 0, 2000), Controller::Action::None);
    QVERIFY(!controller.throttled());
    QCOMPARE(controller.sample(10 * kMiB, 0, 2000),
             Controller::Action::Throttle);
    QVERIFY(controller.throttled());
    QCOMPARE(controller.otherBytesPerSecond(), 5.0 * kMiB);
  }

  void releasesAfterFiveQuietReadings() {
    Controller controller;
    controller.sample(10 * kMiB, 0, 2000);
    QCOMPARE(controller.sample(10 * kMiB, 0, 2000),
             Controller::Action::Throttle);
    for (int i = 0; i < 4; ++i) {
      QCOMPARE(controller.sample(0, 0, 2000), Controller::Action::None);
    }
    // Another app's burst in the middle starts the count again.
    QCOMPARE(controller.sample(10 * kMiB, 0, 2000), Controller::Action::None);
    for (int i = 0; i < 4; ++i) {
      QCOMPARE(controller.sample(0, 0, 2000), Controller::Action::None);
    }
    QCOMPARE(controller.sample(0, 0, 2000), Controller::Action::Release);
    QVERIFY(!controller.throttled());
  }

  void rcloneOwnTrafficIsNotSomeoneElses() {
    Controller controller;
    // rclone moving 20 MiB shows up as a little more on the wire.
    for (int i = 0; i < 5; ++i) {
      QCOMPARE(controller.sample(21 * kMiB, 20 * kMiB, 2000),
               Controller::Action::None);
    }
    QVERIFY(!controller.throttled());
    // On top of that, another app pulling 2 MiB/s.
    controller.sample(22 * kMiB + 4 * kMiB, 20 * kMiB, 2000);
    QCOMPARE(controller.sample(22 * kMiB + 4 * kMiB, 20 * kMiB, 2000),
             Controller::Action::Throttle);
  }

  void backgroundChatterIsQuiet() {
    Controller controller;
    // 64 KiB/s of updates and sync clients is not worth slowing down for.
    for (int i = 0; i < 5; ++i) {
      QCOMPARE(controller.sample(128 * 1024, 0, 2000),
               Controller::Action::None);
    }
    QVERIFY(!controller.throttled());
  }

  void aReadingWithNoTimeDoesNothing() {
    Controller controller;
    controller.sample(10 * kMiB, 0, 2000);
    QCOMPARE(controller.sample(10 * kMiB, 0, 0), Controller::Action::None);
    QVERIFY(!controller.throttled());
  }

  void readsThisComputersCounters() {
    bool ok = false;
    const QList<InterfaceCounter> counters =
        AdaptiveBandwidth::ReadInterfaceCounters(&ok);
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    QVERIFY(ok);
#endif
    for (const InterfaceCounter &c : counters) {
      QVERIFY(!c.name.isEmpty());
      QVERIFY(c.name != QLatin1String("lo"));
      QVERIFY(c.name != QLatin1String("lo0"));
    }
  }

  // The driver against a real daemon, with made-up interface traffic.
  void throttlesTheDaemonWhileOtherAppsAreBusy() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("no rclone; set RCLONE_BROWSER_TEST_RCLONE");
    }
    SetRclone(rclone);
    const QString conf = mDir.filePath("rclone.conf");
    {
      QFile file(conf);
      QVERIFY(file.open(QIODevice::WriteOnly));
    }
    SetRcloneConf(conf);

    RcloneRcEngine engine;
    bool started = false;
    QString startError;
    engine.ensureStartedAsync(this, [&](bool ok, const QString &error) {
      started = ok;
      startError = error;
    });
    QTRY_VERIFY2_WITH_TIMEOUT(started, qPrintable(startError), 60000);

    AdaptiveBandwidthDriver *driver =
        AdaptiveBandwidthDriver::forEngine(&engine);
    QCOMPARE(AdaptiveBandwidthDriver::forEngine(&engine), driver);

    quint64 received = 0;
    bool otherAppBusy = false;
    driver->setInterval(150);
    driver->setCounterSource([&](bool *ok) {
      // About 25 MiB/s while busy, nothing otherwise.
      if (otherAppBusy) {
        received += 4 * kMiB;
      }
      *ok = true;
      return QList<InterfaceCounter>{counter("eth0", received, 0)};
    });
    QSignalSpy changes(driver, &AdaptiveBandwidthDriver::stateChanged);

    driver->start("off", "1M");
    QVERIFY(driver->isActive());
    QCOMPARE(daemonRate(engine), QStringLiteral("off"));

    otherAppBusy = true;
    QTRY_VERIFY_WITH_TIMEOUT(driver->isThrottled(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(daemonRate(engine), QStringLiteral("1Mi"),
                              10000);

    otherAppBusy = false;
    QTRY_VERIFY_WITH_TIMEOUT(!driver->isThrottled(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(daemonRate(engine), QStringLiteral("off"),
                              10000);

    // Switching off while held to the floor puts the ceiling back.
    driver->setCeiling("10M");
    QTRY_COMPARE_WITH_TIMEOUT(daemonRate(engine), QStringLiteral("10Mi"),
                              10000);
    otherAppBusy = true;
    QTRY_VERIFY_WITH_TIMEOUT(driver->isThrottled(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(daemonRate(engine), QStringLiteral("1Mi"),
                              10000);
    driver->stop();
    QVERIFY(!driver->isActive());
    QVERIFY(driver->problem().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(daemonRate(engine), QStringLiteral("10Mi"),
                              10000);
    QVERIFY(changes.count() >= 5);

    // A system that can't report traffic says so instead of pretending.
    driver->setCounterSource([](bool *ok) {
      *ok = false;
      return QList<InterfaceCounter>();
    });
    driver->start("off", "1M");
    QVERIFY(!driver->isActive());
    QVERIFY(!driver->problem().isEmpty());

    bool cleared = false;
    engine.bandwidthLimit("off", this,
                          [&](const QJsonObject &, const QString &) {
                            cleared = true;
                          });
    QTRY_VERIFY_WITH_TIMEOUT(cleared, 30000);
  }
};

QTEST_MAIN(AdaptiveBandwidthTest)
#include "adaptive_bandwidth_test.moc"
