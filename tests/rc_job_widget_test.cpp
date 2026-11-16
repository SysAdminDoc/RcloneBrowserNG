#include "fake_rc_server.h"
#include "job_history.h"
#include "rc_job_result.h"
#include "rc_job_widget.h"
#include "rclone_exit_code.h"
#include "rclone_rc_engine.h"
#include "test_rclone.h"
#include "utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
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
      // The CLI ends a hard --max-transfer stop with this line, not with
      // an attempt that succeeded.
      QVERIFY2(!mOutput.contains("succeeded"), qPrintable(mOutput));
      if (route.second == "rclonebrowser-") {
        QVERIFY2(mOutput.contains(
                     "Fatal error received - not attempting retries"),
                 qPrintable(mOutput));
      }
    }
  }

  // A job that failed without counting an error failed before it got to
  // the files, as a remote that cannot be opened does. The CLI fails those
  // before its retry loop and tries once.
  void aJobThatFailedBeforeTheFilesIsNotRetried() {
    const FakeRun run = runFakeCard(
        [](const QString &path, const QJsonObject &,
           const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            reply(QJsonObject{{"finished", true},
                              {"success", false},
                              {"error", "didn't find section in config file"},
                              {"output", QJsonObject()}});
          } else {
            reply(QJsonObject{{"errors", 0}});
          }
        });
    QVERIFY2(run.finished, qPrintable(run.output));
    QVERIFY(!run.success);
    QCOMPARE(run.resubmits, 0);
    QVERIFY2(!run.output.contains("Attempt"), qPrintable(run.output));
  }

  // A --max-transfer stop with --cutoff-mode soft counts no fatal error,
  // and the CLI says it has nothing it can retry. A hard one is the fatal
  // line. A --max-duration stop is the fatal line whatever the stats say,
  // since cmd.Run counts the fatal error the sync returns for it.
  void aLimitStopSaysWhyItIsNotRetried_data() {
    QTest::addColumn<QString>("error");
    QTest::addColumn<int>("exitCode");
    QTest::addColumn<bool>("fatal");
    QTest::addColumn<bool>("retryable");
    QTest::addColumn<QString>("line");
    const QString transfer =
        "max transfer limit reached as set by --max-transfer";
    const QString duration =
        "max transfer duration reached as set by --max-duration";
    const QString fatalLine = "Fatal error received - not attempting retries";
    const QString noRetryLine =
        "Can't retry any of the errors - not attempting retries";
    QTest::newRow("transfer hard")
        << transfer << 8 << true << false << fatalLine;
    QTest::newRow("transfer soft")
        << transfer << 8 << false << false << noRetryLine;
    QTest::newRow("duration hard")
        << duration << 10 << true << false << fatalLine;
    QTest::newRow("duration soft")
        << duration << 10 << false << false << fatalLine;
    QTest::newRow("duration, retryable errors")
        << duration << 10 << false << true << fatalLine;
  }

  void aLimitStopSaysWhyItIsNotRetried() {
    QFETCH(QString, error);
    QFETCH(int, exitCode);
    QFETCH(bool, fatal);
    QFETCH(bool, retryable);
    QFETCH(QString, line);
    const FakeRun run = runFakeCard(
        [&](const QString &path, const QJsonObject &,
            const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            reply(QJsonObject{{"finished", true},
                              {"success", false},
                              {"error", error},
                              {"output", QJsonObject()}});
          } else {
            reply(QJsonObject{{"errors", 1},
                              {"fatalError", fatal},
                              {"retryError", retryable}});
          }
        });
    QVERIFY2(run.finished, qPrintable(run.output));
    QVERIFY(run.success);
    QCOMPARE(run.entry.exitCode, exitCode);
    QCOMPARE(run.resubmits, 0);
    QVERIFY2(run.output.contains(line), qPrintable(run.output));
    QCOMPARE(run.output.count("not attempting retries"), 1);
    QVERIFY2(!run.output.contains("succeeded"), qPrintable(run.output));
  }

  // Measured with the CLI on v1.76.0: every --cutoff-mode ends a
  // --max-duration stop with the fatal line, and exits 10.
  void aMaxDurationStopEndsWithTheFatalLine_data() {
    QTest::addColumn<QString>("mode");
    QTest::newRow("hard") << QString("hard");
    QTest::newRow("soft") << QString("soft");
    QTest::newRow("cautious") << QString("cautious");
  }

  void aMaxDurationStopEndsWithTheFatalLine() {
    QFETCH(QString, mode);
    const QString source = makeDir("duration-src-" + mode);
    const QString dest = makeDir("duration-dst-" + mode);
    for (int i = 0; i < 4; ++i) {
      writeFile(source + QString("/f%1.bin").arg(i),
                QByteArray(1024 * 1024, char('a' + i)));
    }
    const QStringList args{"copy",          "--max-duration", "2s",
                           "--cutoff-mode", mode,             "--bwlimit-file",
                           "300k",          "--transfers",    "1",
                           source,          dest};
    const JobHistoryEntry entry = runCard(args, "rclonebrowser-");
    QVERIFY2(mSuccess, qPrintable(mOutput));
    QCOMPARE(entry.exitCode, 10);
    QVERIFY2(!mOutput.contains("succeeded"), qPrintable(mOutput));
    QVERIFY2(mOutput.contains("Fatal error received - not attempting retries"),
             qPrintable(mOutput));
    QVERIFY2(!mOutput.contains("Can't retry"), qPrintable(mOutput));
  }

  // A sync's own checks return their error without counting it, and
  // cmd.Run counts it as one. A plain one is retried on every attempt
  // (the CLI logs this line three times), and one rclone marks fatal is
  // not.
  void aSyncsOwnCheckIsRetriedLikeTheCli() {
    const QString source = makeDir("check-src");
    const QString dest = makeDir("check-dst");
    writeFile(source + "/a.txt", "a");
    const QStringList args{"sync", "--delete-after", "--no-check-dest", source,
                           dest};
    runCard(args, "rclonebrowser-");
    QVERIFY(!mSuccess);
    const QString line = "failed with 1 errors and: can't use --no-check-dest "
                         "with sync: use copy instead";
    QVERIFY2(mOutput.contains("Attempt 1/3 " + line), qPrintable(mOutput));
    QVERIFY2(mOutput.contains("Attempt 3/3 " + line), qPrintable(mOutput));
    QVERIFY2(!mOutput.contains("not attempting retries"), qPrintable(mOutput));
  }

  void aFatalCheckBeforeTheSyncIsNotRetried() {
    const FakeRun run = runFakeCard(
        [](const QString &path, const QJsonObject &,
           const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            reply(QJsonObject{
                {"finished", true},
                {"success", false},
                {"error",
                 "destination and parameter to --backup-dir mustn't overlap"},
                {"output", QJsonObject()}});
          } else {
            reply(QJsonObject{{"errors", 0}});
          }
        });
    QVERIFY2(run.finished, qPrintable(run.output));
    QVERIFY(!run.success);
    QCOMPARE(run.resubmits, 0);
    QVERIFY2(run.output.contains(
                 "Fatal error received - not attempting retries"),
             qPrintable(run.output));
    QVERIFY2(!run.output.contains("Attempt"), qPrintable(run.output));
  }

  // newPipe wraps a bad --order-by in a fatal error before the sync starts
  // (fs/sync/pipe.go), and the CLI says so and tries once. The pattern rows
  // are newLess's errors for --order-by pattern,<globs>, which v1.76.0's
  // CLI ends with the same fatal line and exit 7.
  void aBadOrderByIsFatal_data() {
    QTest::addColumn<QString>("orderBy");
    QTest::addColumn<QString>("error");
    QTest::newRow("comparison") << "foo" << "unknown --order-by comparison";
    QTest::newRow("no glob")
        << "pattern" << "pattern requires at least one glob";
    QTest::newRow("empty glob") << "pattern,," << "empty pattern glob";
    QTest::newRow("bad glob") << "pattern,[" << "bad pattern glob \"[\"";
  }

  void aBadOrderByIsFatal() {
    QFETCH(QString, orderBy);
    QFETCH(QString, error);
    const QString name = QTest::currentDataTag();
    const QString source = makeDir("order-by-src-" + name);
    const QString dest = makeDir("order-by-dst-" + name);
    writeFile(source + "/a.txt", "a");
    runCard({"copy", "--order-by", orderBy, source, dest}, "rclonebrowser-");
    QVERIFY(!mSuccess);
    QVERIFY2(mOutput.contains(error), qPrintable(mOutput));
    QVERIFY2(mOutput.contains("Fatal error received - not attempting retries"),
             qPrintable(mOutput));
    QVERIFY2(!mOutput.contains("Attempt"), qPrintable(mOutput));
  }

  // bisync runs cmd.Run with Retry off (cmd/bisync/cmd.go), which tries once
  // and prints none of the retry loop's lines, whatever the group counted.
  void bisyncPrintsNoAttemptLines_data() {
    QTest::addColumn<QString>("error");
    QTest::addColumn<bool>("retryable");
    QTest::newRow("retryable error") << QString("bisync aborted") << true;
    QTest::newRow("transfer stop")
        << QString("max transfer limit reached as set by --max-transfer")
        << false;
  }

  void bisyncPrintsNoAttemptLines() {
    QFETCH(QString, error);
    QFETCH(bool, retryable);
    const FakeRun run = runFakeCard(
        [&](const QString &path, const QJsonObject &,
            const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            reply(QJsonObject{{"finished", true},
                              {"success", false},
                              {"error", error},
                              {"output", QJsonObject()}});
          } else {
            reply(QJsonObject{{"errors", 1},
                              {"retryError", retryable},
                              {"lastError", "failed to open source object"}});
          }
        },
        {}, QJsonObject(), capsOff(), "sync/bisync", 1);
    QVERIFY2(run.finished, qPrintable(run.output));
    QCOMPARE(run.resubmits, 0);
    QVERIFY2(!run.output.contains("Attempt"), qPrintable(run.output));
    QVERIFY2(!run.output.contains("not attempting retries"),
             qPrintable(run.output));
  }

  // cmd.Run clears the errors and the retry flag between attempts, so an
  // attempt that only hits errors a retry can't fix is the last one, even
  // after an attempt that could be retried. Attempt 1: a locked file
  // (retryable) and an --immutable mismatch (not). Attempt 2, unlocked:
  // only the mismatch.
  void aLaterAttemptWithOnlyNoRetryErrorsIsTheLast() {
#ifdef Q_OS_WIN
    const QString source = makeDir("immutable-src");
    const QString dest = makeDir("immutable-dst");
    writeFile(source + "/keep.txt", "k");
    writeFile(source + "/locked.txt", "locked");
    writeFile(source + "/changed.txt", "AAAAAAAA");
    writeFile(dest + "/changed.txt", "BB");
    HANDLE lock = lockFile(source + "/locked.txt");
    QVERIFY(lock != INVALID_HANDLE_VALUE);
    const QStringList args{"copy", "--immutable", "--retries-sleep", "1s",
                           source, dest};
    runCard(args, "rclonebrowser-", [&](RcJobWidget &card) {
      QPlainTextEdit *output = card.findChild<QPlainTextEdit *>("output");
      QVERIFY(output);
      QVERIFY2(QTest::qWaitFor(
                   [&]() {
                     return output->toPlainText().contains(
                         "Attempt 1/3 failed with 2 errors");
                   },
                   60000),
               qPrintable(output->toPlainText()));
      CloseHandle(lock);
      lock = INVALID_HANDLE_VALUE;
    });
    if (lock != INVALID_HANDLE_VALUE) {
      CloseHandle(lock);
    }
    QVERIFY(!mSuccess);
    QVERIFY(QFile::exists(dest + "/locked.txt"));
    QVERIFY2(!mOutput.contains("Attempt 2/3 failed"), qPrintable(mOutput));
    QVERIFY2(!mOutput.contains("Attempt 3/3"), qPrintable(mOutput));
    QVERIFY2(mOutput.contains(
                 "Can't retry any of the errors - not attempting retries"),
             qPrintable(mOutput));
#else
    QSKIP("locks a file the Windows way");
#endif
  }

  // Each attempt reads a stats group of its own, and the card adds the
  // earlier attempts' transfers and bytes to it, as one CLI run counts
  // them. Read from the first attempt's group, attempt 2 would count its
  // files twice and carry attempt 1's error.
  void eachAttemptHasAStatsGroupOfItsOwn() {
    QStringList groups;
    const FakeRun run = runFakeCard(
        [&](const QString &path, const QJsonObject &body,
            const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            if (body.value("jobid").toInt() == 1) {
              reply(QJsonObject{{"finished", true},
                                {"success", false},
                                {"error", "boom"},
                                {"output", QJsonObject()}});
            } else {
              reply(QJsonObject{{"finished", true},
                                {"success", true},
                                {"output", QJsonObject()}});
            }
            return;
          }
          const QString group = body.value("group").toString();
          if (!groups.contains(group)) {
            groups << group;
          }
          if (group == "rclonebrowser-1") {
            reply(QJsonObject{{"errors", 1},
                              {"retryError", true},
                              {"lastError", "boom"},
                              {"transfers", 2},
                              {"bytes", 100},
                              {"totalBytes", 200},
                              {"checks", 3}});
          } else {
            reply(QJsonObject{{"errors", 0},
                              {"transfers", 1},
                              {"bytes", 50},
                              {"totalBytes", 50},
                              {"checks", 4}});
          }
        });
    QVERIFY2(run.finished, qPrintable(run.output));
    QVERIFY2(run.success, qPrintable(run.output));
    QCOMPARE(run.resubmits, 1);
    QCOMPARE(run.resubmitted.value("_group").toString(),
             QString("rclonebrowser-1-attempt-2"));
    QCOMPARE(groups, (QStringList{"rclonebrowser-1",
                                  "rclonebrowser-1-attempt-2"}));
    QVERIFY2(run.output.contains("Attempt 1/3 failed with 1 errors and: boom"),
             qPrintable(run.output));
    QVERIFY2(run.output.contains("Attempt 2/3 succeeded"),
             qPrintable(run.output));
    QCOMPARE(run.entry.files, 3);
    QCOMPARE(run.entry.bytes, qint64(150));
    QCOMPARE(run.entry.errors, 0);
  }

  // rclone counts --max-delete and --max-transfer per stats group, and the
  // CLI's one group keeps counting across attempts. A new group gets the
  // caps less what the earlier attempts deleted and moved.
  void theCapsCarryIntoTheNextAttemptsGroup() {
    int current = 1;
    const FakeRun run = runFakeCard(
        [&](const QString &path, const QJsonObject &body,
            const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            current = body.value("jobid").toInt();
            const bool last = current == 3;
            reply(QJsonObject{{"finished", true},
                              {"success", last},
                              {"error", last ? QString() : QString("boom")},
                              {"output", QJsonObject()}});
            return;
          }
          if (current == 3) {
            reply(QJsonObject{{"errors", 0}});
          } else {
            reply(QJsonObject{{"errors", 1},
                              {"retryError", true},
                              {"lastError", "boom"},
                              {"deletes", current == 1 ? 2 : 1},
                              {"bytes", current == 1 ? 1000 : 24}});
          }
        },
        {}, QJsonObject{{"MaxDelete", 5}, {"MaxTransfer", "1M"},
                        {"Transfers", 1}});
    QVERIFY2(run.finished, qPrintable(run.output));
    QVERIFY2(run.success, qPrintable(run.output));
    QCOMPARE(run.resubmits, 2);
    const QJsonObject second = run.resubmittedAll.at(0);
    const QJsonObject third = run.resubmittedAll.at(1);
    QCOMPARE(second.value("_group").toString(),
             QString("rclonebrowser-1-attempt-2"));
    QCOMPARE(third.value("_group").toString(),
             QString("rclonebrowser-1-attempt-3"));
    const QJsonObject config2 = second.value("_config").toObject();
    const QJsonObject config3 = third.value("_config").toObject();
    QCOMPARE(config2.value("MaxDelete").toInteger(), qint64(3));
    QCOMPARE(config2.value("MaxTransfer").toString(), QString("1047576b"));
    QCOMPARE(config2.value("Transfers").toInteger(), qint64(1));
    QCOMPARE(config3.value("MaxDelete").toInteger(), qint64(2));
    QCOMPARE(config3.value("MaxTransfer").toString(), QString("1047552b"));
    QVERIFY2(run.output.contains("Attempt 3/3 succeeded"),
             qPrintable(run.output));
  }

  // A --max-delete-size run whose deletes core/transferred doesn't list in
  // full (here it lists none, as once rclone has pruned them) can't tell
  // what they came to, so it stays in its group, which still counts them.
  // The card then reads each attempt's errors on top of the earlier ones',
  // and the group's transfers already include the earlier attempts'.
  void aDeleteSizeCapKeepsTheRunInOneGroup() {
    int current = 1;
    const FakeRun run = runFakeCard(
        [&](const QString &path, const QJsonObject &body,
            const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            current = body.value("jobid").toInt();
            const bool last = current == 3;
            reply(QJsonObject{{"finished", true},
                              {"success", last},
                              {"error", last ? QString() : QString("boom")},
                              {"output", QJsonObject()}});
            return;
          }
          const QStringList lastErrors{"boom", "boom2", "boom2"};
          const QList<int> errors{1, 3, 3};
          reply(QJsonObject{{"errors", errors.at(current - 1)},
                            {"retryError", true},
                            {"lastError", lastErrors.at(current - 1)},
                            {"deletes", current},
                            {"transfers", current}});
        },
        {}, QJsonObject{{"MaxDeleteSize", "10M"}});
    QVERIFY2(run.finished, qPrintable(run.output));
    QVERIFY2(run.success, qPrintable(run.output));
    QCOMPARE(run.resubmits, 2);
    for (const QJsonObject &payload : run.resubmittedAll) {
      QCOMPARE(payload.value("_group").toString(), QString("rclonebrowser-1"));
      QCOMPARE(payload.value("_config").toObject(),
               (QJsonObject{{"MaxDeleteSize", "10M"}}));
    }
    QVERIFY2(run.output.contains("Attempt 1/3 failed with 1 errors and: boom"),
             qPrintable(run.output));
    QVERIFY2(
        run.output.contains("Attempt 2/3 failed with 2 errors and: boom2"),
        qPrintable(run.output));
    QVERIFY2(run.output.contains("Attempt 3/3 succeeded"),
             qPrintable(run.output));
    QCOMPARE(run.entry.files, 3);
    QCOMPARE(run.entry.errors, 0);
  }

  // In that one group the retry flag attempt 1 raised stays up, since rc
  // can't clear a group's errors without its deletes too. A sync returns a
  // plain error ahead of one rclone marks as not worth a retry, so an
  // attempt that returned that kind had nothing a retry could fix, and it's
  // the last, as on the CLI, whose cmd.Run clears the flag between attempts.
  // A soft --max-transfer stop is that kind too. This misses a retryable
  // error rclone counted and never returned, a failed hash next to the
  // mismatch, which only a group of its own can tell apart
  // (aLockedFileNextToAnImmutableMismatchIsRetried).
  void aRetryFlagLeftFromAnEarlierAttemptIsNotThisOnes_data() {
    QTest::addColumn<QString>("error");
    QTest::addColumn<bool>("limitStop");
    QTest::newRow("immutable") << QString("immutable file modified") << false;
    QTest::newRow("deadline") << QString("context deadline exceeded")
                              << false;
    QTest::newRow("transfer stop")
        << QString("max transfer limit reached as set by --max-transfer")
        << true;
  }

  void aRetryFlagLeftFromAnEarlierAttemptIsNotThisOnes() {
    QFETCH(QString, error);
    QFETCH(bool, limitStop);
    int current = 1;
    const FakeRun run = runFakeCard(
        [&](const QString &path, const QJsonObject &body,
            const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            current = body.value("jobid").toInt();
            reply(QJsonObject{{"finished", true},
                              {"success", false},
                              {"error", current == 1 ? QString("boom") : error},
                              {"output", QJsonObject()}});
            return;
          }
          reply(QJsonObject{
              {"errors", current == 1 ? 1 : 2},
              {"retryError", true},
              {"lastError", current == 1 ? QString("boom") : error},
              {"deletes", 1}});
        },
        {}, QJsonObject{{"MaxDeleteSize", "10M"}});
    QVERIFY2(run.finished, qPrintable(run.output));
    QCOMPARE(run.success, limitStop);
    QCOMPARE(run.resubmits, 1);
    QCOMPARE(run.resubmitted.value("_group").toString(),
             QString("rclonebrowser-1"));
    QVERIFY2(run.output.contains("Attempt 1/3 failed with 1 errors and: boom"),
             qPrintable(run.output));
    QVERIFY2(run.output.contains(
                 "Can't retry any of the errors - not attempting retries"),
             qPrintable(run.output));
    QCOMPARE(run.output.count("not attempting retries"), 1);
    QVERIFY2(!run.output.contains("Attempt 2/3 failed"), qPrintable(run.output));
    QVERIFY2(!run.output.contains("Attempt 3/3"), qPrintable(run.output));
  }

  // When core/transferred lists every file the group deleted, a
  // --max-delete-size run gets a group of its own for the next attempt,
  // its cap lowered by their sizes: 3 and 4 bytes here, with a check and a
  // delete the cap refused left out. With one of them gone from the list,
  // as once rclone has pruned it, the run stays in its group. A cap the
  // daemon has from its own settings is lowered the same way.
  void aDeleteSizeCapIsLoweredByWhatWasDeleted_data() {
    QTest::addColumn<bool>("allListed");
    QTest::addColumn<bool>("daemonsOwn");
    QTest::addColumn<QString>("lowered");
    QTest::newRow("all listed") << true << false << QString("10485753b");
    QTest::newRow("one pruned") << false << false << QString();
    QTest::newRow("daemon's own") << true << true << QString("1017b");
  }

  void aDeleteSizeCapIsLoweredByWhatWasDeleted() {
    QFETCH(bool, allListed);
    QFETCH(bool, daemonsOwn);
    QFETCH(QString, lowered);
    const auto entry = [](const QString &name, const QString &what, int size,
                          const QString &error) {
      return QJsonObject{{"name", name},
                         {"what", what},
                         {"size", size},
                         {"bytes", 0},
                         {"error", error},
                         {"group", "rclonebrowser-1"},
                         {"started_at", "2026-11-16T10:00:00Z"},
                         {"completed_at", "2026-11-16T10:00:00Z"}};
    };
    QJsonArray listed{
        entry("b.txt", "deleting", 4, QString()),
        entry("c.txt", "checking", 100, QString()),
        entry("d.txt", "deleting", 50, "--max-delete-size threshold reached")};
    if (allListed) {
      listed.prepend(entry("a.txt", "deleting", 3, QString()));
    }
    int current = 1;
    int lists = 0;
    const FakeRun run = runFakeCard(
        [&](const QString &path, const QJsonObject &body,
            const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            current = body.value("jobid").toInt();
            const bool last = current == 2;
            reply(QJsonObject{{"finished", true},
                              {"success", last},
                              {"error", last ? QString() : QString("boom")},
                              {"output", QJsonObject()}});
            return;
          }
          const bool firstGroup =
              body.value("group").toString() == "rclonebrowser-1";
          if (path == "core/transferred") {
            lists++;
            reply(QJsonObject{
                {"transferred", firstGroup ? listed : QJsonArray()}});
            return;
          }
          if (!firstGroup) {
            reply(QJsonObject{{"errors", 0}});
            return;
          }
          reply(QJsonObject{{"errors", 1},
                            {"retryError", true},
                            {"lastError", "boom"},
                            {"deletes", 2}});
        },
        {},
        daemonsOwn ? QJsonObject()
                   : QJsonObject{{"MaxDeleteSize", "10M"}},
        daemonsOwn ? QJsonObject{{"MaxDelete", -1},
                                 {"MaxDeleteSize", 1024},
                                 {"MaxTransfer", -1}}
                   : capsOff());
    QVERIFY2(run.finished, qPrintable(run.output));
    QVERIFY2(run.success, qPrintable(run.output));
    QVERIFY(lists > 0);
    QCOMPARE(run.resubmits, 1);
    QCOMPARE(run.settingsReads, 1);
    const QJsonObject config = run.resubmitted.value("_config").toObject();
    if (lowered.isEmpty()) {
      QCOMPARE(run.resubmitted.value("_group").toString(),
               QString("rclonebrowser-1"));
      QCOMPARE(config, (QJsonObject{{"MaxDeleteSize", "10M"}}));
    } else {
      QCOMPARE(run.resubmitted.value("_group").toString(),
               QString("rclonebrowser-1-attempt-2"));
      QCOMPARE(config.value("MaxDeleteSize").toString(), lowered);
    }
    QVERIFY2(run.output.contains("Attempt 2/3 succeeded"),
             qPrintable(run.output));
  }

  // The daemon's caps are read again before each retry, since another
  // client can set them while the run goes on (options/set). The daemon had
  // none when it connected and has --max-delete 2 by the retry, after
  // attempt 1 deleted a file, so attempt 2 may delete one more. A daemon
  // that won't say what its caps are may have any, so the run stays in its
  // group, which still counts that delete.
  void theDaemonsCapsAreReadAgainBeforeARetry_data() {
    QTest::addColumn<bool>("answers");
    QTest::newRow("set since") << true;
    QTest::newRow("not said") << false;
  }

  void theDaemonsCapsAreReadAgainBeforeARetry() {
    QFETCH(bool, answers);
    const QJsonObject later =
        answers ? QJsonObject{{"MaxDelete", 2},
                              {"MaxDeleteSize", -1},
                              {"MaxTransfer", -1}}
                : QJsonObject();
    int current = 1;
    const FakeRun run = runFakeCard(
        [&](const QString &path, const QJsonObject &body,
            const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            current = body.value("jobid").toInt();
            const bool last = current == 2;
            reply(QJsonObject{{"finished", true},
                              {"success", last},
                              {"error", last ? QString() : QString("boom")},
                              {"output", QJsonObject()}});
            return;
          }
          if (path == "core/transferred") {
            reply(QJsonObject{{"transferred", QJsonArray()}});
            return;
          }
          if (body.value("group").toString() != "rclonebrowser-1") {
            reply(QJsonObject{{"errors", 0}});
            return;
          }
          reply(QJsonObject{{"errors", 1},
                            {"retryError", true},
                            {"lastError", "boom"},
                            {"deletes", 1}});
        },
        {}, QJsonObject(), capsOff(), "sync/copy", 3, &later);
    QVERIFY2(run.finished, qPrintable(run.output));
    QVERIFY2(run.success, qPrintable(run.output));
    QCOMPARE(run.resubmits, 1);
    QCOMPARE(run.settingsReads, 1);
    if (answers) {
      QCOMPARE(run.resubmitted.value("_group").toString(),
               QString("rclonebrowser-1-attempt-2"));
      QCOMPARE(run.resubmitted.value("_config")
                   .toObject()
                   .value("MaxDelete")
                   .toInteger(-1),
               qint64(1));
    } else {
      QCOMPARE(run.resubmitted.value("_group").toString(),
               QString("rclonebrowser-1"));
      QVERIFY(!run.resubmitted.contains("_config"));
    }
    QVERIFY2(run.output.contains("Attempt 2/3 succeeded"),
             qPrintable(run.output));
  }

  void planningTheNextAttempt() {
    const QJsonObject off = capsOff();
    const auto payload = [](const QJsonObject &config) {
      QJsonObject out{{"srcFs", "a:"}, {"_group", "g"}};
      if (!config.isEmpty()) {
        out.insert("_config", config);
      }
      return out;
    };
    using RcJobResult::PlanNextAttempt;

    // No caps: a new group, nothing added.
    RcJobResult::NextAttempt next =
        PlanNextAttempt(payload({}), payload({}), 2, 10, 3, off);
    QVERIFY(!next.sameGroup);
    QCOMPARE(next.payload.value("_group").toString(), QString("g-attempt-2"));
    QVERIFY(!next.payload.contains("_config"));

    // A cap of 0 is a cap, and a lowered one stops at 0.
    next = PlanNextAttempt(payload({{"MaxDelete", 0}}), payload({}), 2, 0, 0,
                           off);
    QCOMPARE(next.payload.value("_config").toObject().value("MaxDelete")
                 .toInteger(-1),
             qint64(0));
    next = PlanNextAttempt(payload({{"MaxDelete", 2}, {"MaxTransfer", "1k"}}),
                           payload({}), 2, 5000, 5, off);
    QVERIFY(!next.sameGroup);
    QCOMPARE(next.payload.value("_config").toObject().value("MaxDelete")
                 .toInteger(-1),
             qint64(0));
    QCOMPARE(next.payload.value("_config").toObject().value("MaxTransfer")
                 .toString(),
             QString("0b"));

    // Off stays off.
    next = PlanNextAttempt(
        payload({{"MaxTransfer", "off"}, {"MaxDeleteSize", "OFF"}}),
        payload({}), 3, 100, 2, off);
    QVERIFY(!next.sameGroup);
    QCOMPARE(next.payload.value("_group").toString(), QString("g-attempt-3"));
    QCOMPARE(next.payload.value("_config").toObject().value("MaxTransfer")
                 .toString(),
             QString("off"));

    // A size cap on deletes: a new group only while nothing was deleted.
    const QJsonObject sized = payload({{"MaxDeleteSize", "1k"}});
    const QJsonObject current{{"srcFs", "a:"}, {"_group", "g-attempt-2"}};
    next = PlanNextAttempt(sized, current, 3, 100, 0, off);
    QVERIFY(!next.sameGroup);
    next = PlanNextAttempt(sized, current, 3, 100, 1, off);
    QVERIFY(next.sameGroup);
    QCOMPARE(next.payload, current);
    // Unless core/transferred told what those deletes came to. The cap is
    // then lowered by that, and stops at 0. Deletes of empty files leave it
    // as it was.
    next = PlanNextAttempt(sized, current, 3, 100, 1, off, 24);
    QVERIFY(!next.sameGroup);
    QCOMPARE(next.payload.value("_group").toString(), QString("g-attempt-3"));
    QCOMPARE(next.payload.value("_config").toObject().value("MaxDeleteSize")
                 .toString(),
             QString("1000b"));
    next = PlanNextAttempt(sized, current, 3, 100, 1, off, 5000);
    QCOMPARE(next.payload.value("_config").toObject().value("MaxDeleteSize")
                 .toString(),
             QString("0b"));
    next = PlanNextAttempt(sized, current, 3, 100, 2, off, 0);
    QVERIFY(!next.sameGroup);
    QCOMPARE(next.payload.value("_config").toObject().value("MaxDeleteSize")
                 .toString(),
             QString("1k"));

    // A token this can't read is no cap it can lower.
    next = PlanNextAttempt(payload({{"MaxTransfer", "1MB"}}), current, 3, 1, 0,
                           off);
    QVERIFY(next.sameGroup);
    next = PlanNextAttempt(payload({{"MaxTransfer", "1MB"}}), current, 3, 0, 0,
                           off);
    QVERIFY(!next.sameGroup);

    // A cap _config leaves out is the daemon's own, from its environment or
    // flags, which options/get gives, sizes as a byte count. It's lowered
    // like one the job set, which takes its place.
    next = PlanNextAttempt(payload({}), current, 3, 1000, 1,
                           QJsonObject{{"MaxDelete", 4},
                                       {"MaxDeleteSize", -1},
                                       {"MaxTransfer", 1048576}});
    QVERIFY(!next.sameGroup);
    QCOMPARE(next.payload.value("_config").toObject().value("MaxDelete")
                 .toInteger(-1),
             qint64(3));
    QCOMPARE(next.payload.value("_config").toObject().value("MaxTransfer")
                 .toString(),
             QString("1047576b"));
    next = PlanNextAttempt(payload({{"MaxDelete", 2}}), current, 3, 0, 1,
                           QJsonObject{{"MaxDelete", 4}});
    QCOMPARE(next.payload.value("_config").toObject().value("MaxDelete")
                 .toInteger(-1),
             qint64(1));
    // An rclone whose settings have no such key has no such cap.
    next = PlanNextAttempt(payload({}), current, 3, 100, 1,
                           QJsonObject{{"Transfers", 4}});
    QVERIFY(!next.sameGroup);
    QVERIFY(!next.payload.contains("_config"));
    // The daemon's --max-delete-size goes the same way as the job's.
    next = PlanNextAttempt(payload({}), current, 3, 0, 1,
                           QJsonObject{{"MaxDeleteSize", 1024}});
    QVERIFY(next.sameGroup);
    next = PlanNextAttempt(payload({}), current, 3, 0, 1,
                           QJsonObject{{"MaxDeleteSize", 1024}}, 24);
    QVERIFY(!next.sameGroup);
    QCOMPARE(next.payload.value("_config").toObject().value("MaxDeleteSize")
                 .toString(),
             QString("1000b"));

    // A daemon that never said may have any cap, so a run that deleted or
    // moved something stays in its group, and one that didn't has nothing
    // to carry.
    next = PlanNextAttempt(payload({}), current, 3, 0, 1, QJsonObject());
    QVERIFY(next.sameGroup);
    next = PlanNextAttempt(payload({}), current, 3, 1, 0, QJsonObject());
    QVERIFY(next.sameGroup);
    next = PlanNextAttempt(payload({}), current, 3, 0, 0, QJsonObject());
    QVERIFY(!next.sameGroup);
    // A value that isn't a number is one this can't read either.
    next = PlanNextAttempt(payload({}), current, 3, 1, 0,
                           QJsonObject{{"MaxTransfer", "1G"}});
    QVERIFY(next.sameGroup);

    // With no group to stay in, the caps that can be lowered are.
    const QJsonObject loose{{"srcFs", "a:"},
                            {"_config", QJsonObject{{"MaxDelete", 3},
                                                    {"MaxDeleteSize", "1k"}}}};
    next = PlanNextAttempt(loose, loose, 2, 0, 1, off);
    QVERIFY(!next.sameGroup);
    QVERIFY(!next.payload.contains("_group"));
    QCOMPARE(next.payload.value("_config").toObject().value("MaxDelete")
                 .toInteger(-1),
             qint64(2));
  }

  // What core/transferred says a group deleted. Transfers, checks and the
  // deletes a cap refused don't count. A delete that failed does, as rclone
  // counts it before it tries, and one with no size counts 0. Two deletes
  // can share their times, and a file a later answer lists again counts
  // once.
  void countingWhatWasDeleted() {
    const auto entry = [](const QString &name, const QString &what,
                          qint64 size, const QString &error = QString()) {
      return QJsonObject{{"name", name},
                         {"what", what},
                         {"size", size},
                         {"error", error},
                         {"started_at", "2026-11-16T10:00:00Z"},
                         {"completed_at", "2026-11-16T10:00:01Z"}};
    };
    const auto sum = [](const QHash<QString, qint64> &deletes) {
      qint64 total = 0;
      for (const qint64 size : deletes) {
        total += size;
      }
      return total;
    };
    QHash<QString, qint64> deletes;
    RcJobResult::AddDeletes(
        QJsonObject{
            {"transferred",
             QJsonArray{
                 entry("a.txt", "deleting", 3),
                 entry("big.txt", "transferring", 100),
                 entry("c.txt", "checking", 7),
                 entry("d.txt", "deleting", 50,
                       "--max-delete-size threshold reached"),
                 entry("e.txt", "deleting", 9, "--max-delete threshold reached"),
                 entry("f.txt", "deleting", -1),
                 entry("g.txt", "deleting", 4, "Access is denied."),
                 entry("x1.txt", "deleting", 2),
                 entry("x2.txt", "deleting", 2)}}},
        &deletes);
    QCOMPARE(deletes.size(), 5);
    QCOMPARE(sum(deletes), qint64(11));
    RcJobResult::AddDeletes(
        QJsonObject{{"transferred", QJsonArray{entry("a.txt", "deleting", 3),
                                               entry("h.txt", "deleting", 5)}}},
        &deletes);
    QCOMPARE(deletes.size(), 6);
    QCOMPARE(sum(deletes), qint64(16));
    RcJobResult::AddDeletes(QJsonObject(), &deletes);
    QCOMPARE(deletes.size(), 6);
  }

  // The CLI's attempts share one --max-delete count. Attempt 1 deletes two
  // of the destination's extra files and fails on a locked one. Two more
  // extras turn up before attempt 2, which may delete only one of them
  // under --max-delete 3. In a group of its own with the cap as given, it
  // would delete both.
  void maxDeleteCountsAcrossAttempts_data() {
    QTest::addColumn<bool>("fromDaemon");
    QTest::newRow("--max-delete") << false;
    // A cap the daemon has from its own environment or flags isn't in the
    // job's _config, and options/get says what it is. The daemon here gets
    // RCLONE_MAX_DELETE from this app's environment, which loses it before
    // attempt 2 is planned, as with a daemon the app didn't start.
    QTest::newRow("daemon's own") << true;
  }

  void maxDeleteCountsAcrossAttempts() {
#ifdef Q_OS_WIN
    QFETCH(bool, fromDaemon);
    const QString name = fromDaemon ? "daemon" : "flag";
    const QString source = makeDir("cap-src-" + name);
    const QString dest = makeDir("cap-dst-" + name);
    writeFile(source + "/keep.txt", "k");
    writeFile(source + "/locked.txt", "locked");
    writeFile(dest + "/x1.txt", "1");
    writeFile(dest + "/x2.txt", "2");
    HANDLE lock = lockFile(source + "/locked.txt");
    QVERIFY(lock != INVALID_HANDLE_VALUE);
    QStringList args{"sync", "--delete-during", "--retries-sleep", "2s"};
    if (fromDaemon) {
      qputenv("RCLONE_MAX_DELETE", "3");
    } else {
      args << "--max-delete" << "3";
    }
    args << source << dest;
    runCard(args, "rclonebrowser-", [&](RcJobWidget &card) {
      qunsetenv("RCLONE_MAX_DELETE");
      QPlainTextEdit *output = card.findChild<QPlainTextEdit *>("output");
      QVERIFY(output);
      QVERIFY2(QTest::qWaitFor(
                   [&]() {
                     return output->toPlainText().contains(
                         "Attempt 1/3 failed with");
                   },
                   60000),
               qPrintable(output->toPlainText()));
      writeFile(dest + "/x3.txt", "3");
      writeFile(dest + "/x4.txt", "4");
    });
    qunsetenv("RCLONE_MAX_DELETE");
    CloseHandle(lock);
    QVERIFY(!mSuccess);
    QVERIFY(!QFile::exists(dest + "/x1.txt"));
    QVERIFY(!QFile::exists(dest + "/x2.txt"));
    const int left = int(QFile::exists(dest + "/x3.txt")) +
                     int(QFile::exists(dest + "/x4.txt"));
    QVERIFY2(left == 1, qPrintable(mOutput));
    QVERIFY2(mOutput.contains("Fatal error received - not attempting retries"),
             qPrintable(mOutput));
    QVERIFY2(!mOutput.contains("Attempt 2/3 failed"), qPrintable(mOutput));
#else
    QSKIP("locks a file the Windows way");
#endif
  }

  // A source file held open is one rclone can list but not hash. Next to an
  // --immutable mismatch, each attempt counts the hash failure, which a
  // retry could fix, and returns only the mismatch, which it can't, and the
  // CLI tries all 3 times. Under --max-delete-size, attempt 1's delete used
  // to keep the run in one group, where the mismatch the job returned was
  // all the card could go by, and it stopped after attempt 2.
  void aLockedFileNextToAnImmutableMismatchIsRetried() {
#ifdef Q_OS_WIN
    const QString source = makeDir("hash-src");
    const QString dest = makeDir("hash-dst");
    writeFile(source + "/same.txt", "AAAA");
    writeFile(source + "/moveme.txt", "m");
    writeFile(dest + "/same.txt", "BBBB");
    writeFile(dest + "/extra.txt", "extra");
    {
      QFile older(dest + "/same.txt");
      QVERIFY(older.open(QIODevice::ReadWrite));
      QVERIFY(older.setFileTime(QDateTime::currentDateTime().addDays(-1),
                                QFileDevice::FileModificationTime));
    }
    HANDLE lock = lockFile(source + "/same.txt");
    QVERIFY(lock != INVALID_HANDLE_VALUE);
    runCard({"sync", "--delete-before", "--immutable", "--max-delete-size",
             "1G", "--retries-sleep", "1s", source, dest},
            "rclonebrowser-");
    CloseHandle(lock);
    QVERIFY(!mSuccess);
    QVERIFY(!QFile::exists(dest + "/extra.txt"));
    QVERIFY(QFile::exists(dest + "/moveme.txt"));
    QVERIFY2(mOutput.contains("Attempt 1/3 failed"), qPrintable(mOutput));
    QVERIFY2(mOutput.contains("immutable file modified"), qPrintable(mOutput));
    QVERIFY2(mOutput.contains("Attempt 3/3 failed"), qPrintable(mOutput));
    QVERIFY2(!mOutput.contains("not attempting retries"), qPrintable(mOutput));
#else
    QSKIP("locks a file the Windows way");
#endif
  }

  // A cancel that comes after an attempt ended, while its last stats are
  // read, stops a retry that was due. When none was due the click changed
  // nothing, and the run keeps the result it had.
  void aCancelAfterAnAttemptThatCantBeRetriedKeepsItsResult() {
    bool ended = false;
    FakeRcServer::Reply lastStats;
    const FakeRun run = runFakeCard(
        [&](const QString &path, const QJsonObject &,
            const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            ended = true;
            reply(QJsonObject{{"finished", true},
                              {"success", false},
                              {"error", "immutable file modified"},
                              {"output", QJsonObject()}});
          } else if (ended && !lastStats) {
            lastStats = reply;
          } else {
            reply(QJsonObject{{"errors", 0}});
          }
        },
        [&](RcJobWidget &card) {
          if (!QTest::qWaitFor([&]() { return bool(lastStats); }, 20000)) {
            qWarning() << "the last stats read never came";
            return;
          }
          card.cancel();
          lastStats(QJsonObject{{"errors", 1},
                                {"retryError", false},
                                {"lastError", "immutable file modified"}});
        });
    QVERIFY2(run.finished, qPrintable(run.output));
    QCOMPARE(run.resubmits, 0);
    QVERIFY2(run.output.contains(
                 "Can't retry any of the errors - not attempting retries"),
             qPrintable(run.output));
    QVERIFY2(!run.output.contains("Cancelled"), qPrintable(run.output));
    QCOMPARE(run.entry.statusLabel, RcloneExitCode::Describe(1).name);
  }

  // Cancel clicked after the job ended, while its last stats are still on
  // the way, has no job to stop. It used to be dropped, and the failed
  // attempt was posted again as if nobody had clicked.
  void aCancelWhileTheLastStatsAreReadStopsTheRetry() {
    bool ended = false;
    FakeRcServer::Reply lastStats;
    const FakeRun run = runFakeCard(
        [&](const QString &path, const QJsonObject &,
            const FakeRcServer::Reply &reply) {
          if (path == "job/status") {
            ended = true;
            reply(QJsonObject{{"finished", true},
                              {"success", false},
                              {"error", "directory not found"},
                              {"output", QJsonObject()}});
          } else if (ended && !lastStats) {
            lastStats = reply;
          } else {
            reply(QJsonObject{{"errors", 0}});
          }
        },
        [&](RcJobWidget &card) {
          if (!QTest::qWaitFor([&]() { return bool(lastStats); }, 20000)) {
            qWarning() << "the last stats read never came";
            return;
          }
          card.cancel();
          lastStats(QJsonObject{{"errors", 1},
                                {"retryError", true},
                                {"lastError", "directory not found"}});
        });
    QVERIFY2(run.finished, qPrintable(run.output));
    QVERIFY(!run.success);
    QCOMPARE(run.resubmits, 0);
    QVERIFY2(run.output.contains("Cancelled before the next attempt started."),
             qPrintable(run.output));
    QCOMPARE(run.entry.statusLabel,
             RcloneExitCode::DescribeProcess(run.entry.exitCode, false, true,
                                             false)
                 .name);
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
    // The CLI's elapsed time runs on through --retries-sleep, which no
    // attempt's stats group covers.
    QVERIFY2(mElapsed.endsWith('s') && mElapsed.chopped(1).toInt() >= 1,
             qPrintable(mElapsed));
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
  QString mElapsed;

  // options/get's "main" from a daemon with every cap off.
  static QJsonObject capsOff() {
    return QJsonObject{
        {"MaxDelete", -1}, {"MaxDeleteSize", -1}, {"MaxTransfer", -1}};
  }

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

  struct FakeRun {
    bool finished = false;
    bool success = false;
    QString output;
    JobHistoryEntry entry;
    int resubmits = 0;
    // How often the card asked for the daemon's settings.
    int settingsReads = 0;
    // The last job posted again, and every one in order.
    QJsonObject resubmitted;
    QList<QJsonObject> resubmittedAll;
  };

  // Puts a card on a fake daemon, as attempt 1 of 3 of a sync/copy job, and
  // waits for it to finish. answers takes job/status (given the job id here),
  // core/stats and core/transferred, and anything else gets an empty answer.
  // A repost of the job counts as a resubmit and starts job 2, then job 3.
  // config, when given, is the job's _config, and daemonMain is what the
  // daemon said its own settings are when it connected. options/get gives
  // daemonLater as "main" when the card asks again before a retry, and
  // daemonMain when that's null.
  FakeRun runFakeCard(
      const FakeRcServer::Handler &answers,
      const std::function<void(RcJobWidget &)> &whileRunning = {},
      const QJsonObject &config = QJsonObject(),
      const QJsonObject &daemonMain = capsOff(),
      const QString &endpoint = QStringLiteral("sync/copy"),
      int attempts = 3, const QJsonObject *daemonLater = nullptr) {
    FakeRun run;
    const QJsonObject settings{
        {"main", daemonLater ? *daemonLater : daemonMain}};
    FakeRcServer daemon([&](const QString &path, const QJsonObject &body,
                            const FakeRcServer::Reply &reply) {
      if (path == "job/status") {
        // rclone puts the job id in every status answer, and the engine
        // takes an answer without it for a failed call.
        const QJsonValue id = body.value("jobid");
        answers(path, body, [reply, id](const QJsonObject &answer) {
          QJsonObject status = answer;
          status.insert("id", id);
          reply(status);
        });
      } else if (path == "core/stats" || path == "core/transferred") {
        answers(path, body, reply);
      } else if (path == "options/get") {
        run.settingsReads++;
        reply(settings);
      } else if (path == endpoint) {
        run.resubmits++;
        run.resubmitted = body;
        run.resubmittedAll << body;
        reply(QJsonObject{{"jobid", run.resubmits + 1}});
      } else {
        reply(QJsonObject());
      }
    });
    if (!daemon.isListening()) {
      qWarning() << "the fake daemon isn't listening";
      return run;
    }
    RcloneRcEngine engine;
    engine.useRemoteDaemon(daemon.url(), "user", "pass");
    bool up = false;
    engine.ensureStartedAsync(this, [&](bool ok, const QString &) { up = ok; });
    if (!QTest::qWaitFor([&]() { return up; }, 10000)) {
      qWarning() << "the engine never reached the fake daemon";
      return run;
    }

    RcloneRcEngine::StartedJob job;
    job.jobId = 1;
    job.group = "rclonebrowser-1";
    job.endpoint = endpoint;
    job.payload = QJsonObject{{"srcFs", "nas:a"},
                              {"dstFs", "nas:b"},
                              {"_async", true},
                              {"_group", job.group}};
    if (!config.isEmpty()) {
      job.payload.insert("_config", config);
    }
    job.attempts = attempts;
    job.daemonMain = daemonMain;
    const QStringList args{"copy", "nas:a", "nas:b"};
    RcJobWidget card(&engine, job.jobId, job.group, "test run", args,
                     "nas:a", "nas:b");
    card.setHistoryArgs(args);
    card.setStartedJob(job);
    QSignalSpy finished(&card, &RcJobWidget::finished);
    if (whileRunning) {
      whileRunning(card);
    }
    run.finished = !finished.isEmpty() || finished.wait(30000);
    if (QPlainTextEdit *output = card.findChild<QPlainTextEdit *>("output")) {
      run.output = output->toPlainText();
    }
    run.success = card.wasSuccessful();
    run.entry = card.historyEntry();
    return run;
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
    mElapsed.clear();
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
    if (QLineEdit *elapsed = card.findChild<QLineEdit *>("elapsed")) {
      mElapsed = elapsed->text();
    }
    mSuccess = card.wasSuccessful();
    mCompletedFully = card.completedFully();
    return card.historyEntry();
  }
};

QTEST_MAIN(RcJobWidgetTest)
#include "rc_job_widget_test.moc"
