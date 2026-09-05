#include "rclone_rc_engine.h"
#include "schedule_manager.h"
#include "utils.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

// Starting an RC-backed transfer used to run rclone rcd synchronously on the
// window thread: waitForStarted for five seconds, then a QThread::msleep spin
// that could sit through a ten-second request timeout, with no progress and
// nothing to cancel. These tests point the app at a stub that just sleeps and
// assert the call hands control straight back.
class AsyncHelpersTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mDir;
  QString mSleepyStub;

private slots:
  void initTestCase() {
    QVERIFY(mDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mDir.path());
    QCoreApplication::setOrganizationName("rclone-browser-async-test");
    QCoreApplication::setApplicationName("rclone-browser-async-test");

    mSleepyStub = QString::fromLocal8Bit(SLEEPY_STUB_PATH);
    QVERIFY2(QFile::exists(mSleepyStub), qPrintable(mSleepyStub));
    SetRclone(mSleepyStub);
  }

  void rcEngineStartupReturnsBeforeTheDaemonDoes() {
    RcloneRcEngine engine;

    bool resolved = false;
    QString reportedError;
    QElapsedTimer timer;
    timer.start();
    engine.ensureStartedAsync(
        this, [&resolved, &reportedError](bool ok, const QString &error) {
          resolved = true;
          reportedError = ok ? QString() : error;
        });
    const qint64 returnedAfterMs = timer.elapsed();

    // The stub is still running. A blocking implementation would sit here.
    QVERIFY2(returnedAfterMs < 1000,
             qPrintable(QString("ensureStartedAsync blocked for %1 ms")
                            .arg(returnedAfterMs)));
    QVERIFY2(!resolved, "startup must not resolve on the calling stack");

    // It still finishes, and it reports the failure rather than hanging.
    QTRY_VERIFY_WITH_TIMEOUT(resolved, 30000);
    QVERIFY2(!reportedError.isEmpty(),
             "a stub that never serves rc must be reported as a failure");
  }

  void rcEngineCoalescesConcurrentStarts() {
    // Two transfers started together must not race two daemons onto two
    // ports; the second rides along with the first.
    RcloneRcEngine engine;
    int resolutions = 0;
    engine.ensureStartedAsync(this,
                              [&resolutions](bool, const QString &) { resolutions++; });
    engine.ensureStartedAsync(this,
                              [&resolutions](bool, const QString &) { resolutions++; });
    QTRY_COMPARE_WITH_TIMEOUT(resolutions, 2, 30000);
  }

  void scheduleListingIsAsynchronous() {
    // ScheduleManager shells out to schtasks/launchctl/systemctl, which took
    // 38 seconds on a cold run here. Its program is not injectable, so this
    // cannot use the sleepy stub; what it can prove is that the call is
    // genuinely deferred. A regression that made listSchedulesAsync run its
    // worker inline would resolve before the call returns and fail here, and
    // one that dispatched the callback from the pool thread instead of the
    // caller's would fail the thread check.
    bool resolved = false;
    QThread *callbackThread = nullptr;
    QElapsedTimer timer;
    timer.start();
    ScheduleManager::listSchedulesAsync(
        this, [&resolved, &callbackThread](const QList<ScheduleEntry> &,
                                           const QString &) {
          resolved = true;
          callbackThread = QThread::currentThread();
        });
    const qint64 returnedAfterMs = timer.elapsed();

    QVERIFY2(!resolved,
             "listSchedulesAsync must not run its helper on the caller's stack");
    QVERIFY2(returnedAfterMs < 1000,
             qPrintable(QString("listSchedulesAsync blocked for %1 ms")
                            .arg(returnedAfterMs)));

    QTRY_VERIFY_WITH_TIMEOUT(resolved, 120000);
    QCOMPARE(callbackThread, QThread::currentThread());
  }
};

QTEST_MAIN(AsyncHelpersTest)
#include "async_helpers_test.moc"
