#include "utils.h"

#if defined(Q_OS_WIN32)
#include <wincred.h>
#endif

static QString gRclone;
static QString gRcloneConf;
static QString gRclonePassword;

namespace {
const char *kUsePasswordCommandKey = "Settings/usePasswordCommand";
const char *kPasswordCommandArg = "--rclone-config-password-command";

QString configuredRcloneConf() {
  if (!gRcloneConf.isEmpty()) {
    return gRcloneConf;
  }
  auto settings = GetSettings();
  return settings->value("Settings/rcloneConf").toString();
}

QString resolveRcloneConfPath(QString conf) {
  if (conf.isEmpty()) {
    return conf;
  }
  if (IsPortableMode() && QFileInfo(conf).isRelative()) {
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/rclone-browser to get actual bundle
    // folder we have to traverse three levels up
    conf = QDir(qApp->applicationDirPath() + "/../../..").filePath(conf);
#else
#ifdef Q_OS_WIN
    conf = QDir(qApp->applicationDirPath()).filePath(conf);
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    if (xdg_config_home.isEmpty()) {
      xdg_config_home =
          QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    }
    conf = QDir(xdg_config_home.isEmpty() ? qApp->applicationDirPath()
                                          : xdg_config_home + "/..")
               .filePath(conf);
#endif
#endif
  }
  return conf;
}

QString credentialTarget() {
  QString source = configuredRcloneConf().trimmed();
  if (source.isEmpty()) {
    source = "default";
  } else {
    source = resolveRcloneConfPath(source);
  }
  if (QFileInfo(source).isRelative()) {
    source = QFileInfo(source).absoluteFilePath();
  }
  const QByteArray digest =
      QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256)
          .toHex();
  return "RcloneBrowserNG/rclone-config-password/" +
         QString::fromLatin1(digest);
}

QString passwordCommandValue() {
  QString exe = QDir::toNativeSeparators(
      QDir(qApp->applicationDirPath()).filePath("RcloneBrowserPassword.exe"));
  exe.replace('"', "\\\"");
  return '"' + exe + "\" " + kPasswordCommandArg;
}

bool storeRcloneConfigPassword(const QString &password, QString *error) {
  if (error) {
    error->clear();
  }

#if defined(Q_OS_WIN32)
  const QByteArray passwordBytes = password.toUtf8();
  const std::wstring target = credentialTarget().toStdWString();
  std::wstring user = L"RcloneBrowserNG";

  CREDENTIALW credential = {};
  credential.Type = CRED_TYPE_GENERIC;
  credential.TargetName = const_cast<LPWSTR>(target.c_str());
  credential.CredentialBlobSize =
      static_cast<DWORD>(passwordBytes.size());
  credential.CredentialBlob =
      reinterpret_cast<LPBYTE>(const_cast<char *>(passwordBytes.constData()));
  credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
  credential.UserName = const_cast<LPWSTR>(user.c_str());

  if (!CredWriteW(&credential, 0)) {
    if (error) {
      *error = QString("Windows Credential Manager write failed (%1).")
                   .arg(GetLastError());
    }
    return false;
  }
  return true;
#else
  Q_UNUSED(password);
  if (error) {
    *error = "OS credential storage is not available in this build.";
  }
  return false;
#endif
}
} // namespace

// Software versions comparison
// source: https://helloacm.com/how-to-compare-version-numbers-in-c/
std::vector<std::string> split(const std::string &s, char d) {
  std::vector<std::string> r;
  int j = 0;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (s[i] == d) {
      r.push_back(s.substr(j, i - j));
      j = i + 1;
    }
  }
  r.push_back(s.substr(j));
  return r;
}

// parse leading digits of a version segment; tolerates suffixes like
// "0-beta" or empty segments so odd rclone version strings can't throw
static unsigned int parseVersionSegment(const std::string &s) {
  unsigned int value = 0;
  for (char c : s) {
    if (c < '0' || c > '9') {
      break;
    }
    value = value * 10 + static_cast<unsigned int>(c - '0');
  }
  return value;
}

unsigned int compareVersion(std::string version1, std::string version2) {
  auto v1 = split(version1, '.');
  auto v2 = split(version2, '.');
  unsigned int max = v1.size() > v2.size() ? v1.size() : v2.size();
  // pad the shorter version string
  if (v1.size() != max) {
    for (unsigned int i = max - v1.size(); i--;) {
      v1.push_back("0");
    }
  } else {
    for (unsigned int i = max - v2.size(); i--;) {
      v2.push_back("0");
    }
  }
  for (unsigned int i = 0; i < max; i++) {
    unsigned int n1 = parseVersionSegment(v1[i]);
    unsigned int n2 = parseVersionSegment(v2[i]);
    if (n1 > n2) {
      // version1 is higher than version2
      return 1;
    } else if (n1 < n2) {
      // version2 is higher than version1
      return 2;
    }
  }
  // the same versions
  return 0;
}

quint16 GetRcMountPort(const QString &folder) {
  return static_cast<quint16>(19000 + (qHash(folder) % 10000));
}

QString MakeRcPassword() {
  // 128 bits of randomness, hex-encoded; enough to make the loopback rc
  // endpoint unguessable so the unauthenticated-rc CVEs can't be reached
  quint64 a = QRandomGenerator::system()->generate64();
  quint64 b = QRandomGenerator::system()->generate64();
  return QString::number(a, 16).rightJustified(16, '0') +
         QString::number(b, 16).rightJustified(16, '0');
}

static QString GetIniFilename() {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
  auto linuxConfigHome = []() {
    QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (configHome.isEmpty()) {
      configHome = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    }
    if (configHome.isEmpty()) {
      configHome = QDir::home().filePath(".config");
    }
    return configHome;
  };
#endif

#ifdef Q_OS_MACOS
  QFileInfo applicationPath(qApp->applicationFilePath());
  //  qDebug() << QString(applicationPath.absolutePath());
  // on macOS excecutable file is located in
  // ./rclone-browser.app/Contents/MasOS/ to get actual bundle folder we have to
  // traverse three levels up
  QFileInfo MacOSPath(applicationPath.dir().path());
  QFileInfo ContentsPath(MacOSPath.dir().path());
  QFileInfo appBundlePath(ContentsPath.dir().path());
  //  qDebug() << QString("utils.cpp appBundle.absolutePath: " +
  //                      appBundlePath.absolutePath());
  //  qDebug() << QString(
  //      "utils.cpp ini file:" +
  //      appBundlePath.dir().filePath(appBundlePath.baseName() + ".ini"));
  return appBundlePath.dir().filePath(appBundlePath.baseName() + ".ini");
#else
#ifdef Q_OS_WIN
  QFileInfo applicationPath(qApp->applicationFilePath());
  return applicationPath.dir().filePath(applicationPath.baseName() + ".ini");
#else
  return QDir(linuxConfigHome()).filePath("rclone-browser/rclone-browser.ini");
#endif
#endif
}

bool IsPortableMode() {
  QString ini = GetIniFilename();
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
  QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
  //  qDebug() << QString("utils.cpp $XDG_CONFIG_HOME: " + xdg_config_home);
  QString appimage = qgetenv("APPIMAGE");
  //  qDebug() << QString("utils.cpp $APPIMAGE: " + appimage);

  if (!xdg_config_home.isEmpty() && !appimage.isEmpty() &&
      QDir::cleanPath(xdg_config_home) == QDir::cleanPath(appimage + ".config")) {

    return true;
  }
#endif

  if (QFileInfo(ini).exists()) {

    return true;
  } else {
    return false;
  }

  //  return QFileInfo(ini).exists();
}

std::unique_ptr<QSettings> GetSettings() {
  if (IsPortableMode()) {
    return std::unique_ptr<QSettings>(
        new QSettings(GetIniFilename(), QSettings::IniFormat));
  }
  return std::unique_ptr<QSettings>(new QSettings);
}

void ReadSettings(QSettings *settings, QObject *widget) {
  QString name = widget->objectName();
  if (!name.isEmpty() && settings->contains(name)) {
    if (QRadioButton *obj = qobject_cast<QRadioButton *>(widget)) {
      obj->setChecked(settings->value(name).toBool());
      return;
    }
    if (QCheckBox *obj = qobject_cast<QCheckBox *>(widget)) {
      obj->setChecked(settings->value(name).toBool());
      return;
    }
    if (QComboBox *obj = qobject_cast<QComboBox *>(widget)) {
      obj->setCurrentIndex(settings->value(name).toInt());
      return;
    }
    if (QSpinBox *obj = qobject_cast<QSpinBox *>(widget)) {
      obj->setValue(settings->value(name).toInt());
      return;
    }
    if (QLineEdit *obj = qobject_cast<QLineEdit *>(widget)) {
      obj->setText(settings->value(name).toString());
      return;
    }
    if (QPlainTextEdit *obj = qobject_cast<QPlainTextEdit *>(widget)) {
      int count = settings->beginReadArray(name);
      QStringList lines;
      lines.reserve(count);
      for (int i = 0; i < count; i++) {
        settings->setArrayIndex(i);
        lines.append(settings->value("value").toString());
      }
      settings->endArray();

      obj->setPlainText(lines.join('\n'));
      return;
    }
  }

  for (auto child : widget->children()) {
    ReadSettings(settings, child);
  }
}

void WriteSettings(QSettings *settings, QObject *widget) {
  QString name = widget->objectName();
  if (QCheckBox *obj = qobject_cast<QCheckBox *>(widget)) {
    settings->setValue(name, obj->isChecked());
    return;
  }
  if (QComboBox *obj = qobject_cast<QComboBox *>(widget)) {
    settings->setValue(name, obj->currentIndex());
    return;
  }
  if (QSpinBox *obj = qobject_cast<QSpinBox *>(widget)) {
    settings->setValue(name, obj->value());
    return;
  }
  if (QLineEdit *obj = qobject_cast<QLineEdit *>(widget)) {
    if (obj->text().isEmpty()) {
      settings->remove(name);
    } else {
      settings->setValue(name, obj->text());
    }
    return;
  }
  if (QRadioButton *obj = qobject_cast<QRadioButton *>(widget)) {
    settings->setValue(name, obj->isChecked());
    return;
  }
  if (QPlainTextEdit *obj = qobject_cast<QPlainTextEdit *>(widget)) {
    QString text = obj->toPlainText().trimmed();
    if (text.isEmpty()) {
      settings->remove(name);
    } else {
      QStringList lines = text.split('\n');
      settings->beginWriteArray(name, lines.size());
      for (int i = 0; i < lines.count(); i++) {
        settings->setArrayIndex(i);
        settings->setValue("value", lines[i]);
      }
      settings->endArray();
    }
    return;
  }

  for (auto child : widget->children()) {
    WriteSettings(settings, child);
  }
}

QStringList GetRcloneConf() {
  QStringList args;

  if (!gRcloneConf.isEmpty()) {
    QString conf = resolveRcloneConfPath(gRcloneConf);
    //    qDebug() << QString("utils.cpp conf: " + conf);
    args << "--config" << conf;
  }

  if (IsRclonePasswordCommandEnabled()) {
    args << "--password-command" << passwordCommandValue();
  }
  return args;
}

void SetRcloneConf(const QString &rcloneConf) { gRcloneConf = rcloneConf; }

QString GetRclone() {
  QString rclone = gRclone;
  if (IsPortableMode() && QFileInfo(rclone).isRelative()) {
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/rclone-browser to get actual bundle
    // folder we have to traverse three levels up
    rclone = QDir(qApp->applicationDirPath() + "/../../..").filePath(rclone);
#else
#ifdef Q_OS_WIN
    rclone = QDir(qApp->applicationDirPath()).filePath(rclone);
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    if (xdg_config_home.isEmpty()) {
      xdg_config_home =
          QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    }
    rclone = QDir(xdg_config_home.isEmpty() ? qApp->applicationDirPath()
                                            : xdg_config_home + "/..")
                 .filePath(rclone);
#endif
#endif
    //    qDebug() << QString("utils.cpp rclone portable: " + rclone);
  }

  return rclone;
}

void SetRclone(const QString &rclone) {
  QString path = rclone.trimmed();
#if defined(Q_OS_WIN32)
  // BatBadBut (CVE-2024-24576): a .bat/.cmd rclone path would route
  // through cmd.exe, defeating QProcess argument escaping
  if (path.endsWith(".bat", Qt::CaseInsensitive) ||
      path.endsWith(".cmd", Qt::CaseInsensitive)) {
    qWarning("Refusing rclone path ending in .bat/.cmd: %s",
             qPrintable(path));
    return;
  }
#endif
  gRclone = path;
}

void UseRclonePassword(QProcess *process) {
  if (!gRclonePassword.isEmpty() && !IsRclonePasswordCommandEnabled()) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("RCLONE_CONFIG_PASS", gRclonePassword);
    process->setProcessEnvironment(env);
  }
}

void SetRclonePassword(const QString &rclonePassword) {
  if (IsRclonePasswordCommandEnabled()) {
    QString error;
    if (storeRcloneConfigPassword(rclonePassword, &error)) {
      gRclonePassword.clear();
      return;
    }
    qWarning().noquote() << error;
    SetRclonePasswordCommandEnabled(false);
  }
  gRclonePassword = rclonePassword;
}

bool IsRclonePasswordCommandEnabled() {
#if !defined(Q_OS_WIN32)
  return false;
#else
  auto settings = GetSettings();
  return settings->value(kUsePasswordCommandKey, false).toBool();
#endif
}

void SetRclonePasswordCommandEnabled(bool enabled) {
  auto settings = GetSettings();
  settings->setValue(kUsePasswordCommandKey, enabled);
}

QString ReadRcloneConfigPassword(QString *error) {
  if (error) {
    error->clear();
  }

#if defined(Q_OS_WIN32)
  const std::wstring target = credentialTarget().toStdWString();
  PCREDENTIALW credential = nullptr;
  if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    if (error) {
      *error = QString("Windows Credential Manager read failed (%1).")
                   .arg(GetLastError());
    }
    return QString();
  }

  const QByteArray passwordBytes(
      reinterpret_cast<const char *>(credential->CredentialBlob),
      static_cast<int>(credential->CredentialBlobSize));
  CredFree(credential);
  if (passwordBytes.size() >= 2 && passwordBytes.size() % 2 == 0) {
    int nulOddBytes = 0;
    for (int i = 1; i < passwordBytes.size(); i += 2) {
      if (passwordBytes.at(i) == '\0') {
        nulOddBytes++;
      }
    }
    if (nulOddBytes >= passwordBytes.size() / 4) {
      return QString::fromUtf16(
          reinterpret_cast<const ushort *>(passwordBytes.constData()),
          passwordBytes.size() / 2);
    }
  }
  return QString::fromUtf8(passwordBytes);
#else
  if (error) {
    *error = "OS credential storage is not available in this build.";
  }
  return QString();
#endif
}

void ClearRcloneConfigPassword() {
#if defined(Q_OS_WIN32)
  const std::wstring target = credentialTarget().toStdWString();
  CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
#endif
}

bool IsRclonePasswordCommandRequest(const QStringList &arguments) {
  return arguments.contains(kPasswordCommandArg);
}

QStringList GetDriveSharedWithMe() {
  auto settings = GetSettings();
  bool driveShared = settings->value("Settings/driveShared", false).toBool();
  QStringList driveSharedOption;
  if (driveShared) {
    driveSharedOption << "--drive-shared-with-me";
  }
  return driveSharedOption;
}

QStringList SplitRcloneOptions(const QString &options) {
  const QString trimmed = options.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }
  return QProcess::splitCommand(trimmed);
}

bool ParseHttpUrl(const QString &value, QUrl *url, QString *error) {
  const QString trimmed = value.trimmed();
  const QUrl parsed(trimmed, QUrl::StrictMode);
  const QString scheme = parsed.scheme().toLower();
  if (!parsed.isValid() || parsed.host().isEmpty() ||
      (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
    if (error) {
      *error =
          QStringLiteral("Only valid http:// or https:// URLs are supported.");
    }
    return false;
  }
  if (url) {
    *url = parsed;
  }
  if (error) {
    error->clear();
  }
  return true;
}

namespace {
QString JoinBackupRetentionPath(const QString &parent, const QString &child) {
  if (parent.endsWith(':') || parent.endsWith('/') || parent.endsWith('\\')) {
    return parent + child;
  }
  return parent + "/" + child;
}
} // namespace

bool BuildBackupRetentionPlan(const QString &backupDirTemplate, int keepCount,
                              const QStringList &snapshotNames,
                              QString *parentPath,
                              QStringList *deleteTargets) {
  if (parentPath) {
    parentPath->clear();
  }
  if (deleteTargets) {
    deleteTargets->clear();
  }
  if (keepCount <= 0) {
    return false;
  }

  constexpr qsizetype kTokenSize = 6;
  const qsizetype tokenStart = backupDirTemplate.indexOf("{date}");
  if (tokenStart < 0) {
    return false;
  }

  const qsizetype slashBefore =
      qMax(backupDirTemplate.lastIndexOf('/', tokenStart),
           backupDirTemplate.lastIndexOf('\\', tokenStart));
  const qsizetype segmentStart = slashBefore + 1;
  const qsizetype slashAfterForward =
      backupDirTemplate.indexOf('/', tokenStart + kTokenSize);
  const qsizetype slashAfterBack =
      backupDirTemplate.indexOf('\\', tokenStart + kTokenSize);
  qsizetype segmentEnd = backupDirTemplate.size();
  if (slashAfterForward >= 0) {
    segmentEnd = slashAfterForward;
  }
  if (slashAfterBack >= 0) {
    segmentEnd = qMin(segmentEnd, slashAfterBack);
  }

  QString parent = backupDirTemplate.left(segmentStart);
  while (parent.size() > 1 &&
         (parent.endsWith('/') || parent.endsWith('\\')) &&
         !parent.endsWith(":/")) {
    parent.chop(1);
  }
  if (parent.isEmpty()) {
    return false;
  }
  if (parentPath) {
    *parentPath = parent;
  }

  const QString segment =
      backupDirTemplate.mid(segmentStart, segmentEnd - segmentStart);
  const qsizetype segmentTokenStart = segment.indexOf("{date}");
  const QString prefix = segment.left(segmentTokenStart);
  const QString suffix = segment.mid(segmentTokenStart + kTokenSize);
  const QString datePattern =
      "\\d{4}-\\d{2}-\\d{2}_\\d{6}(?:_\\d{3})?"
      "(?:_[0-9a-z]+_[0-9a-z]{4})?";
  const QRegularExpression snapshotPattern(
      "^" + QRegularExpression::escape(prefix) + datePattern +
      QRegularExpression::escape(suffix) + "$");

  QStringList snapshots;
  for (QString name : snapshotNames) {
    name = name.trimmed();
    while (name.endsWith('/') || name.endsWith('\\')) {
      name.chop(1);
    }
    if (snapshotPattern.match(name).hasMatch()) {
      snapshots << name;
    }
  }

  snapshots.sort(Qt::CaseSensitive);
  const int removeCount = snapshots.size() - keepCount;
  if (removeCount <= 0 || !deleteTargets) {
    return true;
  }
  for (int i = 0; i < removeCount; ++i) {
    deleteTargets->append(JoinBackupRetentionPath(parent, snapshots.at(i)));
  }
  return true;
}

QStringList GetDefaultRcloneOptionsList() {
  auto settings = GetSettings();
  QString defaultRcloneOptions =
      settings->value("Settings/defaultRcloneOptions").toString();
  return SplitRcloneOptions(defaultRcloneOptions);
}

QStringList GetDefaultExcludeList() {
  auto settings = GetSettings();
  QString patterns =
      settings->value("Settings/defaultExclude").toString();
  QStringList result;
  if (!patterns.isEmpty()) {
    for (const auto &line : patterns.split('\n')) {
      QString trimmed = line.trimmed();
      if (!trimmed.isEmpty()) {
        result << "--exclude" << trimmed;
      }
    }
  }
  return result;
}

QStringList GetGlobalBandwidthLimit() {
  auto settings = GetSettings();
  QString bwlimit =
      settings->value("Settings/globalBandwidthLimit").toString().trimmed();
  if (!bwlimit.isEmpty()) {
    return QStringList() << "--bwlimit" << bwlimit;
  }
  return {};
}

QStringList GetShowHidden() {
  auto settings = GetSettings();
  bool showHidden = settings->value("Settings/showHidden", true).toBool();
  QStringList showHiddenOption;
  if (!showHidden) {
    showHiddenOption << "--exclude"
                     << ".*/**"
                     << "--exclude"
                     << ".*";
  }
  return showHiddenOption;
}

QString GetNiceSize(quint64 size) {
  static const char prefix[] = "KMGTPE";
  for (int i = sizeof(prefix) - 2; i >= 0; i--) {
    quint64 base = quint64(1) << ((i + 1) * 10);
    if (size >= base) {
      double value = double(size) / double(base);
      return QString("%1 %2")
          .arg(value, 0, 'f', value >= 100 ? 0 : 1)
          .arg(QChar(prefix[i]));
    }
  }
  return QString("%1 B").arg(size);
}
