// Runs the real app the way a system scheduler does (--run-task) against a
// portable copy, so it reads its own ini and tasks.json and nothing on the
// host.

#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#ifndef RCLONE_BROWSER_APP
#error "RCLONE_BROWSER_APP must point at the built app"
#endif

class ScheduledRunTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mDir;
  QString mRclone;
  QString mApp;
  QString mDataDir;
  QString mIni;
  QProcessEnvironment mEnv = QProcessEnvironment::systemEnvironment();

  static void writeFile(const QString &path, const QByteArray &content) {
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
  }

  QString src() const { return mDir.filePath("src"); }
  QString dst() const { return mDir.filePath("dst"); }
  QString logText() const {
    QFile file(mDataDir + "/logs/rclonebrowser.log");
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll())
                                          : QString();
  }

  // An ini value reads a backslash as an escape, so paths go in with
  // forward slashes, the way QSettings itself writes them.
  void writeSettings(const QString &rclone) {
    writeFile(mIni, QString("[Settings]\nrclone=%1\nrcloneConf=%2\n")
                        .arg(QDir::fromNativeSeparators(rclone),
                             mDataDir + "/rclone.conf")
                        .toUtf8());
  }

  int runTask(const QString &name) {
    QProcess proc;
    proc.setProcessEnvironment(mEnv);
    proc.start(mApp, {"--run-task", name});
    if (!proc.waitForFinished(120000)) {
      proc.kill();
      proc.waitForFinished();
      return -1;
    }
    return proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -2;
  }

private slots:
  void initTestCase() {
    mRclone = qEnvironmentVariable("RCLONE_BROWSER_TEST_RCLONE");
    if (mRclone.isEmpty()) {
      mRclone = QStandardPaths::findExecutable("rclone");
    }
    if (mRclone.isEmpty()) {
      QSKIP("no rclone; set RCLONE_BROWSER_TEST_RCLONE");
    }
    QVERIFY(mDir.isValid());
    mEnv.insert("QT_QPA_PLATFORM", "offscreen");
#if defined(Q_OS_MACOS)
    QSKIP("the portable ini sits next to the .app bundle on macOS");
#elif defined(Q_OS_WIN)
    // Portable mode on Windows is an ini next to the executable, so the test
    // runs a copy instead of turning the build folder portable.
    mApp = mDir.filePath("app/RcloneBrowser.exe");
    QVERIFY(QDir().mkpath(mDir.filePath("app")));
    QVERIFY(QFile::copy(RCLONE_BROWSER_APP, mApp));
    mDataDir = mDir.filePath("app");
    mIni = mDir.filePath("app/RcloneBrowser.ini");
#else
    // An AppImage turns portable when its config folder sits next to it.
    mApp = RCLONE_BROWSER_APP;
    const QString appImage = mDir.filePath("RcloneBrowser.AppImage");
    mEnv.insert("APPIMAGE", appImage);
    mEnv.insert("XDG_CONFIG_HOME", appImage + ".config");
    mDataDir = appImage + ".config/rclone-browser";
    mIni = mDataDir + "/rclone-browser.ini";
#endif
    writeFile(mDataDir + "/rclone.conf", QByteArray());
    writeFile(src() + "/a.txt", "first");
    writeFile(src() + "/sub/b.txt", "second");
    const QJsonObject task{{"description", "Nightly Copy"},
                           {"jobType", 2},
                           {"operation", 1},
                           {"source", src()},
                           {"dest", dst()},
                           {"isFolder", true},
                           {"uniqueId",
                            "{6f1c2a3e-1111-4222-8333-944455556666}"}};
    writeFile(mDataDir + "/tasks.json",
              QJsonDocument(QJsonObject{{"version", 1},
                                        {"tasks", QJsonArray{task}}})
                  .toJson());
  }

  void init() {
    QDir(dst()).removeRecursively();
    writeSettings(mRclone);
  }

  void runsTheTaskWithTheSavedRclone() {
    const int exitCode = runTask("Nightly Copy");
    QVERIFY2(exitCode == 0, qPrintable(logText()));
    QVERIFY(QFileInfo::exists(dst() + "/a.txt"));
    QVERIFY(QFileInfo::exists(dst() + "/sub/b.txt"));
    QVERIFY(logText().contains(
        "The scheduled run of \"Nightly Copy\" finished, rclone exit code 0."));
  }

  void runsWhileTheAppIsOpen() {
    const qsizetype startsBefore = logText().count(" started");
    QProcess gui;
    gui.setProcessEnvironment(mEnv);
    gui.start(mApp, QStringList());
    QVERIFY(gui.waitForStarted(30000));
    // The open copy owns the single-instance handshake once it has started
    // and logged.
    QTRY_VERIFY_WITH_TIMEOUT(logText().count(" started") > startsBefore,
                             30000);
    QTest::qWait(1500);

    const int exitCode = runTask("Nightly Copy");
    const bool guiStillUp = gui.state() == QProcess::Running;
    gui.kill();
    gui.waitForFinished(10000);

    QVERIFY2(exitCode == 0, qPrintable(logText()));
    QVERIFY(QFileInfo::exists(dst() + "/a.txt"));
    QVERIFY(QFileInfo::exists(dst() + "/sub/b.txt"));
    QVERIFY(guiStillUp);
  }

  void reportsARcloneThatWontStart() {
    writeSettings(mDir.filePath("missing/rclone.exe"));
    QCOMPARE(runTask("Nightly Copy"), 1);
    QVERIFY(!QFileInfo::exists(dst() + "/a.txt"));
    QVERIFY(logText().contains("The scheduled run of \"Nightly Copy\" didn't "
                               "start"));
  }

  void reportsAMissingTask() {
    QCOMPARE(runTask("No Such Task"), 1);
  }
};

QTEST_GUILESS_MAIN(ScheduledRunTest)
#include "scheduled_run_test.moc"
