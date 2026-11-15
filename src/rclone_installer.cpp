#include "rclone_installer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <chrono>
#include <utility>

#if defined(Q_OS_WIN32)
#include <qt_windows.h>
#include <shellapi.h>
#endif

namespace RcloneInstaller {

namespace {

// rclone.exe is about 90 MB and its zip about 32 MB. Anything far past that
// isn't an rclone release.
constexpr qint64 kMaxDownload = 512LL * 1024 * 1024;
constexpr int kTransferTimeoutMs = 30000;
constexpr int kExtractTimeoutMs = 120000;
constexpr int kHelperTimeoutMs = 60000;

QString &HelperOverride() {
  static QString path;
  return path;
}

#if defined(Q_OS_WIN32)
QString HelperProgram() {
  return HelperOverride().isEmpty() ? QCoreApplication::applicationFilePath()
                                    : HelperOverride();
}
#endif

bool SameDir(const QString &a, const QString &b) {
  const QString left = QDir::cleanPath(QFileInfo(a).absoluteFilePath());
  const QString right = QDir::cleanPath(QFileInfo(b).absoluteFilePath());
#if defined(Q_OS_WIN32)
  return left.compare(right, Qt::CaseInsensitive) == 0;
#else
  return left == right;
#endif
}

QString Megabytes(qint64 bytes) {
  return QLocale().toString(static_cast<double>(bytes) / (1024.0 * 1024.0),
                            'f', 1);
}

#if defined(Q_OS_WIN32)
QString QuoteForShell(const QString &argument) {
  if (!argument.isEmpty() &&
      !argument.contains(QRegularExpression(QStringLiteral("[\\s\"]")))) {
    return argument;
  }
  // Windows paths can't hold a quote, so only a trailing backslash needs
  // doubling before the closing quote.
  QString body = argument;
  qsizetype trailing = 0;
  while (trailing < body.size() &&
         body.at(body.size() - 1 - trailing) == QLatin1Char('\\')) {
    ++trailing;
  }
  body += QString(trailing, QLatin1Char('\\'));
  return QLatin1Char('"') + body + QLatin1Char('"');
}
#endif

} // namespace

QString PlatformTag() {
  QString os;
#if defined(Q_OS_WIN32)
  os = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
  os = QStringLiteral("osx");
#elif defined(Q_OS_LINUX)
  os = QStringLiteral("linux");
#elif defined(Q_OS_FREEBSD)
  os = QStringLiteral("freebsd");
#else
  return QString();
#endif
  const QString cpu = QSysInfo::currentCpuArchitecture();
  QString arch;
  if (cpu == QLatin1String("x86_64")) {
    arch = QStringLiteral("amd64");
  } else if (cpu == QLatin1String("arm64")) {
    arch = QStringLiteral("arm64");
  } else if (cpu == QLatin1String("i386")) {
    arch = QStringLiteral("386");
  } else if (cpu == QLatin1String("arm") && os != QLatin1String("windows") &&
             os != QLatin1String("osx")) {
    arch = QStringLiteral("arm-v7");
  } else {
    return QString();
  }
  return os + QLatin1Char('-') + arch;
}

QString BinaryName(const QString &platform) {
  return platform.startsWith(QLatin1String("windows-"))
             ? QStringLiteral("rclone.exe")
             : QStringLiteral("rclone");
}

QString ParseVersionText(const QByteArray &text) {
  static const QRegularExpression pattern(
      QStringLiteral(R"(^rclone v(\d+\.\d+\.\d+)$)"));
  const QRegularExpressionMatch match =
      pattern.match(QString::fromUtf8(text).trimmed());
  return match.hasMatch() ? match.captured(1) : QString();
}

QString ArchiveName(const QString &version, const QString &platform) {
  return QStringLiteral("rclone-v%1-%2.zip").arg(version, platform);
}

QString ArchiveMember(const QString &version, const QString &platform) {
  return QStringLiteral("rclone-v%1-%2/%3")
      .arg(version, platform, BinaryName(platform));
}

QByteArray ChecksumFor(const QByteArray &sums, const QString &fileName) {
  static const QRegularExpression line(
      QStringLiteral(R"(^([0-9A-Fa-f]{64}) [ *](.+)$)"));
  for (const QByteArray &raw : sums.split('\n')) {
    const QRegularExpressionMatch match =
        line.match(QString::fromUtf8(raw).trimmed());
    if (match.hasMatch() && match.captured(2) == fileName) {
      return match.captured(1).toLower().toLatin1();
    }
  }
  return QByteArray();
}

QString InstallDir() {
#if defined(Q_OS_WIN32)
  return QCoreApplication::applicationDirPath();
#else
  return FallbackDir();
#endif
}

QString FallbackDir() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::AppLocalDataLocation))
      .filePath(QStringLiteral("rclone"));
}

QString FindDownloaded() {
  const QString name = BinaryName(PlatformTag());
  for (const QString &dir : {InstallDir(), FallbackDir()}) {
    const QFileInfo candidate(QDir(dir).filePath(name));
    if (candidate.isFile() && candidate.isExecutable()) {
      return candidate.absoluteFilePath();
    }
  }
  return QString();
}

bool CanWriteTo(const QString &dir) {
  if (dir.isEmpty() || !QDir().mkpath(dir)) {
    return false;
  }
  QFile probe(QDir(dir).filePath(
      QStringLiteral(".rclone-browser-write-test-%1")
          .arg(QCoreApplication::applicationPid())));
  if (!probe.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    return false;
  }
  probe.close();
  probe.remove();
  return true;
}

Extractor ExtractorFor(const QString &archive, const QString &member,
                       const QString &outDir) {
  const QStringList tarArguments = {
      QStringLiteral("-xf"), QDir::toNativeSeparators(archive),
      QStringLiteral("-C"), QDir::toNativeSeparators(outDir), member};
#if defined(Q_OS_WIN32)
  // From System32 by full path, never whatever "tar" PATH finds first.
  wchar_t system[MAX_PATH] = {};
  const UINT length = GetSystemDirectoryW(system, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return Extractor();
  }
  const QString tar =
      QDir(QString::fromWCharArray(system, static_cast<int>(length)))
          .filePath(QStringLiteral("tar.exe"));
  if (!QFileInfo::exists(tar)) {
    return Extractor();
  }
  return Extractor{tar, tarArguments};
#elif defined(Q_OS_MACOS)
  return Extractor{QStringLiteral("/usr/bin/tar"), tarArguments};
#else
  const QString unzip = QStandardPaths::findExecutable(QStringLiteral("unzip"));
  if (!unzip.isEmpty()) {
    return Extractor{unzip,
                     {QStringLiteral("-q"), QStringLiteral("-o"), archive,
                      member, QStringLiteral("-d"), outDir}};
  }
  const QString bsdtar =
      QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
  if (!bsdtar.isEmpty()) {
    return Extractor{bsdtar, tarArguments};
  }
  return Extractor();
#endif
}

QStringList HelperArguments(const QString &staged, const QByteArray &sha256) {
  return {QString::fromLatin1(kHelperFlag), QDir::toNativeSeparators(staged),
          QString::fromLatin1(sha256)};
}

int RunHelper(const QStringList &arguments) {
  const qsizetype at = arguments.indexOf(QString::fromLatin1(kHelperFlag));
  if (at < 0 || at + 2 >= arguments.size()) {
    return 2;
  }
  const QString staged = arguments.at(at + 1);
  const QByteArray expected = arguments.at(at + 2).toLatin1().toLower();
  if (expected.size() != 64) {
    return 2;
  }
  QFile in(staged);
  if (!in.open(QIODevice::ReadOnly) || in.size() > kMaxDownload) {
    return 4;
  }
  // Hashes the very bytes it writes, so swapping the staged file after the
  // app checked it gets nowhere.
  const QByteArray data = in.readAll();
  if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() !=
      expected) {
    return 3;
  }
  // Only ever beside itself, under rclone's own name.
  QSaveFile out(QDir(QCoreApplication::applicationDirPath())
                    .filePath(BinaryName(PlatformTag())));
  if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() ||
      !out.commit()) {
    return 5;
  }
  return 0;
}

QString HelperExitMessage(int code) {
  switch (code) {
  case 2:
    return QStringLiteral("the administrator step was started wrongly");
  case 3:
    return QStringLiteral(
        "the downloaded file changed before it could be copied");
  case 4:
    return QStringLiteral("the administrator step couldn't read the download");
  case 5:
    return QStringLiteral(
        "the administrator step couldn't write to the folder");
  default:
    return QStringLiteral("the administrator step stopped with code %1")
        .arg(code);
  }
}

void SetHelperProgram(const QString &path) { HelperOverride() = path; }

Download::Download(const Source &source, QObject *parent)
    : QObject(parent), mSource(source),
      mNetwork(new QNetworkAccessManager(this)) {}

Download::~Download() { stopWork(); }

QUrl Download::at(const QString &path) const {
  QUrl url = mSource.base;
  QString base = url.path();
  while (base.endsWith(QLatin1Char('/'))) {
    base.chop(1);
  }
  url.setPath(base + QLatin1Char('/') + path);
  return url;
}

void Download::start(const QString &destDir) {
  mDestDir = destDir;
  mDone = false;
  mNote.clear();
  if (mSource.platform.isEmpty()) {
    fail(QStringLiteral("rclone doesn't publish a build for this system (%1 "
                        "on %2). Install rclone another way and choose it in "
                        "Preferences.")
             .arg(QSysInfo::currentCpuArchitecture(),
                  QSysInfo::prettyProductName()));
    return;
  }
  mWork = std::make_unique<QTemporaryDir>();
  if (!mWork->isValid()) {
    fail(QStringLiteral("Couldn't make a temporary folder for the download: "
                        "%1")
             .arg(mWork->errorString()));
    return;
  }
  emit stage(QStringLiteral("Finding the current rclone..."));
  get(at(QStringLiteral("version.txt")), [this](const QByteArray &body) {
    mVersion = ParseVersionText(body);
    if (mVersion.isEmpty()) {
      fail(QStringLiteral("%1 didn't answer with an rclone version.")
               .arg(mSource.base.host()));
      return;
    }
    fetchSums();
  });
}

void Download::cancel() {
  mDone = true;
  stopWork();
}

void Download::stopWork() {
  if (mReply != nullptr) {
    QNetworkReply *reply = mReply;
    mReply = nullptr;
    reply->abort();
  }
  if (mExtractor != nullptr) {
    mExtractor->disconnect(this);
    mExtractor->kill();
    mExtractor->waitForFinished(2000);
    mExtractor->deleteLater();
    mExtractor = nullptr;
  }
  if (mHelperPoll != nullptr) {
    mHelperPoll->stop();
    mHelperPoll->deleteLater();
    mHelperPoll = nullptr;
  }
#if defined(Q_OS_WIN32)
  if (mHelperProcess != nullptr) {
    CloseHandle(static_cast<HANDLE>(mHelperProcess));
    mHelperProcess = nullptr;
  }
#endif
}

void Download::get(const QUrl &url,
                   std::function<void(const QByteArray &)> done,
                   bool reportProgress) {
  QNetworkRequest request(url);
  // Gives up after this long with no data, however long the whole file takes.
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
  request.setTransferTimeout(std::chrono::milliseconds(kTransferTimeoutMs));
#else
  request.setTransferTimeout(kTransferTimeoutMs);
#endif
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("RcloneBrowserNG"));
  QNetworkReply *reply = mNetwork->get(request);
  mReply = reply;
  connect(reply, &QNetworkReply::downloadProgress, this,
          [this, reply, reportProgress](qint64 received, qint64 total) {
            if (received > kMaxDownload || total > kMaxDownload) {
              reply->abort();
              return;
            }
            if (reportProgress && !mDone) {
              emit progress(received, total);
            }
          });
  connect(reply, &QNetworkReply::finished, this, [this, reply, url, done]() {
    reply->deleteLater();
    if (mReply == reply) {
      mReply = nullptr;
    }
    if (mDone) {
      return;
    }
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status != 200) {
      const QString why = reply->error() == QNetworkReply::OperationCanceledError
                              ? QStringLiteral("the download was too large or "
                                               "stopped")
                              : reply->errorString();
      fail(QStringLiteral("Couldn't download %1: %2")
               .arg(url.toString(QUrl::RemoveUserInfo), why));
      return;
    }
    done(reply->readAll());
  });
}

void Download::fetchSums() {
  emit stage(QStringLiteral("Checking rclone v%1...").arg(mVersion));
  get(at(QStringLiteral("v%1/SHA256SUMS").arg(mVersion)),
      [this](const QByteArray &body) {
        const QString archive = ArchiveName(mVersion, mSource.platform);
        mExpected = ChecksumFor(body, archive);
        if (mExpected.isEmpty()) {
          fail(QStringLiteral("rclone's checksum list has no entry for %1.")
                   .arg(archive));
          return;
        }
        fetchArchive();
      });
}

void Download::fetchArchive() {
  emit stage(QStringLiteral("Downloading rclone v%1...").arg(mVersion));
  get(at(QStringLiteral("v%1/%2")
             .arg(mVersion, ArchiveName(mVersion, mSource.platform))),
      [this](const QByteArray &body) { extract(body); }, true);
}

void Download::extract(const QByteArray &archive) {
  const QByteArray actual =
      QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex();
  if (actual != mExpected) {
    fail(QStringLiteral("The download didn't match rclone's published "
                        "checksum, so it was thrown away. Try again later."));
    return;
  }
  const QString archivePath =
      mWork->filePath(ArchiveName(mVersion, mSource.platform));
  {
    QFile file(archivePath);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(archive) != archive.size()) {
      fail(QStringLiteral("Couldn't save the download to %1: %2")
               .arg(QDir::toNativeSeparators(archivePath),
                    file.errorString()));
      return;
    }
  }
  const QString outDir = mWork->filePath(QStringLiteral("unpacked"));
  QDir().mkpath(outDir);
  const QString member = ArchiveMember(mVersion, mSource.platform);
  const Extractor extractor = ExtractorFor(archivePath, member, outDir);
  if (extractor.program.isEmpty()) {
    fail(QStringLiteral("This system has no unzip tool (unzip or bsdtar), so "
                        "the download can't be unpacked. Install rclone from "
                        "your package manager and choose it in Preferences."));
    return;
  }

  emit stage(QStringLiteral("Unpacking rclone v%1...").arg(mVersion));
  mExtractor = new QProcess(this);
  mExtractor->setProgram(extractor.program);
  mExtractor->setArguments(extractor.arguments);
  mExtractor->setProcessChannelMode(QProcess::MergedChannels);
  QProcess *process = mExtractor;
  connect(process, &QProcess::errorOccurred, this,
          [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart || mExtractor != process) {
              return;
            }
            mExtractor = nullptr;
            process->deleteLater();
            fail(QStringLiteral("Couldn't start %1: %2")
                     .arg(QDir::toNativeSeparators(process->program()),
                          process->errorString()));
          });
  connect(process,
          static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
              &QProcess::finished),
          this,
          [this, process, outDir, member](int code,
                                          QProcess::ExitStatus status) {
            if (mExtractor != process) {
              return;
            }
            mExtractor = nullptr;
            process->deleteLater();
            const QString output =
                QString::fromLocal8Bit(process->readAll()).trimmed();
            const QString extracted = QDir(outDir).filePath(member);
            if (status != QProcess::NormalExit || code != 0 ||
                !QFileInfo(extracted).isFile()) {
              fail(QStringLiteral("Couldn't unpack rclone (%1 exited with "
                                  "%2). %3")
                       .arg(QFileInfo(process->program()).fileName(),
                            QString::number(code), output));
              return;
            }
            install(extracted);
          });
  QTimer::singleShot(kExtractTimeoutMs, process, [process]() {
    if (process->state() != QProcess::NotRunning) {
      process->kill();
    }
  });
  mExtractor->start();
}

void Download::install(const QString &extracted) {
  QFile file(extracted);
  if (!file.open(QIODevice::ReadOnly)) {
    fail(QStringLiteral("Couldn't read the unpacked rclone: %1")
             .arg(file.errorString()));
    return;
  }
  mBinary = file.readAll();
  file.close();
  mBinarySha =
      QCryptographicHash::hash(mBinary, QCryptographicHash::Sha256).toHex();

  QString path;
  QString error;
  if (CanWriteTo(mDestDir) && writeBinary(mDestDir, &path, &error)) {
    succeed(path);
    return;
  }
#if defined(Q_OS_WIN32)
  // The helper writes only beside itself, so this is the one folder it can
  // fill. Program Files is the usual reason to land here.
  if (SameDir(mDestDir, QFileInfo(HelperProgram()).absolutePath())) {
    installElevated(extracted);
    return;
  }
#endif
  installFallback(error.isEmpty()
                      ? QStringLiteral("it can't be written to")
                      : error);
}

void Download::installElevated(const QString &staged) {
#if defined(Q_OS_WIN32)
  emit stage(QStringLiteral("Saving rclone to %1...")
                 .arg(QDir::toNativeSeparators(mDestDir)));
  QStringList quoted;
  for (const QString &argument : HelperArguments(staged, mBinarySha)) {
    quoted << QuoteForShell(argument);
  }
  const std::wstring program =
      QDir::toNativeSeparators(HelperProgram()).toStdWString();
  const std::wstring parameters = quoted.join(QLatin1Char(' ')).toStdWString();

  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  info.lpVerb = L"runas";
  info.lpFile = program.c_str();
  info.lpParameters = parameters.c_str();
  info.nShow = SW_HIDE;
  if (!ShellExecuteExW(&info) || info.hProcess == nullptr) {
    const DWORD code = GetLastError();
    installFallback(
        code == ERROR_CANCELLED
            ? QStringLiteral("the administrator prompt was declined")
            : QStringLiteral("the administrator step couldn't start (error "
                             "%1)")
                  .arg(code));
    return;
  }
  mHelperProcess = info.hProcess;
  mHelperPoll = new QTimer(this);
  mHelperPoll->setInterval(100);
  auto elapsed = std::make_shared<QElapsedTimer>();
  elapsed->start();
  connect(mHelperPoll, &QTimer::timeout, this, [this, elapsed]() {
    const HANDLE process = static_cast<HANDLE>(mHelperProcess);
    const bool exited = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    if (!exited && !elapsed->hasExpired(kHelperTimeoutMs)) {
      return;
    }
    DWORD code = STILL_ACTIVE;
    if (exited) {
      GetExitCodeProcess(process, &code);
    }
    mHelperPoll->stop();
    mHelperPoll->deleteLater();
    mHelperPoll = nullptr;
    CloseHandle(process);
    mHelperProcess = nullptr;
    const QString path = QDir(mDestDir).filePath(BinaryName(mSource.platform));
    if (exited && code == 0 && QFileInfo(path).isFile()) {
      succeed(QFileInfo(path).absoluteFilePath());
      return;
    }
    installFallback(exited ? HelperExitMessage(static_cast<int>(code))
                           : QStringLiteral("the administrator step didn't "
                                            "finish within a minute"));
  });
  mHelperPoll->start();
#else
  Q_UNUSED(staged);
  installFallback(QStringLiteral("it can't be written to"));
#endif
}

void Download::installFallback(const QString &reason) {
  const QString fallback = FallbackDir();
  QString path;
  QString error;
  if (!SameDir(fallback, mDestDir) && writeBinary(fallback, &path, &error)) {
    mNote = QStringLiteral("rclone couldn't be saved to %1 (%2), so it went "
                           "to %3 instead. It works the same from there.")
                .arg(QDir::toNativeSeparators(mDestDir), reason,
                     QDir::toNativeSeparators(fallback));
    succeed(path);
    return;
  }
  fail(QStringLiteral("Couldn't save rclone to %1: %2")
           .arg(QDir::toNativeSeparators(mDestDir),
                error.isEmpty() ? reason : error));
}

bool Download::writeBinary(const QString &dir, QString *path,
                           QString *error) {
  if (!QDir().mkpath(dir)) {
    *error = QStringLiteral("couldn't create %1")
                 .arg(QDir::toNativeSeparators(dir));
    return false;
  }
  const QString target = QDir(dir).filePath(BinaryName(mSource.platform));
  QSaveFile out(target);
  if (!out.open(QIODevice::WriteOnly) ||
      out.write(mBinary) != mBinary.size() || !out.commit()) {
    *error = out.errorString();
    return false;
  }
  QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner |
                                    QFile::ExeOwner | QFile::ReadGroup |
                                    QFile::ExeGroup | QFile::ReadOther |
                                    QFile::ExeOther);
  *path = QFileInfo(target).absoluteFilePath();
  return true;
}

void Download::succeed(const QString &path) {
  if (mDone) {
    return;
  }
  mDone = true;
  mBinary.clear();
  mWork.reset();
  emit finished(path);
}

void Download::fail(const QString &error) {
  if (mDone) {
    return;
  }
  mDone = true;
  stopWork();
  mBinary.clear();
  mWork.reset();
  emit failed(error);
}

void OfferDownload(
    QWidget *parent, const Download::Source &source, const QString &destDir,
    std::function<void(SetupChoice choice, const QString &rclonePath)> done) {
  QString text =
      QStringLiteral("Rclone Browser NG does its work through rclone, and "
                     "there's no rclone on this computer yet.\n\n"
                     "It can download the current rclone from rclone.org, "
                     "check it against rclone's published checksums and "
                     "save it in:\n%1")
          .arg(QDir::toNativeSeparators(destDir));
#if defined(Q_OS_WIN32)
  if (!CanWriteTo(destDir)) {
    text += QStringLiteral(
        "\n\nWindows will ask for permission to save into that folder.");
  }
#endif
  QMessageBox box(QMessageBox::Question, QStringLiteral("rclone not found"),
                  text, QMessageBox::NoButton, parent);
  QPushButton *download =
      box.addButton(QStringLiteral("Download rclone"), QMessageBox::AcceptRole);
  QPushButton *choose = box.addButton(QStringLiteral("Choose rclone..."),
                                      QMessageBox::ActionRole);
  box.addButton(QStringLiteral("Not now"), QMessageBox::RejectRole);
  box.setDefaultButton(download);
  box.exec();
  if (box.clickedButton() == choose) {
    done(SetupChoice::ChooseManually, QString());
    return;
  }
  if (box.clickedButton() != download) {
    done(SetupChoice::Later, QString());
    return;
  }
  RunDownload(parent, source, destDir, std::move(done));
}

void RunDownload(
    QWidget *parent, const Download::Source &source, const QString &destDir,
    std::function<void(SetupChoice choice, const QString &rclonePath)> done) {
  auto *job = new Download(source, parent);
  auto *dialog = new QProgressDialog(
      QStringLiteral("Finding the current rclone..."),
      QStringLiteral("Cancel"), 0, 0, parent);
  dialog->setWindowTitle(QStringLiteral("Downloading rclone"));
  dialog->setWindowModality(Qt::WindowModal);
  dialog->setMinimumDuration(0);
  dialog->setAutoClose(false);
  dialog->setAutoReset(false);
  dialog->setMinimumWidth(420);
  // Closing the dialog also emits canceled, so every ending goes through
  // here once.
  auto settled = std::make_shared<bool>(false);
  auto settle = [job, dialog, settled]() {
    *settled = true;
    dialog->close();
    dialog->deleteLater();
    job->deleteLater();
  };

  QObject::connect(job, &Download::stage, dialog,
                   &QProgressDialog::setLabelText);
  QObject::connect(job, &Download::progress, dialog,
                   [job, dialog](qint64 received, qint64 total) {
                     if (total <= 0) {
                       return;
                     }
                     dialog->setMaximum(1000);
                     dialog->setValue(
                         static_cast<int>(received * 1000 / total));
                     dialog->setLabelText(
                         QStringLiteral("Downloading rclone v%1... %2 of %3 MB")
                             .arg(job->version(), Megabytes(received),
                                  Megabytes(total)));
                   });
  QObject::connect(dialog, &QProgressDialog::canceled, job,
                   [job, settled, settle, done]() {
                     if (*settled) {
                       return;
                     }
                     job->cancel();
                     settle();
                     done(SetupChoice::Later, QString());
                   });
  QObject::connect(job, &Download::finished, dialog,
                   [parent, job, settled, settle, done](const QString &path) {
                     if (*settled) {
                       return;
                     }
                     const QString note = job->note();
                     settle();
                     if (!note.isEmpty()) {
                       QMessageBox::information(
                           parent, QStringLiteral("rclone downloaded"), note);
                     }
                     done(SetupChoice::Installed, path);
                   });
  QObject::connect(job, &Download::failed, dialog,
                   [parent, settled, settle, done](const QString &error) {
                     if (*settled) {
                       return;
                     }
                     settle();
                     QMessageBox::warning(
                         parent, QStringLiteral("rclone download failed"),
                         error + QStringLiteral(
                                     "\n\nYou can choose an rclone you "
                                     "already have in Preferences."));
                     done(SetupChoice::ChooseManually, QString());
                   });
  dialog->show();
  job->start(destDir);
}

} // namespace RcloneInstaller
