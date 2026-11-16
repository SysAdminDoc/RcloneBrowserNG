#include "rclone_installer.h"
#include "pgp_test_signer.h"
#include "rclone_security_floor.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

namespace {

quint32 Crc32(const QByteArray &data) {
  quint32 crc = 0xFFFFFFFFu;
  for (const char c : data) {
    crc ^= static_cast<quint8>(c);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

void Put16(QByteArray &out, quint16 value) {
  out.append(static_cast<char>(value & 0xFF));
  out.append(static_cast<char>((value >> 8) & 0xFF));
}

void Put32(QByteArray &out, quint32 value) {
  Put16(out, static_cast<quint16>(value & 0xFFFF));
  Put16(out, static_cast<quint16>(value >> 16));
}

// A zip with every entry stored uncompressed, laid out the way rclone's
// release zips are: one folder holding the binary and its docs.
QByteArray StoredZip(const QList<QPair<QString, QByteArray>> &entries) {
  QByteArray zip;
  QByteArray central;
  for (const auto &entry : entries) {
    const QByteArray name = entry.first.toUtf8();
    const QByteArray &data = entry.second;
    const quint32 crc = Crc32(data);
    const quint32 offset = static_cast<quint32>(zip.size());
    Put32(zip, 0x04034b50u);
    Put16(zip, 20);
    Put16(zip, 0);
    Put16(zip, 0);
    Put16(zip, 0);
    Put16(zip, 0x21);
    Put32(zip, crc);
    Put32(zip, static_cast<quint32>(data.size()));
    Put32(zip, static_cast<quint32>(data.size()));
    Put16(zip, static_cast<quint16>(name.size()));
    Put16(zip, 0);
    zip.append(name);
    zip.append(data);

    Put32(central, 0x02014b50u);
    Put16(central, 20);
    Put16(central, 20);
    Put16(central, 0);
    Put16(central, 0);
    Put16(central, 0);
    Put16(central, 0x21);
    Put32(central, crc);
    Put32(central, static_cast<quint32>(data.size()));
    Put32(central, static_cast<quint32>(data.size()));
    Put16(central, static_cast<quint16>(name.size()));
    Put16(central, 0);
    Put16(central, 0);
    Put16(central, 0);
    Put16(central, 0);
    Put32(central, 0);
    Put32(central, offset);
    central.append(name);
  }
  const quint32 centralOffset = static_cast<quint32>(zip.size());
  zip.append(central);
  Put32(zip, 0x06054b50u);
  Put16(zip, 0);
  Put16(zip, 0);
  Put16(zip, static_cast<quint16>(entries.size()));
  Put16(zip, static_cast<quint16>(entries.size()));
  Put32(zip, static_cast<quint32>(central.size()));
  Put32(zip, centralOffset);
  Put16(zip, 0);
  return zip;
}

QByteArray Sha256Hex(const QByteArray &data) {
  return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

// Just enough of downloads.rclone.org: fixed answers per path.
class FakeRelease {
public:
  FakeRelease() {
    QObject::connect(&mServer, &QTcpServer::newConnection, &mServer,
                     [this]() { accept(); });
    mServer.listen(QHostAddress::LocalHost, 0);
  }

  QUrl base() const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(mServer.serverPort()));
  }

  void serve(const QString &path, const QByteArray &body, int status = 200) {
    mRoutes.insert(path, qMakePair(status, body));
  }

  // A release whose zip holds binary under rclone's real layout, with a
  // SHA256SUMS signed by the test key.
  QByteArray publish(const QString &version, const QString &platform,
                     const QByteArray &binary, bool corruptSums = false) {
    const QByteArray zip = StoredZip(
        {{QStringLiteral("rclone-v%1-%2/README.txt").arg(version, platform),
          QByteArray("read me\n")},
         {RcloneInstaller::ArchiveMember(version, platform), binary}});
    const QString archive = RcloneInstaller::ArchiveName(version, platform);
    QByteArray hash = Sha256Hex(zip);
    if (corruptSums) {
      hash = Sha256Hex("something else");
    }
    serve(QStringLiteral("/version.txt"),
          QStringLiteral("rclone v%1\n").arg(version).toUtf8());
    serve(QStringLiteral("/v%1/SHA256SUMS").arg(version),
          PgpTestSigner::Clearsign(SumsText(version, archive, hash)));
    serve(QStringLiteral("/v%1/%2").arg(version, archive), zip);
    return zip;
  }

  // The list's text before signing: another platform's line, then archive's.
  static QByteArray SumsText(const QString &version, const QString &archive,
                             const QByteArray &hash) {
    return Sha256Hex("other") + "  rclone-v" + version.toUtf8() +
           "-aix-ppc64.zip\n" + hash + "  " + archive.toUtf8() + "\n";
  }

  int hits(const QString &path) const { return mHits.value(path); }

private:
  void accept() {
    while (QTcpSocket *socket = mServer.nextPendingConnection()) {
      auto buffer = std::make_shared<QByteArray>();
      QObject::connect(socket, &QTcpSocket::readyRead, socket,
                       [this, socket, buffer]() {
                         buffer->append(socket->readAll());
                         if (!buffer->contains("\r\n\r\n")) {
                           return;
                         }
                         const QList<QByteArray> first =
                             buffer->left(buffer->indexOf("\r\n")).split(' ');
                         const QString path =
                             first.size() > 1 ? QString::fromUtf8(first.at(1))
                                              : QString();
                         mHits[path] += 1;
                         const auto route = mRoutes.value(
                             path, qMakePair(404, QByteArray("not found")));
                         QByteArray response =
                             "HTTP/1.1 " + QByteArray::number(route.first) +
                             (route.first == 200 ? " OK" : " Not Found") +
                             "\r\nContent-Length: " +
                             QByteArray::number(route.second.size()) +
                             "\r\nConnection: close\r\n\r\n";
                         response.append(route.second);
                         socket->write(response);
                         socket->disconnectFromHost();
                       });
      QObject::connect(socket, &QTcpSocket::disconnected, socket,
                       &QObject::deleteLater);
    }
  }

  QTcpServer mServer;
  QHash<QString, QPair<int, QByteArray>> mRoutes;
  QHash<QString, int> mHits;
};

// The fake release, trusting the test key that signs its lists. A plain
// {base, platform} source trusts only rclone's own key, as the app does.
RcloneInstaller::Download::Source Trusting(const FakeRelease &release,
                                           const QString &platform) {
  RcloneInstaller::Download::Source source{release.base(), platform};
  source.trustedKeys = {QByteArray(PgpTestSigner::kPublicKey)};
  return source;
}

struct Outcome {
  QString path;
  QString error;
  bool ended = false;
};

Outcome Run(RcloneInstaller::Download &download, const QString &dest,
            int timeoutMs = 30000) {
  auto outcome = std::make_shared<Outcome>();
  QObject::connect(&download, &RcloneInstaller::Download::finished,
                   [outcome](const QString &path) {
                     outcome->path = path;
                     outcome->ended = true;
                   });
  QObject::connect(&download, &RcloneInstaller::Download::failed,
                   [outcome](const QString &error) {
                     outcome->error = error;
                     outcome->ended = true;
                   });
  download.start(dest);
  if (!QTest::qWaitFor([outcome]() { return outcome->ended; }, timeoutMs)) {
    outcome->error = QStringLiteral("timed out");
  }
  return *outcome;
}

// The helper's command line as Windows hands it over: program first.
QStringList HelperCall(const QString &staged, const QByteArray &sha256) {
  return QStringList{QCoreApplication::applicationFilePath()} +
         RcloneInstaller::HelperArguments(staged, sha256);
}

QByteArray ReadAll(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Clicks the button labelled text on the first message box that has one,
// after saving a picture of the box when RB_GRAB_DIR names a folder. Boxes
// without that button are left for another answer.
void AnswerNextQuestion(const QString &text, const QString &grabAs) {
  auto *poll = new QTimer(qApp);
  auto waited = std::make_shared<QElapsedTimer>();
  waited->start();
  poll->setInterval(20);
  QObject::connect(poll, &QTimer::timeout, poll, [poll, waited, text, grabAs]() {
    if (waited->hasExpired(60000)) {
      poll->stop();
      poll->deleteLater();
      return;
    }
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
    if (box == nullptr) {
      return;
    }
    for (QAbstractButton *button : box->buttons()) {
      if (button->text().remove(QLatin1Char('&')) != text) {
        continue;
      }
      poll->stop();
      poll->deleteLater();
      const QString grabDir = qEnvironmentVariable("RB_GRAB_DIR");
      if (!grabDir.isEmpty() && !grabAs.isEmpty()) {
        box->grab().save(QDir(grabDir).filePath(grabAs));
      }
      button->click();
      return;
    }
  });
  poll->start();
}

} // namespace

class RcloneInstallerTest : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("rbng-test"));
    QCoreApplication::setApplicationName(QStringLiteral("rclone_installer_test"));
    QDir(RcloneInstaller::FallbackDir()).removeRecursively();
  }

  void cleanupTestCase() {
    QDir(RcloneInstaller::FallbackDir()).removeRecursively();
  }

  void parsesTheVersionFile() {
    QCOMPARE(RcloneInstaller::ParseVersionText("rclone v1.76.0\n"),
             QStringLiteral("1.76.0"));
    QCOMPARE(RcloneInstaller::ParseVersionText("  rclone v1.80.12\r\n"),
             QStringLiteral("1.80.12"));
    QVERIFY(RcloneInstaller::ParseVersionText("rclone v1.77.0-beta.9000")
                .isEmpty());
    QVERIFY(RcloneInstaller::ParseVersionText("<html>Sign in</html>")
                .isEmpty());
    QVERIFY(RcloneInstaller::ParseVersionText("").isEmpty());
  }

  void namesTheArchiveTheWayRcloneDoes() {
    QCOMPARE(RcloneInstaller::ArchiveName("1.76.0", "windows-amd64"),
             QStringLiteral("rclone-v1.76.0-windows-amd64.zip"));
    QCOMPARE(RcloneInstaller::ArchiveMember("1.76.0", "windows-amd64"),
             QStringLiteral("rclone-v1.76.0-windows-amd64/rclone.exe"));
    QCOMPARE(RcloneInstaller::ArchiveMember("1.76.0", "osx-arm64"),
             QStringLiteral("rclone-v1.76.0-osx-arm64/rclone"));
  }

  void readsTheChecksumFromASignedList() {
    // Lines from rclone's real v1.76.0 SHA256SUMS.
    const QByteArray sums =
        "-----BEGIN PGP SIGNED MESSAGE-----\n"
        "Hash: SHA1\n\n"
        "b9c8d062ff61e2addfc31c1981219d90f99e0012cba45b649f728082ad810d5c  "
        "rclone-v1.76.0-windows-386.zip\n"
        "8B4FD2737ADE4E70C057EF4A49DEB7CFFE94B0FB9D7587C189D149823F21A1DC  "
        "rclone-v1.76.0-windows-amd64.zip\r\n"
        "0123  rclone-v1.76.0-windows-arm64.zip\n"
        "-----BEGIN PGP SIGNATURE-----\n";
    QCOMPARE(RcloneInstaller::ChecksumFor(sums, "rclone-v1.76.0-windows-amd64.zip"),
             QByteArray("8b4fd2737ade4e70c057ef4a49deb7cffe94b0fb9d7587c189d149823f21a1dc"));
    QVERIFY(RcloneInstaller::ChecksumFor(sums, "rclone-v1.76.0-windows-arm64.zip")
                .isEmpty());
    QVERIFY(RcloneInstaller::ChecksumFor(sums, "rclone-v1.76.0-linux-amd64.zip")
                .isEmpty());
    QVERIFY(RcloneInstaller::ChecksumFor(sums, "windows-amd64.zip").isEmpty());
  }

  void knowsThisMachine() {
    const QString platform = RcloneInstaller::PlatformTag();
#if defined(Q_OS_WIN32)
    QVERIFY2(platform.startsWith("windows-"), qPrintable(platform));
    QCOMPARE(RcloneInstaller::BinaryName(platform), QStringLiteral("rclone.exe"));
    QCOMPARE(RcloneInstaller::InstallDir(), QCoreApplication::applicationDirPath());
#else
    QCOMPARE(RcloneInstaller::BinaryName(platform), QStringLiteral("rclone"));
    QCOMPARE(RcloneInstaller::InstallDir(), RcloneInstaller::FallbackDir());
#endif
    QVERIFY2(!platform.isEmpty(), "rclone builds for every machine this test runs on");
  }

  void downloadsChecksAndInstalls() {
    const QString platform = RcloneInstaller::PlatformTag();
    if (RcloneInstaller::ExtractorFor("a.zip", "m", "out").program.isEmpty()) {
      QSKIP("This system has no unzip or bsdtar.");
    }
    FakeRelease release;
    const QByteArray binary = "the rclone binary, more or less\n";
    release.publish("9.9.9", platform, binary);
    QTemporaryDir dest;
    QVERIFY(dest.isValid());

    RcloneInstaller::Download download(Trusting(release, platform));
    QSignalSpy progress(&download, &RcloneInstaller::Download::progress);
    const Outcome outcome = Run(download, dest.path());
    QVERIFY2(outcome.error.isEmpty(), qPrintable(outcome.error));
    const QString expected = QDir(dest.path()).filePath(
        RcloneInstaller::BinaryName(platform));
    QCOMPARE(QDir::cleanPath(outcome.path), QDir::cleanPath(expected));
    QCOMPARE(ReadAll(expected), binary);
    QVERIFY(download.note().isEmpty());
    QCOMPARE(download.version(), QStringLiteral("9.9.9"));
    QVERIFY(progress.count() > 0);
    // Only the binary lands in the folder, not the docs beside it.
    QCOMPARE(QDir(dest.path()).entryList(QDir::Files | QDir::NoDotAndDotDot),
             QStringList{RcloneInstaller::BinaryName(platform)});
#if !defined(Q_OS_WIN32)
    QVERIFY(QFileInfo(expected).isExecutable());
#endif
  }

  void throwsAwayADownloadThatDoesNotMatch() {
    const QString platform = RcloneInstaller::PlatformTag();
    FakeRelease release;
    release.publish("9.9.9", platform, "tampered", true);
    QTemporaryDir dest;
    RcloneInstaller::Download download(Trusting(release, platform));
    const Outcome outcome = Run(download, dest.path());
    QVERIFY(outcome.path.isEmpty());
    QVERIFY2(outcome.error.contains("checksum"), qPrintable(outcome.error));
    QVERIFY(QDir(dest.path()).isEmpty());
    QCOMPARE(release.hits(QStringLiteral("/v9.9.9/") +
                          RcloneInstaller::ArchiveName("9.9.9", platform)),
             1);
  }

  void stopsWhenTheReleaseIsNotListed() {
    const QString platform = RcloneInstaller::PlatformTag();
    FakeRelease release;
    release.publish("9.9.9", platform, "x");
    release.serve("/v9.9.9/SHA256SUMS",
                  PgpTestSigner::Clearsign(
                      Sha256Hex("x") + "  rclone-v9.9.9-plan9-amd64.zip\n"));
    QTemporaryDir dest;
    RcloneInstaller::Download download(Trusting(release, platform));
    const Outcome outcome = Run(download, dest.path());
    QVERIFY2(outcome.error.contains("no entry"), qPrintable(outcome.error));
    QCOMPARE(release.hits(QStringLiteral("/v9.9.9/") +
                          RcloneInstaller::ArchiveName("9.9.9", platform)),
             0);
  }

  // The app trusts rclone's key alone, so a list signed by any other key,
  // here the test key, stops the download before the zip is fetched.
  void refusesAListRcloneDidNotSign() {
    const QString platform = RcloneInstaller::PlatformTag();
    FakeRelease release;
    release.publish("9.9.9", platform, "signed by someone else");
    QTemporaryDir dest;
    RcloneInstaller::Download download({release.base(), platform});
    const Outcome outcome = Run(download, dest.path());
    QVERIFY(outcome.path.isEmpty());
    QVERIFY2(outcome.error.contains("signature check"),
             qPrintable(outcome.error));
    QVERIFY(QDir(dest.path()).isEmpty());
    QCOMPARE(release.hits(QStringLiteral("/v9.9.9/") +
                          RcloneInstaller::ArchiveName("9.9.9", platform)),
             0);
  }

  // A server that swaps the zip and the hash beside it. The new hash
  // matches the new zip, so only the signature can catch it.
  void refusesAListChangedAfterSigning() {
    const QString platform = RcloneInstaller::PlatformTag();
    FakeRelease release;
    const QByteArray zip = release.publish("9.9.9", platform, "swapped in");
    const QString archive = RcloneInstaller::ArchiveName("9.9.9", platform);
    const QByteArray genuine = Sha256Hex("the zip that was signed");
    QByteArray tampered = PgpTestSigner::Clearsign(
        FakeRelease::SumsText("9.9.9", archive, genuine));
    QVERIFY(tampered.contains(genuine));
    tampered.replace(genuine, Sha256Hex(zip));
    release.serve("/v9.9.9/SHA256SUMS", tampered);
    QTemporaryDir dest;
    RcloneInstaller::Download download(Trusting(release, platform));
    const Outcome outcome = Run(download, dest.path());
    QVERIFY(outcome.path.isEmpty());
    QVERIFY2(outcome.error.contains("signature check"),
             qPrintable(outcome.error));
    QVERIFY(QDir(dest.path()).isEmpty());
    QCOMPARE(release.hits(QStringLiteral("/v9.9.9/") + archive), 0);
  }

  void refusesAnUnsignedList() {
    const QString platform = RcloneInstaller::PlatformTag();
    FakeRelease release;
    const QByteArray zip = release.publish("9.9.9", platform, "unsigned");
    const QString archive = RcloneInstaller::ArchiveName("9.9.9", platform);
    release.serve("/v9.9.9/SHA256SUMS",
                  FakeRelease::SumsText("9.9.9", archive, Sha256Hex(zip)));
    QTemporaryDir dest;
    RcloneInstaller::Download download(Trusting(release, platform));
    const Outcome outcome = Run(download, dest.path());
    QVERIFY2(outcome.error.contains("signature check"),
             qPrintable(outcome.error));
    QVERIFY(QDir(dest.path()).isEmpty());
    QCOMPARE(release.hits(QStringLiteral("/v9.9.9/") + archive), 0);
  }

  // version.txt isn't signed and old releases carry real signatures, so a
  // server could otherwise steer the download back to an rclone with known
  // holes. Nothing below the security floor is fetched.
  void refusesAnRcloneBelowTheSecurityFloor() {
    const QString platform = RcloneInstaller::PlatformTag();
    FakeRelease release;
    release.publish("1.70.0", platform, "an old rclone");
    QTemporaryDir dest;
    RcloneInstaller::Download download(Trusting(release, platform));
    const Outcome outcome = Run(download, dest.path());
    QVERIFY(outcome.path.isEmpty());
    QVERIFY(RcloneSecurityFloor::IsBelowFloor("1.70.0"));
    QVERIFY2(outcome.error.contains(
                 "older than v" +
                 QString::fromLatin1(RcloneSecurityFloor::kMinimumVersion)),
             qPrintable(outcome.error));
    QVERIFY(QDir(dest.path()).isEmpty());
    QCOMPARE(release.hits(QStringLiteral("/v1.70.0/SHA256SUMS")), 0);
  }

  void reportsAServerWithoutRclone() {
    FakeRelease release;
    QTemporaryDir dest;
    RcloneInstaller::Download missing({release.base(), "windows-amd64"});
    const Outcome gone = Run(missing, dest.path());
    QVERIFY2(gone.error.startsWith("Couldn't download"), qPrintable(gone.error));

    release.serve("/version.txt", "<html>Please sign in to the Wi-Fi</html>");
    RcloneInstaller::Download portal({release.base(), "windows-amd64"});
    const Outcome captive = Run(portal, dest.path());
    QVERIFY2(captive.error.contains("didn't answer with an rclone version"),
             qPrintable(captive.error));
    QVERIFY(QDir(dest.path()).isEmpty());
  }

  void refusesAMachineRcloneDoesNotBuildFor() {
    RcloneInstaller::Download download(
        {QUrl("http://127.0.0.1:9"), QString()});
    QTemporaryDir dest;
    const Outcome outcome = Run(download, dest.path());
    QVERIFY2(outcome.error.contains("doesn't publish a build"),
             qPrintable(outcome.error));
  }

  void savesToTheUserFolderWhenTheTargetCannotBeWritten() {
    const QString platform = RcloneInstaller::PlatformTag();
    if (RcloneInstaller::ExtractorFor("a.zip", "m", "out").program.isEmpty()) {
      QSKIP("This system has no unzip or bsdtar.");
    }
    FakeRelease release;
    release.publish("9.9.9", platform, "fallback binary");
    QTemporaryDir scratch;
    // A folder inside a file can never be made.
    const QString blocker = scratch.filePath("plain-file");
    {
      QFile file(blocker);
      QVERIFY(file.open(QIODevice::WriteOnly));
    }
    const QString dest = blocker + "/rclone-here";
    QVERIFY(!RcloneInstaller::CanWriteTo(dest));

    RcloneInstaller::Download download(Trusting(release, platform));
    const Outcome outcome = Run(download, dest);
    QVERIFY2(outcome.error.isEmpty(), qPrintable(outcome.error));
    const QString expected = QDir(RcloneInstaller::FallbackDir())
                                 .filePath(RcloneInstaller::BinaryName(platform));
    QCOMPARE(QDir::cleanPath(outcome.path), QDir::cleanPath(expected));
    QCOMPARE(ReadAll(expected), QByteArray("fallback binary"));
    QVERIFY2(download.note().contains("instead"), qPrintable(download.note()));
    QCOMPARE(QDir::cleanPath(RcloneInstaller::FindDownloaded()),
             QDir::cleanPath(expected));
    QDir(RcloneInstaller::FallbackDir()).removeRecursively();
  }

  void helperWritesOnlyWhatItWasToldTo() {
    const QString target = QDir(QCoreApplication::applicationDirPath())
                               .filePath(RcloneInstaller::BinaryName(
                                   RcloneInstaller::PlatformTag()));
    if (QFileInfo::exists(target)) {
      QSKIP("An rclone already sits beside this test; leaving it alone.");
    }
    QTemporaryDir scratch;
    const QString staged = scratch.filePath("staged");
    const QByteArray data = "staged rclone";
    {
      QFile file(staged);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(data);
    }
    QCOMPARE(RcloneInstaller::RunHelper(
                 {QCoreApplication::applicationFilePath(),
                  QString::fromLatin1(RcloneInstaller::kHelperFlag)}),
             2);
    QCOMPARE(RcloneInstaller::RunHelper(HelperCall(staged, "abc")), 2);
    QCOMPARE(RcloneInstaller::RunHelper(HelperCall(staged, Sha256Hex("other"))),
             3);
    QVERIFY(!QFileInfo::exists(target));
    QCOMPARE(RcloneInstaller::RunHelper(
                 HelperCall(scratch.filePath("gone"), Sha256Hex(data))),
             4);

    QCOMPARE(RcloneInstaller::RunHelper(
                 HelperCall(staged, Sha256Hex(data).toUpper())),
             0);
    const QByteArray written = ReadAll(target);
    QFile::remove(target);
    QCOMPARE(written, data);
    QVERIFY(RcloneInstaller::HelperExitMessage(3).contains("changed"));
  }

  void theQuestionDownloadsWhenAsked() {
    const QString platform = RcloneInstaller::PlatformTag();
    if (RcloneInstaller::ExtractorFor("a.zip", "m", "out").program.isEmpty()) {
      QSKIP("This system has no unzip or bsdtar.");
    }
    FakeRelease release;
    release.publish("9.9.9", platform, "asked for");
    QTemporaryDir dest;
    auto choice = std::make_shared<int>(-1);
    auto path = std::make_shared<QString>();
    AnswerNextQuestion("Download rclone", "rclone-not-found.png");
    RcloneInstaller::OfferDownload(
        nullptr, Trusting(release, platform), dest.path(),
        [choice, path](RcloneInstaller::SetupChoice made, const QString &where) {
          *choice = static_cast<int>(made);
          *path = where;
        });
    QTRY_VERIFY_WITH_TIMEOUT(*choice != -1, 30000);
    QCOMPARE(*choice, static_cast<int>(RcloneInstaller::SetupChoice::Installed));
    QCOMPARE(ReadAll(*path), QByteArray("asked for"));
  }

  // The update prompt's path: an older rclone in the folder gets replaced.
  void anUpdateReplacesTheOlderRclone() {
    const QString platform = RcloneInstaller::PlatformTag();
    if (RcloneInstaller::ExtractorFor("a.zip", "m", "out").program.isEmpty()) {
      QSKIP("This system has no unzip or bsdtar.");
    }
    FakeRelease release;
    release.publish("9.9.9", platform, "the newer one");
    QTemporaryDir dest;
    const QString old = dest.filePath(RcloneInstaller::BinaryName(platform));
    {
      QFile file(old);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("the older one");
    }
    auto choice = std::make_shared<int>(-1);
    auto path = std::make_shared<QString>();
    RcloneInstaller::RunDownload(
        nullptr, Trusting(release, platform), dest.path(),
        [choice, path](RcloneInstaller::SetupChoice made, const QString &where) {
          *choice = static_cast<int>(made);
          *path = where;
        });
    QTRY_VERIFY_WITH_TIMEOUT(*choice != -1, 30000);
    QCOMPARE(*choice, static_cast<int>(RcloneInstaller::SetupChoice::Installed));
    QCOMPARE(QDir::cleanPath(*path), QDir::cleanPath(old));
    QCOMPARE(ReadAll(old), QByteArray("the newer one"));
  }

  void theQuestionCanBeAnsweredOtherwise() {
    QTemporaryDir dest;
    auto choice = std::make_shared<int>(-1);
    auto record = [choice](RcloneInstaller::SetupChoice made, const QString &) {
      *choice = static_cast<int>(made);
    };
    AnswerNextQuestion("Choose rclone...", QString());
    RcloneInstaller::OfferDownload(nullptr, {QUrl("http://127.0.0.1:9"), "windows-amd64"},
                                   dest.path(), record);
    QCOMPARE(*choice,
             static_cast<int>(RcloneInstaller::SetupChoice::ChooseManually));

    *choice = -1;
    AnswerNextQuestion("Not now", QString());
    RcloneInstaller::OfferDownload(nullptr, {QUrl("http://127.0.0.1:9"), "windows-amd64"},
                                   dest.path(), record);
    QCOMPARE(*choice, static_cast<int>(RcloneInstaller::SetupChoice::Later));
    QVERIFY(QDir(dest.path()).isEmpty());
  }

  void aFailedDownloadSendsYouToPreferences() {
    FakeRelease release;
    QTemporaryDir dest;
    auto choice = std::make_shared<int>(-1);
    AnswerNextQuestion("Download rclone", QString());
    AnswerNextQuestion("OK", "rclone-download-failed.png");
    RcloneInstaller::OfferDownload(
        nullptr, {release.base(), "windows-amd64"}, dest.path(),
        [choice](RcloneInstaller::SetupChoice made, const QString &) {
          *choice = static_cast<int>(made);
        });
    QTRY_VERIFY_WITH_TIMEOUT(*choice != -1, 30000);
    QCOMPARE(*choice,
             static_cast<int>(RcloneInstaller::SetupChoice::ChooseManually));
  }

  // Needs a deployed copy of the app (windeployqt) in a folder this account
  // can't write, such as one under Program Files. Elevation shows a UAC
  // prompt unless the machine is set to elevate without one.
  void elevatedHelperFillsAProtectedFolder() {
#if defined(Q_OS_WIN32)
    const QString app = qEnvironmentVariable("RB_RCLONE_INSTALL_E2E_APP");
    if (app.isEmpty()) {
      QSKIP("Set RB_RCLONE_INSTALL_E2E_APP to a deployed RcloneBrowser.exe in "
            "a protected folder to run this.");
    }
    const QString folder = QFileInfo(app).absolutePath();
    QVERIFY2(!RcloneInstaller::CanWriteTo(folder),
             "The folder has to be one this account can't write.");
    RcloneInstaller::SetHelperProgram(app);
    auto restore = qScopeGuard([] { RcloneInstaller::SetHelperProgram(QString()); });
    // The second round replaces the first, the way an update does.
    for (const QByteArray &round : {QByteArray("first"), QByteArray("second")}) {
      FakeRelease release;
      const QByteArray binary =
          round + " " + QByteArray::number(QDateTime::currentMSecsSinceEpoch());
      release.publish("9.9.9", RcloneInstaller::PlatformTag(), binary);
      RcloneInstaller::Download download(
          Trusting(release, RcloneInstaller::PlatformTag()));
      const Outcome outcome = Run(download, folder, 90000);
      QVERIFY2(outcome.error.isEmpty(), qPrintable(outcome.error));
      QVERIFY2(download.note().isEmpty(), qPrintable(download.note()));
      QCOMPARE(QDir::cleanPath(outcome.path),
               QDir::cleanPath(QDir(folder).filePath("rclone.exe")));
      QCOMPARE(ReadAll(outcome.path), binary);
    }
#else
    QSKIP("Windows only.");
#endif
  }

  // The real thing, from downloads.rclone.org. Off unless asked for.
  void downloadsTheRealRclone() {
    if (qEnvironmentVariable("RB_RCLONE_LIVE_DOWNLOAD") != "1") {
      QSKIP("Set RB_RCLONE_LIVE_DOWNLOAD=1 to fetch rclone from rclone.org.");
    }
    QTemporaryDir dest;
    RcloneInstaller::Download download(RcloneInstaller::Download::Source{});
    const Outcome outcome = Run(download, dest.path(), 600000);
    QVERIFY2(outcome.error.isEmpty(), qPrintable(outcome.error));
    QProcess rclone;
    rclone.start(outcome.path, {"version"});
    QVERIFY(rclone.waitForFinished(60000));
    const QString printed = QString::fromUtf8(rclone.readAllStandardOutput());
    QVERIFY2(printed.startsWith("rclone v" + download.version()),
             qPrintable(printed));
  }
};

QTEST_MAIN(RcloneInstallerTest)
#include "rclone_installer_test.moc"
