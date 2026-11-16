#include "job_history.h"
#include "rc_job_widget.h"
#include "rclone_exit_code.h"
#include "rclone_rc_engine.h"
#include "test_rclone.h"
#include "utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

  // A --max-transfer stop: a success at the limit, not a finished copy, so
  // backup pruning must not run. History keeps the real arguments, which
  // Restart and Dry Run can run again. A Sync takes sync/sync now that the
  // delete timings map to DeleteMode, and --one-file-system, which sync/*
  // can't carry, keeps the same run on core/command.
  void aLimitStopIsASuccessThatDidNotFinish() {
    const QList<QPair<QStringList, QString>> routes{
        {{}, "rclonebrowser-"}, {{"--one-file-system"}, "job/"}};
    for (const auto &route : routes) {
      const QString name = route.second.left(3);
      const QString source = makeDir("limit-src-" + name);
      const QString dest = makeDir("limit-dst-" + name);
      writeFile(source + "/big.bin", QByteArray(2 * 1024 * 1024, 'x'));
      QStringList args{"sync", "--delete-during", "--max-transfer", "1k",
                       "--transfers", "1"};
      args << route.first << source << dest;

      const JobHistoryEntry entry = runCard(args, route.second);
      QVERIFY2(mSuccess, qPrintable(route.second));
      QVERIFY(!mCompletedFully);
      QCOMPARE(entry.exitCode, 8);
      QCOMPARE(entry.statusLabel, RcloneExitCode::Describe(8).name);
      QCOMPARE(entry.args, args);
      QVERIFY(CanRerun(entry));
    }
  }

  // The daemon has one bandwidth limit for every job on it and ignores a
  // per-job one, so a task with its own limit runs as its own rclone, and
  // the card says why it has no live progress.
  void aJobWithItsOwnLimitRunsAsItsOwnRclone() {
    const QString source = makeDir("bw-src");
    const QString dest = makeDir("bw-dst");
    writeFile(source + "/a.txt", "a");
    const QStringList args{"copy", "--bwlimit", "10M", source, dest};

    const JobHistoryEntry entry = runCard(args, "job/");
    QVERIFY(mSuccess);
    QVERIFY2(mOutput.contains("because --bwlimit is one limit shared by "
                              "every job on the daemon"),
             qPrintable(mOutput));
    QVERIFY(QFile::exists(dest + "/a.txt"));
  }

  // sync/copy doesn't retry, so the card does what the CLI's loop does: a
  // file locked for the first attempt copies on the second, and the card
  // ends with that attempt's error count.
  void aFailedAttemptIsRetriedLikeTheCli() {
#ifdef Q_OS_WIN
    const QString source = makeDir("retry-src");
    const QString dest = makeDir("retry-dst");
    writeFile(source + "/keep.txt", "k");
    writeFile(source + "/locked.txt", "locked");
    HANDLE lock = lockFile(source + "/locked.txt");
    QVERIFY(lock != INVALID_HANDLE_VALUE);
    const QStringList args{"copy", "--retries-sleep", "1s", source, dest};

    const JobHistoryEntry entry =
        runCard(args, "rclonebrowser-", [&](RcJobWidget &card) {
          QPlainTextEdit *output = card.findChild<QPlainTextEdit *>("output");
          QVERIFY(output);
          QVERIFY2(QTest::qWaitFor(
                       [&]() {
                         return output->toPlainText().contains(
                             "Attempt 1/3 failed with ");
                       },
                       60000),
                   qPrintable(output->toPlainText()));
          CloseHandle(lock);
          lock = INVALID_HANDLE_VALUE;
        });
    if (lock != INVALID_HANDLE_VALUE) {
      CloseHandle(lock);
    }
    QVERIFY2(mSuccess, qPrintable(mOutput));
    QVERIFY2(mOutput.contains("Attempt 2/3 succeeded"), qPrintable(mOutput));
    QCOMPARE(entry.errors, 0);
    QVERIFY(QFile::exists(dest + "/locked.txt"));
    QVERIFY(QFile::exists(dest + "/keep.txt"));
#else
    QSKIP("locks a file the Windows way");
#endif
  }

  // --retries 1 is one attempt and no more, as on the CLI.
  void oneRetryMeansOneAttempt() {
#ifdef Q_OS_WIN
    const QString source = makeDir("once-src");
    const QString dest = makeDir("once-dst");
    writeFile(source + "/locked.txt", "locked");
    HANDLE lock = lockFile(source + "/locked.txt");
    QVERIFY(lock != INVALID_HANDLE_VALUE);
    const QStringList args{"copy", "--retries", "1", source, dest};

    const JobHistoryEntry entry = runCard(args, "rclonebrowser-");
    CloseHandle(lock);
    QVERIFY(!mSuccess);
    QVERIFY2(mOutput.contains("Attempt 1/1 failed with "), qPrintable(mOutput));
    QVERIFY2(!mOutput.contains("Attempt 2/"), qPrintable(mOutput));
    QVERIFY(entry.errors > 0);
#else
    QSKIP("locks a file the Windows way");
#endif
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
  QString mOutput;

#ifdef Q_OS_WIN
  // Open with no sharing, so rclone can list the file but not read it.
  static HANDLE lockFile(const QString &path) {
    return CreateFileW(reinterpret_cast<LPCWSTR>(
                           QDir::toNativeSeparators(path).utf16()),
                       GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
  }
#endif

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
  JobHistoryEntry runCard(
      const QStringList &args, const QString &groupPrefix,
      const std::function<void(RcJobWidget &)> &whileRunning = {}) {
    mSuccess = false;
    mCompletedFully = false;
    mOutput.clear();
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
    card.setStartedJob(job);
    QSignalSpy finished(&card, &RcJobWidget::finished);
    if (whileRunning) {
      whileRunning(card);
    }
    if (finished.isEmpty() && !finished.wait(120000)) {
      qWarning() << "the card never finished";
      return JobHistoryEntry();
    }
    if (QPlainTextEdit *output = card.findChild<QPlainTextEdit *>("output")) {
      mOutput = output->toPlainText();
    }
    mSuccess = card.wasSuccessful();
    mCompletedFully = card.completedFully();
    return card.historyEntry();
  }
};

QTEST_MAIN(RcJobWidgetTest)
#include "rc_job_widget_test.moc"
