#include "rclone_rc_engine.h"
#include "test_rclone.h"
#include "utils.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

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

    bool finished = false;
    QTest::qWaitFor(
        [&]() {
          engine.jobStatus(result.jobId, this,
                           [&finished](const QJsonObject &status,
                                       const QString &error) {
                             if (!error.isEmpty() ||
                                 status.value("finished").toBool()) {
                               finished = true;
                             }
                           });
          return finished;
        },
        60000);
    return result.group;
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
};

QTEST_MAIN(RcRouteTest)
#include "rc_route_test.moc"
