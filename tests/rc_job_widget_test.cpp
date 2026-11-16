#include "job_history.h"
#include "rc_job_widget.h"
#include "rclone_exit_code.h"
#include "rclone_rc_engine.h"
#include "test_rclone.h"
#include "utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// Drives the daemon route's job card against a real rclone rcd. The card is
// what turns a finished job into the history row, the status label and the
// "completed fully" answer the backup pruning waits on, and none of that
// shows in a test of RcJobResult alone.
class RcJobWidgetTest : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    QVERIFY(mDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mDir.path());
    QCoreApplication::setOrganizationName("rclone-browser-rc-job-widget-test");
    QCoreApplication::setApplicationName("rclone-browser-rc-job-widget-test");

    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("rclone is not on PATH and RCLONE_BROWSER_TEST_RCLONE is unset");
    }
    const QString configPath = QDir(mDir.path()).filePath("rclone.conf");
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write("[local]\ntype = alias\nremote = " + mDir.path().toUtf8() +
                 "\n");
    config.close();
    SetRclone(rclone);
    SetRcloneConf(configPath);
  }

  // A --max-transfer stop through core/command: a success at the limit, not
  // a finished copy, so backup pruning must not run. History keeps the real
  // arguments, which Restart and Dry Run can run again.
  void aLimitStopIsASuccessThatDidNotFinish() {
    const QString source = makeDir("limit-src");
    const QString dest = makeDir("limit-dst");
    writeFile(source + "/big.bin", QByteArray(2 * 1024 * 1024, 'x'));
    const QStringList args{"sync",        "--delete-during", "--max-transfer",
                           "1k",          "--transfers",     "1",
                           source,        dest};

    const JobHistoryEntry entry = runCard(args, "job/");
    QVERIFY(mSuccess);
    QVERIFY(!mCompletedFully);
    QCOMPARE(entry.exitCode, 8);
    QCOMPARE(entry.statusLabel, RcloneExitCode::Describe(8).name);
    QCOMPARE(entry.args, args);
    QVERIFY(CanRerun(entry));
  }

  // sync/copy reports a run with nothing to copy as plain success even with
  // --error-on-no-transfer. The process route exits 9 for it, and the card
  // now says the same from the transfer count.
  void nothingToCopyOnTheNativeRouteIsExit9() {
    const QString source = makeDir("none-src");
    const QString dest = makeDir("none-dst");
    writeFile(source + "/same.txt", "unchanged");
    writeFile(dest + "/same.txt", "unchanged");
    {
      QFile copy(dest + "/same.txt");
      QVERIFY(copy.open(QIODevice::ReadWrite));
      QVERIFY(copy.setFileTime(QFileInfo(source + "/same.txt").lastModified(),
                               QFileDevice::FileModificationTime));
    }
    const QStringList args{"copy", "--error-on-no-transfer", source, dest};

    const JobHistoryEntry entry = runCard(args, "rclonebrowser-");
    QVERIFY(mSuccess);
    QVERIFY(!mCompletedFully);
    QCOMPARE(entry.exitCode, 9);
    QCOMPARE(entry.statusLabel, RcloneExitCode::Describe(9).name);
    QCOMPARE(entry.files, 0);
  }

  // The same copy with something to copy finishes fully, and the transfer
  // count comes from the stats read after the job ended.
  void aCopyThatCopiesCompletesFully() {
    const QString source = makeDir("full-src");
    const QString dest = makeDir("full-dst");
    writeFile(source + "/new.txt", "new");
    const QStringList args{"copy", "--error-on-no-transfer", source, dest};

    const JobHistoryEntry entry = runCard(args, "rclonebrowser-");
    QVERIFY(mSuccess);
    QVERIFY(mCompletedFully);
    QCOMPARE(entry.exitCode, 0);
    QCOMPARE(entry.files, 1);
    QVERIFY(QFile::exists(dest + "/new.txt"));
  }

private:
  QTemporaryDir mDir;
  bool mSuccess = false;
  bool mCompletedFully = false;

  QString makeDir(const QString &name) {
    const QString path = QDir(mDir.path()).filePath(name);
    if (!QDir().mkpath(path)) {
      qWarning() << "couldn't make" << path;
    }
    return path;
  }

  static void writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
      qWarning() << "couldn't write" << path;
      return;
    }
    file.write(data);
  }

  // Starts args on a fresh daemon, puts the card on it the way MainWindow
  // does, and waits for the card to finish. groupPrefix names the route
  // the run must have taken.
  JobHistoryEntry runCard(const QStringList &args, const QString &groupPrefix) {
    mSuccess = false;
    mCompletedFully = false;
    RcloneRcEngine engine;
    RcloneRcEngine::StartedJob job;
    bool started = false;
    engine.runCommand(args, this,
                      [&](const RcloneRcEngine::StartedJob &result) {
                        job = result;
                        started = true;
                      });
    if (!QTest::qWaitFor([&]() { return started; }, 60000) || job.jobId < 0) {
      qWarning() << "the job never started:" << job.error;
      return JobHistoryEntry();
    }
    if (!job.group.startsWith(groupPrefix)) {
      qWarning() << "took the wrong route:" << job.group;
      return JobHistoryEntry();
    }
    RcJobWidget card(&engine, job.jobId, job.group, "test run",
                     job.displayCommand, args.value(args.size() - 2),
                     args.last());
    card.setHistoryArgs(args);
    QSignalSpy finished(&card, &RcJobWidget::finished);
    if (!finished.wait(120000)) {
      qWarning() << "the card never finished";
      return JobHistoryEntry();
    }
    mSuccess = card.wasSuccessful();
    mCompletedFully = card.completedFully();
    return card.historyEntry();
  }
};

QTEST_MAIN(RcJobWidgetTest)
#include "rc_job_widget_test.moc"
