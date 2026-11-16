#include "rc_job_result.h"
#include "rclone_exit_code.h"
#include "rclone_rc_engine.h"
#include "test_rclone.h"
#include "utils.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// Proves the engine really takes the sync/* route, which no test of the
// request builder can show. The two routes are told apart by the stats group
// they report: a sync/* job is given an explicit "rclonebrowser-N", while an
// async core/command job inherits rclone's own "job/<id>".
//
// This drives a real rclone rcd, because the interesting failures are all on
// the wire. Probing v1.75.0 while writing this: an unknown key inside _config
// is accepted with HTTP 200 and silently ignored, and _filter in the nested
// shape rclone's own options/info reports is likewise accepted and filters
// nothing. Neither shows up without actually copying files and looking.
class RcRouteTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mDir;
  QString mRclone;
  QString mConfigPath;

  QString makeTree(const QString &name) {
    const QString root = QDir(mDir.path()).filePath(name);
    QDir().mkpath(root + "/sub");
    for (const QString &rel : {QStringLiteral("keep.txt"),
                               QStringLiteral("drop.tmp"),
                               QStringLiteral("sub/keep2.txt"),
                               QStringLiteral("sub/drop2.tmp")}) {
      QFile file(root + "/" + rel);
      if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(rel.toUtf8());
      }
    }
    return root;
  }

  static QStringList filesUnder(const QString &root) {
    QStringList found;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      found << QDir(root).relativeFilePath(it.next());
    }
    found.sort();
    return found;
  }

  // Runs one transfer to completion and hands back the group it was given.
  QString runAndWait(RcloneRcEngine &engine, const QStringList &args,
                     bool *started) {
    RcloneRcEngine::StartedJob result;
    bool called = false;
    engine.runCommand(args, this,
                      [&](const RcloneRcEngine::StartedJob &job) {
                        result = job;
                        called = true;
                      });
    // Bringing the daemon up and reading its option tables takes a few round
    // trips before the job is even posted.
    QTest::qWait(0);
    bool ok = QTest::qWaitFor([&]() { return called; }, 60000);
    if (!ok) {
      *started = false;
      return QString();
    }
    *started = result.jobId >= 0;
    if (result.jobId < 0) {
      qWarning("job did not start: %s", qPrintable(result.error));
      return result.group;
    }

    // One status request at a time, and state the callback owns a share of,
    // since an answer can arrive after this returns.
    auto finished = std::make_shared<bool>(false);
    auto asking = std::make_shared<bool>(false);
    const bool done = QTest::qWaitFor(
        [&]() {
          if (!*asking) {
            *asking = true;
            engine.jobStatus(result.jobId, this,
                             [finished, asking](const QJsonObject &status,
                                                const QString &error) {
                               *asking = false;
                               if (!error.isEmpty() ||
                                   status.value("finished").toBool()) {
                                 *finished = true;
                               }
                             });
          }
          return *finished;
        },
        60000);
    if (!done) {
      qWarning("job %lld didn't finish within a minute",
               static_cast<long long>(result.jobId));
      *started = false;
    }
    return result.group;
  }

  // Deletes keep.txt, the sub folder and a file that isn't there, all under
  // tree, through one job/batch call, and hands back the finished job's
  // status.
  QJsonObject runDeleteBatch(RcloneRcEngine &engine, const QString &tree,
                             const QStringList &extraOptions,
                             RcloneRcEngine::StartedJob *started) {
    RcDeleteBatch::Plan plan;
    plan.extraOptions = extraOptions;
    const QList<QPair<QString, bool>> entries = {
        {"keep.txt", false}, {"sub", true}, {"missing.txt", false}};
    for (const auto &entry : entries) {
      RcDeleteBatch::Item item;
      // The fixture's "local" remote is an alias for the temporary folder.
      item.target = "local:" + tree + "/" + entry.first;
      item.label = entry.first;
      item.folder = entry.second;
      plan.items.append(item);
    }

    bool called = false;
    engine.runDeleteBatch(plan, this,
                          [&](const RcloneRcEngine::StartedJob &job) {
                            *started = job;
                            called = true;
                          });
    if (!QTest::qWaitFor([&]() { return called; }, 60000) ||
        started->jobId < 0) {
      return QJsonObject();
    }

    auto finished = std::make_shared<QJsonObject>();
    auto asking = std::make_shared<bool>(false);
    const bool done = QTest::qWaitFor(
        [&]() {
          if (!*asking) {
            *asking = true;
            engine.jobStatus(started->jobId, this,
                             [finished, asking](const QJsonObject &status,
                                                const QString &error) {
                               *asking = false;
                               if (error.isEmpty() &&
                                   status.value("finished").toBool()) {
                                 *finished = status;
                               }
                             });
          }
          return !finished->isEmpty();
        },
        60000);
    if (!done) {
      qWarning("batch job %lld didn't finish within a minute",
               static_cast<long long>(started->jobId));
    }
    return *finished;
  }

private slots:
  void initTestCase() {
    QVERIFY(mDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mDir.path());
    QCoreApplication::setOrganizationName("rclone-browser-rc-route-test");
    QCoreApplication::setApplicationName("rclone-browser-rc-route-test");

    mRclone = FindRcloneForTests();
    if (mRclone.isEmpty()) {
      QSKIP("rclone is not on PATH and RCLONE_BROWSER_TEST_RCLONE is unset");
    }

    mConfigPath = QDir(mDir.path()).filePath("rclone.conf");
    QFile config(mConfigPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write("[local]\ntype = alias\nremote = " +
                 mDir.path().toUtf8() + "\n");
    config.close();

    SetRclone(mRclone);
    SetRcloneConf(mConfigPath);
  }

  // A copy whose every flag maps onto an rclone option goes native, and the
  // filter has to reach the wire in the shape rclone honours: the nested form
  // is accepted and does nothing.
  void aMappableCopyTakesTheSyncRouteAndItsFilterApplies() {
    const QString source = makeTree("src-native");
    const QString dest = QDir(mDir.path()).filePath("dst-native");

    RcloneRcEngine engine;
    QStringList args;
    args << "copy" << "--transfers" << "2" << "--exclude" << "*.tmp"
         << "--verbose" << "--stats" << "1s" << source << dest;

    bool started = false;
    const QString group = runAndWait(engine, args, &started);
    QVERIFY2(started, "the transfer never started");
    QVERIFY2(group.startsWith("rclonebrowser-"),
             qPrintable("group was " + group));

    QCOMPARE(filesUnder(dest),
             QStringList() << "keep.txt" << "sub/keep2.txt");
  }

  // --one-file-system is a real rclone flag that options/info does not list,
  // so it cannot be mapped. Dropping it would change what the transfer does,
  // so the whole request falls back to running the CLI in the daemon.
  void anUnmappableFlagFallsBackToCoreCommand() {
    const QString source = makeTree("src-fallback");
    const QString dest = QDir(mDir.path()).filePath("dst-fallback");

    RcloneRcEngine engine;
    QStringList args;
    args << "copy" << "--one-file-system" << "--verbose" << source << dest;

    bool started = false;
    const QString group = runAndWait(engine, args, &started);
    QVERIFY2(started, "the transfer never started");
    QVERIFY2(group.startsWith("job/"), qPrintable("group was " + group));

    QCOMPARE(filesUnder(dest).size(), 4);
  }

  // core/command runs a second rclone inside the daemon, and that one reads
  // the default rclone.conf unless it's told otherwise. A remote that only
  // exists in the config chosen in Preferences has to resolve there too.
  void coreCommandReadsTheConfigFromPreferences() {
    makeTree("src-conf");
    const QString dest = QDir(mDir.path()).filePath("dst-conf");

    RcloneRcEngine engine;
    QStringList args;
    // The fixture's "local" remote is an alias for the temporary folder.
    args << "copy" << "--one-file-system" << "--verbose" << "local:src-conf"
         << dest;

    bool started = false;
    const QString group = runAndWait(engine, args, &started);
    QVERIFY2(started, "the transfer never started");
    QVERIFY2(group.startsWith("job/"), qPrintable("group was " + group));

    QCOMPARE(filesUnder(dest).size(), 4);
  }

  // sync/copy takes a directory Fs and fails a file source with "is a file
  // not a directory", while the CLI splits it and copies the one file. The
  // engine has to notice before it commits to the route.
  void aSingleFileSourceStaysOnCoreCommand() {
    const QString source = makeTree("src-onefile");
    const QString dest = QDir(mDir.path()).filePath("dst-onefile");

    RcloneRcEngine engine;
    QStringList args;
    args << "copy" << "--verbose" << (source + "/keep.txt") << dest;

    bool started = false;
    const QString group = runAndWait(engine, args, &started);
    QVERIFY2(started, "the transfer never started");
    QVERIFY2(group.startsWith("job/"), qPrintable("group was " + group));

    QCOMPARE(filesUnder(dest), QStringList() << "keep.txt");
  }

  // rclone ignores a path's global.<option> for a job started over rc.
  // Measured on v1.76.0, sync/sync deleted these files while the CLI moves
  // them into the backup folder, which is what the deletion check promised.
  // The run has to take the CLI in the daemon.
  void aPathThatSetsAGlobalOptionRunsOnCoreCommand() {
    const QString source = QDir(mDir.path()).filePath("global-src");
    QVERIFY(QDir().mkpath(source));
    const QString dest = makeTree("global-dst");
    const QString backup = QDir(mDir.path()).filePath("global-bak");

    RcloneRcEngine engine;
    QStringList args;
    args << "sync" << "--verbose" << source
         << (":local,global.backup_dir=':local:" + backup + "':" + dest);

    bool started = false;
    const QString group = runAndWait(engine, args, &started);
    QVERIFY2(started, "the transfer never started");
    QVERIFY2(group.startsWith("job/"), qPrintable("group was " + group));

    QVERIFY2(filesUnder(dest).isEmpty(),
             qPrintable("left in the destination: " +
                        filesUnder(dest).join(", ")));
    QCOMPARE(filesUnder(backup), QStringList() << "drop.tmp" << "keep.txt"
                                               << "sub/drop2.tmp"
                                               << "sub/keep2.txt");
  }

  // The same option set by rclone.conf, as a key of the remote's own, inside
  // the remote's value, or by the remote's environment variable. None of it
  // shows in the arguments, and sync/sync ignores all of it.
  void aConfigThatSetsAGlobalOptionRunsOnCoreCommand_data() {
    QTest::addColumn<QString>("how");
    QTest::newRow("a key") << QStringLiteral("key");
    QTest::newRow("a value") << QStringLiteral("value");
    QTest::newRow("a variable") << QStringLiteral("variable");
  }

  void aConfigThatSetsAGlobalOptionRunsOnCoreCommand() {
    QFETCH(QString, how);
    const QString tag = "conf-" + how;
    const QString root = mDir.path();
    const QString source = QDir(root).filePath(tag + "-src");
    QVERIFY(QDir().mkpath(source));
    const QString dest = makeTree(tag + "-dst");
    const QString backup = QDir(root).filePath(tag + "-bak");

    QByteArray section = "[bk]\ntype = alias\n";
    QByteArray variable;
    if (how == "key") {
      section += "remote = " + root.toUtf8() + "\nglobal.backup_dir = bk:" +
                 (tag + "-bak").toUtf8() + "\n";
    } else if (how == "value") {
      section += "remote = :local,global.backup_dir=':local:" +
                 backup.toUtf8() + "':" + root.toUtf8() + "\n";
    } else {
      section += "remote = " + root.toUtf8() + "\n";
      variable = "RCLONE_CONFIG_BK_GLOBAL.BACKUP_DIR";
    }
    const QString configPath = QDir(root).filePath(tag + ".conf");
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write(section);
    config.close();

    // The other tests read the fixture's config and a clean environment.
    struct Restore {
      QString fixture;
      QByteArray name;
      ~Restore() {
        SetRcloneConf(fixture);
        if (!name.isEmpty()) {
          qunsetenv(name.constData());
        }
      }
    } restore{mConfigPath, variable};
    SetRcloneConf(configPath);
    if (!variable.isEmpty()) {
      QVERIFY(qputenv(variable.constData(), "bk:" + (tag + "-bak").toUtf8()));
    }

    RcloneRcEngine engine;
    QStringList args;
    args << "sync" << "--verbose" << source << ("bk:" + tag + "-dst");

    bool started = false;
    const QString group = runAndWait(engine, args, &started);
    QVERIFY2(started, "the transfer never started");
    QVERIFY2(group.startsWith("job/"), qPrintable("group was " + group));

    QVERIFY2(filesUnder(dest).isEmpty(),
             qPrintable("left in the destination: " +
                        filesUnder(dest).join(", ")));
    QCOMPARE(filesUnder(backup), QStringList() << "drop.tmp" << "keep.txt"
                                               << "sub/drop2.tmp"
                                               << "sub/keep2.txt");
  }

  // core/bwlimit is the only live tuning rclone actually offers. Measured on
  // v1.75.0: setting main.Transfers to 8 mid-job left the running job at one
  // file in flight for eight seconds while a job started afterwards used all
  // eight, so concurrency is fixed when the sync starts and no control for it
  // can exist on a running job's panel.
  void theBandwidthLimitCanBeReadAndChangedOnALiveDaemon() {
    RcloneRcEngine engine;

    QJsonObject initial;
    QString initialError;
    bool got = false;
    engine.ensureStartedAsync(this, [&](bool ok, const QString &error) {
      QVERIFY2(ok, qPrintable(error));
      engine.bandwidthLimit(QString(), this,
                            [&](const QJsonObject &response,
                                const QString &responseError) {
                              initial = response;
                              initialError = responseError;
                              got = true;
                            });
    });
    QVERIFY(QTest::qWaitFor([&]() { return got; }, 60000));
    QVERIFY2(initialError.isEmpty(), qPrintable(initialError));
    QCOMPARE(initial.value("rate").toString(), QStringLiteral("off"));

    // rclone rewrites the rate it is given, so a panel that echoed back what
    // the user typed would show "1M" while the daemon holds "1Mi".
    QJsonObject applied;
    bool set = false;
    engine.bandwidthLimit("1M", this,
                          [&](const QJsonObject &response, const QString &) {
                            applied = response;
                            set = true;
                          });
    QVERIFY(QTest::qWaitFor([&]() { return set; }, 30000));
    QCOMPARE(applied.value("rate").toString(), QStringLiteral("1Mi"));
    QCOMPARE(applied.value("bytesPerSecond").toDouble(), 1048576.0);

    // And it is really on the daemon, not just in the reply.
    QJsonObject reread;
    bool again = false;
    engine.bandwidthLimit(QString(), this,
                          [&](const QJsonObject &response, const QString &) {
                            reread = response;
                            again = true;
                          });
    QVERIFY(QTest::qWaitFor([&]() { return again; }, 30000));
    QCOMPARE(reread.value("rate").toString(), QStringLiteral("1Mi"));

    bool cleared = false;
    engine.bandwidthLimit("off", this,
                          [&](const QJsonObject &, const QString &) {
                            cleared = true;
                          });
    QVERIFY(QTest::qWaitFor([&]() { return cleared; }, 30000));
  }

  // Deleting is not a sync operation at all, and there is no sync/delete.
  void aNonSyncCommandStaysOnCoreCommand() {
    const QString source = makeTree("src-delete");

    RcloneRcEngine engine;
    QStringList args;
    args << "delete" << "--verbose" << source;

    bool started = false;
    const QString group = runAndWait(engine, args, &started);
    QVERIFY2(started, "the command never started");
    QVERIFY2(group.startsWith("job/"), qPrintable("group was " + group));
  }

  // Several selected items go to rclone as one job/batch call. rclone calls
  // the whole job a success even when an item in it failed, so the item that
  // wasn't there has to come out of the per-item results.
  void severalDeletesGoThroughOneBatchAndNameTheFailure() {
    const QString root = makeTree("batch-delete");

    RcloneRcEngine engine;
    RcloneRcEngine::StartedJob started;
    const QJsonObject status = runDeleteBatch(
        engine, "batch-delete", QStringList() << "--fast-list", &started);
    QVERIFY2(started.jobId >= 0, qPrintable(started.error));
    QVERIFY2(started.group.startsWith("rclonebrowser-"),
             qPrintable("group was " + started.group));
    QVERIFY2(!status.isEmpty(), "the batch never finished");

    const QStringList failures =
        RcDeleteBatch::Failures(status.value("output"), started.batchLabels);
    QCOMPARE(failures.size(), 1);
    QVERIFY2(failures.first().startsWith("missing.txt: "),
             qPrintable(failures.first()));
    // The job card reads it through RcJobResult and must agree.
    const RcJobResult::Result result =
        RcJobResult::Read(status, started.batchLabels);
    QVERIFY(!result.success);
    QCOMPARE(result.output, failures);
    QCOMPARE(result.problems, failures);
    QCOMPARE(result.error, QString("1 of %1 items couldn't be deleted.")
                               .arg(started.batchLabels.size()));

    QCOMPARE(filesUnder(root), QStringList() << "drop.tmp");
    QVERIFY(!QDir(root + "/sub").exists());
  }

  // The default options ride along in the batch's _config. rclone ignores a
  // _config key it doesn't know without an error, so only deleting nothing
  // under --dry-run shows the option really arrived.
  void theDefaultOptionsReachTheBatch() {
    const QString root = makeTree("batch-dry-run");

    RcloneRcEngine engine;
    RcloneRcEngine::StartedJob started;
    const QJsonObject status = runDeleteBatch(
        engine, "batch-dry-run", QStringList() << "--dry-run", &started);
    QVERIFY2(started.jobId >= 0, qPrintable(started.error));
    QVERIFY2(!status.isEmpty(), "the batch never finished");

    QCOMPARE(filesUnder(root).size(), 4);
  }

  // The source a scheduled sync finds empty: an unplugged drive, an expired
  // login. The cap has to reach the daemon's _config and stop the deletes
  // there, and the reason has to come back where the job card reads it.
  // job/status only says "failed to delete N files"; core/stats lastError
  // carries rclone's cap message.
  void theDeleteCapStopsASyncOnTheDaemon() {
    const QString source = QDir(mDir.path()).filePath("cap-empty-src");
    QVERIFY(QDir().mkpath(source));
    const QString dest = QDir(mDir.path()).filePath("cap-dst");
    QVERIFY(QDir().mkpath(dest));
    for (int i = 1; i <= 5; ++i) {
      QFile file(dest + QString("/f%1.txt").arg(i));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("keep me");
    }

    RcloneRcEngine engine;
    RcloneRcEngine::StartedJob job;
    bool called = false;
    engine.runCommand(QStringList() << "sync" << "--max-delete" << "2"
                                    << source << dest,
                      this, [&](const RcloneRcEngine::StartedJob &started) {
                        job = started;
                        called = true;
                      });
    QVERIFY2(QTest::qWaitFor([&]() { return called; }, 60000),
             "the sync never started");
    QVERIFY2(job.jobId >= 0, qPrintable(job.error));
    QVERIFY2(job.group.startsWith("rclonebrowser-"),
             qPrintable("group was " + job.group));

    auto status = std::make_shared<QJsonObject>();
    auto asking = std::make_shared<bool>(false);
    QVERIFY2(QTest::qWaitFor(
                 [&]() {
                   if (!*asking) {
                     *asking = true;
                     engine.jobStatus(job.jobId, this,
                                      [status, asking](const QJsonObject &s,
                                                       const QString &error) {
                                        *asking = false;
                                        if (error.isEmpty() &&
                                            s.value("finished").toBool()) {
                                          *status = s;
                                        }
                                      });
                   }
                   return !status->isEmpty();
                 },
                 60000),
             "the sync never finished");
    QVERIFY2(!status->value("success").toBool(),
             "a sync stopped by the cap reported success");
    QCOMPARE(filesUnder(dest).size(), 3);

    QJsonObject stats;
    bool statsCalled = false;
    engine.coreStats(job.group, this,
                     [&](const QJsonObject &s, const QString &) {
                       stats = s;
                       statsCalled = true;
                     });
    QVERIFY(QTest::qWaitFor([&]() { return statsCalled; }, 30000));
    const QString lastError = stats.value("lastError").toString();
    QVERIFY2(RcloneExitCode::IsDeleteCapError(lastError),
             qPrintable("lastError was: " + lastError + " / job error: " +
                        status->value("error").toString()));
  }

  // A Sync with a flag options/info doesn't list (--one-file-system) goes
  // through core/command. job/status calls that job a success whenever the
  // daemon ran the command; the cap stop is only in the child rclone's
  // output.
  void theDeleteCapStopsASyncOnCoreCommand() {
    const QString source = QDir(mDir.path()).filePath("cap-cmd-src");
    QVERIFY(QDir().mkpath(source));
    const QString dest = QDir(mDir.path()).filePath("cap-cmd-dst");
    QVERIFY(QDir().mkpath(dest));
    for (int i = 1; i <= 5; ++i) {
      QFile file(dest + QString("/f%1.txt").arg(i));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("keep me");
    }

    RcloneRcEngine engine;
    RcloneRcEngine::StartedJob job;
    bool called = false;
    engine.runCommand(QStringList() << "sync" << "--one-file-system"
                                    << "--max-delete" << "2" << source << dest,
                      this, [&](const RcloneRcEngine::StartedJob &started) {
                        job = started;
                        called = true;
                      });
    QVERIFY2(QTest::qWaitFor([&]() { return called; }, 60000),
             "the sync never started");
    QVERIFY2(job.jobId >= 0, qPrintable(job.error));
    QVERIFY2(job.group.startsWith("job/"),
             qPrintable("expected the core/command route, group was " +
                        job.group));

    auto status = std::make_shared<QJsonObject>();
    auto asking = std::make_shared<bool>(false);
    QVERIFY2(QTest::qWaitFor(
                 [&]() {
                   if (!*asking) {
                     *asking = true;
                     engine.jobStatus(job.jobId, this,
                                      [status, asking](const QJsonObject &s,
                                                       const QString &error) {
                                        *asking = false;
                                        if (error.isEmpty() &&
                                            s.value("finished").toBool()) {
                                          *status = s;
                                        }
                                      });
                   }
                   return !status->isEmpty();
                 },
                 60000),
             "the sync never finished");
    QCOMPARE(filesUnder(dest).size(), 3);

    const RcJobResult::Result result = RcJobResult::Read(*status, {});
    QVERIFY2(!result.success,
             qPrintable("a capped sync read as a success: " +
                        QJsonDocument(*status).toJson()));
    QVERIFY2(!result.deleteCapLine.isEmpty(),
             qPrintable("no cap line in: " + result.output.join('\n')));
    QVERIFY(!result.error.isEmpty());
  }

  // --max-transfer stops a run on purpose, and the CLI route calls exit 8 a
  // success. Every daemon route must read it the same way: core/command
  // (a Sync with an unlisted flag), sync/sync and sync/copy.
  void aTransferLimitStopReadsAsFinishedOnEveryRoute() {
    const QString source = QDir(mDir.path()).filePath("limit-src");
    QVERIFY(QDir().mkpath(source));
    {
      QFile big(source + "/big.bin");
      QVERIFY(big.open(QIODevice::WriteOnly));
      QByteArray block(64 * 1024, '\0');
      for (int i = 0; i < 32; ++i) {
        block.fill(char('a' + i % 26));
        big.write(block);
      }
    }

    const QList<QStringList> routes{
        {"sync", "--one-file-system"},
        {"sync", "--delete-during"},
        {"copy"},
    };
    QStringList groups;
    for (const QStringList &command : routes) {
      const QString dest = QDir(mDir.path()).filePath(
          "limit-dst-" + command.join('_').remove('-'));
      QVERIFY(QDir().mkpath(dest));
      RcloneRcEngine engine;
      RcloneRcEngine::StartedJob job;
      const QJsonObject status = RunToEnd(
          engine,
          QStringList(command) << "--max-transfer" << "1k" << "--transfers"
                               << "1" << source << dest,
          &job);
      QVERIFY2(!status.isEmpty(), qPrintable(command.first() + " never finished"));
      groups << job.group;
      const RcJobResult::Result result = RcJobResult::Read(status, {});
      QVERIFY2(result.exitCode == 8 && result.success,
               qPrintable(QString("%1 via %2 read as exit %3: %4")
                              .arg(command.first(), job.group)
                              .arg(result.exitCode)
                              .arg(QString::fromUtf8(
                                  QJsonDocument(status).toJson()))));
      QVERIFY(result.error.isEmpty());
    }
    // Each route really ran: --one-file-system sends the first Sync through
    // core/command, and the other two go to sync/sync and sync/copy.
    QVERIFY2(groups.at(0).startsWith("job/"), qPrintable(groups.join(", ")));
    QVERIFY2(groups.at(1).startsWith("rclonebrowser-"),
             qPrintable(groups.join(", ")));
    QVERIFY2(groups.at(2).startsWith("rclonebrowser-"),
             qPrintable(groups.join(", ")));
  }

  // Every Sync the app builds names a delete timing. Each one has to reach
  // sync/sync with that timing applied, so its card reads live stats from
  // its own group. A --max-transfer stop shows the timing: before and
  // during delete first, and after skips the deletes once the copy failed.
  void eachDeleteTimingRunsOnTheSyncRoute() {
    const QString source = QDir(mDir.path()).filePath("timing-src");
    QVERIFY(QDir().mkpath(source));
    {
      QFile big(source + "/big.bin");
      QVERIFY(big.open(QIODevice::WriteOnly));
      QByteArray block(64 * 1024, '\0');
      for (int i = 0; i < 32; ++i) {
        block.fill(char('a' + i % 26));
        big.write(block);
      }
    }
    const QList<QPair<QString, bool>> timings{{"--delete-before", true},
                                              {"--delete-during", true},
                                              {"--delete-after", false}};
    for (const auto &timing : timings) {
      const QString dest = QDir(mDir.path()).filePath(
          "timing-dst" + QString(timing.first).remove('-'));
      QVERIFY(QDir().mkpath(dest));
      for (int i = 1; i <= 3; ++i) {
        QFile old(dest + QString("/old%1.txt").arg(i));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("old");
      }
      // The options a default Sync from the transfer dialog carries.
      const QStringList args{"sync",
                             timing.first,
                             "--max-delete",
                             "100",
                             "--verbose",
                             "--transfers",
                             "1",
                             "--checkers",
                             "8",
                             "--contimeout",
                             "60s",
                             "--timeout",
                             "300s",
                             "--retries",
                             "1",
                             "--low-level-retries",
                             "10",
                             "--max-transfer",
                             "1k",
                             "--use-json-log",
                             "--stats",
                             "1s",
                             "--stats-file-name-length",
                             "0",
                             source,
                             dest};
      RcloneRcEngine engine;
      RcloneRcEngine::StartedJob job;
      const QJsonObject status = RunToEnd(engine, args, &job);
      QVERIFY2(!status.isEmpty(), qPrintable(timing.first + " never finished"));
      QVERIFY2(job.group.startsWith("rclonebrowser-"),
               qPrintable(timing.first + " ran via " + job.group));

      const QStringList left = filesUnder(dest);
      const bool deletedFirst = !left.contains("old1.txt");
      QVERIFY2(deletedFirst == timing.second,
               qPrintable(timing.first + " left " + left.join(", ")));

      QJsonObject stats;
      bool statsCalled = false;
      engine.coreStats(job.group, this,
                       [&](const QJsonObject &s, const QString &) {
                         stats = s;
                         statsCalled = true;
                       });
      QVERIFY(QTest::qWaitFor([&]() { return statsCalled; }, 30000));
      QVERIFY2(stats.value("bytes").toDouble() > 0,
               qPrintable(timing.first + " stats: " +
                          QJsonDocument(stats).toJson()));
    }
  }

  // sync/copy takes --error-on-no-transfer into its _config, then reports a
  // run that copied nothing as plain success. The process route exits 9, so
  // the card reads the transfer count to say the same.
  void nothingToCopyOnTheNativeRouteReadsAsExit9() {
    const QString source = QDir(mDir.path()).filePath("none-src");
    const QString dest = QDir(mDir.path()).filePath("none-dst");
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(dest));
    for (const QString &dir : {source, dest}) {
      QFile same(dir + "/same.txt");
      QVERIFY(same.open(QIODevice::WriteOnly));
      same.write("unchanged");
    }
    {
      // setFileTime needs the file open.
      QFile copy(dest + "/same.txt");
      QVERIFY(copy.open(QIODevice::ReadWrite));
      QVERIFY(copy.setFileTime(
          QFileInfo(source + "/same.txt").lastModified(),
          QFileDevice::FileModificationTime));
    }

    const QStringList args{"copy", "--error-on-no-transfer", source, dest};
    RcloneRcEngine engine;
    RcloneRcEngine::StartedJob job;
    const QJsonObject status = RunToEnd(engine, args, &job);
    QVERIFY2(!status.isEmpty(), "the copy never finished");
    QVERIFY2(job.group.startsWith("rclonebrowser-"), qPrintable(job.group));
    const RcJobResult::Result result = RcJobResult::Read(status, {});
    QVERIFY(!result.fromCommand);
    QVERIFY2(result.success && result.exitCode == 0,
             qPrintable(QString::fromUtf8(QJsonDocument(status).toJson())));

    QJsonObject stats;
    bool statsCalled = false;
    engine.coreStats(job.group, this,
                     [&](const QJsonObject &s, const QString &) {
                       stats = s;
                       statsCalled = true;
                     });
    QVERIFY(QTest::qWaitFor([&]() { return statsCalled; }, 30000));
    QCOMPARE(stats.value("transfers").toInt(), 0);
    QCOMPARE(RcJobResult::WithNoTransferExit(
                 result.exitCode, stats.value("transfers").toInt(), args),
             9);
  }

  void noTransferExitNeedsTheFlagAndNothingCopied() {
    const QStringList flag{"copy", "--error-on-no-transfer", "a:", "b:"};
    QCOMPARE(RcJobResult::WithNoTransferExit(0, 0, flag), 9);
    QCOMPARE(RcJobResult::WithNoTransferExit(
                 0, 0, {"copy", "--error-on-no-transfer=true", "a:", "b:"}),
             9);
    QCOMPARE(RcJobResult::WithNoTransferExit(0, 1, flag), 0);
    QCOMPARE(RcJobResult::WithNoTransferExit(8, 0, flag), 8);
    QCOMPARE(RcJobResult::WithNoTransferExit(0, 0, {"copy", "a:", "b:"}), 0);
    QCOMPARE(RcJobResult::WithNoTransferExit(0, 0, {}), 0);
  }

  // The shapes job/status comes back in, one per route, as rclone sends them.
  void readsEachRoutesResult() {
    const QJsonObject plain{{"finished", true},
                            {"success", true},
                            {"error", ""},
                            {"output", QJsonObject()}};
    RcJobResult::Result result = RcJobResult::Read(plain, {});
    QVERIFY(result.success);
    QVERIFY(result.error.isEmpty());
    QVERIFY(result.output.isEmpty());

    const QJsonObject failed{
        {"finished", true},
        {"success", false},
        {"error", "failed to delete 3 files"},
        {"output", QJsonObject()}};
    result = RcJobResult::Read(failed, {});
    QVERIFY(!result.success);
    QCOMPARE(result.error, QString("failed to delete 3 files"));
    QVERIFY(result.deleteCapLine.isEmpty());

    const QJsonObject commandOk{
        {"finished", true},
        {"success", true},
        {"error", ""},
        {"output", QJsonObject{{"error", false},
                               {"result", "Transferred: 2 / 2, 100%\n"}}}};
    result = RcJobResult::Read(commandOk, {});
    QVERIFY(result.success);
    QCOMPARE(result.output, QStringList() << "Transferred: 2 / 2, 100%");

    const QJsonObject commandFailed{
        {"finished", true},
        {"success", true},
        {"error", ""},
        {"output",
         QJsonObject{{"error", true},
                     {"result", "NOTICE: f1.txt: Deleted\n"
                                "ERROR : Local file system: not deleting "
                                "files as there were IO errors\n"}}}};
    result = RcJobResult::Read(commandFailed, {});
    QVERIFY(!result.success);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.deleteCapLine.isEmpty());
    QCOMPARE(result.exitCode, 1);
    // The log now comes back a line at a time, so the card can leave out
    // the lines nobody reads.
    QCOMPARE(result.output,
             QStringList() << "NOTICE: f1.txt: Deleted"
                           << "ERROR : Local file system: not deleting files "
                              "as there were IO errors");
  }

  // core/command never gives the child's exit code, so the result reads it
  // from the log. Lines as rclone v1.76.0 logged them (2026-11-15).
  void infersTheChildExitCodeFromItsLog() {
    const QString transferLimit = Log({
        Line("info", "Starting bandwidth limiter at 50Ki Byte/s"),
        Line("notice",
             "Removing partially written file on error: max transfer limit "
             "reached as set by --max-transfer",
             "big.bin.344ab8b0.partial"),
        Line("error",
             "Failed to copy: max transfer limit reached as set by "
             "--max-transfer",
             "big.bin"),
        Line("error", "Cancelling sync due to fatal error: max transfer limit "
                      "reached as set by --max-transfer"),
        Line("error", "Fatal error received - not attempting retries"),
        Line("notice", "Failed to copy: max transfer limit reached as set by "
                       "--max-transfer"),
    });
    RcJobResult::Result result = RcJobResult::Read(Command(true, transferLimit), {});
    QCOMPARE(result.exitCode, 8);
    QVERIFY(result.success);
    QVERIFY(result.error.isEmpty());
    QVERIFY(result.deleteCapLine.isEmpty());

    const QString duration = Log({
        Line("error", "Failed to copy: context deadline exceeded", "big.bin"),
        Line("error", "max transfer duration reached as set by --max-duration",
             "Local file system at //?/C:/dest"),
        Line("error", "Fatal error received - not attempting retries"),
        Line("notice", "Failed to copy with 3 errors: last error was: max "
                       "transfer duration reached as set by --max-duration"),
    });
    result = RcJobResult::Read(Command(true, duration), {});
    QCOMPARE(result.exitCode, 10);
    QVERIFY(result.success);

    const QString nothing = Log({Line("info", "There was nothing to transfer")});
    result = RcJobResult::Read(Command(true, nothing), {});
    QCOMPARE(result.exitCode, 9);
    QVERIFY(result.success);
    QVERIFY(result.error.isEmpty());

    // A run with nothing to transfer that also hit an error failed on the
    // error, and without --error-on-no-transfer it isn't a stop at all.
    const QString nothingButBroken =
        Log({Line("error", "Failed to read directory: access denied", "docs"),
             Line("info", "There was nothing to transfer")});
    result = RcJobResult::Read(Command(true, nothingButBroken), {});
    QCOMPARE(result.exitCode, 1);
    QVERIFY(!result.success);
    QVERIFY(!result.error.isEmpty());
    result = RcJobResult::Read(Command(false, nothing), {});
    QCOMPARE(result.exitCode, 0);
    QVERIFY(result.success);

    // rclone logs a retry that came good at error level, after the first
    // attempt's errors (lines measured on rclone 1.76.0). Those errors were
    // retried away, so only what follows the last good attempt counts.
    const QString retried = Log({
        Line("error", "Failed to copy: The process cannot access the file",
             "locked.bin"),
        Line("error", "Attempt 1/3 failed with 1 errors and: The process "
                      "cannot access the file"),
        Line("error", "Attempt 2/3 succeeded"),
        Line("info", "There was nothing to transfer"),
    });
    result = RcJobResult::Read(Command(true, retried), {});
    QCOMPARE(result.exitCode, 9);
    QVERIFY(result.success);
    const QString retriedThenBroken =
        Log({Line("error", "Attempt 2/3 succeeded"),
             Line("error", "Failed to read directory: access denied", "docs"),
             Line("info", "There was nothing to transfer")});
    QCOMPARE(RcJobResult::ExitCodeFromLog(retriedThenBroken), 1);
    QVERIFY(RcJobResult::Read(Command(true, retried), {}).fromCommand);

    const QString capped = Log({
        Line("notice", "Deleted", "a.txt"),
        Line("error", "Couldn't delete: --max-delete threshold reached",
             "b.txt"),
        Line("notice", "Failed to sync: --max-delete threshold reached"),
    });
    result = RcJobResult::Read(Command(true, capped), {});
    QCOMPARE(result.exitCode, 7);
    QVERIFY(!result.success);
    QCOMPARE(result.deleteCapLine,
             QString("b.txt: Couldn't delete: --max-delete threshold reached"));
  }

  // resolveExitCode (cmd/cmd.go) takes a missing folder or file, then an
  // error worth a retry, ahead of a no-retry or a fatal one, and cmd.Run's
  // last line names the error the child exits on. Lines as rclone v1.76.0
  // logged them (2026-11-16).
  void readsTheChildsExitCodeFromTheErrorItEndedOn_data() {
    QTest::addColumn<QString>("log");
    QTest::addColumn<int>("exitCode");
    const QString refused =
        "failed to make fs for --backup-dir \":ftp,host=127.0.0.1,port=1:bk\": "
        "NewFs: failed to make FTP connection to \"127.0.0.1:1\": dial tcp "
        "127.0.0.1:1: connectex: No connection could be made because the "
        "target machine actively refused it.";
    const QString noSection =
        "failed to make fs for --backup-dir \"nosuch:bk\": didn't find "
        "section in config file (\"nosuch\")";
    const QString fatalLine = Line(
        "error", "Fatal error received - not attempting retries");
    const QString cantRetryLine = Line(
        "error", "Can't retry any of the errors - not attempting retries");
    QTest::newRow("missing folder")
        << Log({Line("error",
                     "error reading source root directory: directory not found",
                     "Local file system at //?/C:/src"),
                Line("error", "Attempt 1/3 failed with 1 errors and: "
                              "directory not found"),
                Line("error", "Attempt 3/3 failed with 1 errors and: "
                              "directory not found"),
                Line("notice", "Failed to copy: directory not found")})
        << 3;
    QTest::newRow("missing folder, several errors")
        << Log({Line("notice", "Failed to sync with 2 errors: last error "
                               "was: directory not found")})
        << 3;
    QTest::newRow("missing file")
        << Log({Line("notice", "Failed to copyto: object not found")}) << 4;
    QTest::newRow("fatal, refused")
        << Log({fatalLine, Line("notice", "Failed to sync: " + refused)}) << 5;
    QTest::newRow("fatal, no section")
        << Log({fatalLine, Line("notice", "Failed to sync: " + noSection)})
        << 7;
    QTest::newRow("no section, not fatal")
        << Log({Line("notice", "Failed to sync: " + noSection)}) << 1;
    QTest::newRow("can't retry")
        << Log({cantRetryLine, Line("notice", "Failed to copy: boom")}) << 6;
    QTest::newRow("immutable")
        << Log({Line("notice", "Failed to copy: immutable file modified")})
        << 6;
    QTest::newRow("bisync aborted")
        << Log({Line("notice", "Failed to bisync: bisync aborted")}) << 7;
    QTest::newRow("bisync aborted, several errors")
        << Log({Line("notice", "Failed to bisync with 2 errors: last error "
                               "was: bisync aborted")})
        << 7;
    // A file's own line starts with its name, and the error it names isn't
    // the one the run ended on.
    QTest::newRow("a file's line")
        << Log({Line("error", "Failed to copy: directory not found", "a.txt")})
        << 1;
    // A remote that can't be opened fails before cmd.Run, with its own line.
    QTest::newRow("can't open the destination")
        << Log({Line("critical", "Failed to create file system for "
                                 "destination \":ftp:x\": NewFs: failed to "
                                 "make FTP connection: connection refused")})
        << 1;
  }

  void readsTheChildsExitCodeFromTheErrorItEndedOn() {
    QFETCH(QString, log);
    QFETCH(int, exitCode);
    QCOMPARE(RcJobResult::ExitCodeFromLog(log), exitCode);
    const RcJobResult::Result result = RcJobResult::Read(Command(true, log), {});
    QCOMPARE(result.exitCode, exitCode);
    QVERIFY(!result.success);
  }

  // What resolveExitCode makes of an error from its text: the end of the
  // chain, where Go's "%s: %w" leaves the cause, for an error fs.ErrorDirNotFound,
  // fs.ErrorObjectNotFound or a root fserrors.ShouldRetry retries, and a
  // retriableErrorStrings phrase anywhere.
  void readsTheCauseOfAnError_data() {
    QTest::addColumn<QString>("error");
    QTest::addColumn<int>("cause");
    QTest::newRow("folder") << QString("directory not found") << 3;
    QTest::newRow("folder, wrapped")
        << QString("error reading source root directory: directory not found")
        << 3;
    QTest::newRow("file")
        << QString("failed to open source object: object not found") << 4;
    QTest::newRow("EOF") << QString("read: EOF") << 5;
    QTest::newRow("unexpected EOF") << QString("unexpected EOF") << 5;
    QTest::newRow("reset")
        << QString("read tcp 10.0.0.2:50122->10.0.0.9:22: read: connection "
                   "reset by peer")
        << 5;
    QTest::newRow("refused, Windows")
        << QString("dial tcp 127.0.0.1:1: connectex: No connection could be "
                   "made because the target machine actively refused it.")
        << 5;
    QTest::newRow("timeout")
        << QString("dial tcp 10.255.255.1:21: i/o timeout") << 5;
    QTest::newRow("deadline") << QString("context deadline exceeded") << 5;
    QTest::newRow("TLS handshake")
        << QString("Get \"https://example.com/\": net/http: TLS handshake "
                   "timeout")
        << 5;
    QTest::newRow("client timeout")
        << QString("Get \"https://example.com/\": context deadline exceeded "
                   "(Client.Timeout exceeded while awaiting headers)")
        << 5;
    QTest::newRow("GOAWAY")
        << QString("http2: server sent GOAWAY and closed the connection; "
                   "LastStreamID=1, ErrCode=NO_ERROR")
        << 5;
    // Only the end of the chain is its cause.
    QTest::newRow("folder, not the cause")
        << QString("directory not found: retrying with the parent") << -1;
    QTest::newRow("EOF, part of a word") << QString("read past XEOF") << -1;
    QTest::newRow("immutable") << QString("immutable file modified") << -1;
    QTest::newRow("plain") << QString("boom") << -1;
    QTest::newRow("empty") << QString() << -1;
  }

  void readsTheCauseOfAnError() {
    QFETCH(QString, error);
    QFETCH(int, cause);
    QCOMPARE(RcJobResult::CauseExitCode(error), cause);
  }

  // resolveExitCode's order once the cause has its say: no-retry, 6, ahead
  // of fatal, 7, else 1.
  void ordersTheExitCodesLikeTheCli_data() {
    QTest::addColumn<QString>("error");
    QTest::addColumn<bool>("fatal");
    QTest::addColumn<bool>("noRetry");
    QTest::addColumn<int>("exitCode");
    QTest::newRow("cause first")
        << QString("directory not found") << true << true << 3;
    QTest::newRow("worth a retry, though fatal")
        << QString("connection refused") << true << false << 5;
    QTest::newRow("no-retry deadline")
        << QString("context deadline exceeded") << false << true << 5;
    QTest::newRow("no-retry before fatal")
        << QString("boom") << true << true << 6;
    QTest::newRow("immutable")
        << QString("immutable file modified") << false << false << 6;
    QTest::newRow("fatal") << QString("boom") << true << false << 7;
    QTest::newRow("plain") << QString("boom") << false << false << 1;
  }

  void ordersTheExitCodesLikeTheCli() {
    QFETCH(QString, error);
    QFETCH(bool, fatal);
    QFETCH(bool, noRetry);
    QFETCH(int, exitCode);
    QCOMPARE(RcJobResult::ExitCodeForError(error, fatal, noRetry), exitCode);
  }

  // The log of a long run is mostly stats every second and a line per file.
  // The card shows rclone's notices and errors, the log panel only the
  // errors and warnings, and neither gets thousands of lines in one go.
  void keepsTheCardToTheLinesThatMatter() {
    QStringList lines;
    lines << Stats(1) << Line("info", "Copied (new)", "a.txt")
          << Line("notice", "Deleted", "old.txt")
          << Line("warning", "Can't follow symlink", "link")
          << Line("error", "Failed to copy: access denied", "b.txt") << Stats(2)
          << "panic: plain text from a crash";
    RcJobResult::Result result =
        RcJobResult::Read(Command(false, Log(lines)), {});
    QVERIFY(result.success);
    QCOMPARE(result.output,
             QStringList() << "old.txt: Deleted" << "link: Can't follow symlink"
                           << "b.txt: Failed to copy: access denied"
                           << "panic: plain text from a crash");
    QCOMPARE(result.problems,
             QStringList() << "link: Can't follow symlink"
                           << "b.txt: Failed to copy: access denied"
                           << "panic: plain text from a crash");

    // Past the card's own line limit, the log is cut to its last lines with
    // a note, rather than the pane dropping lines silently.
    const qsizetype total = RcJobResult::kMaxShownLines + 50;
    lines.clear();
    for (qsizetype i = 0; i < total; i++) {
      lines << Line("notice", "Deleted", QString("f%1.txt").arg(i)) << Stats(1);
    }
    result = RcJobResult::Read(Command(false, Log(lines)), {});
    QCOMPARE(result.output.size(), RcJobResult::kMaxShownLines + 1);
    QCOMPARE(result.output.first(), QString("(50 earlier lines left out)"));
    QCOMPARE(result.output.at(1), QString("f50.txt: Deleted"));
    QCOMPARE(result.output.last(), QString("f%1.txt: Deleted").arg(total - 1));
    QVERIFY(result.problems.isEmpty());

    // The log panel gets at most its own 500 lines of problems.
    const qsizetype errors = RcJobResult::kMaxProblems + 20;
    lines.clear();
    for (qsizetype i = 0; i < errors; i++) {
      lines << Line("error", "Failed to copy: denied", QString("e%1").arg(i));
    }
    result = RcJobResult::Read(Command(false, Log(lines)), {});
    QCOMPARE(result.problems.size(), RcJobResult::kMaxProblems + 1);
    QCOMPARE(result.problems.first(), QString("(20 earlier problems left out)"));
    QCOMPARE(result.problems.last(),
             QString("e%1: Failed to copy: denied").arg(errors - 1));
    QCOMPARE(result.output.size(), errors);
  }

private:
  // Starts args on the daemon and waits for job/status to say finished.
  // Empty when it never started or never finished.
  QJsonObject RunToEnd(RcloneRcEngine &engine, const QStringList &args,
                       RcloneRcEngine::StartedJob *job) {
    bool called = false;
    engine.runCommand(args, this,
                      [&](const RcloneRcEngine::StartedJob &started) {
                        *job = started;
                        called = true;
                      });
    if (!QTest::qWaitFor([&]() { return called; }, 60000) || job->jobId < 0) {
      qWarning() << "never started:" << job->error;
      return QJsonObject();
    }
    auto status = std::make_shared<QJsonObject>();
    auto asking = std::make_shared<bool>(false);
    const bool finished = QTest::qWaitFor(
        [&]() {
          if (!*asking) {
            *asking = true;
            engine.jobStatus(job->jobId, this,
                             [status, asking](const QJsonObject &s,
                                              const QString &error) {
                               *asking = false;
                               if (error.isEmpty() &&
                                   s.value("finished").toBool()) {
                                 *status = s;
                               }
                             });
          }
          return !status->isEmpty();
        },
        60000);
    if (!finished) {
      qWarning() << "job" << job->jobId << "never finished";
    }
    return *status;
  }

  static QString Line(const QString &level, const QString &msg,
                      const QString &object = QString()) {
    QJsonObject line{{"time", "2026-11-15T19:10:39.2225348-05:00"},
                     {"level", level},
                     {"msg", msg},
                     {"source", "sync/sync.go:313"}};
    if (!object.isEmpty()) {
      line.insert("object", object);
      line.insert("objectType", "*local.Object");
    }
    return QString::fromUtf8(QJsonDocument(line).toJson(QJsonDocument::Compact));
  }

  static QString Stats(int bytes) {
    const QJsonObject line{
        {"level", "info"},
        {"msg", "\nTransferred:   \t  0 B / 0 B, -, 0 B/s, ETA -\n"},
        {"stats", QJsonObject{{"bytes", bytes}, {"transfers", 0}}}};
    return QString::fromUtf8(QJsonDocument(line).toJson(QJsonDocument::Compact));
  }

  static QString Log(const QStringList &lines) {
    return lines.join('\n') + '\n';
  }

  static QJsonObject Command(bool error, const QString &log) {
    return QJsonObject{{"finished", true},
                       {"success", true},
                       {"error", ""},
                       {"output", QJsonObject{{"error", error}, {"result", log}}}};
  }
};

QTEST_MAIN(RcRouteTest)
#include "rc_route_test.moc"
