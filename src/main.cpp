#include "main_window.h"
#include "utils.h"

namespace {
bool writePasswordCommandOutput(const QString &password) {
  const QByteArray output = password.toUtf8() + '\n';
#if defined(Q_OS_WIN32)
  HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    AttachConsole(ATTACH_PARENT_PROCESS);
    handle = GetStdHandle(STD_OUTPUT_HANDLE);
  }
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  DWORD written = 0;
  return WriteFile(handle, output.constData(),
                   static_cast<DWORD>(output.size()), &written, nullptr) &&
         written == static_cast<DWORD>(output.size());
#else
  QTextStream(stdout) << QString::fromUtf8(output);
  return true;
#endif
}
} // namespace

int main(int argc, char *argv[]) {

#if defined(Q_OS_WIN32)
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  static const char ENV_VAR_QT_DEVICE_PIXEL_RATIO[] = "QT_DEVICE_PIXEL_RATIO";
  if (!qEnvironmentVariableIsSet(ENV_VAR_QT_DEVICE_PIXEL_RATIO) &&
      !qEnvironmentVariableIsSet("QT_AUTO_SCREEN_SCALE_FACTOR") &&
      !qEnvironmentVariableIsSet("QT_SCALE_FACTOR") &&
      !qEnvironmentVariableIsSet("QT_SCREEN_SCALE_FACTORS")) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  }
#endif

  QApplication app(argc, argv);

  app.setApplicationDisplayName("Rclone Browser NG");
  app.setApplicationName("rclone-browser");
  app.setOrganizationName("rclone-browser");
  QGuiApplication::setDesktopFileName("io.github.sysadmindoc.rclonebrowserng");
  app.setWindowIcon(QIcon(":/icons/icon.png"));

  if (IsRclonePasswordCommandRequest(app.arguments())) {
    QString error;
    const QString password = ReadRcloneConfigPassword(&error);
    if (password.isEmpty()) {
      qCritical().noquote() << error;
      return 1;
    }
    return writePasswordCommandOutput(password) ? 0 : 1;
  }

// initialize SSL libraries
// see: https://github.com/linuxdeploy/linuxdeploy-plugin-qt/issues/57
#if defined(Q_OS_LINUX)
  QString currentDir = QDir::currentPath();
  QDir::setCurrent(QCoreApplication::applicationDirPath());
  QSslSocket::supportsSsl();
  QDir::setCurrent(currentDir);
#endif

  auto settings = GetSettings();

  // initialize proxy settings
  if (!(settings->contains("Settings/useProxy"))) {
    settings->setValue("Settings/useProxy", "false");
  };
  if (!(settings->contains("Settings/http_proxy"))) {
    settings->setValue("Settings/http_proxy", "");
  };
  if (!(settings->contains("Settings/https_proxy"))) {
    settings->setValue("Settings/https_proxy", "");
  };
  if (!(settings->contains("Settings/no_proxy"))) {
    settings->setValue("Settings/no_proxy", "");
  };

  if (settings->value("Settings/useProxy").toBool()) {
    qputenv("HTTP_PROXY", settings->value("Settings/http_proxy").toByteArray());
    qputenv("http_proxy", settings->value("Settings/http_proxy").toByteArray());
    qputenv("HTTPS_PROXY",
            settings->value("Settings/https_proxy").toByteArray());
    qputenv("https_proxy",
            settings->value("Settings/https_proxy").toByteArray());
    qputenv("NO_PROXY", settings->value("Settings/no_proxy").toByteArray());
    qputenv("no_proxy", settings->value("Settings/no_proxy").toByteArray());
  }

  // remmber darkMode state on app startup
  // during first run the darkModeIni key might not exist
  if (!(settings->contains("Settings/darkModeIni"))) {
    // if darkModeIni does not exist create new key
    settings->setValue("Settings/darkModeIni", "false");
  };

  // during first run the darkMode key might not exist
  if (!(settings->contains("Settings/darkMode"))) {
    // if darkMode does not exist create new key
    settings->setValue("Settings/darkMode", "false");
  };

  bool darkMode = settings->value("Settings/darkMode").toBool();

  settings->setValue("Settings/darkModeIni", darkMode);

  // during first run the iconSize key might not exist
  if (!(settings->contains("Settings/iconSize"))) {
    // if iconSize does not exist create new key
    settings->setValue("Settings/iconSize", "medium");
  };

  // enforce one instance of Rclone Browser NG per user
  QString tmpDir;
  QString applicationNameBase;
  QFileInfo applicationPath;
  QFileInfo applicationUserPath;

  // QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
  // qDebug() << QString("main.cpp $XDG_CONFIG_HOME: " + xdg_config_home);

  // QString APPIMAGE = qgetenv("APPIMAGE");
  // qDebug() << QString("main.cpp $APPIMAGE: " + APPIMAGE);

  QFileInfo appBundlePath;

  if (IsPortableMode()) {

    //  qDebug() << QString("isPortable is true");
    //  applicationPath = qApp->applicationFilePath();
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/
    // to get actual bundle folder we have
    // to traverse three levels up
    applicationPath = QFileInfo(qApp->applicationFilePath());
    tmpDir = applicationPath.absolutePath() + "/../../..";

    // get bundle name
    QFileInfo MacOSPath(applicationPath.dir().path());
    QFileInfo ContentsPath(MacOSPath.dir().path());
    appBundlePath = QFileInfo(ContentsPath.dir().path());

#else
    // not macOS
#ifdef Q_OS_WIN
    applicationPath = QFileInfo(qApp->applicationFilePath());
    tmpDir = applicationPath.absolutePath();
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    if (xdg_config_home.isEmpty()) {
      xdg_config_home =
          QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    }
    if (xdg_config_home.isEmpty()) {
      xdg_config_home = QDir::home().filePath(".config");
    }
    tmpDir = QDir(xdg_config_home).filePath("rclone-browser");
    // create ./rclone-browser folder
    if (!QDir(tmpDir).exists()) {
      QDir().mkpath(tmpDir);
    }
#endif
#endif
  } else {
    // not portable mode
    // get tmp folder from Qt  - OS dependend
    tmpDir = QDir::tempPath();
  }

  // check if tmpDir writable
  // as isWritable does weird things on Windows
  // we do this old fashioned way by creating temp file
  QTemporaryFile tempfile(tmpDir + "/rclone-browserXXXXXX.test");

  if (tempfile.open()) {
    tempfile.close();
    tempfile.remove();
  } else {
    // folder has no write access
    if (IsPortableMode()) {
      QMessageBox msgBox;
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("You need write "
                     "access to this folder:\n\n"
#ifdef Q_OS_MACOS
                     + appBundlePath.absolutePath() +
#else
#ifdef Q_OS_WIN
                     + tmpDir +
#else
                     + tmpDir.left(tmpDir.length() - 15) +
#endif
#endif
                     "\n\n"
#ifdef Q_OS_MACOS
                     "Or remove file:\n\n" +
                     appBundlePath.baseName() +
                     ".ini \n\nfrom the above folder "
#else
#ifdef Q_OS_WIN
                     "Or remove file:\n\n" +
                     applicationPath.baseName() +
                     ".ini \n\nfrom the above folder "
#else
                     "Or remove folder:\n\n" +
                     tmpDir.left(tmpDir.length() - 15) +
                     "\n\n"
#endif
#endif
                     "to disable portable mode.");
      msgBox.exec();
    } else {
      QMessageBox msgBox;
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("You need write "
                     "access to this folder: \n\n"
#ifdef Q_OS_MACOS
                     + tmpDir
#else

#ifdef Q_OS_WIN
                     + tmpDir
#else

                     + tmpDir.left(tmpDir.length() - 15)
#endif
#endif
      );
      msgBox.exec();
    }
    return static_cast<int>(
        0x80004004); // exit immediately if folder not writable
  }

  // qDebug() << QString("main.cpp tmpDir:  " + tmpDir);

  QString lockUser = qEnvironmentVariable("USER");
  if (lockUser.isEmpty()) lockUser = qEnvironmentVariable("USERNAME");
  if (lockUser.isEmpty()) lockUser = QString::number(qHash(QDir::homePath()));
  QLockFile lockFile(tmpDir + "/.RcloneBrowser_" + lockUser + ".lock");

  if (!lockFile.tryLock(100)) {
    if (lockFile.removeStaleLockFile() && lockFile.tryLock(100)) {
      QMessageBox msgBox;
      msgBox.setIcon(QMessageBox::Information);
      msgBox.setText("Recovered from a stale Rclone Browser NG lock file.\n\n"
                     "The previous process appears to have exited without "
                     "releasing its single-instance lock.");
      msgBox.exec();
    } else {
      qint64 lockPid = 0;
      QString lockHostname;
      QString lockAppName;
      QString lockDetails;
      if (lockFile.getLockInfo(&lockPid, &lockHostname, &lockAppName)) {
        lockDetails =
            QString("\n\nLock owner: %1 on %2 (pid %3)")
                .arg(lockAppName.isEmpty() ? "unknown process" : lockAppName)
                .arg(lockHostname.isEmpty() ? "unknown host" : lockHostname)
                .arg(lockPid);
      }

    // if already running display warning and quit
      QMessageBox msgBox;
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("Rclone Browser NG is already running."
                     "\r\n\nOnly one instance is allowed." +
                     lockDetails);
      msgBox.exec();
      return static_cast<int>(
          0x80004004); // exit immediately if another instance is running
    }
  }

  MainWindow w;
  w.show();

  return app.exec();
}
