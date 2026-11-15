#include "vss_snapshot.h"

#if defined(Q_OS_WIN32)
#include <wbemidl.h>
#include <wrl/client.h>
#endif

namespace VssSnapshot {

namespace {

const QRegularExpression &drivePathPattern() {
  static const QRegularExpression pattern(
      QStringLiteral(R"(^[A-Za-z]:[\\/])"));
  return pattern;
}

} // namespace

bool IsLocalDrivePath(const QString &source) {
  return drivePathPattern().match(source).hasMatch();
}

QString VolumeRoot(const QString &source) {
  if (!IsLocalDrivePath(source)) {
    return QString();
  }
  return source.left(1).toUpper() + QStringLiteral(":\\");
}

QString PathInSnapshot(const QString &source, const QString &linkPath) {
  if (!IsLocalDrivePath(source)) {
    return QString();
  }
  const QString rest = QDir::fromNativeSeparators(source.mid(3));
  QString base = QDir::fromNativeSeparators(linkPath);
  while (base.endsWith('/')) {
    base.chop(1);
  }
  return rest.isEmpty() ? base : base + '/' + rest;
}

QStringList WithSource(QStringList args, const QString &newSource) {
  if (args.size() >= 2) {
    args[args.size() - 2] = newSource;
  }
  return args;
}

QString UnavailableReason(const QString &source) {
#if !defined(Q_OS_WIN32)
  Q_UNUSED(source);
  return QStringLiteral("Snapshots of locked files are a Windows feature.");
#else
  if (!IsLocalDrivePath(source)) {
    return QStringLiteral(
        "A snapshot needs the source to be a folder or file on a drive of "
        "this PC, like C:\\Users\\you\\Documents.");
  }
  const QString root = VolumeRoot(source);
  const UINT type =
      GetDriveTypeW(reinterpret_cast<const wchar_t *>(root.utf16()));
  if (type == DRIVE_REMOTE) {
    return QStringLiteral(
        "%1 is a network drive. Windows can only snapshot drives in this PC.")
        .arg(root);
  }
  if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN) {
    return QStringLiteral("There's no drive %1 on this PC.").arg(root);
  }
  wchar_t fileSystem[MAX_PATH + 1] = {};
  if (GetVolumeInformationW(reinterpret_cast<const wchar_t *>(root.utf16()),
                            nullptr, 0, nullptr, nullptr, nullptr, fileSystem,
                            MAX_PATH)) {
    const QString name = QString::fromWCharArray(fileSystem);
    if (name.compare("NTFS", Qt::CaseInsensitive) != 0 &&
        name.compare("ReFS", Qt::CaseInsensitive) != 0) {
      return QStringLiteral(
                 "%1 is formatted %2. Windows can only snapshot NTFS and "
                 "ReFS drives.")
          .arg(root, name);
    }
  }
  return QString();
#endif
}

QStringList HelperArguments(const HelperRequest &request) {
  return {QString::fromLatin1(kHelperFlag),
          QDir::toNativeSeparators(request.sessionDir),
          // Without the trailing backslash, which the Windows command line
          // would read as escaping the closing quote.
          request.volume.left(2), QString::number(request.parentPid)};
}

bool ParseHelperArguments(const QStringList &arguments,
                          HelperRequest *request) {
  const qsizetype at = arguments.indexOf(QString::fromLatin1(kHelperFlag));
  if (at < 0 || at + 3 >= arguments.size() || request == nullptr) {
    return false;
  }
  const QString &sessionDir = arguments.at(at + 1);
  const QString &volume = arguments.at(at + 2);
  bool ok = false;
  const qint64 pid = arguments.at(at + 3).toLongLong(&ok);
  if (sessionDir.isEmpty() || !QDir::isAbsolutePath(sessionDir) || !ok ||
      pid <= 0 ||
      !QRegularExpression(QStringLiteral("^[A-Za-z]:$")).match(volume)
           .hasMatch()) {
    return false;
  }
  request->sessionDir = QDir::cleanPath(QDir::fromNativeSeparators(sessionDir));
  request->volume = volume.toUpper() + QStringLiteral("\\");
  request->parentPid = pid;
  return true;
}

QString LinkPath(const QString &sessionDir) {
  return QDir(sessionDir).filePath(QStringLiteral("snapshot"));
}
QString ReadyFile(const QString &sessionDir) {
  return QDir(sessionDir).filePath(QStringLiteral("ready"));
}
QString ErrorFile(const QString &sessionDir) {
  return QDir(sessionDir).filePath(QStringLiteral("error"));
}
QString ReleaseFile(const QString &sessionDir) {
  return QDir(sessionDir).filePath(QStringLiteral("release"));
}
QString ReleasedFile(const QString &sessionDir) {
  return QDir(sessionDir).filePath(QStringLiteral("released"));
}
namespace {
QString &HelperProgram() {
  static QString program;
  return program;
}
} // namespace

void SetHelperProgram(const QString &path) { HelperProgram() = path; }

QString HelperPidFile(const QString &sessionDir) {
  return QDir(sessionDir).filePath(QStringLiteral("helper-pid"));
}

QString SessionRoot() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
      .filePath(QStringLiteral("rclone-browser-vss"));
}

#if defined(Q_OS_WIN32)
namespace {

using Microsoft::WRL::ComPtr;

// A shadow copy id as WMI prints it. Anything else is refused before it gets
// near a WQL query, since the elevated helper builds one from it.
bool IsShadowId(const QString &id) {
  static const QRegularExpression pattern(QStringLiteral(
      R"(^\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}$)"));
  return pattern.match(id).hasMatch();
}

// Writes a file that must not exist yet. The session folder belongs to the
// user and the helper runs elevated, so it never writes through a file or
// link someone else put there first.
bool WriteNew(const QString &path, const QString &text) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    return false;
  }
  return file.write(text.toUtf8()) >= 0;
}

bool IsReparsePoint(const QString &path) {
  const DWORD attributes = GetFileAttributesW(reinterpret_cast<const wchar_t *>(
      QDir::toNativeSeparators(path).utf16()));
  return attributes == INVALID_FILE_ATTRIBUTES ||
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

QString HrText(HRESULT hr) {
  return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16,
                                    QChar('0'));
}

class Bstr {
public:
  explicit Bstr(const QString &text)
      : mValue(SysAllocString(reinterpret_cast<const wchar_t *>(text.utf16()))) {}
  ~Bstr() { SysFreeString(mValue); }
  Bstr(const Bstr &) = delete;
  Bstr &operator=(const Bstr &) = delete;
  BSTR get() const { return mValue; }

private:
  BSTR mValue;
};

QString VariantString(const VARIANT &value) {
  if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
    return QString::fromWCharArray(value.bstrVal);
  }
  return QString();
}

QString CreateReturnText(long code) {
  switch (code) {
  case 1:
    return QStringLiteral("Windows refused: making a snapshot needs an "
                          "administrator.");
  case 3:
  case 4:
    return QStringLiteral("Windows can't snapshot that drive.");
  case 6:
    return QStringLiteral(
        "There isn't enough free space on the drive for a snapshot.");
  case 8:
    return QStringLiteral(
        "The drive already has as many snapshots as Windows allows.");
  case 9:
    return QStringLiteral("Another snapshot is being made. Try again in a "
                          "minute.");
  case 10:
    return QStringLiteral(
        "Windows called off the snapshot before it finished. A very busy "
        "drive or another backup program can cause this, so try again in a "
        "few minutes.");
  default:
    return QStringLiteral("Windows couldn't make a snapshot (code %1).")
        .arg(code);
  }
}

// The shadow copies this app made and hasn't deleted yet, kept where only an
// administrator can write. Cleanup deletes only these, never a snapshot a
// file in the user's folders names.
QSettings &Ledger() {
  static QSettings settings(
      QStringLiteral(
          R"(HKEY_LOCAL_MACHINE\SOFTWARE\RcloneBrowserNG\VssSnapshots)"),
      QSettings::Registry64Format);
  return settings;
}

QString MutexName(const QString &shadowId) {
  QString bare = shadowId;
  bare.remove('{').remove('}');
  return QStringLiteral("Global\\RcloneBrowserNG-VSS-") + bare;
}

class Wmi {
public:
  bool connect(QString *error) {
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&mLocator));
    if (FAILED(hr)) {
      *error = QStringLiteral("WMI isn't available (%1).").arg(HrText(hr));
      return false;
    }
    const Bstr ns(QStringLiteral("ROOT\\CIMV2"));
    hr = mLocator->ConnectServer(ns.get(), nullptr, nullptr, nullptr, 0,
                                 nullptr, nullptr, &mServices);
    if (FAILED(hr)) {
      *error =
          QStringLiteral("Couldn't connect to WMI (%1).").arg(HrText(hr));
      return false;
    }
    hr = CoSetProxyBlanket(mServices.Get(), RPC_C_AUTHN_WINNT,
                           RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                           RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
      *error = QStringLiteral("Couldn't set WMI security (%1).").arg(HrText(hr));
      return false;
    }
    return true;
  }

  bool create(const QString &volume, QString *shadowId, long *code,
              QString *error) {
    *code = -1;
    const Bstr className(QStringLiteral("Win32_ShadowCopy"));
    const Bstr methodName(QStringLiteral("Create"));
    ComPtr<IWbemClassObject> shadowClass;
    HRESULT hr = mServices->GetObject(className.get(), 0, nullptr,
                                      &shadowClass, nullptr);
    ComPtr<IWbemClassObject> inDefinition;
    if (SUCCEEDED(hr)) {
      hr = shadowClass->GetMethod(L"Create", 0, &inDefinition, nullptr);
    }
    ComPtr<IWbemClassObject> in;
    if (SUCCEEDED(hr)) {
      hr = inDefinition->SpawnInstance(0, &in);
    }
    if (SUCCEEDED(hr)) {
      hr = putString(in.Get(), L"Volume", volume);
    }
    if (SUCCEEDED(hr)) {
      // ClientAccessible is the kind Previous Versions uses, readable as a
      // volume once it exists.
      hr = putString(in.Get(), L"Context", QStringLiteral("ClientAccessible"));
    }
    ComPtr<IWbemClassObject> out;
    if (SUCCEEDED(hr)) {
      hr = mServices->ExecMethod(className.get(), methodName.get(), 0, nullptr,
                                 in.Get(), &out, nullptr);
    }
    if (hr == static_cast<HRESULT>(WBEM_E_ACCESS_DENIED) ||
        hr == E_ACCESSDENIED) {
      *code = 1;
      *error = CreateReturnText(1);
      return false;
    }
    if (FAILED(hr) || !out) {
      *error = QStringLiteral("Windows couldn't make a snapshot (%1).")
                   .arg(HrText(hr));
      return false;
    }
    VARIANT returned;
    VariantInit(&returned);
    hr = out->Get(L"ReturnValue", 0, &returned, nullptr, nullptr);
    *code = SUCCEEDED(hr) ? returned.lVal : -1;
    VariantClear(&returned);
    if (*code != 0) {
      *error = CreateReturnText(*code);
      return false;
    }
    VARIANT id;
    VariantInit(&id);
    hr = out->Get(L"ShadowID", 0, &id, nullptr, nullptr);
    *shadowId = SUCCEEDED(hr) ? VariantString(id) : QString();
    VariantClear(&id);
    if (!IsShadowId(*shadowId)) {
      *code = -1;
      *error = QStringLiteral("Windows made a snapshot but didn't say which.");
      return false;
    }
    return true;
  }

  bool deviceObject(const QString &shadowId, QString *device,
                    QString *error) {
    ComPtr<IWbemClassObject> shadow = find(shadowId);
    if (!shadow) {
      *error = QStringLiteral("The new snapshot can't be found.");
      return false;
    }
    VARIANT value;
    VariantInit(&value);
    const HRESULT hr = shadow->Get(L"DeviceObject", 0, &value, nullptr, nullptr);
    *device = SUCCEEDED(hr) ? VariantString(value) : QString();
    VariantClear(&value);
    if (!device->startsWith(QStringLiteral("\\\\?\\GLOBALROOT\\Device\\"))) {
      *error = QStringLiteral("The snapshot has no device to read from.");
      return false;
    }
    return true;
  }

  // True when the snapshot is gone, including when it was already.
  bool remove(const QString &shadowId) {
    ComPtr<IWbemClassObject> shadow = find(shadowId);
    if (!shadow) {
      return true;
    }
    VARIANT path;
    VariantInit(&path);
    HRESULT hr = shadow->Get(L"__PATH", 0, &path, nullptr, nullptr);
    if (SUCCEEDED(hr) && path.vt == VT_BSTR) {
      hr = mServices->DeleteInstance(path.bstrVal, 0, nullptr, nullptr);
    }
    VariantClear(&path);
    return SUCCEEDED(hr);
  }

private:
  ComPtr<IWbemLocator> mLocator;
  ComPtr<IWbemServices> mServices;

  static HRESULT putString(IWbemClassObject *object, const wchar_t *name,
                           const QString &text) {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal =
        SysAllocString(reinterpret_cast<const wchar_t *>(text.utf16()));
    const HRESULT hr = object->Put(name, 0, &value, 0);
    VariantClear(&value);
    return hr;
  }

  ComPtr<IWbemClassObject> find(const QString &shadowId) {
    if (!IsShadowId(shadowId)) {
      return nullptr;
    }
    const Bstr language(QStringLiteral("WQL"));
    const Bstr query(
        QStringLiteral("SELECT * FROM Win32_ShadowCopy WHERE ID='%1'")
            .arg(shadowId));
    ComPtr<IEnumWbemClassObject> results;
    HRESULT hr = mServices->ExecQuery(
        language.get(), query.get(),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
        &results);
    if (FAILED(hr)) {
      return nullptr;
    }
    ComPtr<IWbemClassObject> shadow;
    ULONG count = 0;
    hr = results->Next(WBEM_INFINITE, 1, &shadow, &count);
    if (FAILED(hr) || count == 0) {
      return nullptr;
    }
    return shadow;
  }
};

// Deletes snapshots an earlier helper recorded and never got to delete,
// because the PC shut down or the helper was killed. A live helper holds a
// mutex named for its snapshot, so its snapshot is left alone.
void DeleteAbandoned(Wmi &wmi) {
  QSettings &ledger = Ledger();
  const QStringList ids = ledger.childKeys();
  for (const QString &id : ids) {
    if (!IsShadowId(id)) {
      ledger.remove(id);
      continue;
    }
    const std::wstring name = MutexName(id).toStdWString();
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
    if (mutex != nullptr) {
      CloseHandle(mutex);
      continue;
    }
    if (wmi.remove(id)) {
      ledger.remove(id);
    }
  }
  ledger.sync();
}

class ComApartment {
public:
  ComApartment() : mResult(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
    if (SUCCEEDED(mResult)) {
      CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                           RPC_C_AUTHN_LEVEL_DEFAULT,
                           RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE,
                           nullptr);
    }
  }
  ~ComApartment() {
    if (SUCCEEDED(mResult)) {
      CoUninitialize();
    }
  }
  ComApartment(const ComApartment &) = delete;
  ComApartment &operator=(const ComApartment &) = delete;
  bool ok() const { return SUCCEEDED(mResult); }

private:
  HRESULT mResult;
};

} // namespace
#endif

bool RetryableCreateCode(long code) { return code == 9 || code == 10; }

bool CreateWithRetry(const std::function<bool(long *code)> &attempt,
                     const std::function<void()> &pause, int attempts) {
  for (int tried = 1;; ++tried) {
    long code = -1;
    if (attempt(&code)) {
      return true;
    }
    if (tried >= attempts || !RetryableCreateCode(code)) {
      return false;
    }
    pause();
  }
}

int RunHelper(const QStringList &arguments) {
  HelperRequest request;
  if (!ParseHelperArguments(arguments, &request)) {
    return 2;
  }
#if !defined(Q_OS_WIN32)
  return 1;
#else
  const QString sessionDir = request.sessionDir;
  // A session folder swapped for a junction would point these writes
  // somewhere the user can't write but an administrator can.
  if (IsReparsePoint(sessionDir) ||
      IsReparsePoint(QFileInfo(sessionDir).absolutePath())) {
    return 3;
  }
  WriteNew(HelperPidFile(sessionDir),
           QString::number(QCoreApplication::applicationPid()));
  const auto fail = [&sessionDir](const QString &message) {
    WriteNew(ErrorFile(sessionDir), message);
    return 1;
  };

  ComApartment com;
  if (!com.ok()) {
    return fail(QStringLiteral("Couldn't start COM for the snapshot."));
  }
  Wmi wmi;
  QString error;
  if (!wmi.connect(&error)) {
    return fail(error);
  }
  DeleteAbandoned(wmi);

  QString shadowId;
  // A scheduled backup that lands on a busy moment would otherwise fail
  // on a veto that's gone ten seconds later.
  const bool made = CreateWithRetry(
      [&](long *code) {
        return wmi.create(request.volume, &shadowId, code, &error);
      },
      [] { Sleep(10000); });
  if (!made) {
    return fail(error);
  }
  const std::wstring mutexName = MutexName(shadowId).toStdWString();
  HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
  Ledger().setValue(shadowId, sessionDir);
  Ledger().sync();
  const auto dropSnapshot = [&wmi, &shadowId]() {
    if (wmi.remove(shadowId)) {
      Ledger().remove(shadowId);
      Ledger().sync();
    }
  };

  QString device;
  if (!wmi.deviceObject(shadowId, &device, &error)) {
    dropSnapshot();
    return fail(error);
  }
  const std::wstring link =
      QDir::toNativeSeparators(LinkPath(sessionDir)).toStdWString();
  const std::wstring target = (device + QStringLiteral("\\")).toStdWString();
  if (!CreateSymbolicLinkW(link.c_str(), target.c_str(),
                           SYMBOLIC_LINK_FLAG_DIRECTORY)) {
    const DWORD code = GetLastError();
    dropSnapshot();
    return fail(QStringLiteral("Couldn't link the snapshot (error %1).")
                    .arg(code));
  }
  WriteNew(ReadyFile(sessionDir), shadowId);

  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE,
                              static_cast<DWORD>(request.parentPid));
  // A transfer that runs for two days has outlived any reason to keep its
  // snapshot, and the drive's shadow storage with it.
  QDeadlineTimer deadline(std::chrono::hours(48));
  const QString releaseFile = ReleaseFile(sessionDir);
  while (!deadline.hasExpired()) {
    if (parent != nullptr) {
      if (WaitForSingleObject(parent, 500) == WAIT_OBJECT_0) {
        break;
      }
    } else {
      Sleep(500);
    }
    if (QFileInfo::exists(releaseFile)) {
      break;
    }
  }
  if (parent != nullptr) {
    CloseHandle(parent);
  }

  RemoveDirectoryW(link.c_str());
  dropSnapshot();
  if (mutex != nullptr) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  WriteNew(ReleasedFile(sessionDir), shadowId);
  return 0;
#endif
}

} // namespace VssSnapshot

VssSession::VssSession(QString sessionDir, QObject *parent)
    : QObject(parent), mSessionDir(std::move(sessionDir)) {}

VssSession::~VssSession() { release(); }

QString VssSession::pathFor(const QString &source) const {
  return VssSnapshot::PathInSnapshot(source,
                                     VssSnapshot::LinkPath(mSessionDir));
}

void VssSession::releaseWith(QObject *owner) {
  setParent(owner);
}

void VssSession::release() {
  if (mReleased) {
    return;
  }
  mReleased = true;
  QFile file(VssSnapshot::ReleaseFile(mSessionDir));
  if (file.open(QIODevice::WriteOnly)) {
    file.close();
  }
}

namespace {

#if defined(Q_OS_WIN32)
// One argument for a Windows command line, quoted the way
// CommandLineToArgvW reads it back.
QString QuoteArgument(const QString &argument) {
  if (!argument.isEmpty() && !argument.contains(QRegularExpression("[\\s\"]"))) {
    return argument;
  }
  QString quoted = QStringLiteral("\"");
  qsizetype backslashes = 0;
  for (const QChar c : argument) {
    if (c == '\\') {
      ++backslashes;
      continue;
    }
    if (c == '"') {
      quoted += QString(backslashes * 2 + 1, '\\');
    } else {
      quoted += QString(backslashes, '\\');
    }
    backslashes = 0;
    quoted += c;
  }
  quoted += QString(backslashes * 2, '\\');
  quoted += '"';
  return quoted;
}

// Session folders whose helper has finished, so nothing links into them.
void RemoveFinishedSessions() {
  const QDir root(VssSnapshot::SessionRoot());
  const QStringList names =
      root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &name : names) {
    const QString dir = root.filePath(name);
    if (QFileInfo::exists(VssSnapshot::ReleasedFile(dir)) &&
        !QFileInfo(VssSnapshot::LinkPath(dir)).exists()) {
      QDir(dir).removeRecursively();
    }
  }
}
#endif

} // namespace

void VssSession::Begin(const QString &source, QObject *context,
                       const ReadyCallback &callback) {
  const QString reason = VssSnapshot::UnavailableReason(source);
  if (!reason.isEmpty()) {
    callback(nullptr, reason);
    return;
  }
#if !defined(Q_OS_WIN32)
  Q_UNUSED(context);
#else
  RemoveFinishedSessions();
  const QString sessionDir =
      QDir(VssSnapshot::SessionRoot())
          .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
  if (!QDir().mkpath(sessionDir)) {
    callback(nullptr,
             QStringLiteral("Couldn't create %1 for the snapshot.")
                 .arg(QDir::toNativeSeparators(sessionDir)));
    return;
  }
  VssSnapshot::HelperRequest request;
  request.sessionDir = sessionDir;
  request.volume = VssSnapshot::VolumeRoot(source);
  request.parentPid = QCoreApplication::applicationPid();
  QStringList quoted;
  for (const QString &argument : VssSnapshot::HelperArguments(request)) {
    quoted << QuoteArgument(argument);
  }
  const QString helperPath = VssSnapshot::HelperProgram().isEmpty()
                                 ? QCoreApplication::applicationFilePath()
                                 : VssSnapshot::HelperProgram();
  const std::wstring program =
      QDir::toNativeSeparators(helperPath).toStdWString();
  const std::wstring parameters = quoted.join(' ').toStdWString();

  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
  info.lpVerb = L"runas";
  info.lpFile = program.c_str();
  info.lpParameters = parameters.c_str();
  info.nShow = SW_HIDE;
  if (!ShellExecuteExW(&info) || info.hProcess == nullptr) {
    const DWORD code = GetLastError();
    QDir(sessionDir).removeRecursively();
    callback(nullptr,
             code == ERROR_CANCELLED
                 ? QStringLiteral("The administrator prompt was declined, so "
                                  "there's no snapshot.")
                 : QStringLiteral("Couldn't start the snapshot helper (error "
                                  "%1).")
                       .arg(code));
    return;
  }

  HANDLE helper = info.hProcess;
  auto *timer = new QTimer(context);
  timer->setInterval(250);
  QElapsedTimer elapsed;
  elapsed.start();
  QObject::connect(timer, &QTimer::timeout, context, [=]() mutable {
    QString error;
    if (!QFileInfo::exists(VssSnapshot::ReadyFile(sessionDir))) {
      if (QFileInfo::exists(VssSnapshot::ErrorFile(sessionDir))) {
        QFile file(VssSnapshot::ErrorFile(sessionDir));
        error = file.open(QIODevice::ReadOnly)
                    ? QString::fromUtf8(file.readAll()).trimmed()
                    : QString();
        if (error.isEmpty()) {
          error = QStringLiteral("The snapshot failed.");
        }
      } else if (WaitForSingleObject(helper, 0) == WAIT_OBJECT_0) {
        error = QStringLiteral(
            "The snapshot helper stopped before it made a snapshot.");
      } else if (elapsed.hasExpired(180000)) {
        error = QStringLiteral("The snapshot took more than three minutes.");
      } else {
        return;
      }
    }
    timer->stop();
    timer->deleteLater();
    CloseHandle(helper);
    if (!error.isEmpty()) {
      // Tells a helper that is still running to give up and clean up.
      QFile release(VssSnapshot::ReleaseFile(sessionDir));
      if (release.open(QIODevice::WriteOnly)) {
        release.close();
      }
      callback(nullptr, error);
      return;
    }
    auto *session = new VssSession(sessionDir);
    // The snapshot can exist and still be closed to this account. Better to
    // say so now than to have rclone fail on every file.
    const QFileInfo readable(session->pathFor(source));
    if (!readable.exists() || !readable.isReadable()) {
      delete session;
      callback(nullptr,
               QStringLiteral("Windows made the snapshot, but this account "
                              "can't read %1 in it.")
                   .arg(QDir::toNativeSeparators(source)));
      return;
    }
    callback(session, QString());
  });
  timer->start();
#endif
}
