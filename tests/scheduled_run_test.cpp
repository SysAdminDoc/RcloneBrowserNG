// Runs the real app the way a system scheduler does (--run-task) against a
// portable copy, so it reads its own ini and tasks.json and nothing on the
// host.

#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <functional>
#include <memory>

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

  // Stands in for a monitoring service: answers every request with 200 and
  // keeps the request line and body.
  struct HookRequest {
    QByteArray method;
    QByteArray path;
    QByteArray body;
  };
  QTcpServer mHooks;
  QList<HookRequest> mHookRequests;

  void serveHooks() {
    QVERIFY(mHooks.listen(QHostAddress::LocalHost));
    connect(&mHooks, &QTcpServer::newConnection, this, [this]() {
      while (QTcpSocket *socket = mHooks.nextPendingConnection()) {
        auto buffer = std::make_shared<QByteArray>();
        auto read = std::make_shared<std::function<void()>>();
        *read = [this, socket, buffer]() {
          *buffer += socket->readAll();
          const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
          if (headerEnd < 0) {
            return;
          }
          const QByteArray head = buffer->left(headerEnd);
          qsizetype length = 0;
          for (const QByteArray &line : head.split('\n')) {
            if (line.toLower().startsWith("content-length:")) {
              length = line.mid(15).trimmed().toLongLong();
            }
          }
          if (buffer->size() < headerEnd + 4 + length) {
            return;
          }
          const QList<QByteArray> requestLine =
              head.left(head.indexOf('\r')).split(' ');
          mHookRequests.append({requestLine.value(0), requestLine.value(1),
                                buffer->mid(headerEnd + 4, length)});
          buffer->clear();
          socket->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n"
                        "Connection: close\r\n\r\n");
          socket->disconnectFromHost();
        };
        connect(socket, &QTcpSocket::readyRead, socket,
                [read]() { (*read)(); });
        connect(socket, &QTcpSocket::disconnected, socket,
                &QObject::deleteLater);
        (*read)();
      }
    });
  }

  QString hookUrl(const QString &path) const {
    return QString("http://127.0.0.1:%1%2").arg(mHooks.serverPort()).arg(path);
  }

  QJsonArray historyEntries() const {
    QFile file(mDataDir + "/job-history.json");
    return file.open(QIODevice::ReadOnly)
               ? QJsonDocument::fromJson(file.readAll()).array()
               : QJsonArray();
  }

  // Keeps this test's event loop turning while the run waits on the hooks,
  // which runTask() can't, since it blocks.
  int runTaskServingHooks(const QString &name) {
    QProcess proc;
    proc.setProcessEnvironment(mEnv);
    proc.start(mApp, {"--run-task", name});
    if (!QTest::qWaitFor(
            [&proc]() { return proc.state() == QProcess::NotRunning; },
            120000)) {
      proc.kill();
      proc.waitForFinished();
      return -1;
    }
    return proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : -2;
  }

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
    serveHooks();
    const QJsonObject task{{"description", "Nightly Copy"},
                           {"jobType", 2},
                           {"operation", 1},
                           {"source", src()},
                           {"dest", dst()},
                           {"isFolder", true},
                           {"uniqueId",
                            "{6f1c2a3e-1111-4222-8333-944455556666}"}};
    // A Sync whose source came back empty, with a cap of 2 and both hooks.
    QVERIFY(QDir().mkpath(mDir.filePath("guarded-src")));
    const QJsonObject guarded{
        {"description", "Guarded Sync"},
        {"jobType", 1},
        {"operation", 3},
        {"source", mDir.filePath("guarded-src")},
        {"dest", mDir.filePath("guarded-dst")},
        {"isFolder", true},
        {"maxDelete", 2},
        {"webhookUrl", hookUrl("/hook")},
        {"heartbeatUrl", hookUrl("/ping")},
        {"uniqueId", "{6f1c2a3e-2222-4222-8333-944455556666}"}};
    writeFile(mDataDir + "/tasks.json",
              QJsonDocument(QJsonObject{{"version", 1},
                                        {"tasks", QJsonArray{task, guarded}}})
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

  // Nobody watches a scheduled run, so a sync stopped by its delete cap has
  // to say so where someone will see it: the job history, the webhook and
  // the heartbeat's /fail.
  void aDeleteCapStopIsRecordedAndReported() {
    const QString dest = mDir.filePath("guarded-dst");
    for (int i = 1; i <= 5; ++i) {
      writeFile(dest + QString("/f%1.txt").arg(i), "keep me");
    }
    mHookRequests.clear();

    QCOMPARE(runTaskServingHooks("Guarded Sync"), 7);
    QCOMPARE(QDir(dest).entryList(QDir::Files).size(), 3);
    QVERIFY2(logText().contains("The scheduled run of \"Guarded Sync\" "
                                "finished, rclone exit code 7. Stopped by "
                                "delete cap."),
             qPrintable(logText()));

    QJsonObject entry;
    for (const QJsonValue &value : historyEntries()) {
      if (value.toObject().value("name").toString() == "Guarded Sync") {
        entry = value.toObject();
      }
    }
    QVERIFY2(!entry.isEmpty(), "no history entry for the scheduled run");
    QCOMPARE(entry.value("statusLabel").toString(),
             QString("Stopped by delete cap"));
    QCOMPARE(entry.value("exitCode").toInt(), 7);
    QVERIFY(!entry.value("success").toBool());

    // The run waits for both answers before it exits.
    QCOMPARE(mHookRequests.size(), 2);
    HookRequest webhook;
    HookRequest heartbeat;
    for (const HookRequest &request : mHookRequests) {
      (request.method == "POST" ? webhook : heartbeat) = request;
    }
    QCOMPARE(webhook.path, QByteArray("/hook"));
    const QJsonObject body = QJsonDocument::fromJson(webhook.body).object();
    QCOMPARE(body.value("task").toString(), QString("Guarded Sync"));
    QCOMPARE(body.value("status").toString(), QString("failed"));
    QCOMPARE(body.value("error").toString(), QString("Stopped by delete cap"));
    QCOMPARE(heartbeat.method, QByteArray("GET"));
    QCOMPARE(heartbeat.path, QByteArray("/ping/fail"));

    // History shows the options that ran, as it does for a run in the app,
    // without the config arguments put in front of them.
    QStringList args;
    for (const QJsonValue &arg : entry.value("args").toArray()) {
      args << arg.toString();
    }
    QVERIFY2(args.contains("--max-delete"), qPrintable(args.join(' ')));
    QVERIFY2(!args.contains("--config"), qPrintable(args.join(' ')));
  }

  // An rclone that moved away after an update is exactly when nobody is
  // looking, so a run that never starts gets a history row and both hooks.
  void aRunThatNeverStartsIsRecordedAndReported() {
    writeSettings(mDir.filePath("missing/rclone.exe"));
    mHookRequests.clear();
    const qsizetype rowsBefore = historyEntries().size();

    QCOMPARE(runTaskServingHooks("Guarded Sync"), 1);

    const QJsonArray rows = historyEntries();
    QCOMPARE(rows.size(), rowsBefore + 1);
    const QJsonObject entry = rows.last().toObject();
    QCOMPARE(entry.value("name").toString(), QString("Guarded Sync"));
    QCOMPARE(entry.value("statusLabel").toString(), QString("Didn't start"));
    QVERIFY(!entry.value("success").toBool());
    QVERIFY2(entry.value("transferDetail").toArray().first().toString().contains(
                 "didn't start"),
             qPrintable(QJsonDocument(entry).toJson()));

    QCOMPARE(mHookRequests.size(), 2);
    HookRequest webhook;
    HookRequest heartbeat;
    for (const HookRequest &request : mHookRequests) {
      (request.method == "POST" ? webhook : heartbeat) = request;
    }
    const QJsonObject body = QJsonDocument::fromJson(webhook.body).object();
    QCOMPARE(body.value("status").toString(), QString("failed"));
    QCOMPARE(body.value("error").toString(), QString("Didn't start"));
    QCOMPARE(heartbeat.path, QByteArray("/ping/fail"));
  }
};

QTEST_GUILESS_MAIN(ScheduledRunTest)
#include "scheduled_run_test.moc"
