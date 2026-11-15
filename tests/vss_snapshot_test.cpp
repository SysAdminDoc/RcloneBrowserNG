#include "vss_snapshot.h"

#include <QTest>
#include <memory>

#if defined(Q_OS_WIN32)
#include <windows.h>
#endif

namespace {

#if defined(Q_OS_WIN32)
bool IsElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return false;
  }
  TOKEN_ELEVATION elevation = {};
  DWORD size = 0;
  const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                                      sizeof(elevation), &size) != 0;
  CloseHandle(token);
  return ok && elevation.TokenIsElevated != 0;
}

// A drive letter with nothing behind it, or an empty string when all 26
// are taken.
QString UnusedDrive() {
  const DWORD used = GetLogicalDrives();
  for (int letter = 25; letter >= 3; --letter) {
    if ((used & (1U << letter)) == 0) {
      return QString(QChar(static_cast<char16_t>(u'A' + letter))) +
             QStringLiteral(":\\");
    }
  }
  return QString();
}

bool PathEntryExists(const QString &path) {
  // Unlike QFileInfo::exists, this doesn't follow a symbolic link, so a
  // link to a deleted snapshot still counts.
  return GetFileAttributesW(reinterpret_cast<const wchar_t *>(
             QDir::toNativeSeparators(path).utf16())) !=
         INVALID_FILE_ATTRIBUTES;
}
#endif

QByteArray ReadFile(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class VssSnapshotTest : public QObject {
  Q_OBJECT

private slots:
  void recognizesDrivePaths() {
    QVERIFY(VssSnapshot::IsLocalDrivePath("C:\\Users\\me"));
    QVERIFY(VssSnapshot::IsLocalDrivePath("d:/data/mail.pst"));
    QVERIFY(VssSnapshot::IsLocalDrivePath("E:\\"));
    QVERIFY(!VssSnapshot::IsLocalDrivePath("remote:path"));
    QVERIFY(!VssSnapshot::IsLocalDrivePath("gdrive:C:/odd"));
    QVERIFY(!VssSnapshot::IsLocalDrivePath("\\\\server\\share\\folder"));
    QVERIFY(!VssSnapshot::IsLocalDrivePath("C:relative"));
    QVERIFY(!VssSnapshot::IsLocalDrivePath(""));
  }

  void volumeRootIsTheDriveLetter() {
    QCOMPARE(VssSnapshot::VolumeRoot("c:\\Users\\me"), QString("C:\\"));
    QCOMPARE(VssSnapshot::VolumeRoot("D:/data"), QString("D:\\"));
    QVERIFY(VssSnapshot::VolumeRoot("remote:path").isEmpty());
  }

  void pathInSnapshotKeepsTheRestOfThePath() {
    QCOMPARE(VssSnapshot::PathInSnapshot("C:\\Users\\me\\Mail",
                                         "D:\\tmp\\s\\snapshot"),
             QString("D:/tmp/s/snapshot/Users/me/Mail"));
    QCOMPARE(VssSnapshot::PathInSnapshot("C:/data/file.pst",
                                         "D:/tmp/s/snapshot/"),
             QString("D:/tmp/s/snapshot/data/file.pst"));
    // A whole drive reads from the root of the snapshot.
    QCOMPARE(VssSnapshot::PathInSnapshot("C:\\", "D:\\tmp\\s\\snapshot"),
             QString("D:/tmp/s/snapshot"));
    QVERIFY(VssSnapshot::PathInSnapshot("remote:path", "D:/x").isEmpty());
  }

  void withSourceSwapsOnlyTheSource() {
    const QStringList args = {"copy",    "--transfers", "4",
                              "C:/data", "remote:backup"};
    QCOMPARE(VssSnapshot::WithSource(args, "D:/s/snapshot/data"),
             QStringList({"copy", "--transfers", "4", "D:/s/snapshot/data",
                          "remote:backup"}));
    // Too short to have a source and a destination: left alone.
    QCOMPARE(VssSnapshot::WithSource({"copy"}, "x"), QStringList({"copy"}));
  }

  void helperArgumentsRoundTrip() {
    VssSnapshot::HelperRequest request;
    request.sessionDir = QDir::tempPath() + "/rclone-browser-vss/one";
    request.volume = "d:\\";
    request.parentPid = 4242;
    const QStringList arguments =
        QStringList({"RcloneBrowser.exe"}) +
        VssSnapshot::HelperArguments(request);
    QCOMPARE(arguments.at(1), QString(VssSnapshot::kHelperFlag));
    // No trailing backslash, which would escape the closing quote on a
    // Windows command line.
    QCOMPARE(arguments.at(3), QString("d:"));

    VssSnapshot::HelperRequest parsed;
    QVERIFY(VssSnapshot::ParseHelperArguments(arguments, &parsed));
    QCOMPARE(parsed.sessionDir, QDir::cleanPath(request.sessionDir));
    QCOMPARE(parsed.volume, QString("D:\\"));
    QCOMPARE(parsed.parentPid, qint64(4242));
  }

  // A real run here got code 10 while a build was writing to C: (Volsnap
  // logged that the shadow storage couldn't grow in time) and passed on the
  // next try. Codes that won't change fail at once.
  void createRetriesOnlyWhatClearsUp() {
    QVERIFY(VssSnapshot::RetryableCreateCode(9));
    QVERIFY(VssSnapshot::RetryableCreateCode(10));
    for (const long code : {-1L, 1L, 2L, 3L, 4L, 5L, 6L, 8L, 11L, 12L, 13L}) {
      QVERIFY2(!VssSnapshot::RetryableCreateCode(code),
               qPrintable(QString::number(code)));
    }

    QList<long> codes;
    int calls = 0;
    int pauses = 0;
    const auto attempt = [&](long *code) {
      *code = codes.value(calls++, 0);
      return *code == 0;
    };
    const auto pause = [&] { ++pauses; };

    codes = {10, 0};
    QVERIFY(VssSnapshot::CreateWithRetry(attempt, pause));
    QCOMPARE(calls, 2);
    QCOMPARE(pauses, 1);

    codes = {1};
    calls = pauses = 0;
    QVERIFY(!VssSnapshot::CreateWithRetry(attempt, pause));
    QCOMPARE(calls, 1);
    QCOMPARE(pauses, 0);

    codes = {9, 10, 10};
    calls = pauses = 0;
    QVERIFY(!VssSnapshot::CreateWithRetry(attempt, pause));
    QCOMPARE(calls, 2);
    QCOMPARE(pauses, 1);

    codes = {0};
    calls = pauses = 0;
    QVERIFY(VssSnapshot::CreateWithRetry(attempt, pause));
    QCOMPARE(calls, 1);
    QCOMPARE(pauses, 0);
  }

  void helperArgumentsRejectBadInput() {
    const QString dir = QDir::tempPath() + "/rclone-browser-vss/one";
    const QString flag = VssSnapshot::kHelperFlag;
    VssSnapshot::HelperRequest parsed;
    QVERIFY(!VssSnapshot::ParseHelperArguments({"app", dir, "C:", "1"},
                                               &parsed));
    QVERIFY(!VssSnapshot::ParseHelperArguments({"app", flag, dir, "C:"},
                                               &parsed));
    QVERIFY(!VssSnapshot::ParseHelperArguments(
        {"app", flag, "relative/dir", "C:", "1"}, &parsed));
    QVERIFY(!VssSnapshot::ParseHelperArguments({"app", flag, dir, "C:", "0"},
                                               &parsed));
    QVERIFY(!VssSnapshot::ParseHelperArguments({"app", flag, dir, "C:", "x"},
                                               &parsed));
    QVERIFY(!VssSnapshot::ParseHelperArguments(
        {"app", flag, dir, "C:\\", "1"}, &parsed));
    QVERIFY(!VssSnapshot::ParseHelperArguments({"app", flag, dir, "CC:", "1"},
                                               &parsed));
    QVERIFY(!VssSnapshot::ParseHelperArguments({"app", flag, dir, "C:", "1"},
                                               nullptr));
    QCOMPARE(VssSnapshot::RunHelper({"app", flag}), 2);
  }

  void unavailableReasonExplainsItself() {
    QVERIFY(!VssSnapshot::UnavailableReason("remote:path").isEmpty());
#if !defined(Q_OS_WIN32)
    QVERIFY(!VssSnapshot::UnavailableReason("/home/me").isEmpty());
#else
    const QString system = qEnvironmentVariable("SystemDrive", "C:") + "\\";
    QVERIFY2(VssSnapshot::UnavailableReason(system + "Windows").isEmpty(),
             qPrintable(VssSnapshot::UnavailableReason(system + "Windows")));
    const QString missing = UnusedDrive();
    if (!missing.isEmpty()) {
      QVERIFY(VssSnapshot::UnavailableReason(missing + "data")
                  .contains("no drive"));
    }
#endif
  }

  void beginRefusesARemoteSourceStraightAway() {
    QObject context;
    bool called = false;
    QString error;
    VssSession::Begin("remote:path", &context,
                      [&](VssSession *session, const QString &message) {
                        called = true;
                        QVERIFY(session == nullptr);
                        error = message;
                      });
    // Before any helper could start.
    QVERIFY(called);
    QVERIFY(!error.isEmpty());
  }

  void helperWithoutAdministratorWritesAnError() {
#if !defined(Q_OS_WIN32)
    QSKIP("Volume Shadow Copy is Windows only.");
#else
    if (IsElevated()) {
      QSKIP("This run is elevated, so the helper would make a real snapshot.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    VssSnapshot::HelperRequest request;
    request.sessionDir = dir.path();
    request.volume = qEnvironmentVariable("SystemDrive", "C:") + "\\";
    request.parentPid = QCoreApplication::applicationPid();
    const int code = VssSnapshot::RunHelper(
        QStringList({"app"}) + VssSnapshot::HelperArguments(request));
    QCOMPARE(code, 1);
    QVERIFY(!ReadFile(VssSnapshot::ErrorFile(dir.path())).isEmpty());
    QVERIFY(QFileInfo::exists(VssSnapshot::HelperPidFile(dir.path())));
    QVERIFY(!QFileInfo::exists(VssSnapshot::ReadyFile(dir.path())));
    QVERIFY(!PathEntryExists(VssSnapshot::LinkPath(dir.path())));
#endif
  }

  void helperRefusesAJunctionedSessionFolder() {
#if !defined(Q_OS_WIN32)
    QSKIP("Volume Shadow Copy is Windows only.");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath("target");
    const QString junction = dir.filePath("junction");
    QVERIFY(QDir().mkpath(target));
    QProcess mklink;
    mklink.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
          args->flags |= CREATE_NO_WINDOW;
        });
    mklink.start("cmd.exe", {"/c", "mklink", "/J",
                             QDir::toNativeSeparators(junction),
                             QDir::toNativeSeparators(target)});
    QVERIFY(mklink.waitForFinished(30000));
    QCOMPARE(mklink.exitCode(), 0);

    VssSnapshot::HelperRequest request;
    request.sessionDir = junction;
    request.volume = "C:\\";
    request.parentPid = QCoreApplication::applicationPid();
    QCOMPARE(VssSnapshot::RunHelper(QStringList({"app"}) +
                                    VssSnapshot::HelperArguments(request)),
             3);
    // Nothing written through the junction.
    QVERIFY(QDir(target).isEmpty());
    QDir().rmdir(junction);
#endif
  }

  // The whole round trip with a real snapshot: a file no other program can
  // open is read through the snapshot, rclone copies it, and releasing the
  // session deletes the snapshot. It needs an administrator account and may
  // show the Windows approval prompt, so it only runs when asked.
  void snapshotReadsALockedFile() {
#if !defined(Q_OS_WIN32)
    QSKIP("Volume Shadow Copy is Windows only.");
#else
    if (qEnvironmentVariable("RB_VSS_E2E") != "1") {
      QSKIP("Set RB_VSS_E2E=1 to make a real snapshot.");
    }
    QString app = qEnvironmentVariable("RB_VSS_E2E_APP");
    if (app.isEmpty()) {
      app = QDir(QCoreApplication::applicationDirPath())
                .filePath("RcloneBrowser.exe");
    }
    QVERIFY2(QFileInfo::exists(app), qPrintable(app));
    // The helper starts elevated, and an elevated process gets the user's
    // PATH, not this test's. An exe with no Qt beside it (the build folder)
    // can't load, and Windows shows a "System Error" box per missing DLL
    // while the snapshot never comes. Refuse before launching it.
    const QDir appDir = QFileInfo(app).dir();
    if (!appDir.exists("Qt6Core.dll") && !appDir.exists("Qt6Cored.dll")) {
      QFAIL(qPrintable(
          QString("%1 has no Qt DLLs beside it, so the elevated helper "
                  "can't start. Run windeployqt on a copy of it and point "
                  "RB_VSS_E2E_APP at that copy.")
              .arg(QDir::toNativeSeparators(app))));
    }
    VssSnapshot::SetHelperProgram(app);

    QTemporaryDir source;
    QVERIFY(source.isValid());
    const QByteArray content = "only a snapshot can read this\n";
    const QString file = source.filePath("locked.pst");
    {
      QFile out(file);
      QVERIFY(out.open(QIODevice::WriteOnly));
      out.write(content);
    }
    // No sharing at all, the way Outlook holds its data file.
    HANDLE lock = CreateFileW(
        reinterpret_cast<const wchar_t *>(
            QDir::toNativeSeparators(file).utf16()),
        GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
        nullptr);
    QVERIFY(lock != INVALID_HANDLE_VALUE);
    // Closed on every way out, so a failed check still lets the temporary
    // folder go instead of leaving it in %TEMP%.
    const std::unique_ptr<void, decltype(&CloseHandle)> lockGuard(
        lock, &CloseHandle);
    QVERIFY(!QFile(file).open(QIODevice::ReadOnly));

    QObject context;
    VssSession *session = nullptr;
    QString error;
    bool done = false;
    VssSession::Begin(QDir::toNativeSeparators(source.path()), &context,
                      [&](VssSession *ready, const QString &message) {
                        session = ready;
                        error = message;
                        done = true;
                      });
    QTRY_VERIFY_WITH_TIMEOUT(done, 200000);
    QVERIFY2(session != nullptr, qPrintable(error));
    // Deleting the session releases the snapshot, so a failed check below
    // can't leave one on the drive.
    std::unique_ptr<VssSession> owned(session);
    const QString sessionDir = session->sessionDir();
    const QString shadowId =
        QString::fromUtf8(ReadFile(VssSnapshot::ReadyFile(sessionDir)))
            .trimmed();
    QVERIFY(!shadowId.isEmpty());

    QCOMPARE(ReadFile(session->pathFor(QDir::toNativeSeparators(file))),
             content);

    const QString rclone = qEnvironmentVariable("RCLONE_BROWSER_TEST_RCLONE");
    if (!rclone.isEmpty()) {
      QTemporaryDir dest;
      QVERIFY(dest.isValid());
      const QString from = QDir::toNativeSeparators(source.path());
      const QStringList args = VssSnapshot::WithSource(
          {"copy", from, dest.path()}, session->pathFor(from));
      QProcess copy;
      copy.setProcessChannelMode(QProcess::MergedChannels);
      copy.start(rclone, args);
      QVERIFY(copy.waitForFinished(120000));
      QVERIFY2(copy.exitCode() == 0, copy.readAll().constData());
      QCOMPARE(ReadFile(QDir(dest.path()).filePath("locked.pst")), content);
    }

    owned.reset();
    QTRY_VERIFY_WITH_TIMEOUT(
        QFileInfo::exists(VssSnapshot::ReleasedFile(sessionDir)), 60000);
    QVERIFY(!PathEntryExists(VssSnapshot::LinkPath(sessionDir)));
    const QSettings ledger(
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\RcloneBrowserNG\\VssSnapshots",
        QSettings::Registry64Format);
    QVERIFY2(!ledger.contains(shadowId), qPrintable(shadowId));
#endif
  }
};

QTEST_GUILESS_MAIN(VssSnapshotTest)
#include "vss_snapshot_test.moc"
