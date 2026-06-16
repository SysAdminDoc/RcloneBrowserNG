#include "list_of_job_options.h"
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
    QByteArray socksProxy =
        settings->value("Settings/socksProxy").toByteArray();
    if (!socksProxy.isEmpty()) {
      qputenv("ALL_PROXY", socksProxy);
      qputenv("all_proxy", socksProxy);
    }
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

  QString lockUser = qEnvironmentVariable("USER");
  if (lockUser.isEmpty()) lockUser = qEnvironmentVariable("USERNAME");
  if (lockUser.isEmpty()) lockUser = QString::number(qHash(QDir::homePath()));

  const QString serverName =
      "RcloneBrowserNG_" + lockUser + "_" +
      QString::number(qHash(tmpDir));

  // Try to connect to an already-running instance.
  {
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) {
      socket.write("raise");
      socket.waitForBytesWritten(1000);
      socket.disconnectFromServer();
      return 0;
    }
  }

  // No running instance found — clean up any leftover server and start ours.
  QLocalServer::removeServer(serverName);
  QLocalServer server;
  if (!server.listen(serverName)) {
    QMessageBox::warning(nullptr, "Rclone Browser NG",
                         "Could not create single-instance server:\n" +
                             server.errorString());
  }

  // Also keep the lockfile for backwards compat and stale-pid reporting
  QLockFile lockFile(tmpDir + "/.RcloneBrowser_" + lockUser + ".lock");
  if (!lockFile.tryLock(100)) {
    lockFile.removeStaleLockFile();
    lockFile.tryLock(100);
  }

  int runTaskIdx = app.arguments().indexOf("--run-task");
  if (runTaskIdx >= 0 && runTaskIdx + 1 < app.arguments().size()) {
    QString taskName = app.arguments().at(runTaskIdx + 1);
    auto *store = ListOfJobOptions::getInstance();
    JobOptions *found = nullptr;
    for (auto *jo : store->getTasks()) {
      if (jo->description == taskName) {
        found = jo;
        break;
      }
    }
    if (!found) {
      qCritical().noquote()
          << "Task not found:" << taskName;
      QStringList available;
      for (auto *jo : store->getTasks()) {
        available << "  " + jo->description;
      }
      if (!available.isEmpty()) {
        qCritical().noquote() << "Available tasks:\n" + available.join("\n");
      }
      return 1;
    }
    QStringList args = GetRcloneConf() + found->getOptions();
    QProcess proc;
    proc.setProcessChannelMode(QProcess::ForwardedChannels);
    UseRclonePassword(&proc);
    proc.start(GetRclone(), args);
    proc.waitForFinished(-1);
    return proc.exitCode();
  }

  if (app.arguments().contains("--list-tasks")) {
    auto *store = ListOfJobOptions::getInstance();
    for (auto *jo : store->getTasks()) {
      QTextStream(stdout) << jo->description << "\n";
    }
    return 0;
  }

  MainWindow w;
  bool startMinimized =
      app.arguments().contains("--minimized") ||
      app.arguments().contains("--tray") ||
      settings->value("Settings/startMinimized", false).toBool();
  if (startMinimized && QSystemTrayIcon::isSystemTrayAvailable()) {
    w.hide();
  } else {
    w.show();
  }

  QObject::connect(&server, &QLocalServer::newConnection, &w, [&]() {
    while (auto *client = server.nextPendingConnection()) {
      client->waitForReadyRead(500);
      client->deleteLater();
      w.bringToFront();
    }
  });

  return app.exec();
}
