#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTemporaryDir;
class QTimer;
class QWidget;

// Fetches the current rclone from downloads.rclone.org when none is found,
// checks the PGP signature on rclone's published SHA256SUMS against rclone's
// release key (as `rclone selfupdate` does), checks the archive against that
// list and puts the binary beside the app. A Program Files install can't be
// written without admin rights, so on Windows the app starts an elevated copy
// of itself with
// kHelperFlag to place the file. That helper only ever writes rclone.exe into
// its own folder, and only bytes whose hash matches the one it was given.
namespace RcloneInstaller {

inline constexpr char kHelperFlag[] = "--install-rclone";
inline constexpr char kDefaultBaseUrl[] = "https://downloads.rclone.org";

// rclone's name for this OS and CPU ("windows-amd64", "osx-arm64",
// "linux-arm64"), or empty when rclone publishes no build for it.
QString PlatformTag();
// rclone.exe for a windows-* platform, rclone for the rest.
QString BinaryName(const QString &platform);
// "rclone v1.76.0" (what version.txt holds) gives "1.76.0". Anything else,
// a beta or a captive portal's HTML included, gives an empty string.
QString ParseVersionText(const QByteArray &text);
// rclone-v1.76.0-windows-amd64.zip
QString ArchiveName(const QString &version, const QString &platform);
// The binary's path inside that archive:
// rclone-v1.76.0-windows-amd64/rclone.exe
QString ArchiveMember(const QString &version, const QString &platform);
// The lowercase hex SHA-256 listed for fileName in SHA256SUMS text. The
// download passes only the text a verified signature covers. Empty when the
// file isn't listed or its line is malformed.
QByteArray ChecksumFor(const QByteArray &sums, const QString &fileName);
// The armored keys a download trusts by default: rclone's release key.
QList<QByteArray> RcloneReleaseKeys();

// Where a downloaded rclone goes: the app's own folder on Windows (the
// install or portable folder), a per-user folder elsewhere, since a macOS
// bundle or an AppImage can't take a new file.
QString InstallDir();
// The per-user folder used when the install folder can't be written.
QString FallbackDir();
// An rclone already sitting in InstallDir() or FallbackDir(), else empty.
QString FindDownloaded();
// Whether a file can be created in dir right now (made if missing).
bool CanWriteTo(const QString &dir);

// The system's own unzip for this platform: tar.exe from System32 on
// Windows, /usr/bin/tar on macOS, unzip or bsdtar on Linux. An empty program
// means this system has none.
struct Extractor {
  QString program;
  QStringList arguments;
};
Extractor ExtractorFor(const QString &archive, const QString &member,
                       const QString &outDir);

// The elevated half. Arguments after kHelperFlag: the staged binary and its
// expected SHA-256 in hex. Returns the process exit code.
QStringList HelperArguments(const QString &staged, const QByteArray &sha256);
int RunHelper(const QStringList &arguments);
QString HelperExitMessage(int code);
// Tests point the elevated step at a deployed copy of the app.
void SetHelperProgram(const QString &path);

class Download : public QObject {
  Q_OBJECT

public:
  struct Source {
    QUrl base = QUrl(QString::fromLatin1(kDefaultBaseUrl));
    QString platform = PlatformTag();
    // SHA256SUMS must carry a signature from one of these. Only tests add a
    // key of their own.
    QList<QByteArray> trustedKeys = RcloneReleaseKeys();
  };

  explicit Download(const Source &source, QObject *parent = nullptr);
  ~Download() override;

  // Downloads, verifies and installs into destDir. When destDir can't be
  // written, Windows asks once for admin rights if destDir is the helper's
  // own folder, and the binary goes to FallbackDir() if that is refused.
  void start(const QString &destDir);
  void cancel();

  QString version() const { return mVersion; }
  // Set when the binary went somewhere other than the folder asked for.
  QString note() const { return mNote; }

signals:
  void stage(const QString &text);
  void progress(qint64 received, qint64 total);
  void finished(const QString &rclonePath);
  void failed(const QString &error);

private:
  QUrl at(const QString &path) const;
  void get(const QUrl &url, std::function<void(const QByteArray &)> done,
           bool reportProgress = false);
  void fetchSums();
  void fetchArchive();
  void extract(const QByteArray &archive);
  void install(const QString &extracted);
  void installElevated(const QString &staged);
  void installFallback(const QString &reason);
  bool writeBinary(const QString &dir, QString *path, QString *error);
  void succeed(const QString &path);
  void fail(const QString &error);
  void stopWork();

  Source mSource;
  QString mDestDir;
  QString mVersion;
  QByteArray mExpected;
  QByteArray mBinary;
  QByteArray mBinarySha;
  QString mNote;
  bool mDone = false;
  QNetworkAccessManager *mNetwork = nullptr;
  QNetworkReply *mReply = nullptr;
  QProcess *mExtractor = nullptr;
  QTimer *mHelperPoll = nullptr;
  void *mHelperProcess = nullptr;
  std::unique_ptr<QTemporaryDir> mWork;
};

enum class SetupChoice { Installed, ChooseManually, Later };

// The question a first start asks when there's no rclone: download it, pick
// one by hand, or not now. Download shows a progress dialog and reports the
// installed path. Failures are shown to the user and come back as
// ChooseManually, so the caller can open Preferences as before.
void OfferDownload(
    QWidget *parent, const Download::Source &source, const QString &destDir,
    std::function<void(SetupChoice choice, const QString &rclonePath)> done);

// The download itself, behind a progress dialog with Cancel: what the
// question's Download button runs, and how the update prompt replaces an
// rclone that selfupdate can't (one in a folder this account can't write).
// Reports Installed, Later on Cancel, or ChooseManually after showing the
// failure.
void RunDownload(
    QWidget *parent, const Download::Source &source, const QString &destDir,
    std::function<void(SetupChoice choice, const QString &rclonePath)> done);

} // namespace RcloneInstaller
