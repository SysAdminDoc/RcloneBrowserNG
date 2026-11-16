#include "dry_run_check.h"
#include "fake_rc_server.h"
#include "rc_sync_request.h"
#include "rclone_rc_engine.h"
#include "remote_daemon.h"
#include "test_rclone.h"
#include "utils.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#if defined(Q_OS_WIN32)
#include <windows.h>
#endif

namespace {

// A self-signed certificate for CN=nas.local and its key, made for this test
// alone. The key protects nothing.
const char kTestCertificate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBlTCCATugAwIBAgIUK14koTilwcIo2x2hhXBC+4ugFdIwCgYIKoZIzj0EAwIw\n"
    "FDESMBAGA1UEAwwJbmFzLmxvY2FsMCAXDTI2MTExNTE5MjMzMFoYDzIxMjYxMDIy\n"
    "MTkyMzMwWjAUMRIwEAYDVQQDDAluYXMubG9jYWwwWTATBgcqhkjOPQIBBggqhkjO\n"
    "PQMBBwNCAAQ/KYintzCvK23Nqx0Ot305CzU2fR5d8n/bUdyTn4wAz5rur3RHOWio\n"
    "ENTWpU/ej9ReDt0N3lobhW1iiHOCmQzMo2kwZzAdBgNVHQ4EFgQUYowa4RHJKmML\n"
    "w2RwMskT560fe4wwHwYDVR0jBBgwFoAUYowa4RHJKmMLw2RwMskT560fe4wwDwYD\n"
    "VR0TAQH/BAUwAwEB/zAUBgNVHREEDTALggluYXMubG9jYWwwCgYIKoZIzj0EAwID\n"
    "SAAwRQIgIs0bYHjXUuhk8edhL26+g5ezlrtPaJAHZBi7Wpr2wbcCIQCxoWY4nxDj\n"
    "cd01mgnxfYQVuWEneFsQnMFwluBAMA/Djg==\n"
    "-----END CERTIFICATE-----\n";
const char kTestKey[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgzxGGp39io8PeV0g3\n"
    "le5gxdfqF5L9S7nNcQ1o5o47iumhRANCAAQ/KYintzCvK23Nqx0Ot305CzU2fR5d\n"
    "8n/bUdyTn4wAz5rur3RHOWioENTWpU/ej9ReDt0N3lobhW1iiHOCmQzM\n"
    "-----END PRIVATE KEY-----\n";
// What openssl x509 -noout -fingerprint -sha256 prints for it.
const char kTestFingerprint[] =
    "91:A5:C5:76:74:45:13:68:A4:01:16:17:CD:96:58:57:"
    "2F:D3:5F:5D:01:CB:95:88:B0:34:11:B2:3D:E5:43:2C";
// Another certificate's.
const char kOtherFingerprint[] =
    "88:E7:7A:CB:F9:6D:AA:AF:D2:FF:7B:27:9D:A5:D6:13:"
    "82:D3:9A:DA:3C:19:90:E0:C0:1E:53:F4:3E:69:03:CB";

} // namespace

// The remote rclone mode drives an rclone rcd on another machine. Here that
// machine is a second rcd on 127.0.0.1 with its own config file, a login, and
// an alias remote the app's own (bogus) config doesn't have. Anything that
// leaks the local config, skips the login or builds the wrong fs string fails
// on the wire, which is where the request builders alone can't be checked.
class RemoteDaemonTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mDir;
  QString mRclone;
  QString mServed;
  QProcess *mDaemon = nullptr;
  QProcess *mTlsDaemon = nullptr;
  RemoteDaemon::Settings mRemote;

  static quint16 freePort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
      return 0;
    }
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
  }

  QProcess *startRcd(const QStringList &args) {
    auto *daemon = new QProcess(this);
    daemon->setProcessChannelMode(QProcess::MergedChannels);
#if defined(Q_OS_WIN32)
    daemon->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
          args->flags |= CREATE_NO_WINDOW;
        });
#endif
    daemon->start(mRclone, QStringList{"rcd"} + args);
    return daemon;
  }

  QString writeText(const QString &name, const char *text) {
    const QString path = QDir(mDir.path()).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
      return QString();
    }
    file.write(text);
    return path;
  }

  // Starts the engine on settings and hands back why it couldn't, if it
  // couldn't.
  QString startEngine(RcloneRcEngine &engine,
                      const RemoteDaemon::Settings &settings) {
    engine.useRemoteDaemon(settings.url, settings.user, settings.password,
                           settings.certificateSha256);
    QString error;
    bool done = false;
    engine.ensureStartedAsync(this, [&](bool ok, const QString &startError) {
      error = ok ? QString() : startError;
      done = true;
    });
    if (!QTest::qWaitFor([&]() { return done; }, 40000)) {
      return QStringLiteral("no answer");
    }
    return error;
  }

  static QJsonObject callAndWait(const RemoteDaemon::Settings &settings,
                                 const QString &endpoint,
                                 const QJsonObject &payload, QString *error,
                                 QObject *context) {
    QJsonObject answer;
    bool done = false;
    RemoteDaemon::Call(
        settings, endpoint, payload, context,
        [&](const QJsonObject &result, const QString &callError) {
          answer = result;
          *error = callError;
          done = true;
        },
        30000);
    if (!QTest::qWaitFor([&]() { return done; }, 40000)) {
      *error = QStringLiteral("no answer");
    }
    return answer;
  }

  QJsonObject call(const QString &endpoint, const QJsonObject &payload,
                   QString *error) {
    return callAndWait(mRemote, endpoint, payload, error, this);
  }

  QStringList listNames(bool showHidden) {
    QString error;
    const QJsonObject listed = call(
        "operations/list",
        RemoteDaemon::ListPayload("rbtestnas:", showHidden, false, false),
        &error);
    if (!error.isEmpty()) {
      qWarning("operations/list failed: %s", qPrintable(error));
    }
    QStringList names;
    for (const QJsonValue &entry : listed.value("list").toArray()) {
      names << entry.toObject().value("Name").toString();
    }
    names.sort();
    return names;
  }

  void writeFile(const QString &relative, const QByteArray &data = "x") {
    const QString path = QDir(mServed).filePath(relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
  }

  bool served(const QString &relative) const {
    return QFileInfo::exists(QDir(mServed).filePath(relative));
  }

  void runEngineJob(RcloneRcEngine &engine, const QStringList &args) {
    RcloneRcEngine::StartedJob started;
    bool called = false;
    engine.runCommand(args, this,
                      [&](const RcloneRcEngine::StartedJob &job) {
                        started = job;
                        called = true;
                      });
    QVERIFY(QTest::qWaitFor([&]() { return called; }, 60000));
    QVERIFY2(started.jobId >= 0, qPrintable(started.error));

    // One status request at a time, and state the callback owns a share of.
    // A request per poll left answers arriving after this returned, writing
    // into a stack frame that was gone.
    auto finished = std::make_shared<QJsonObject>();
    auto asking = std::make_shared<bool>(false);
    QVERIFY(QTest::qWaitFor(
        [&]() {
          if (!*asking) {
            *asking = true;
            engine.jobStatus(started.jobId, this,
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
        60000));
    QVERIFY2(finished->value("success").toBool(),
             qPrintable(finished->value("error").toString()));
  }

  // What a daemon whose config file is config answers during a start, and
  // to the core/command a transfer posts. options/info lists one option, so
  // the start goes on to options/get, and rc/list has no sync/* endpoint, so
  // every transfer takes core/command.
  static QJsonObject startAnswer(const QString &path, const QString &config) {
    if (path == "config/paths") {
      return QJsonObject{{"config", config}};
    }
    if (path == "options/info") {
      return QJsonObject{
          {"main", QJsonArray{QJsonObject{{"Name", "max_delete"},
                                          {"FieldName", "MaxDelete"},
                                          {"Type", "int"},
                                          {"NoPrefix", true}}}}};
    }
    if (path == "rc/list") {
      return QJsonObject{
          {"commands", QJsonArray{QJsonObject{{"Path", "core/command"}}}}};
    }
    if (path == "core/command") {
      return QJsonObject{{"jobid", 7}};
    }
    return QJsonObject();
  }

private slots:
  // The daemon can be switched while a start still waits on the old one.
  // Its late answer says nothing about the new daemon, so the start has to
  // begin again there, and the waiters hear back once the new one answered.
  // Taking the old answer told them a daemon nobody had reached was up, and
  // an old config/paths answer sent core/command runs to the old config.
  void aDaemonSwitchedMidStartIsTheOneReached_data() {
    QTest::addColumn<QString>("held");
    QTest::newRow("ping") << QString("rc/noopauth");
    QTest::newRow("config path") << QString("config/paths");
    QTest::newRow("command list") << QString("rc/list");
    QTest::newRow("option list") << QString("options/info");
    QTest::newRow("option values") << QString("options/get");
  }

  // A start that ended calls its waiters on the next turn of the event
  // loop. A switch on that turn, here by the first waiter, left the rest
  // to hear the old daemon's answer, though the current one was never
  // asked anything.
  void aSwitchBeforeTheWaitersHearSendsTheRestToTheNewDaemon() {
    FakeRcServer before([](const QString &path, const QJsonObject &,
                           const FakeRcServer::Reply &reply) {
      reply(startAnswer(path, "/before.conf"));
    });
    FakeRcServer after([](const QString &path, const QJsonObject &,
                          const FakeRcServer::Reply &reply) {
      reply(startAnswer(path, "/after.conf"));
    });
    QVERIFY(before.isListening());
    QVERIFY(after.isListening());

    RcloneRcEngine engine;
    engine.useRemoteDaemon(before.url(), "user", "pass");
    int firstCalls = 0;
    int secondCalls = 0;
    bool secondOk = false;
    QStringList afterByThen;
    engine.ensureStartedAsync(this, [&](bool, const QString &) {
      firstCalls++;
      engine.useRemoteDaemon(after.url(), "user", "pass");
    });
    engine.ensureStartedAsync(this, [&](bool startOk, const QString &) {
      secondCalls++;
      secondOk = startOk;
      afterByThen = after.paths();
    });
    QVERIFY(QTest::qWaitFor([&]() { return secondCalls > 0; }, 10000));
    QTest::qWait(200);
    QCOMPARE(firstCalls, 1);
    QCOMPARE(secondCalls, 1);
    QVERIFY(secondOk);
    QVERIFY2(afterByThen.contains("config/paths"),
             qPrintable(afterByThen.join(", ")));
  }

  // A null context means the reply isn't tied to any object. Qt refuses a
  // connection whose receiver is null, so the answer was never delivered.
  void aNullContextStillGetsItsAnswer() {
    FakeRcServer daemon([](const QString &path, const QJsonObject &,
                           const FakeRcServer::Reply &reply) {
      if (path == "job/status") {
        reply(QJsonObject{{"id", 7}, {"finished", true}, {"success", true},
                          {"error", ""}});
        return;
      }
      reply(startAnswer(path, "/null.conf"));
    });
    QVERIFY(daemon.isListening());

    RcloneRcEngine engine;
    engine.useRemoteDaemon(daemon.url(), "user", "pass");
    bool started = false;
    engine.ensureStartedAsync(
        this, [&](bool startOk, const QString &) { started = startOk; });
    QVERIFY(QTest::qWaitFor([&]() { return started; }, 10000));

    bool answered = false;
    QString error;
    engine.jobStatus(7, nullptr,
                     [&](const QJsonObject &, const QString &callError) {
                       answered = true;
                       error = callError;
                     });
    QVERIFY2(QTest::qWaitFor([&]() { return answered; }, 5000),
             "job/status with no context never answered");
    QVERIFY2(error.isEmpty(), qPrintable(error));
  }

  void aDaemonSwitchedMidStartIsTheOneReached() {
    QFETCH(QString, held);
    FakeRcServer::Reply release;
    FakeRcServer before([&](const QString &path, const QJsonObject &,
                            const FakeRcServer::Reply &reply) {
      if (path == held && !release) {
        release = reply;
        return;
      }
      reply(startAnswer(path, "/before.conf"));
    });
    FakeRcServer after([](const QString &path, const QJsonObject &,
                          const FakeRcServer::Reply &reply) {
      reply(startAnswer(path, "/after.conf"));
    });
    QVERIFY(before.isListening());
    QVERIFY(after.isListening());

    RcloneRcEngine engine;
    engine.useRemoteDaemon(before.url(), "user", "pass");
    bool done = false;
    bool ok = false;
    QStringList reachedByThen;
    engine.ensureStartedAsync(this, [&](bool startOk, const QString &) {
      ok = startOk;
      done = true;
      reachedByThen = after.paths();
    });
    QVERIFY2(QTest::qWaitFor([&]() { return bool(release); }, 10000),
             qPrintable(before.paths().join(", ")));

    engine.useRemoteDaemon(after.url(), "user", "pass");
    const int askedBefore = before.paths().size();
    release(startAnswer(held, "/before.conf"));
    QVERIFY(QTest::qWaitFor([&]() { return done; }, 10000));
    QVERIFY(ok);
    for (const QString &path :
         {"rc/noopauth", "config/paths", "rc/list", "options/info",
          "options/get"}) {
      QVERIFY2(reachedByThen.contains(path),
               qPrintable(path + " missing from: " + reachedByThen.join(", ")));
    }
    QCOMPARE(before.paths().size(), askedBefore);

    // A transfer now carries the config of the daemon it runs on.
    RcloneRcEngine::StartedJob job;
    bool started = false;
    engine.runCommand({"copy", "nas:a", "nas:b"}, this,
                      [&](const RcloneRcEngine::StartedJob &result) {
                        job = result;
                        started = true;
                      });
    QVERIFY(QTest::qWaitFor([&]() { return started; }, 10000));
    QVERIFY2(job.jobId == 7, qPrintable(job.error));
    const QString command = QString::fromUtf8(
        QJsonDocument(after.lastBody("core/command")).toJson());
    QVERIFY2(command.contains("/after.conf"), qPrintable(command));
    QVERIFY2(!command.contains("/before.conf"), qPrintable(command));
  }

  // rclone keeps a stats group for as long as the daemon runs, so a second
  // copy of the app on the same daemon, or the next session on one that
  // outlived the first, must not take the names of the groups already
  // there: it would read their errors and deletes as its own. Each job also
  // carries what the daemon said its own settings are, for the caps a
  // retry's _config leaves out, and a switch drops the old daemon's.
  void eachEngineNamesGroupsOfItsOwn() {
    const auto daemon = [](const QJsonObject &main) {
      return [main](const QString &path, const QJsonObject &,
                    const FakeRcServer::Reply &reply) {
        if (path == "rc/list") {
          reply(QJsonObject{
              {"commands", QJsonArray{QJsonObject{{"Path", "sync/copy"}},
                                      QJsonObject{{"Path", "core/command"}}}}});
        } else if (path == "options/get") {
          reply(QJsonObject{{"main", main}});
        } else if (path == "sync/copy") {
          reply(QJsonObject{{"jobid", 7}});
        } else {
          reply(startAnswer(path, "/nas.conf"));
        }
      };
    };
    FakeRcServer nas(daemon(QJsonObject{{"MaxDelete", 4}}));
    FakeRcServer other(daemon(QJsonObject{{"MaxDelete", 9}}));
    QVERIFY(nas.isListening());
    QVERIFY(other.isListening());

    const auto start = [this](RcloneRcEngine &engine) {
      auto job = std::make_shared<RcloneRcEngine::StartedJob>();
      auto started = std::make_shared<bool>(false);
      engine.runCommand({"copy", "nas:", "backup:"}, this,
                        [job, started](const RcloneRcEngine::StartedJob &result) {
                          *job = result;
                          *started = true;
                        });
      if (!QTest::qWaitFor([started]() { return *started; }, 10000)) {
        qWarning("the transfer never started");
      }
      return *job;
    };
    RcloneRcEngine first;
    RcloneRcEngine second;
    first.useRemoteDaemon(nas.url(), "user", "pass");
    second.useRemoteDaemon(nas.url(), "user", "pass");
    const RcloneRcEngine::StartedJob a = start(first);
    const RcloneRcEngine::StartedJob b = start(second);
    QVERIFY2(a.jobId == 7, qPrintable(a.error));
    QVERIFY2(b.jobId == 7, qPrintable(b.error));
    QCOMPARE(a.endpoint, QString("sync/copy"));
    QVERIFY2(a.group.startsWith("rclonebrowser-"), qPrintable(a.group));
    QVERIFY2(a.group != b.group, qPrintable(a.group + " and " + b.group));
    QCOMPARE(a.payload.value("_group").toString(), a.group);
    QCOMPARE(a.daemonMain.value("MaxDelete").toInt(), 4);

    first.useRemoteDaemon(other.url(), "user", "pass");
    const RcloneRcEngine::StartedJob c = start(first);
    QVERIFY2(c.jobId == 7, qPrintable(c.error));
    QCOMPARE(c.daemonMain.value("MaxDelete").toInt(), 9);
    QVERIFY2(c.group != a.group, qPrintable(c.group));
  }

  void initTestCase() {
    QVERIFY(mDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mDir.path());
    QCoreApplication::setOrganizationName("rclone-browser-remote-daemon-test");
    QCoreApplication::setApplicationName("rclone-browser-remote-daemon-test");

    // The app's own config doesn't exist. Every remote below lives only in
    // the daemon's config, the way it would on a NAS.
    SetRcloneConf(QDir(mDir.path()).filePath("not-this-one/rclone.conf"));

    mServed = QDir(mDir.path()).filePath("served");
    QVERIFY(QDir().mkpath(mServed));
  }

  void cleanupTestCase() {
    for (QProcess *daemon : {mDaemon, mTlsDaemon}) {
      if (daemon != nullptr) {
        daemon->kill();
        daemon->waitForFinished(5000);
      }
    }
  }

  void addressesAreNormalized() {
    QCOMPARE(RemoteDaemon::NormalizeUrl("nas.local:5572"),
             QString("http://nas.local:5572/"));
    QCOMPARE(RemoteDaemon::NormalizeUrl("  https://nas/rc  "),
             QString("https://nas/rc/"));
    QCOMPARE(RemoteDaemon::NormalizeUrl("http://192.168.1.5:5572/"),
             QString("http://192.168.1.5:5572/"));
    QVERIFY(RemoteDaemon::NormalizeUrl("ftp://nas").isEmpty());
    QVERIFY(RemoteDaemon::NormalizeUrl("http://").isEmpty());
    QVERIFY(RemoteDaemon::NormalizeUrl("   ").isEmpty());
  }

  // rclone's rc login is as good as a shell on that machine, so plain http
  // to anything but this computer gets a warning in Preferences.
  void plainHttpToTheNetworkIsFlagged() {
    for (const char *url :
         {"http://192.168.1.5:5572", "nas.local:5572", "http://nas.local:5572/",
          "http://localhost.example.com:5572", "http://[fd00::5]:5572",
          "http://10.0.0.1"}) {
      QVERIFY2(RemoteDaemon::IsPlainHttpToNetwork(url), url);
      QVERIFY2(RemoteDaemon::PlainHttpWarning(url).contains("--rc-cert"), url);
    }
    for (const char *url :
         {"http://localhost:5572", "http://LOCALHOST:5572",
          "http://127.0.0.1:5572", "127.0.0.1:5572", "http://127.8.0.1:5572",
          "http://[::1]:5572", "https://nas.local:5572",
          "https://192.168.1.5:5572", "ftp://nas.local", "", "http://"}) {
      QVERIFY2(!RemoteDaemon::IsPlainHttpToNetwork(url), url);
      QVERIFY2(RemoteDaemon::PlainHttpWarning(url).isEmpty(), url);
    }
  }

  void fingerprintsAreNormalizedAndCompared() {
    const QString fingerprint = QString::fromLatin1(kTestFingerprint);
    const QString other = QString::fromLatin1(kOtherFingerprint);
    QString bare = fingerprint;
    bare.remove(QChar(':'));
    QString spaced = fingerprint;
    spaced.replace(QChar(':'), QChar(' '));

    QCOMPARE(RcCertificate::Normalize("sha256 Fingerprint=" + fingerprint),
             fingerprint);
    QCOMPARE(RcCertificate::Normalize(bare.toLower()), fingerprint);
    QCOMPARE(RcCertificate::Normalize("  " + spaced + "\n"), fingerprint);
    QVERIFY(RcCertificate::Normalize(bare.left(62)).isEmpty());
    QVERIFY(RcCertificate::Normalize(bare + "AB").isEmpty());
    QVERIFY(RcCertificate::Normalize(bare.left(63) + "G").isEmpty());
    QVERIFY(RcCertificate::Normalize(QString()).isEmpty());

    const QSslCertificate certificate(QByteArray(kTestCertificate), QSsl::Pem);
    QVERIFY(!certificate.isNull());
    QCOMPARE(RcCertificate::Fingerprint(certificate), fingerprint);
    QVERIFY(RcCertificate::Fingerprint(QSslCertificate()).isEmpty());

    QVERIFY(RcCertificate::Matches(bare.toLower(), fingerprint));
    QVERIFY(!RcCertificate::Matches(other, fingerprint));
    QVERIFY(!RcCertificate::Matches(QString(), QString()));
    QVERIFY(!RcCertificate::Matches("not hex", "not hex"));

    const QString untrusted =
        RcCertificate::Refusal("https://nas:5572/", QString(), fingerprint);
    QVERIFY2(untrusted.contains("doesn't trust"), qPrintable(untrusted));
    QVERIFY2(untrusted.contains(fingerprint), qPrintable(untrusted));
    const QString changed =
        RcCertificate::Refusal("https://nas:5572/", other, fingerprint);
    QVERIFY2(changed.contains("different certificate"), qPrintable(changed));
    QVERIFY2(changed.contains("Trusted: " + other), qPrintable(changed));
    QVERIFY2(changed.contains("Presented: " + fingerprint), qPrintable(changed));

    // A pinned request stays on HTTP/1.1, where every connection's handshake
    // is judged before anything is sent on it. An unpinned one is left alone.
    QNetworkRequest pinned(QUrl("https://nas.local:5572/rc/noopauth"));
    RcCertificate::Prepare(pinned, fingerprint);
    QCOMPARE(pinned.attribute(QNetworkRequest::Http2AllowedAttribute, true)
                 .toBool(),
             false);
    QCOMPARE(pinned.peerVerifyName(), QString("nas.local"));
    QNetworkRequest open(QUrl("https://nas.local:5572/rc/noopauth"));
    RcCertificate::Prepare(open, QString());
    QCOMPARE(
        open.attribute(QNetworkRequest::Http2AllowedAttribute, true).toBool(),
        true);
    QVERIFY(open.peerVerifyName().isEmpty());
  }

  void commandArgumentsCarryTheDaemonsConfig() {
    const QStringList args = {"copy", "a:x", "b:y"};
    QCOMPARE(RemoteDaemon::CommandArguments(args, "/srv/rclone.conf"),
             (QJsonArray{"--config", "/srv/rclone.conf", "a:x", "b:y"}));
    QCOMPARE(RemoteDaemon::CommandArguments(args, QString()),
             (QJsonArray{"a:x", "b:y"}));
    const QStringList named = {"lsf", "--config", "/mine.conf", "a:"};
    QCOMPARE(RemoteDaemon::CommandArguments(named, "/srv/rclone.conf"),
             (QJsonArray{"--config", "/mine.conf", "a:"}));
  }

  void listPayloadsMatchTheCommandLine() {
    const QJsonObject drive =
        RemoteDaemon::ListPayload("gdrive:Docs", false, true, false);
    QCOMPARE(drive.value("fs").toString(),
             QString("gdrive,shared_with_me:Docs"));
    QCOMPARE(drive.value("_filter").toObject().value("ExcludeRule").toArray(),
             (QJsonArray{".*/**", ".*"}));
    QVERIFY(!drive.value("opt").toObject().contains("recurse"));

    const QJsonObject local =
        RemoteDaemon::ListPayload(":local:/data", true, true, true);
    QCOMPARE(local.value("fs").toString(), QString(":local:/data"));
    QVERIFY(!local.contains("_filter"));
    QVERIFY(local.value("opt").toObject().value("recurse").toBool());
    QVERIFY(local.value("opt").toObject().value("filesOnly").toBool());
  }

  void moveRequestsKeepTheRootSlash() {
    RemoteDaemon::Request request =
        RemoteDaemon::MoveTo("nas", "dir/a.txt", "other/b.txt", false);
    QCOMPARE(request.endpoint, QString("operations/movefile"));
    QCOMPARE(request.payload.value("srcFs").toString(), QString("nas:dir"));
    QCOMPARE(request.payload.value("srcRemote").toString(), QString("a.txt"));
    QCOMPARE(request.payload.value("dstFs").toString(), QString("nas:other"));
    QCOMPARE(request.payload.value("dstRemote").toString(), QString("b.txt"));

    request = RemoteDaemon::MoveTo(":local", "/a.txt", "C:/b.txt", false);
    QCOMPARE(request.payload.value("srcFs").toString(), QString(":local:/"));
    QCOMPARE(request.payload.value("dstFs").toString(), QString(":local:C:/"));

    request = RemoteDaemon::MoveTo("nas", "top.txt", "top2.txt", false);
    QCOMPARE(request.payload.value("srcFs").toString(), QString("nas:"));

    request = RemoteDaemon::MoveTo("nas", "folder/", "renamed", true);
    QCOMPARE(request.endpoint, QString("sync/move"));
    QCOMPARE(request.payload.value("srcFs").toString(), QString("nas:folder/"));
    QVERIFY(request.payload.value("deleteEmptySrcDirs").toBool());

    const RemoteDaemon::Request mkdir =
        RemoteDaemon::Mkdir(":local", "C:/data/new");
    QCOMPARE(mkdir.payload.value("fs").toString(), QString(":local:C:/data/new"));
    QCOMPARE(mkdir.payload.value("remote").toString(), QString());
  }

  void remotesComeFromTheDump() {
    QJsonObject dump;
    dump.insert("zeta", QJsonObject{{"type", "s3"}});
    dump.insert("alpha",
                QJsonObject{{"type", "alias"}, {"description", "Photos"}});
    const auto remotes = RemoteDaemon::RemotesFromDump(dump);
    QCOMPARE(remotes.size(), 2);
    QCOMPARE(remotes.at(0).name, QString("alpha"));
    QCOMPARE(remotes.at(0).type, QString("alias"));
    QCOMPARE(remotes.at(0).description, QString("Photos"));
    QCOMPARE(remotes.at(1).name, QString("zeta"));
    QVERIFY(RemoteDaemon::RemotesFromDump(QJsonObject()).isEmpty());

    const QString line = RemoteDaemon::DescribeVersion(
        QJsonObject{{"version", "v1.75.1"}, {"os", "linux"}, {"arch", "arm64"}},
        "http://nas:5572/");
    QCOMPARE(line,
             QString("Remote rclone v1.75.1 at http://nas:5572/ (linux, arm64)"));
  }

  void settingsRoundTripAndProtectThePassword() {
    RemoteDaemon::Settings saved;
    saved.enabled = true;
    saved.url = "  nas.local:5572 ";
    saved.user = "admin";
    saved.password = "s3cret pass";
    QString pasted = QString::fromLatin1(kTestFingerprint).toLower();
    pasted.remove(QChar(':'));
    saved.certificateSha256 = pasted;
    RemoteDaemon::Save(saved);

    const RemoteDaemon::Settings loaded = RemoteDaemon::Load();
    QVERIFY(loaded.enabled);
    QVERIFY(RemoteDaemon::IsEnabled());
    QCOMPARE(loaded.url, QString("http://nas.local:5572/"));
    QCOMPARE(loaded.user, QString("admin"));
    QCOMPARE(loaded.password, QString("s3cret pass"));
    QCOMPARE(loaded.certificateSha256, QString::fromLatin1(kTestFingerprint));
    const QString stored =
        GetSettings()->value("Settings/remoteDaemonPassword").toString();
    QVERIFY2(!stored.contains("s3cret"), qPrintable(stored));

    // Turned on with an address the app can't use counts as off, so nothing
    // half-switches to a daemon that can't exist.
    saved.url = "ftp://nas.local";
    RemoteDaemon::Save(saved);
    QVERIFY(!RemoteDaemon::IsEnabled());
    QVERIFY(!RemoteDaemon::Load().enabled);

    RemoteDaemon::Save(RemoteDaemon::Settings());
    QVERIFY(!RemoteDaemon::IsEnabled());
  }

  void startTheDaemon() {
    mRclone = FindRcloneForTests();
    if (mRclone.isEmpty()) {
      QSKIP("no rclone; set RCLONE_BROWSER_TEST_RCLONE");
    }
    SetRclone(mRclone);

    const QString config = QDir(mDir.path()).filePath("daemon.conf");
    QFile file(config);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("[rbtestnas]\ntype = alias\nremote = " + mServed.toUtf8() +
               "\n");
    file.close();

    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();

    mDaemon = new QProcess(this);
    mDaemon->setProcessChannelMode(QProcess::MergedChannels);
#if defined(Q_OS_WIN32)
    mDaemon->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
          args->flags |= CREATE_NO_WINDOW;
        });
#endif
    mDaemon->start(mRclone,
                   {"rcd", "--config", config, "--rc-addr",
                    QString("127.0.0.1:%1").arg(port), "--rc-user", "nasuser",
                    "--rc-pass", "nas pass"});
    QVERIFY2(mDaemon->waitForStarted(10000), qPrintable(mDaemon->errorString()));

    mRemote.enabled = true;
    mRemote.url = QString("http://127.0.0.1:%1/").arg(port);
    mRemote.user = "nasuser";
    mRemote.password = "nas pass";
    QString error = "not up";
    QVERIFY2(QTest::qWaitFor(
                 [&]() {
                   call("core/version", QJsonObject(), &error);
                   return error.isEmpty();
                 },
                 30000),
             qPrintable(error + " / " +
                        QString::fromUtf8(mDaemon->readAll())));
  }

  void theLoginIsChecked() {
    if (mDaemon == nullptr) {
      QSKIP("no daemon");
    }
    QString error;
    const QJsonObject version = call("core/version", QJsonObject(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(version.value("version").toString().startsWith("v1."));

    // rc/noopauth is what the Test Connection button and the engine use,
    // because core/version answers anyone.
    RemoteDaemon::Settings wrong = mRemote;
    wrong.password = "nope";
    callAndWait(wrong, "rc/noopauth", QJsonObject(), &error, this);
    QVERIFY2(error.contains("didn't accept the user name and password"),
             qPrintable(error));
    call("rc/noopauth", QJsonObject(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
  }

  void listingFollowsShowHidden() {
    if (mDaemon == nullptr) {
      QSKIP("no daemon");
    }
    writeFile("visible.txt");
    writeFile(".hidden.txt");
    writeFile(".hiddendir/inside.txt");
    QCOMPARE(listNames(false), QStringList{"visible.txt"});
    QCOMPARE(listNames(true),
             (QStringList{".hidden.txt", ".hiddendir", "visible.txt"}));
  }

  void foldersAreMadeWhereTheyWereAskedFor() {
    if (mDaemon == nullptr) {
      QSKIP("no daemon");
    }
    QString error;
    RemoteDaemon::Request request =
        RemoteDaemon::Mkdir("rbtestnas", "made/deeper");
    call(request.endpoint, request.payload, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo(QDir(mServed).filePath("made/deeper")).isDir());

    // An absolute path on the daemon's disk. With the path split into fs and
    // remote, rclone made it relative to the daemon's working folder.
    const QString absolute =
        QDir::fromNativeSeparators(QDir(mServed).filePath("abs-made"));
    request = RemoteDaemon::Mkdir(":local", absolute);
    call(request.endpoint, request.payload, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo(absolute).isDir());
  }

  void filesAndFoldersMove() {
    if (mDaemon == nullptr) {
      QSKIP("no daemon");
    }
    writeFile("dir/a.txt", "file a");
    QString error;
    RemoteDaemon::Request request =
        RemoteDaemon::MoveTo("rbtestnas", "dir/a.txt", "dir/b.txt", false);
    call(request.endpoint, request.payload, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!served("dir/a.txt"));
    QVERIFY(served("dir/b.txt"));

    writeFile("top.txt");
    request = RemoteDaemon::MoveTo("rbtestnas", "top.txt", "dir/top.txt", false);
    call(request.endpoint, request.payload, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!served("top.txt"));
    QVERIFY(served("dir/top.txt"));

    writeFile("folder/inner/x.txt");
    request = RemoteDaemon::MoveTo("rbtestnas", "folder", "renamed", true);
    call(request.endpoint, request.payload, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(served("renamed/inner/x.txt"));
    QVERIFY(!served("folder"));
  }

  void commandsRunWithTheDaemonsConfig() {
    if (mDaemon == nullptr) {
      QSKIP("no daemon");
    }
    RemoteDaemon::Save(mRemote);
    writeFile("cmd/listed.txt");

    int exitCode = -1;
    QByteArray output;
    bool done = false;
    RemoteDaemon::RunCommand({"lsf", "rbtestnas:cmd"}, this,
                             [&](int code, const QByteArray &text) {
                               exitCode = code;
                               output = text;
                               done = true;
                             });
    QVERIFY(QTest::qWaitFor([&]() { return done; }, 60000));
    QVERIFY2(exitCode == 0, output.constData());
    QVERIFY2(output.contains("listed.txt"), output.constData());

    done = false;
    RemoteDaemon::RunCommand({"lsf", "rbtestnas:no-such-folder"}, this,
                             [&](int code, const QByteArray &text) {
                               exitCode = code;
                               output = text;
                               done = true;
                             });
    QVERIFY(QTest::qWaitFor([&]() { return done; }, 60000));
    QCOMPARE(exitCode, 1);
    QVERIFY(!output.isEmpty());
    RemoteDaemon::Save(RemoteDaemon::Settings());
  }

  // What DryRunCheck answered for args, or "no answer" when it didn't.
  QString dryRunRefusal(const QStringList &args) {
    struct Answer {
      bool done = false;
      QString refusal;
    };
    const auto answer = std::make_shared<Answer>();
    DryRunCheck::Run(args, QProcessEnvironment(), this,
                     [answer](const QString &refusal) {
                       answer->refusal = refusal;
                       answer->done = true;
                     });
    if (!QTest::qWaitFor([answer]() { return answer->done; }, 60000)) {
      return QStringLiteral("no answer");
    }
    return answer->refusal;
  }

  // A dry run the Transfer dialog or the process fallback starts is checked
  // against the rclone.conf that rclone reads: its own --config first, then
  // the app's on this PC, or the daemon's when it runs there.
  void aDryRunIsCheckedAgainstTheConfigItReads() {
    if (mDaemon == nullptr) {
      QSKIP("no daemon");
    }
    const auto writeConfig = [this](const QString &name, bool off) {
      QByteArray text =
          "[bk]\ntype = alias\nremote = " + mServed.toUtf8() + "\n";
      if (off) {
        text += "global.dry_run = false\n";
      }
      const QString path = QDir(mDir.path()).filePath(name);
      QFile file(path);
      if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
      }
      file.write(text);
      return path;
    };
    const QString off = writeConfig("dry-off.conf", true);
    const QString clean = writeConfig("dry-clean.conf", false);
    QVERIFY(!off.isEmpty());
    QVERIFY(!clean.isEmpty());
    const QString appConfig =
        QDir(mDir.path()).filePath("not-this-one/rclone.conf");
    struct Restore {
      QString config;
      ~Restore() {
        SetRcloneConf(config);
        RemoteDaemon::Save(RemoteDaemon::Settings());
      }
    } restore{appConfig};

    bool answered = false;
    DryRunCheck::Run({"sync", "bk:a", "bk:b"}, QProcessEnvironment(), this,
                     [&answered](const QString &refusal) {
                       answered = refusal.isEmpty();
                     });
    QVERIFY2(answered, "a run that isn't dry is let through at once");

    for (const bool remote : {false, true}) {
      RemoteDaemon::Save(remote ? mRemote : RemoteDaemon::Settings());
      const QString where = remote ? "remote daemon" : "this PC";

      SetRcloneConf(appConfig);
      QString refusal = dryRunRefusal(
          {"sync", "--dry-run", "--config", off, "bk:a", "bk:b"});
      QVERIFY2(refusal.contains("global.dry_run") && refusal.contains("bk"),
               qPrintable(where + ": " + refusal));
      refusal = dryRunRefusal(
          {"sync", "--dry-run", "--config=" + off, "bk:a", "bk:b"});
      QVERIFY2(refusal.contains("global.dry_run"),
               qPrintable(where + ": " + refusal));
      refusal = dryRunRefusal(
          {"sync", "--dry-run", "--config=" + clean, "bk:a", "bk:b"});
      QVERIFY2(refusal.isEmpty(), qPrintable(where + ": " + refusal));

      // The app's rclone.conf is the one read here. The daemon reads its own.
      SetRcloneConf(off);
      refusal = dryRunRefusal({"sync", "--dry-run", "bk:a", "bk:b"});
      QVERIFY2(remote ? refusal.isEmpty() : refusal.contains("global.dry_run"),
               qPrintable(where + ": " + refusal));
    }
  }

  void aDumpThatFailsStopsTheDryRun() {
    const QString refusal =
        DryRunCheck::RefusalFor(1, "Failed to load config file", {});
    QVERIFY2(refusal.contains("couldn't be read (Failed to load config file)"),
             qPrintable(refusal));
    QVERIFY2(!DryRunCheck::RefusalFor(0, "not json", {}).isEmpty(),
             "output that isn't a dump stops it too");
    QVERIFY(DryRunCheck::RefusalFor(0, "{}", {}).isEmpty());
  }

  // The engine is what starts transfers. --one-file-system can't be mapped
  // onto sync/copy, so this goes through core/command, which only finds
  // rbtestnas when the daemon's config path is passed on.
  void theEngineTransfersOnTheRemoteDaemon() {
    if (mDaemon == nullptr) {
      QSKIP("no daemon");
    }
    writeFile("src/one.txt");
    writeFile("src/sub/two.txt");

    RcloneRcEngine engine;
    engine.useRemoteDaemon(mRemote.url, mRemote.user, mRemote.password);
    QVERIFY(engine.isRemote());
    runEngineJob(engine, {"copy", "--one-file-system", "rbtestnas:src",
                          "rbtestnas:dst-command"});
    QVERIFY(served("dst-command/one.txt"));
    QVERIFY(served("dst-command/sub/two.txt"));

    runEngineJob(engine, {"copy", "rbtestnas:src", "rbtestnas:dst-native"});
    QVERIFY(served("dst-native/sub/two.txt"));
  }

  // bisync --resilient retries a failed copy itself, as many times as
  // --retries says (cmd/bisync/queue.go, v1.76.0). options/info lists no
  // bisync flag, so a task with --resilient runs on core/command, where the
  // CLI reads --retries itself. A sync/bisync call gets the task's
  // --retries through _config, so the daemon's own 3 doesn't stand in.
  void aBisyncsCopyRetryTakesTheTasksRetries() {
    if (mDaemon == nullptr) {
      QSKIP("no daemon");
    }
#if !defined(Q_OS_WIN32)
    QSKIP("the locked file is made with a Windows share mode");
#else
    QString error;
    const QJsonObject info = call("options/info", QJsonObject(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const RcSyncRequest::OptionIndex index = RcSyncRequest::IndexOptions(info);

    const QStringList resilientArgs{"bisync", "--resilient", "rbtestnas:bi1",
                                    "rbtestnas:bi2"};
    const RcSyncRequest::Request resilient =
        RcSyncRequest::Build(resilientArgs, "g", index);
    QVERIFY(!resilient.usable);
    QVERIFY2(resilient.reason.contains("--resilient"),
             qPrintable(resilient.reason));

    writeFile("bi1/a.txt");
    QVERIFY(QDir().mkpath(QDir(mServed).filePath("bi2")));
    const QString workdir = QDir(mDir.path()).filePath("bisync-work");
    QVERIFY(QDir().mkpath(workdir));
    const QStringList args{"bisync", "--retries", "2", "rbtestnas:bi1",
                           "rbtestnas:bi2"};
    const RcSyncRequest::Request request =
        RcSyncRequest::Build(args, "g", index);
    QVERIFY2(request.usable, qPrintable(request.reason));
    QJsonObject payload = request.payload;
    payload.remove("_async");
    payload.insert("workdir", workdir);
    QJsonObject resync = payload;
    resync.insert("resync", true);
    call(request.endpoint, resync, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    writeFile("bi1/locked.txt");
    const QString lockedPath = QDir(mServed).filePath("bi1/locked.txt");
    HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(lockedPath.utf16()),
                                GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0,
                                nullptr);
    QVERIFY(locked != INVALID_HANDLE_VALUE);
    mDaemon->readAll();
    payload.insert("resilient", true);
    call(request.endpoint, payload, &error);
    QString log;
    const bool sawTheLastRetry = QTest::qWaitFor(
        [&]() {
          log += QString::fromUtf8(mDaemon->readAll());
          return log.contains("Retry 2/2");
        },
        10000);
    CloseHandle(locked);
    QVERIFY2(!error.isEmpty(), "the locked file let the bisync through");
    QVERIFY2(sawTheLastRetry && !log.contains("Retry 1/3"),
             qPrintable(log.right(2000)));
#endif
  }

  // rclone serving https with a self-signed certificate, the way a NAS would
  // be set up. Only the fingerprint the user trusted lets it through.
  void aSelfSignedDaemonNeedsItsFingerprintTrusted() {
    if (mDaemon == nullptr) {
      QSKIP("no daemon");
    }
    const QString certificate = writeText("tls-cert.pem", kTestCertificate);
    const QString key = writeText("tls-key.pem", kTestKey);
    QVERIFY(!certificate.isEmpty());
    QVERIFY(!key.isEmpty());
    const quint16 port = freePort();
    QVERIFY(port != 0);
    mTlsDaemon = startRcd(
        {"--config", QDir(mDir.path()).filePath("daemon.conf"), "--rc-addr",
         QString("127.0.0.1:%1").arg(port), "--rc-cert", certificate,
         "--rc-key", key, "--rc-min-tls-version", "tls1.2", "--rc-user",
         "nasuser", "--rc-pass", "nas pass"});
    QVERIFY2(mTlsDaemon->waitForStarted(10000),
             qPrintable(mTlsDaemon->errorString()));

    const QString fingerprint = QString::fromLatin1(kTestFingerprint);
    const QString other = QString::fromLatin1(kOtherFingerprint);
    RemoteDaemon::Settings untrusted = mRemote;
    untrusted.url = QString("https://127.0.0.1:%1/").arg(port);
    untrusted.certificateSha256.clear();
    RemoteDaemon::Settings trusted = untrusted;
    trusted.certificateSha256 = fingerprint;
    RemoteDaemon::Settings changed = untrusted;
    changed.certificateSha256 = other;

    // Nothing trusted yet. The refusal carries the certificate's fingerprint
    // for Preferences to offer. Retried until the daemon is listening.
    QString error = "not up";
    QJsonObject refused;
    QVERIFY2(QTest::qWaitFor(
                 [&]() {
                   refused = callAndWait(untrusted, "rc/noopauth",
                                         QJsonObject(), &error, this);
                   return refused.contains(
                       RemoteDaemon::kPresentedCertificateKey);
                 },
                 30000),
             qPrintable(error + " / " +
                        QString::fromUtf8(mTlsDaemon->readAll())));
    QCOMPARE(refused.value(RemoteDaemon::kPresentedCertificateKey).toString(),
             fingerprint);
    QVERIFY2(error.contains("doesn't trust"), qPrintable(error));
    QVERIFY2(error.contains(fingerprint), qPrintable(error));

    callAndWait(changed, "rc/noopauth", QJsonObject(), &error, this);
    QVERIFY2(error.contains("different certificate"), qPrintable(error));
    QVERIFY2(error.contains("Trusted: " + other), qPrintable(error));
    QVERIFY2(error.contains("Presented: " + fingerprint), qPrintable(error));

    // Trusted, although the certificate is for nas.local, not 127.0.0.1.
    const QJsonObject version =
        callAndWait(trusted, "core/version", QJsonObject(), &error, this);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(version.value("version").toString().startsWith("v1."));
    callAndWait(trusted, "rc/noopauth", QJsonObject(), &error, this);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // The trusted calls left a connection open, and Qt would send later
    // requests down it with no handshake to judge. A call under another
    // fingerprint has to get a connection of its own and be refused before
    // the daemon sees it, so this folder is never made.
    const RemoteDaemon::Request refusedMkdir =
        RemoteDaemon::Mkdir("rbtestnas", "tls-refused");
    callAndWait(changed, refusedMkdir.endpoint, refusedMkdir.payload, &error,
                this);
    QVERIFY2(error.contains("different certificate"), qPrintable(error));
    QVERIFY(!served("tls-refused"));
    // The same call under the trusted fingerprint does make it.
    const RemoteDaemon::Request trustedMkdir =
        RemoteDaemon::Mkdir("rbtestnas", "tls-trusted");
    callAndWait(trusted, trustedMkdir.endpoint, trustedMkdir.payload, &error,
                this);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(served("tls-trusted"));
    callAndWait(changed, "rc/noopauth", QJsonObject(), &error, this);
    QVERIFY2(error.contains("different certificate"), qPrintable(error));
    refused =
        callAndWait(untrusted, "rc/noopauth", QJsonObject(), &error, this);
    QVERIFY2(error.contains("doesn't trust"), qPrintable(error));
    QCOMPARE(refused.value(RemoteDaemon::kPresentedCertificateKey).toString(),
             fingerprint);

    // Transfers go through the engine, which keeps connections of its own.
    RcloneRcEngine engine;
    error = startEngine(engine, untrusted);
    QVERIFY2(error.contains("doesn't trust"), qPrintable(error));
    error = startEngine(engine, changed);
    QVERIFY2(error.contains("different certificate"), qPrintable(error));
    error = startEngine(engine, trusted);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    writeFile("tls/one.txt");
    runEngineJob(engine, {"copy", "rbtestnas:tls", "rbtestnas:tls-copied"});
    QVERIFY(served("tls-copied/one.txt"));
    error = startEngine(engine, changed);
    QVERIFY2(error.contains("different certificate"), qPrintable(error));
  }
};

QTEST_MAIN(RemoteDaemonTest)
#include "remote_daemon_test.moc"
