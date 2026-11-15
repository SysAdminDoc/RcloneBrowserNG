#include "utils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <windows.h>
#include <wincred.h>

namespace {
// The long form of a path, so it matches what the helper reads back from
// GetModuleFileName when it is started by that path.
QString longPath(const QString &path) {
  const std::wstring in = QDir::toNativeSeparators(path).toStdWString();
  wchar_t out[MAX_PATH * 4];
  const DWORD length = GetLongPathNameW(in.c_str(), out, MAX_PATH * 4);
  if (length == 0 || length >= MAX_PATH * 4) {
    return QDir::fromNativeSeparators(path);
  }
  return QDir::fromNativeSeparators(
      QString::fromWCharArray(out, static_cast<int>(length)));
}

QString credentialTargetFor(const QString &confPath) {
  return "RcloneBrowserNG/rclone-config-password/" +
         QString::fromLatin1(QCryptographicHash::hash(confPath.toUtf8(),
                                                      QCryptographicHash::Sha256)
                                 .toHex());
}
} // namespace

// With "Use password command" on, rclone unlocks an encrypted rclone.conf by
// running RcloneBrowserPassword.exe from the app's folder. In a portable copy
// the helper looked for RcloneBrowserPassword.ini, never found it, read the
// installed app's settings instead and asked Credential Manager for a
// password stored under a different rclone.conf.
class PasswordHelperTest : public QObject {
  Q_OBJECT

private slots:
  void helperUsesTheAppsPortableIni() {
    QCOMPARE(WindowsPortableIniPath("C:/Apps/RB/RcloneBrowser.exe"),
             QStringLiteral("C:/Apps/RB/RcloneBrowser.ini"));
    QCOMPARE(WindowsPortableIniPath("C:/Apps/RB/RcloneBrowserPassword.exe"),
             QStringLiteral("C:/Apps/RB/RcloneBrowser.ini"));
    QCOMPARE(WindowsPortableIniPath("C:/Apps/RB/rclonebrowserpassword.EXE"),
             QStringLiteral("C:/Apps/RB/RcloneBrowser.ini"));
    // Portable mode has always followed the app's name.
    QCOMPARE(WindowsPortableIniPath("D:/tools/MyBrowser.exe"),
             QStringLiteral("D:/tools/MyBrowser.ini"));
  }

  void portableHelperHandsRcloneThePortablePassword() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString dir = longPath(temp.path());
    const QString helper = QDir(dir).filePath("RcloneBrowserPassword.exe");
    QVERIFY2(QFile::copy(QStringLiteral(PASSWORD_HELPER_EXE), helper),
             PASSWORD_HELPER_EXE);

    // A portable copy whose rclone.conf sits next to it.
    const QString ini = QDir(dir).filePath("RcloneBrowser.ini");
    {
      QSettings settings(ini, QSettings::IniFormat);
      settings.setValue("Settings/rcloneConf", "rclone.conf");
      settings.sync();
    }
    // The helper must see this ini before it runs, or it would read and
    // write this machine's real settings.
    QCOMPARE(WindowsPortableIniPath(helper), ini);

    const QString target =
        credentialTargetFor(QDir(dir).filePath("rclone.conf"));
    const std::wstring targetW = target.toStdWString();
    const QByteArray password = "portable-s3cret";
    std::wstring user = L"RcloneBrowserNG";
    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(targetW.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(password.size());
    credential.CredentialBlob =
        reinterpret_cast<LPBYTE>(const_cast<char *>(password.constData()));
    // Gone at sign-out even if the cleanup below never runs.
    credential.Persist = CRED_PERSIST_SESSION;
    credential.UserName = const_cast<LPWSTR>(user.c_str());
    QVERIFY(CredWriteW(&credential, 0));
    struct Cleanup {
      std::wstring target;
      ~Cleanup() { CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0); }
    } cleanup{targetW};

    QProcess process;
    process.start(helper, {"--rclone-config-password-command"});
    QVERIFY(process.waitForFinished(30000));
    const QString err = QString::fromUtf8(process.readAllStandardError());
    QVERIFY2(process.exitCode() == 0, qPrintable(err));
    QCOMPARE(QString::fromUtf8(process.readAllStandardOutput()).trimmed(),
             QString::fromUtf8(password));

    // It ran as the portable copy: the one-time settings move only marks
    // the portable ini, it never touches the shared settings.
    QSettings settings(ini, QSettings::IniFormat);
    QVERIFY(!settings.value("Migration/sharedIdentity").toString().isEmpty());
  }
};

QTEST_MAIN(PasswordHelperTest)
#include "password_helper_test.moc"
