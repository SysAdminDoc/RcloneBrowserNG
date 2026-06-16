#include "main_window.h"
#include "job_options.h"
#include "job_widget.h"
#include "mount_backend.h"
#include "list_of_job_options.h"
#include "mount_widget.h"
#include "preferences_dialog.h"
#include "rc_job_widget.h"
#include "rclone_rc_engine.h"
#include "remote_provider.h"
#include "schedule_manager.h"
#include "remote_widget.h"
#include "stream_widget.h"
#include "transfer_dialog.h"
#include "interface_polish.h"
#include "job_history.h"
#include "rclone_capabilities.h"
#include "utils.h"
#ifdef Q_OS_MACOS
#include "osx_helper.h"
#endif

namespace {

#if defined(Q_OS_WIN32)
QStringList winFspDllCandidates() {
  return QStringList()
         << "C:/Program Files (x86)/WinFsp/bin/winfsp-x64.dll"
         << "C:/Program Files/WinFsp/bin/winfsp-x64.dll"
         << "C:/Windows/System32/winfsp-x64.dll";
}

QString findWinFspDll() {
  for (const QString &path : winFspDllCandidates()) {
    if (QFileInfo::exists(path)) {
      return path;
    }
  }
  return QString();
}

QString windowsFileVersion(const QString &path) {
  const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
  DWORD handle = 0;
  const DWORD size = GetFileVersionInfoSizeW(nativePath.c_str(), &handle);
  if (size == 0) {
    return QString();
  }

  QByteArray data(static_cast<int>(size), Qt::Uninitialized);
  if (!GetFileVersionInfoW(nativePath.c_str(), 0, size, data.data())) {
    return QString();
  }

  VS_FIXEDFILEINFO *info = nullptr;
  UINT infoSize = 0;
  if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID *>(&info),
                      &infoSize) ||
      !info || infoSize == 0 || info->dwSignature != 0xfeef04bd) {
    return QString();
  }

  return QString("%1.%2.%3.%4")
      .arg(HIWORD(info->dwFileVersionMS))
      .arg(LOWORD(info->dwFileVersionMS))
      .arg(HIWORD(info->dwFileVersionLS))
      .arg(LOWORD(info->dwFileVersionLS));
}
#endif

QString shellQuote(QString arg) {
  return "'" + arg.replace("'", "'\"'\"'") + "'";
}

QVector<RemoteProvider> loadRemoteProviders(QWidget *parent, QString *error) {
  if (error) {
    error->clear();
  }

  QProcess p(parent);
  UseRclonePassword(&p);
  QStringList args = GetRcloneConf();
  args << "rc"
       << "--loopback"
       << "config/providers";
  p.start(GetRclone(), args, QIODevice::ReadOnly);
  if (!p.waitForStarted(5000)) {
    if (error) {
      *error = "Failed to start rclone.";
    }
    return {};
  }
  if (!p.waitForFinished(15000)) {
    p.kill();
    p.waitForFinished(2000);
    if (error) {
      *error = "Timed out while reading rclone providers.";
    }
    return {};
  }
  if (p.exitCode() != 0) {
    if (error) {
      const QString stderrText =
          QString::fromUtf8(p.readAllStandardError()).trimmed();
      *error = stderrText.isEmpty() ? "rclone config/providers failed."
                                    : stderrText;
    }
    return {};
  }

  return ParseRemoteProviders(p.readAllStandardOutput(), error);
}

} // namespace

// Fusion-based dark theme used on Windows, Linux and older macOS.
// Includes Disabled and PlaceholderText roles so secondary states
// stay readable in dark mode.
static void applyDarkTheme() {
  qApp->setStyle(QStyleFactory::create("Fusion"));

  QPalette darkPalette;
  darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::WindowText, Qt::white);
  darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
  darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ToolTipBase, QColor(42, 42, 42));
  darkPalette.setColor(QPalette::ToolTipText, Qt::white);
  darkPalette.setColor(QPalette::Text, Qt::white);
  darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
  darkPalette.setColor(QPalette::ButtonText, Qt::white);
  darkPalette.setColor(QPalette::BrightText, Qt::red);
  darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
  darkPalette.setColor(QPalette::HighlightedText, Qt::black);
  darkPalette.setColor(QPalette::PlaceholderText, QColor(160, 160, 160));

  const QColor disabledText(128, 128, 128);
  darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
  darkPalette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
  darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
  darkPalette.setColor(QPalette::Disabled, QPalette::Highlight,
                       QColor(80, 80, 80));
  darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                       disabledText);

  qApp->setPalette(darkPalette);

  UiPolish::ApplyApplicationStyle(true);
}

MainWindow::MainWindow() {
  ui.setupUi(this);

  if (IsPortableMode()) {
    this->setWindowTitle("Rclone Browser NG - portable mode");
  } else {
    this->setWindowTitle("Rclone Browser NG");
  }

  {
    auto settings = GetSettings();
    bool explicitDark = settings->value("Settings/darkMode").toBool();
    bool systemDark = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    systemDark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#endif
#if defined(Q_OS_MACOS)
    // On macOS 11+ (our deployment target), Qt respects the system dark
    // mode natively — no Fusion override needed.
    Q_UNUSED(explicitDark);
    Q_UNUSED(systemDark);
    UiPolish::ApplyApplicationStyle(false);
#else
    const bool useDarkStyle = explicitDark || systemDark;
    if (useDarkStyle) {
      applyDarkTheme();
    } else {
      UiPolish::ApplyApplicationStyle(false);
    }
#endif
  }

  UiPolish::SetWindowDefaults(this, QSize(720, 460));
  ui.tabs->setDocumentMode(true);
  ui.jobsArea->setFrameShape(QFrame::NoFrame);
  ui.tasksArea->setFrameShape(QFrame::NoFrame);
  ui.verticalLayout->setContentsMargins(12, 12, 12, 12);
  ui.verticalLayout_2->setContentsMargins(12, 12, 12, 12);
  ui.verticalLayout_4->setContentsMargins(0, 0, 0, 0);
  ui.verticalLayout_5->setContentsMargins(12, 12, 12, 12);
  ui.verticalLayout_6->setSpacing(10);
  ui.jobs->setSpacing(8);
  ui.tasksActionBar->setMaximumHeight(QWIDGETSIZE_MAX);
  UiPolish::SetEmptyState(
      ui.noJobsAvailable, "No active work",
      "Transfers, mounts and streams will appear here with live progress.");
  UiPolish::SetMuted(ui.statusBar);
  UiPolish::SetActionBar(ui.tasksActionBar);
  mRemotesFilter = new QLineEdit(this);
  mRemotesFilter->setPlaceholderText("Filter remotes…");
  mRemotesFilter->setClearButtonEnabled(true);
  mRemotesFilter->setAccessibleName("Filter remotes");
  UiPolish::SetPathField(mRemotesFilter, "Filter remotes");
  if (auto *layout = ui.remotes->parentWidget()->layout()) {
    static_cast<QVBoxLayout *>(layout)->insertWidget(0, mRemotesFilter);
  }
  QObject::connect(mRemotesFilter, &QLineEdit::textChanged, this,
                   [=](const QString &text) {
                     bool hasAnyRemote = false;
                     bool hasVisibleMatch = false;
                     for (int i = 0; i < ui.remotes->count(); ++i) {
                       auto *item = ui.remotes->item(i);
                       if (!(item->flags() & Qt::ItemIsEnabled)) {
                         continue;
                       }
                       hasAnyRemote = true;
                       if (item->data(Qt::UserRole + 1).toBool()) {
                         continue;
                       }
                       const bool matches =
                           text.isEmpty() ||
                           item->text().contains(text, Qt::CaseInsensitive);
                       item->setHidden(!matches);
                       hasVisibleMatch = hasVisibleMatch || matches;
                     }
                     if (!mRemotesFilterEmptyItem) {
                       mRemotesFilterEmptyItem = new QListWidgetItem(
                           "No remotes match this filter.");
                       mRemotesFilterEmptyItem->setFlags(Qt::NoItemFlags);
                       mRemotesFilterEmptyItem->setForeground(
                           qApp->palette().color(QPalette::Disabled,
                                                 QPalette::Text));
                       mRemotesFilterEmptyItem->setSizeHint(QSize(0, 54));
                       ui.remotes->addItem(mRemotesFilterEmptyItem);
                     }
                     mRemotesFilterEmptyItem->setHidden(text.isEmpty() ||
                                                        hasVisibleMatch ||
                                                        !hasAnyRemote);
                   });
  UiPolish::SetNavigationView(ui.remotes, "Configured rclone remotes");
  ui.remotes->setSpacing(4);
  {
    auto *viewToggle = new QToolButton(this);
    viewToggle->setIcon(
        QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    viewToggle->setCheckable(true);
    viewToggle->setToolTip("Toggle between list and tile view for remotes.");
    viewToggle->setAccessibleName("Toggle remotes view mode");
    UiPolish::SetCompactToolButton(viewToggle, "View mode",
                                   "Switch between list and tile layout.");
    if (auto *layout = ui.remotes->parentWidget()->layout()) {
      static_cast<QVBoxLayout *>(layout)->insertWidget(1, viewToggle);
    }
    auto settings = GetSettings();
    bool tileMode = settings->value("Settings/remotesTileView", false).toBool();
    viewToggle->setChecked(tileMode);
    if (tileMode) {
      ui.remotes->setViewMode(QListView::IconMode);
      ui.remotes->setResizeMode(QListView::Adjust);
      ui.remotes->setWordWrap(true);
    }
    QObject::connect(viewToggle, &QToolButton::toggled, this, [this](bool checked) {
      auto s = GetSettings();
      s->setValue("Settings/remotesTileView", checked);
      if (checked) {
        ui.remotes->setViewMode(QListView::IconMode);
        ui.remotes->setResizeMode(QListView::Adjust);
        ui.remotes->setWordWrap(true);
      } else {
        ui.remotes->setViewMode(QListView::ListMode);
        ui.remotes->setResizeMode(QListView::Fixed);
        ui.remotes->setWordWrap(false);
      }
    });
  }
  ui.remotes->setUniformItemSizes(true);
  ui.remotes->setTextElideMode(Qt::ElideMiddle);
  ui.remotes->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(
      ui.remotes, &QWidget::customContextMenuRequested, this,
      [this](const QPoint &pos) {
        auto *item = ui.remotes->itemAt(pos);
        if (!item || !(item->flags() & Qt::ItemIsEnabled)) {
          return;
        }
        QString remoteName = item->text();
        QMenu menu(this);
        auto *testConn = menu.addAction("Test Connection");
        auto *storageUsage = menu.addAction("Storage Usage");
        storageUsage->setToolTip(
            "Show storage consumption for this remote.");
        auto *duplicate = menu.addAction("Duplicate Remote...");
        menu.addSeparator();
        auto *autoMount = menu.addAction("Auto-mount on launch");
        autoMount->setCheckable(true);
        {
          auto s = GetSettings();
          QStringList autoMounts =
              s->value("Settings/autoMountRemotes").toStringList();
          autoMount->setChecked(autoMounts.contains(remoteName));
        }
        autoMount->setToolTip(
            "Mount this remote automatically when the app starts.");
        auto *chosen = menu.exec(ui.remotes->viewport()->mapToGlobal(pos));
        if (chosen == testConn) {
          setStatusMessage(QString("Testing %1...").arg(remoteName));
          auto *proc = new QProcess(this);
          proc->setProcessChannelMode(QProcess::MergedChannels);
          UseRclonePassword(proc);
          QStringList args;
          args << "lsjson" << GetRcloneConf() << "--max-depth" << "1"
               << remoteName + ":";
          proc->start(GetRclone(), args, QIODevice::ReadOnly);
          QTimer::singleShot(15000, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning) {
              proc->kill();
            }
          });
          QObject::connect(
              proc,
              static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                  &QProcess::finished),
              this, [=](int code, QProcess::ExitStatus) {
                proc->deleteLater();
                if (code == 0) {
                  setStatusMessage(
                      QString("%1: connection OK").arg(remoteName));
                } else {
                  QString err =
                      QString::fromUtf8(proc->readAll()).trimmed();
                  QMessageBox::warning(
                      this, "Connection test failed",
                      QString("Could not list \"%1:\".\n\n%2")
                          .arg(remoteName, err.left(500)));
                  setStatusMessage(
                      QString("%1: connection failed").arg(remoteName));
                }
              });
        } else if (chosen == storageUsage) {
          setStatusMessage(
              QString("Querying storage for %1...").arg(remoteName));
          auto *proc = new QProcess(this);
          proc->setProcessChannelMode(QProcess::MergedChannels);
          UseRclonePassword(proc);
          QStringList args;
          args << "about" << GetRcloneConf() << "--json"
               << remoteName + ":";
          proc->start(GetRclone(), args, QIODevice::ReadOnly);
          QTimer::singleShot(30000, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning) {
              proc->kill();
            }
          });
          QObject::connect(
              proc,
              static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                  &QProcess::finished),
              this, [=](int code, QProcess::ExitStatus) {
                proc->deleteLater();
                if (code != 0) {
                  QString err =
                      QString::fromUtf8(proc->readAll()).trimmed();
                  QMessageBox::information(
                      this, "Storage Usage",
                      QString("Storage info not available for \"%1\".\n\n%2")
                          .arg(remoteName, err.left(500)));
                  setStatusMessage(QString());
                  return;
                }
                QJsonDocument doc =
                    QJsonDocument::fromJson(proc->readAll());
                QJsonObject obj = doc.object();
                QStringList lines;
                lines << QString("<b>%1</b>").arg(remoteName);
                auto fmt = [](qint64 bytes) -> QString {
                  return GetNiceSize(static_cast<quint64>(bytes));
                };
                if (obj.contains("total"))
                  lines << "Total: " + fmt(obj.value("total").toVariant().toLongLong());
                if (obj.contains("used"))
                  lines << "Used: " + fmt(obj.value("used").toVariant().toLongLong());
                if (obj.contains("free"))
                  lines << "Free: " + fmt(obj.value("free").toVariant().toLongLong());
                if (obj.contains("trashed"))
                  lines << "Trash: " + fmt(obj.value("trashed").toVariant().toLongLong());
                if (obj.contains("other"))
                  lines << "Other: " + fmt(obj.value("other").toVariant().toLongLong());
                QMessageBox::information(
                    this, "Storage Usage",
                    lines.join("<br>"));
                setStatusMessage(QString());
              });
        } else if (chosen == duplicate) {
          QString newName = QInputDialog::getText(
              this, "Duplicate Remote",
              QString("New name for the copy of \"%1\":").arg(remoteName));
          if (newName.isEmpty()) {
            return;
          }
          auto *proc = new QProcess(this);
          proc->setProcessChannelMode(QProcess::MergedChannels);
          UseRclonePassword(proc);
          QStringList args;
          args << "config" << "copy" << remoteName << newName
               << GetRcloneConf();
          proc->start(GetRclone(), args, QIODevice::ReadOnly);
          QObject::connect(
              proc,
              static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                  &QProcess::finished),
              this, [=](int code, QProcess::ExitStatus) {
                proc->deleteLater();
                if (code == 0) {
                  rcloneListRemotes();
                  setStatusMessage(
                      QString("Duplicated %1 as %2")
                          .arg(remoteName, newName));
                } else {
                  QString err =
                      QString::fromUtf8(proc->readAll()).trimmed();
                  QMessageBox::warning(
                      this, "Duplicate failed",
                      QString("Could not duplicate \"%1\".\n\n%2")
                          .arg(remoteName, err.left(500)));
                }
              });
        } else if (chosen == autoMount) {
          auto s = GetSettings();
          QStringList autoMounts =
              s->value("Settings/autoMountRemotes").toStringList();
          if (autoMount->isChecked()) {
            if (!autoMounts.contains(remoteName)) {
              autoMounts.append(remoteName);
            }
            setStatusMessage(
                QString("%1 will auto-mount on launch.").arg(remoteName));
          } else {
            autoMounts.removeAll(remoteName);
            setStatusMessage(
                QString("%1 removed from auto-mount.").arg(remoteName));
          }
          s->setValue("Settings/autoMountRemotes", autoMounts);
        }
      });
  UiPolish::SetNavigationView(ui.tasksListWidget, "Saved tasks");
  ui.tasksListWidget->setSpacing(4);
  ui.tasksListWidget->setUniformItemSizes(true);
  ui.tasksListWidget->setTextElideMode(Qt::ElideRight);
  ui.config->setAccessibleName("Open rclone configuration");
  ui.newRemote->setAccessibleName("Create a new rclone remote");
  ui.refresh->setAccessibleName("Refresh remotes");
  ui.open->setAccessibleName("Open selected remote");
  ui.config->setToolTip("Open rclone config in a terminal.");
  ui.newRemote->setToolTip("Create a remote using the provider list from the installed rclone.");
  ui.refresh->setToolTip("Reload remotes from rclone.conf.");
  ui.open->setToolTip("Open the selected remote in a browser tab.");
  UiPolish::SetPrimaryButton(ui.open);

  mSystemTray.setIcon(qApp->windowIcon());
  updateJobIndicators();
  {
    auto settings = GetSettings();
    if (settings->contains("MainWindow/geometry")) {
      restoreGeometry(settings->value("MainWindow/geometry").toByteArray());
    }
    SetRclone(settings->value("Settings/rclone").toString());
    SetRcloneConf(settings->value("Settings/rcloneConf").toString());
    SetRclonePasswordCommandEnabled(
        settings->value("Settings/usePasswordCommand", false).toBool());

    mAlwaysShowInTray =
        settings->value("Settings/alwaysShowInTray", false).toBool();
    mCloseToTray = settings->value("Settings/closeToTray", false).toBool();
    mNotifyFinishedTransfers =
        settings->value("Settings/notifyFinishedTransfers", true).toBool();

    mSystemTray.setVisible(mAlwaysShowInTray);

    // during first run the lastUsed keys might not exist
    if (!(settings->contains("Settings/lastUsedSourceFolder"))) {
      // if lastUsedSourceFolder does not exist create new empty key
      settings->setValue("Settings/lastUsedSourceFolder", "");
    };
    if (!(settings->contains("Settings/lastUsedDestFolder"))) {
      // if lastUsedDestFolder does not exist create new empty key
      settings->setValue("Settings/lastUsedDestFolder", "");
    };
    if (!(settings->contains("Settings/defaultDownloadOptions"))) {
      // if defaultDownloadOptions does not exist create new empty key
      settings->setValue("Settings/defaultDownloadOptions", "");
    };
#ifdef Q_OS_MACOS
    // for macOS by default exclude .DS_Store files from uploads
    if (!(settings->contains("Settings/defaultUploadOptions"))) {
      // if defaultDownloadOptions does not exist create new empty key
      settings->setValue("Settings/defaultUploadOptions",
                         "--exclude .DS_Store");
    };
#else
    if (!(settings->contains("Settings/defaultUploadOptions"))) {
      // if defaultDownloadOptions does not exist create new empty key
      settings->setValue("Settings/defaultUploadOptions", "");
    };
#endif
    if (!(settings->contains("Settings/defaultRcloneOptions"))) {
      // if defaultRcloneOptions does not exist create new empty key
      settings->setValue("Settings/defaultRcloneOptions", "--fast-list");
    };
  }

  QObject::connect(ui.preferences, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();
    const QString oldRclone = settings->value("Settings/rclone").toString();
    const QString oldRcloneConf =
        settings->value("Settings/rcloneConf").toString();

    PreferencesDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
      const QString newRclone = dialog.getRclone().trimmed();
      const QString newRcloneConf = dialog.getRcloneConf().trimmed();
      if (mJobCount > 0 &&
          (newRclone != oldRclone || newRcloneConf != oldRcloneConf) &&
          !confirmConfigMutation("Changing the rclone executable or config path")) {
        return;
      }

      settings->setValue("Settings/rclone", newRclone);
      settings->setValue("Settings/rcloneConf", newRcloneConf);
      settings->setValue("Settings/stream", dialog.getStream());
      settings->setValue("Settings/mount", dialog.getMount());
      settings->setValue("Settings/defaultDownloadDir",
                         dialog.getDefaultDownloadDir().trimmed());
      settings->setValue("Settings/defaultUploadDir",
                         dialog.getDefaultUploadDir().trimmed());
      settings->setValue("Settings/defaultDownloadOptions",
                         dialog.getDefaultDownloadOptions().trimmed());
      settings->setValue("Settings/defaultUploadOptions",
                         dialog.getDefaultUploadOptions().trimmed());
      settings->setValue("Settings/defaultRcloneOptions",
                         dialog.getDefaultRcloneOptions().trimmed());
      settings->setValue("Settings/defaultExclude",
                         dialog.getDefaultExclude());

      settings->setValue("Settings/checkRcloneBrowserUpdates",
                         dialog.getCheckRcloneBrowserUpdates());
      settings->setValue("Settings/checkRcloneUpdates",
                         dialog.getCheckRcloneUpdates());

      settings->setValue("Settings/alwaysShowInTray",
                         dialog.getAlwaysShowInTray());
      settings->setValue("Settings/closeToTray", dialog.getCloseToTray());
      settings->setValue("Settings/notifyFinishedTransfers",
                         dialog.getNotifyFinishedTransfers());
      settings->setValue("Settings/startMinimized",
                         dialog.getStartMinimized());
      settings->setValue("Settings/maxConcurrentTransfers",
                         dialog.getMaxConcurrentTransfers());

      settings->setValue("Settings/showFolderIcons",
                         dialog.getShowFolderIcons());
      settings->setValue("Settings/showFileIcons", dialog.getShowFileIcons());
      settings->setValue("Settings/rowColors", dialog.getRowColors());
      settings->setValue("Settings/showHidden", dialog.getShowHidden());
      settings->setValue("Settings/darkMode", dialog.getDarkMode());
      settings->setValue("Settings/iconSize", dialog.getIconSize().trimmed());

      settings->setValue("Settings/useProxy", dialog.getUseProxy());
      settings->setValue("Settings/http_proxy",
                         dialog.getHttpProxy().trimmed());
      settings->setValue("Settings/https_proxy",
                         dialog.getHttpsProxy().trimmed());
      settings->setValue("Settings/no_proxy", dialog.getNoProxy().trimmed());
      settings->setValue("Settings/socksProxy",
                         dialog.getSocksProxy().trimmed());
      const bool oldUsePasswordCommand = IsRclonePasswordCommandEnabled();
      settings->setValue("Settings/usePasswordCommand",
                         dialog.getUsePasswordCommand());

      SetRclone(newRclone);
      SetRcloneConf(newRcloneConf);
      SetRclonePasswordCommandEnabled(dialog.getUsePasswordCommand());
      if (oldUsePasswordCommand && !dialog.getUsePasswordCommand()) {
        ClearRcloneConfigPassword();
      }
      mFirstTime = true;
      rcloneGetVersion();

      mAlwaysShowInTray = dialog.getAlwaysShowInTray();
      mCloseToTray = dialog.getCloseToTray();
      mNotifyFinishedTransfers = dialog.getNotifyFinishedTransfers();

      mSystemTray.setVisible(mAlwaysShowInTray);
    }
  });

  QObject::connect(ui.quit, &QAction::triggered, this, [=]() {
    mCloseToTray = false;
    close();
  });

  QObject::connect(ui.about, &QAction::triggered, this, [=]() {
    auto caps = RcloneCapabilities::detect();
    QString env = QString(
        R"(<p style="font-size:small; color:gray;">)"
        R"(rclone %1 &middot; Qt %2 &middot; %3 %4 (%5)</p>)")
        .arg(caps.rcloneVersion.isEmpty() ? "?" : "v" + caps.rcloneVersion,
             qVersion(),
             QSysInfo::productType(),
             QSysInfo::productVersion(),
             QSysInfo::currentCpuArchitecture());
    QMessageBox::about(
        this, qApp->applicationDisplayName(),
        QString(
            R"(<h3>Rclone Browser NG, v)" RCLONE_BROWSER_VERSION "</h3>"
            R"(<p>GUI for <a href="https://rclone.org/">rclone</a></p>)"

            R"(<p>Community continuation<br /><a href="https://github.com/SysAdminDoc/RcloneBrowserNG">RcloneBrowserNG</a></p>)"

            R"(<p>Previous maintenance and features<br /><a href="https://github.com/kapitainsky/RcloneBrowser">kapitainsky</a> and <a href="https://github.com/kapitainsky/RcloneBrowser/graphs/contributors">contributors</a></p>)"

            R"(<p>Original version<br /><a href="https://mmozeiko.github.io/RcloneBrowser">Martins Mozeiko</a></p>)")
        + env);
  });

  auto *copyDiagnostics = new QAction("Copy Diagnostics", this);
  copyDiagnostics->setToolTip(
      "Copy environment and capability info to the clipboard for bug reports.");
  auto *saveSupportBundle = new QAction("Save Support Bundle...", this);
  saveSupportBundle->setToolTip(
      "Save a detailed support bundle with environment info and recent job "
      "output (secrets redacted) for bug reports.");
  ui.menuHelp->addSeparator();
  ui.menuHelp->addAction(copyDiagnostics);
  ui.menuHelp->addAction(saveSupportBundle);
  QObject::connect(copyDiagnostics, &QAction::triggered, this, [=]() {
    auto caps = RcloneCapabilities::detect();
    QGuiApplication::clipboard()->setText(caps.summary());
    ui.statusBar->showMessage("Diagnostics copied to clipboard.", 4000);
  });
  QObject::connect(saveSupportBundle, &QAction::triggered, this, [=]() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Support Bundle", "rclonebrowserng-support.txt",
        "Text files (*.txt)");
    if (path.isEmpty()) {
      return;
    }
    auto caps = RcloneCapabilities::detect();
    QString log = Diagnostics::recentLog();
    QString bundle = caps.summary();
    if (!log.isEmpty()) {
      bundle += "\n\n--- Recent output (secrets redacted) ---\n";
      bundle += Diagnostics::redactSecrets(log);
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      file.write(bundle.toUtf8());
      file.close();
      ui.statusBar->showMessage(
          "Support bundle saved to " + QFileInfo(path).fileName(), 4000);
    }
  });

  QObject::connect(ui.aboutQt, &QAction::triggered, qApp,
                   &QApplication::aboutQt);

  QObject::connect(
      ui.remotes, &QListWidget::currentItemChanged, this,
      [=](QListWidgetItem *current) { ui.open->setEnabled(current != NULL); });
  QObject::connect(ui.remotes, &QListWidget::itemActivated, ui.open,
                   &QPushButton::clicked);

  QObject::connect(ui.config, &QPushButton::clicked, this,
                   &MainWindow::rcloneConfig);
  QObject::connect(ui.newRemote, &QPushButton::clicked, this,
                   &MainWindow::createRemote);
  QObject::connect(ui.refresh, &QPushButton::clicked, this,
                   &MainWindow::rcloneListRemotes);

  auto makeRemoteWidget = [this](const QString &name, const QString &type,
                                  QWidget *parent) -> RemoteWidget * {
    bool isLocal = type == "local";
    bool isGoogle = type == "drive";
    bool isGooglePhotos =
        type.compare("google photos", Qt::CaseInsensitive) == 0;
    auto *remote =
        new RemoteWidget(&mIcons, name, isLocal, isGoogle, isGooglePhotos,
                         parent);
    QObject::connect(remote, &RemoteWidget::addMount, this,
                     &MainWindow::addMount);
    QObject::connect(remote, &RemoteWidget::addStream, this,
                     &MainWindow::addStream);
    QObject::connect(remote, &RemoteWidget::addTransfer, this,
                     &MainWindow::addTransfer);
    QObject::connect(
        remote, &RemoteWidget::enqueueTransfer, this,
        [this](const QString &msg, const QString &src, const QString &dst,
               const QStringList &args) {
          auto *item = new QListWidgetItem(msg, mStagingList);
          item->setData(Qt::UserRole, msg);
          item->setData(Qt::UserRole + 1, src);
          item->setData(Qt::UserRole + 2, dst);
          item->setData(Qt::UserRole + 3, args);
          item->setToolTip(QString("%1 -> %2").arg(src, dst));
          setStatusMessage(
              QString("Enqueued: %1 (%2 staged)")
                  .arg(msg)
                  .arg(mStagingList->count()));
        });
    QObject::connect(remote, &RemoteWidget::requestReconnect, this,
                     [this](const QString &remoteName) {
                       const QDateTime configBefore = rcloneConfigLastModified();
                       startDetachedTerminalCommand(
                           QStringList() << "config" << "reconnect"
                                         << remoteName + ":",
                           configBefore, "Reconnect remote");
                     });
    return remote;
  };

  QObject::connect(ui.open, &QPushButton::clicked, this, [=]() {
    auto selection = ui.remotes->selectedItems();
    if (selection.isEmpty()) {
      return;
    }
    auto item = selection.front();
    auto *remote = makeRemoteWidget(item->text(),
                                     item->data(Qt::UserRole).toString(),
                                     ui.tabs);
    int index = ui.tabs->addTab(remote, item->text());
    ui.tabs->setCurrentIndex(index);
  });

  auto *dualPaneBtn = new QPushButton("Dual Pane", this);
  dualPaneBtn->setToolTip(
      "Open a side-by-side split view with two remote browsers.");
  dualPaneBtn->setAccessibleName("Open dual-pane view");
  dualPaneBtn->setEnabled(false);
  if (auto *layout = ui.open->parentWidget()->layout()) {
    static_cast<QHBoxLayout *>(layout)->insertWidget(
        static_cast<QHBoxLayout *>(layout)->indexOf(ui.open) + 1, dualPaneBtn);
  }
  QObject::connect(ui.remotes, &QListWidget::itemSelectionChanged, this,
                   [this, dualPaneBtn]() {
                     dualPaneBtn->setEnabled(
                         !ui.remotes->selectedItems().isEmpty());
                   });
  QObject::connect(dualPaneBtn, &QPushButton::clicked, this, [=]() {
    auto selection = ui.remotes->selectedItems();
    if (selection.isEmpty())
      return;
    auto *leftItem = selection.front();
    QString leftName = leftItem->text();
    QString leftType = leftItem->data(Qt::UserRole).toString();

    QStringList remoteNames;
    remoteNames << "(Local filesystem)";
    for (int i = 0; i < ui.remotes->count(); ++i) {
      auto *ri = ui.remotes->item(i);
      if ((ri->flags() & Qt::ItemIsEnabled) && !ri->isHidden())
        remoteNames << ri->text();
    }
    bool ok;
    QString rightName = QInputDialog::getItem(
        this, "Dual Pane",
        QString("Right panel remote (left is %1):").arg(leftName),
        remoteNames, 0, false, &ok);
    if (!ok)
      return;

    QString rightType = "local";
    if (rightName != "(Local filesystem)") {
      for (int i = 0; i < ui.remotes->count(); ++i) {
        auto *ri = ui.remotes->item(i);
        if (ri->text() == rightName) {
          rightType = ri->data(Qt::UserRole).toString();
          break;
        }
      }
    } else {
      rightName = "/";
      rightType = "local";
    }

    auto *splitter = new QSplitter(Qt::Horizontal, ui.tabs);
    splitter->addWidget(makeRemoteWidget(leftName, leftType, splitter));
    splitter->addWidget(makeRemoteWidget(rightName, rightType, splitter));
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    int index = ui.tabs->addTab(
        splitter, QString("%1 | %2").arg(leftName, rightName));
    ui.tabs->setCurrentIndex(index);
  });

  QObject::connect(ui.tabs, &QTabWidget::tabCloseRequested, ui.tabs,
                   &QTabWidget::removeTab);

  auto *stagingLabel = new QToolButton(this);
  stagingLabel->setCheckable(true);
  stagingLabel->setChecked(false);
  stagingLabel->setArrowType(Qt::RightArrow);
  stagingLabel->setText("Staging Queue");
  stagingLabel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  stagingLabel->setAutoRaise(true);
  UiPolish::SetDisclosureButton(stagingLabel, "Toggle staging queue");
  mStagingList = new QListWidget(this);
  mStagingList->setVisible(false);
  mStagingList->setMaximumHeight(120);
  mStagingList->setAccessibleName("Staged transfers awaiting review");
  mStagingList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  auto *stagingBar = new QWidget(this);
  auto *stagingBarLayout = new QHBoxLayout(stagingBar);
  stagingBarLayout->setContentsMargins(0, 0, 0, 0);
  stagingBarLayout->setSpacing(4);
  auto *runStaged = new QPushButton("Run All", stagingBar);
  runStaged->setToolTip("Execute all staged transfers now.");
  runStaged->setAccessibleName("Run all staged transfers");
  UiPolish::SetPrimaryButton(runStaged);
  auto *clearStaged = new QPushButton("Clear", stagingBar);
  clearStaged->setToolTip("Remove all staged transfers without running them.");
  clearStaged->setAccessibleName("Clear staging queue");
  stagingBarLayout->addWidget(runStaged);
  stagingBarLayout->addWidget(clearStaged);
  stagingBarLayout->addStretch();
  stagingBar->setVisible(false);

  QObject::connect(stagingLabel, &QToolButton::toggled, this,
                   [=](bool checked) {
                     mStagingList->setVisible(checked);
                     stagingBar->setVisible(checked);
                     stagingLabel->setArrowType(
                         checked ? Qt::DownArrow : Qt::RightArrow);
                   });
  QObject::connect(runStaged, &QPushButton::clicked, this, [this]() {
    while (mStagingList->count() > 0) {
      auto *item = mStagingList->item(0);
      QString msg = item->data(Qt::UserRole).toString();
      QString src = item->data(Qt::UserRole + 1).toString();
      QString dst = item->data(Qt::UserRole + 2).toString();
      QStringList args = item->data(Qt::UserRole + 3).toStringList();
      addTransfer(msg, src, dst, args);
      delete mStagingList->takeItem(0);
    }
    setStatusMessage("All staged transfers started.");
  });
  QObject::connect(clearStaged, &QPushButton::clicked, this, [this]() {
    mStagingList->clear();
    setStatusMessage("Staging queue cleared.");
  });

  if (auto *layout =
          qobject_cast<QVBoxLayout *>(ui.tasksListWidget->parentWidget()->layout())) {
    layout->insertWidget(0, stagingLabel);
    layout->insertWidget(1, mStagingList);
    layout->insertWidget(2, stagingBar);
  }

  mTasksFilter = new QLineEdit(this);
  mTasksFilter->setPlaceholderText("Filter saved tasks...");
  mTasksFilter->setClearButtonEnabled(true);
  mTasksFilter->setAccessibleName("Filter saved tasks");
  UiPolish::SetPathField(mTasksFilter, "Filter saved tasks");
  if (auto *layout =
          qobject_cast<QVBoxLayout *>(ui.tasksListWidget->parentWidget()->layout())) {
    layout->insertWidget(0, mTasksFilter);
  }
  QObject::connect(mTasksFilter, &QLineEdit::textChanged, this,
                   [this](const QString &text) {
                     for (int i = 0; i < ui.tasksListWidget->count(); ++i) {
                       auto *item = ui.tasksListWidget->item(i);
                       auto *task =
                           static_cast<JobOptionsListWidgetItem *>(item);
                       if (!task->GetData()) {
                         item->setHidden(!text.isEmpty());
                         continue;
                       }
                       item->setHidden(
                           !text.isEmpty() &&
                           !task->GetData()->description.contains(
                               text, Qt::CaseInsensitive) &&
                           !task->GetData()->source.contains(
                               text, Qt::CaseInsensitive) &&
                           !task->GetData()->dest.contains(
                               text, Qt::CaseInsensitive));
                     }
                   });

  ui.tasksListWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);

  QObject::connect(ui.tasksListWidget, &QListWidget::itemSelectionChanged, this,
                   [=]() {
                     QList<QListWidgetItem *> sel =
                         ui.tasksListWidget->selectedItems();
                     int validCount = 0;
                     for (auto *item : sel) {
                       auto *task =
                           static_cast<JobOptionsListWidgetItem *>(item);
                       if (task && task->GetData())
                         ++validCount;
                     }
                     const bool hasOne = validCount == 1;
                     const bool hasAny = validCount > 0;
                     ui.buttonDeleteTask->setEnabled(hasAny);
                     ui.buttonEditTask->setEnabled(hasOne);
                     ui.buttonRunTask->setEnabled(hasAny);
                     ui.buttonDryrunTask->setEnabled(hasAny);
                     ui.buttonCopyTaskCmd->setEnabled(hasOne);
                   });

  QObject::connect(ui.buttonRunTask, &QPushButton::clicked, this, [=]() {
    for (auto *item : ui.tasksListWidget->selectedItems()) {
      auto *task = static_cast<JobOptionsListWidgetItem *>(item);
      if (task && task->GetData())
        runItem(task);
    }
  });
  QObject::connect(ui.buttonDryrunTask, &QPushButton::clicked, this, [=]() {
    for (auto *item : ui.tasksListWidget->selectedItems()) {
      auto *task = static_cast<JobOptionsListWidgetItem *>(item);
      if (task && task->GetData())
        runItem(task, true);
    }
  });

  //    QObject::connect(ui.tasksListWidget, &QListWidget::itemDoubleClicked,
  //    this, [=]()
  //    {
  //        editSelectedTask();
  //    });

  QObject::connect(ui.buttonEditTask, &QPushButton::clicked, this,
                   [=]() { editSelectedTask(); });

  QObject::connect(ui.buttonCopyTaskCmd, &QPushButton::clicked, this, [=]() {
    JobOptionsListWidgetItem *item = static_cast<JobOptionsListWidgetItem *>(
        ui.tasksListWidget->currentItem());
    if (!item || !item->GetData())
      return;
    JobOptions *jo = item->GetData();
    QStringList cmd;
    cmd << QDir::toNativeSeparators(GetRclone());
    cmd << GetRcloneConf();
    cmd << jo->getOptions();
    QStringList quoted;
    for (const auto &arg : cmd) {
      if (arg.contains(' ') || arg.contains('"')) {
        quoted << '"' + QString(arg).replace('"', "\\\"") + '"';
      } else {
        quoted << arg;
      }
    }
    QGuiApplication::clipboard()->setText(quoted.join(" "));
  });

  ui.tasksListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(
      ui.tasksListWidget, &QWidget::customContextMenuRequested, this,
      [=](const QPoint &pos) {
        auto *item = static_cast<JobOptionsListWidgetItem *>(
            ui.tasksListWidget->itemAt(pos));
        if (!item || !item->GetData())
          return;
        QMenu menu;
        QAction *exportAction = menu.addAction("Export as script...");
        if (menu.exec(ui.tasksListWidget->mapToGlobal(pos)) != exportAction)
          return;

        JobOptions *jo = item->GetData();
        QStringList cmd;
        cmd << QDir::toNativeSeparators(GetRclone());
        cmd << GetRcloneConf();
        cmd << jo->getOptions();

#ifdef Q_OS_WIN
        QString filter = "PowerShell (*.ps1);;Batch (*.bat);;All (*)";
#else
        QString filter = "Shell (*.sh);;All (*)";
#endif
        QString path = QFileDialog::getSaveFileName(
            this, "Export Task Script", jo->description, filter);
        if (path.isEmpty())
          return;

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
          QMessageBox::warning(this, "Export", "Cannot write to " + path);
          return;
        }
        QTextStream out(&f);
#ifdef Q_OS_WIN
        if (path.endsWith(".bat", Qt::CaseInsensitive)) {
          out << "@echo off\r\n";
          for (const auto &arg : cmd) {
            if (arg.contains(' '))
              out << '"' << arg << "\" ";
            else
              out << arg << ' ';
          }
          out << "\r\npause\r\n";
        } else {
          for (const auto &arg : cmd) {
            if (arg.contains(' ') || arg.contains('\''))
              out << '"' << QString(arg).replace('"', "`\"") << "\" ";
            else
              out << arg << ' ';
          }
          out << "\n";
        }
#else
        out << "#!/bin/sh\n";
        for (const auto &arg : cmd) {
          if (arg.contains(' ') || arg.contains('\'') || arg.contains('"'))
            out << '\'' << QString(arg).replace('\'', "'\\''") << "' ";
          else
            out << arg << ' ';
        }
        out << "\n";
#endif
      });

  QObject::connect(ui.buttonDeleteTask, &QPushButton::clicked, this, [=]() {
    QList<QListWidgetItem *> sel = ui.tasksListWidget->selectedItems();
    QList<JobOptions *> toDelete;
    for (auto *item : sel) {
      auto *task = static_cast<JobOptionsListWidgetItem *>(item);
      if (task && task->GetData())
        toDelete << task->GetData();
    }
    if (toDelete.isEmpty())
      return;
    QString prompt =
        toDelete.size() == 1
            ? QString("Delete saved task \"%1\"?\n\nThis cannot be undone.")
                  .arg(toDelete.first()->description)
            : QString("Delete %1 saved tasks?\n\nThis cannot be undone.")
                  .arg(toDelete.size());
    int button = QMessageBox::question(this, "Delete Task", prompt,
                                       QMessageBox::Yes | QMessageBox::No,
                                       QMessageBox::No);
    if (button == QMessageBox::Yes) {
      auto *store = ListOfJobOptions::getInstance();
      for (auto *jo : toDelete)
        store->Forget(jo);
    }
  });

  auto *taskStore = ListOfJobOptions::getInstance();
  if (!taskStore->lastLoadError().isEmpty()) {
    QMessageBox::warning(this, "Saved Tasks",
                         taskStore->lastLoadError());
  }
  QObject::connect(taskStore, &ListOfJobOptions::tasksListUpdated, this,
                   [this]() {
                     listTasks();
                     refreshTaskWatchers();
                   });

  QStyle *style = QApplication::style();
  ui.config->setIcon(style->standardIcon(QStyle::SP_FileDialogDetailedView));
  ui.newRemote->setIcon(style->standardIcon(QStyle::SP_FileDialogNewFolder));
  ui.refresh->setIcon(style->standardIcon(QStyle::SP_BrowserReload));
  ui.open->setIcon(style->standardIcon(QStyle::SP_DialogOpenButton));
  ui.buttonDeleteTask->setIcon(style->standardIcon(QStyle::SP_TrashIcon));
  ui.buttonEditTask->setIcon(style->standardIcon(QStyle::SP_FileIcon));
  ui.buttonRunTask->setIcon(style->standardIcon(QStyle::SP_CommandLink));
  ui.buttonCopyTaskCmd->setIcon(style->standardIcon(QStyle::SP_FileLinkIcon));
  ui.buttonDryrunTask->setIcon(style->standardIcon(QStyle::SP_FileDialogContentsView));
  ui.buttonDryrunTask->setText("Dry Run");
  ui.buttonCopyTaskCmd->setText("Copy Command");
  ui.buttonDryrunTask->setToolTip("Dry-run selected task(s) with --dry-run.");
  ui.buttonRunTask->setToolTip("Run selected saved task(s). Shift/Ctrl-click to select multiple.");
  ui.buttonEditTask->setToolTip("Edit task options (single selection only).");
  ui.buttonDeleteTask->setToolTip("Delete selected saved task(s).");
  ui.buttonCopyTaskCmd->setToolTip("Copy the selected task's rclone command (single selection only).");
  ui.buttonDryrunTask->setAccessibleName("Dry run selected tasks");
  ui.buttonRunTask->setAccessibleName("Run selected tasks");
  ui.buttonEditTask->setAccessibleName("Edit selected task");
  ui.buttonDeleteTask->setAccessibleName("Delete selected tasks");
  ui.buttonCopyTaskCmd->setAccessibleName("Copy selected task command");
  UiPolish::SetPrimaryButton(ui.buttonRunTask);
  UiPolish::SetDestructiveButton(ui.buttonDeleteTask);

  if (ScheduleManager::isSupported()) {
    auto *scheduleBtn = new QPushButton("Schedule", this);
    scheduleBtn->setIcon(style->standardIcon(QStyle::SP_FileDialogDetailedView));
    scheduleBtn->setToolTip("Install a native OS scheduled task for the selected saved task.");
    scheduleBtn->setAccessibleName("Schedule selected task");
    scheduleBtn->setEnabled(false);
    auto *unscheduleBtn = new QPushButton("Unschedule", this);
    unscheduleBtn->setIcon(style->standardIcon(QStyle::SP_DialogDiscardButton));
    unscheduleBtn->setToolTip("Remove the OS scheduled task for the selected saved task.");
    unscheduleBtn->setAccessibleName("Unschedule selected task");
    unscheduleBtn->setEnabled(false);

    if (auto *layout = qobject_cast<QHBoxLayout *>(
            ui.buttonDeleteTask->parentWidget()->layout())) {
      layout->addWidget(scheduleBtn);
      layout->addWidget(unscheduleBtn);
    }

    QObject::connect(ui.tasksListWidget, &QListWidget::itemSelectionChanged,
                     this, [=]() {
                       QList<QListWidgetItem *> sel =
                           ui.tasksListWidget->selectedItems();
                       bool hasOne = false;
                       for (auto *item : sel) {
                         auto *t =
                             static_cast<JobOptionsListWidgetItem *>(item);
                         if (t && t->GetData()) {
                           hasOne = true;
                           break;
                         }
                       }
                       scheduleBtn->setEnabled(hasOne);
                       unscheduleBtn->setEnabled(hasOne);
                     });

    QObject::connect(scheduleBtn, &QPushButton::clicked, this, [this]() {
      auto *item = static_cast<JobOptionsListWidgetItem *>(
          ui.tasksListWidget->currentItem());
      if (!item || !item->GetData())
        return;
      QString taskName = item->GetData()->description;

      QStringList intervals;
      intervals << "15m" << "30m" << "hourly" << "daily" << "weekly";
      bool ok;
      QString interval = QInputDialog::getItem(
          this, "Schedule Task",
          QString("Run \"%1\" every:").arg(taskName), intervals, 3, false,
          &ok);
      if (!ok)
        return;

      QString time;
      if (interval == "daily" || interval == "weekly") {
        time = QInputDialog::getText(
            this, "Schedule Task", "Start time (HH:MM, 24h format):",
            QLineEdit::Normal, "02:00", &ok);
        if (!ok)
          return;
      }

      QString error;
      if (ScheduleManager::installSchedule(taskName, interval, time, &error)) {
        setStatusMessage(
            QString("Scheduled \"%1\" every %2").arg(taskName, interval));
      } else {
        QMessageBox::warning(this, "Schedule failed",
                             QString("Could not schedule \"%1\":\n%2")
                                 .arg(taskName, error));
      }
    });

    QObject::connect(unscheduleBtn, &QPushButton::clicked, this, [this]() {
      auto *item = static_cast<JobOptionsListWidgetItem *>(
          ui.tasksListWidget->currentItem());
      if (!item || !item->GetData())
        return;
      QString taskName = item->GetData()->description;
      QString error;
      if (ScheduleManager::removeSchedule(taskName, &error)) {
        setStatusMessage(
            QString("Removed schedule for \"%1\"").arg(taskName));
      } else {
        QMessageBox::warning(this, "Unschedule failed",
                             QString("Could not remove schedule:\n%1")
                                 .arg(error));
      }
    });
  }

  mUploadIcon = style->standardIcon(QStyle::SP_ArrowUp);
  mDownloadIcon = style->standardIcon(QStyle::SP_ArrowDown);

  ui.tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
  ui.tabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
  ui.tabs->tabBar()->setTabButton(1, QTabBar::RightSide, nullptr);
  ui.tabs->tabBar()->setTabButton(1, QTabBar::LeftSide, nullptr);
  ui.tabs->tabBar()->setTabButton(2, QTabBar::RightSide, nullptr);
  ui.tabs->tabBar()->setTabButton(2, QTabBar::LeftSide, nullptr);
  ui.tabs->tabBar()->installEventFilter(this);
  ui.tabs->setCurrentIndex(0);

  listTasks();

  QObject::connect(&mSystemTray, &QSystemTrayIcon::activated, this,
                   [=](QSystemTrayIcon::ActivationReason reason) {
                     if (reason == QSystemTrayIcon::DoubleClick ||
                         reason == QSystemTrayIcon::Trigger) {
                       showNormal();
                       mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_MACOS
                       osxShowDockIcon();
#endif
                     }
                   });

  QObject::connect(&mSystemTray, &QSystemTrayIcon::messageClicked, this, [=]() {
    showNormal();
    mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_MACOS
    osxShowDockIcon();
#endif

    ui.tabs->setCurrentIndex(1);
    if (mLastFinished) {
      mLastFinished->showDetails();
      ui.jobsArea->ensureWidgetVisible(mLastFinished);
    }
  });

  QMenu *trayMenu = new QMenu(this);
  QObject::connect(
      trayMenu->addAction("&Show"), &QAction::triggered, this, [=]() {
        MainWindow::setWindowState((windowState() & ~Qt::WindowMinimized) |
                                   Qt::WindowActive);
        MainWindow::show();  // bring window to top on macOS
        MainWindow::raise(); // bring window from minimized state on macOS
        MainWindow::activateWindow(); // bring window to front/unminimize on
                                      // windows
        mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_MACOS
        osxShowDockIcon();
#endif
      });
  QAction *trayHistory = trayMenu->addAction("Job &History");
  QObject::connect(trayHistory, &QAction::triggered, this,
                   &MainWindow::showJobHistory);
  mWatchMenu = trayMenu->addMenu("Watch &Folders");
  QObject::connect(trayMenu->addAction("&Quit"), &QAction::triggered, this,
                   &QWidget::close);
  mSystemTray.setContextMenu(trayMenu);

  QAction *historyAction = new QAction("Job &History", this);
  QObject::connect(historyAction, &QAction::triggered, this,
                   &MainWindow::showJobHistory);
  ui.menuFile->insertAction(ui.quit, historyAction);
  auto *configMenu = new QMenu("Switch &Config", this);
  ui.menuFile->insertMenu(ui.quit, configMenu);
  auto rebuildConfigMenu = [this, configMenu]() {
    configMenu->clear();
    auto settings = GetSettings();
    QStringList configs =
        settings->value("Settings/configProfiles").toStringList();
    QString current =
        settings->value("Settings/rcloneConf").toString();
    for (const QString &path : configs) {
      QAction *action = configMenu->addAction(QFileInfo(path).fileName());
      action->setToolTip(path);
      action->setCheckable(true);
      action->setChecked(path == current);
      QObject::connect(action, &QAction::triggered, this,
                       [this, path]() {
                         auto s = GetSettings();
                         s->setValue("Settings/rcloneConf", path);
                         SetRcloneConf(path);
                         rcloneListRemotes();
                         setStatusMessage("Switched to " +
                                          QFileInfo(path).fileName());
                       });
    }
    if (!configs.isEmpty()) {
      configMenu->addSeparator();
    }
    auto *addConfig = configMenu->addAction("Add Config File...");
    QObject::connect(addConfig, &QAction::triggered, this, [this, rebuildConfigMenu = std::function<void()>()]() mutable {
      QString path = QFileDialog::getOpenFileName(
          this, "Select rclone config file");
      if (path.isEmpty())
        return;
      auto s = GetSettings();
      QStringList configs =
          s->value("Settings/configProfiles").toStringList();
      if (!configs.contains(path)) {
        configs.append(path);
        s->setValue("Settings/configProfiles", configs);
      }
      s->setValue("Settings/rcloneConf", path);
      SetRcloneConf(path);
      rcloneListRemotes();
      setStatusMessage("Switched to " + QFileInfo(path).fileName());
    });
  };
  rebuildConfigMenu();
  QObject::connect(configMenu, &QMenu::aboutToShow, this, rebuildConfigMenu);

  auto *bookmarkMenu = new QMenu("&Bookmarks", this);
  ui.menuFile->insertMenu(ui.quit, bookmarkMenu);
  QObject::connect(bookmarkMenu, &QMenu::aboutToShow, this, [this, bookmarkMenu, makeRemoteWidget]() {
    bookmarkMenu->clear();
    auto settings = GetSettings();
    QStringList bookmarks =
        settings->value("Settings/bookmarks").toStringList();
    for (const QString &bm : bookmarks) {
      QAction *action = bookmarkMenu->addAction(bm);
      QObject::connect(action, &QAction::triggered, this, [this, bm, makeRemoteWidget]() {
        int colon = bm.indexOf(':');
        if (colon <= 0) return;
        QString remoteName = bm.left(colon);
        QString remoteType = "unknown";
        for (int i = 0; i < ui.remotes->count(); ++i) {
          if (ui.remotes->item(i)->text() == remoteName) {
            remoteType = ui.remotes->item(i)->data(Qt::UserRole).toString();
            break;
          }
        }
        auto *remote = makeRemoteWidget(remoteName, remoteType, ui.tabs);
        int index = ui.tabs->addTab(remote, bm);
        ui.tabs->setCurrentIndex(index);
      });
    }
    if (!bookmarks.isEmpty()) {
      bookmarkMenu->addSeparator();
      auto *clearAll = bookmarkMenu->addAction("Clear All Bookmarks");
      QObject::connect(clearAll, &QAction::triggered, this, [this]() {
        auto s = GetSettings();
        s->remove("Settings/bookmarks");
        setStatusMessage("Bookmarks cleared.");
      });
    } else {
      auto *empty = bookmarkMenu->addAction("No bookmarks saved");
      empty->setEnabled(false);
    }
  });

  ui.menuFile->insertSeparator(ui.quit);

  mStatusMessage = new QLabel();
  mStatusMessage->setMinimumWidth(0);
  mStatusMessage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  ui.statusBar->addWidget(mStatusMessage, 1);

  auto *bwLabel = new QLabel("Limit:", this);
  UiPolish::SetMuted(bwLabel);
  mBandwidthLimit = new QLineEdit(this);
  mBandwidthLimit->setMaximumWidth(90);
  mBandwidthLimit->setPlaceholderText("off");
  mBandwidthLimit->setToolTip(
      "Global bandwidth limit (e.g. 10M, 1G, or off). Applied to new "
      "transfers when the per-job bandwidth field is empty.");
  mBandwidthLimit->setAccessibleName("Global bandwidth limit");
  {
    auto settings = GetSettings();
    mBandwidthLimit->setText(
        settings->value("Settings/globalBandwidthLimit").toString());
  }
  QObject::connect(mBandwidthLimit, &QLineEdit::editingFinished, this, [this]() {
    auto settings = GetSettings();
    settings->setValue("Settings/globalBandwidthLimit",
                       mBandwidthLimit->text().trimmed());
  });
  ui.statusBar->addPermanentWidget(bwLabel);
  ui.statusBar->addPermanentWidget(mBandwidthLimit);

  mStatsLabel = new QLabel();
  mStatsLabel->setAccessibleName("Cumulative transfer statistics");
  UiPolish::SetMuted(mStatsLabel);
  ui.statusBar->addPermanentWidget(mStatsLabel);
  updateGlobalStats();

  mErrorLog = new QPlainTextEdit(this);
  mErrorLog->setReadOnly(true);
  mErrorLog->setMaximumHeight(140);
  mErrorLog->setMaximumBlockCount(500);
  mErrorLog->setVisible(false);
  mErrorLog->setAccessibleName("Real-time error log");
  UiPolish::SetOutputView(mErrorLog, "Error log");
  mErrorLogToggle = new QToolButton(this);
  mErrorLogToggle->setCheckable(true);
  mErrorLogToggle->setChecked(false);
  mErrorLogToggle->setArrowType(Qt::RightArrow);
  mErrorLogToggle->setText("Error Log");
  mErrorLogToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  mErrorLogToggle->setAutoRaise(true);
  mErrorLogToggle->setAccessibleName("Toggle error log panel");
  UiPolish::SetDisclosureButton(mErrorLogToggle, "Toggle error log");
  QObject::connect(mErrorLogToggle, &QToolButton::toggled, this,
                   [this](bool checked) {
                     mErrorLog->setVisible(checked);
                     mErrorLogToggle->setArrowType(
                         checked ? Qt::DownArrow : Qt::RightArrow);
                   });
  if (auto *layout =
          qobject_cast<QVBoxLayout *>(ui.jobsArea->parentWidget()->layout())) {
    layout->addWidget(mErrorLogToggle);
    layout->addWidget(mErrorLog);
  }
  Diagnostics::setLogCallback(
      [this](const QString &source, const QString &line) {
        mErrorLog->appendPlainText(
            QString("[%1] %2").arg(source, line));
      });

  QTimer::singleShot(0, ui.remotes, SLOT(setFocus()));
  refreshTaskWatchers();

  QString rclone = GetRclone();
  if (rclone.isEmpty()) {
    rclone = QStandardPaths::findExecutable("rclone");
    if (rclone.isEmpty()) {
      QMessageBox::warning(
          this, "Configuration needed",
          "Cannot find rclone. Please select its location in Preferences.");
      emit ui.preferences->trigger();
    } else {
      auto settings = GetSettings();
      settings->setValue("Settings/rclone", rclone);
      SetRclone(rclone);
    }
  } else {
    rcloneGetVersion();
  }
}

void MainWindow::bringToFront() {
  setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
  show();
  raise();
  activateWindow();
#ifdef Q_OS_MACOS
  osxShowDockIcon();
#endif
  if (mAlwaysShowInTray || mCloseToTray) {
    mSystemTray.setVisible(mAlwaysShowInTray);
  }
}

MainWindow::~MainWindow() {
  auto settings = GetSettings();
  settings->setValue("MainWindow/geometry", saveGeometry());

  QStringList openTabs;
  for (int i = 0; i < ui.tabs->count(); ++i) {
    if (qobject_cast<RemoteWidget *>(ui.tabs->widget(i))) {
      openTabs << ui.tabs->tabText(i);
    }
  }
  settings->setValue("MainWindow/openTabs", openTabs);
}

void MainWindow::rcloneGetVersion() {
  bool firstTime = mFirstTime;
  mFirstTime = false;

  QProcess *p = new QProcess();

  QObject::connect(
      p,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [=](int code, QProcess::ExitStatus) {
        if (code == 0) {
          QString version = p->readAllStandardOutput().trimmed();

          // extract rclone version - numbers only
          QString rclone_info1 = version;
          QString rclone_version_no;
          int lineBreak = rclone_info1.indexOf('\n');
          if (lineBreak != -1) {
            rclone_info1.remove(lineBreak, rclone_info1.length() - lineBreak);
            rclone_version_no = rclone_info1;
            rclone_version_no.replace("rclone v", "");
            rclone_version_no.replace("-DEV", "");
          } else {
            // for very old rclone versions format was one line only
            rclone_version_no = rclone_info1.trimmed();
            rclone_version_no.replace("rclone v", "");
            rclone_version_no.replace("-DEV", "");
          }
          // save current version no in settings
          auto settings = GetSettings();
          settings->setValue("Settings/rcloneVersion", rclone_version_no);

#if defined(Q_OS_WIN32)
          // check if required version
          unsigned int result =
              compareVersion(rclone_version_no.toStdString(), "1.50");

          if (result == 2) {
            QMessageBox::warning(
                this, "Mount unavailable",
                "Mounting requires rclone v1.50 or newer. "
                "You have v" +
                    rclone_version_no +
                    ".\n\nMount is disabled until rclone is updated.");
          };
#endif

          QStringList lines = version.split("\n", Qt::SkipEmptyParts);
          QString rclone_info2;
          QString rclone_info3;

          int counter = 0;
          foreach (QString line, lines) {
            line = line.trimmed();
            if (counter == 1)
              rclone_info2 = line.replace("- ", "");
            if (counter == 2)
              rclone_info3 = line.replace("- ", "");
            counter++;
          };

          QFileInfo appBundlePath;
#ifdef Q_OS_MACOS
          if (IsPortableMode()) {

            QFileInfo applicationPath(qApp->applicationFilePath());
            QFileInfo MacOSPath(applicationPath.dir().path());
            QFileInfo ContentsPath(MacOSPath.dir().path());
            appBundlePath = QFileInfo(ContentsPath.dir().path());

            setStatusMessage(
                rclone_info1 + " in " +
                QDir::toNativeSeparators(GetRclone().replace(
                    appBundlePath.fileName() + "/Contents/MacOS/../../../",
                    "")) +
                ", " + rclone_info2 + ", " + rclone_info3);

          } else {

            setStatusMessage(rclone_info1 + " in " +
                             QDir::toNativeSeparators(GetRclone()) + ", " +
                             rclone_info2 + ", " + rclone_info3);
          }
#else
#ifdef Q_OS_WIN
          setStatusMessage(rclone_info1 + " in " +
                           QDir::toNativeSeparators(GetRclone()) + ", " +
                           rclone_info2 + ", " + rclone_info3);
#else
          if (IsPortableMode()) {
            QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
            QString appImageConfigFolder = xdg_config_home.right(xdg_config_home.length()-xdg_config_home.lastIndexOf("/"));

            setStatusMessage(rclone_info1 + " in " +
                             QDir::toNativeSeparators(GetRclone().replace(
                                 appImageConfigFolder + "/..", "")) +
                             ", " + rclone_info2 + ", " + rclone_info3);
          } else {
            setStatusMessage(rclone_info1 + " in " +
                             QDir::toNativeSeparators(GetRclone()) + ", " +
                             rclone_info2 + ", " + rclone_info3);
         }
#endif
#endif

          // warn (non-blocking) when the detected rclone is old enough to be
          // affected by the unauthenticated remote-control advisories
          // (CVE-2026-41176 fixed in 1.73.5, CVE-2026-49980 in 1.74.3) that
          // Rclone Browser NG's Windows mount feature relies on
          if (!rclone_version_no.isEmpty() &&
              compareVersion(rclone_version_no.toStdString(), "1.74.3") == 2) {
            setStatusMessage(
                mStatusMessage->text() + "  -  WARNING: rclone " +
                rclone_version_no +
                " has security fixes available (update to 1.74.3+)");
            if (settings->value("Settings/rcloneCveWarnedVersion").toString() !=
                rclone_version_no) {
              settings->setValue("Settings/rcloneCveWarnedVersion",
                                 rclone_version_no);
              QMessageBox::warning(
                  this, "rclone update recommended",
                  QString(
                      "You are running rclone %1.\n\nVersions before 1.74.3 "
                      "are affected by security advisories in rclone's "
                      "remote-control interface (CVE-2026-41176, "
                      "CVE-2026-49980), which Rclone Browser NG uses for Windows "
                      "mounts.\n\nPlease update rclone to 1.74.3 or newer.")
                      .arg(rclone_version_no));
            }
          }

          rcloneListRemotes();
        } else {
          QString error =
              QString::fromUtf8(p->readAllStandardError()).trimmed();

          if (error.contains("RCLONE_CONFIG_PASS")) {
            // encrypted rclone.conf - ask for the password and retry
            bool ok;
            QString password = QInputDialog::getText(
                this, qApp->applicationDisplayName(),
                "Enter password for your encrypted rclone "
                "configuration file:",
                QLineEdit::Password, QString(), &ok);
            if (ok) {
              SetRclonePassword(password);
              rcloneGetVersion();
              p->deleteLater();
              return;
            }
            // user cancelled - keep the app open so they can fix it
            // in preferences instead of silently exiting
          }

          if (firstTime) {
            if (p->error() == QProcess::FailedToStart) {
              QMessageBox::warning(
                  this, "Error",
                  "Wrong rclone executable or rclone not found!\nPlease "
                  "select its location in the next dialog.");
            } else {
              QMessageBox::warning(
                  this, "Error",
                  "Cannot check rclone version!\nPlease verify your rclone "
                  "location and configuration." +
                      (error.isEmpty()
                           ? QString()
                           : "\n\nrclone reported:\n" + error.left(500)));
            }
            emit ui.preferences->trigger();
          }
        }

        auto settings = GetSettings();

        /// check rclone version

        // get already stored rclone version no
        QString rclone_version_no =
            settings->value("Settings/rcloneVersion").toString();

        // during first run the key might not exist yet
        if (!(settings->contains("Settings/checkRcloneUpdates"))) {
          // if checkRcloneUpdates does not exist create new key
          settings->setValue("Settings/checkRcloneUpdates", true);
        };

        bool checkRcloneUpdates =
            settings->value("Settings/checkRcloneUpdates").toBool();

        // if check updates enabled in settings
        if (checkRcloneUpdates) {
          QString last_check;
          checkRcloneUpdate(rclone_version_no);
        };

        checkBrowserUpdate();

        p->deleteLater();
      });

  UseRclonePassword(p);
  p->start(GetRclone(),
           QStringList() << "version"
                         << "--ask-password=false",
           QIODevice::ReadOnly);
}

void MainWindow::rcloneConfig() {
  if (!confirmConfigMutation("Opening rclone config")) {
    return;
  }

  const QDateTime configBefore = rcloneConfigLastModified();
  startDetachedTerminalCommand(QStringList() << "config", configBefore,
                               "rclone config");
}

void MainWindow::createRemote() {
  if (!confirmConfigMutation("Creating a new remote")) {
    return;
  }

  QString error;
  const QVector<RemoteProvider> providers = loadRemoteProviders(this, &error);
  if (!error.isEmpty()) {
    QMessageBox::critical(this, qApp->applicationDisplayName(), error);
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle("Create Remote");
  UiPolish::SetWindowDefaults(&dialog, QSize(600, 340));
  auto layout = new QVBoxLayout(&dialog);
  layout->setSpacing(12);
  layout->setContentsMargins(12, 12, 12, 12);

  auto intro = new QLabel(&dialog);
  UiPolish::SetNotice(
      intro,
      "Choose a name and provider type. Rclone Browser NG will open rclone's "
      "interactive setup in a terminal so credentials stay with rclone.");
  layout->addWidget(intro);

  auto form = new QFormLayout();
  form->setLabelAlignment(Qt::AlignRight);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(8);
  layout->addLayout(form);

  auto name = new QLineEdit(&dialog);
  name->setPlaceholderText("e.g. work-drive");
  name->setClearButtonEnabled(true);
  UiPolish::SetPathField(name, "Remote name");
  form->addRow("Remote name:", name);

  auto provider = new QComboBox(&dialog);
  provider->setEditable(true);
  provider->setInsertPolicy(QComboBox::NoInsert);
  provider->setMaxVisibleItems(30);
  provider->setAccessibleName("Provider type");
  provider->setMinimumContentsLength(34);
  provider->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  provider->lineEdit()->setPlaceholderText("Search provider types");
  provider->lineEdit()->setClearButtonEnabled(true);
  if (provider->completer()) {
    provider->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    provider->completer()->setFilterMode(Qt::MatchContains);
  }
  for (const RemoteProvider &p : providers) {
    provider->addItem(RemoteProviderDisplayName(p), p.prefix);
    provider->setItemData(provider->count() - 1, p.description,
                          Qt::ToolTipRole);
  }
  form->addRow("Provider:", provider);

  auto description = new QLabel(&dialog);
  description->setWordWrap(true);
  description->setTextInteractionFlags(Qt::TextSelectableByMouse);
  UiPolish::SetMuted(description);
  form->addRow("Description:", description);
  auto updateDescription = [=](int index) {
    const QString text = provider->itemData(index, Qt::ToolTipRole).toString();
    description->setText(text.isEmpty()
                             ? "This provider did not include a description."
                             : text);
  };
  QObject::connect(provider, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   &dialog, updateDescription);
  updateDescription(provider->currentIndex());

  auto validation = new QLabel(&dialog);
  UiPolish::SetValidationMessage(validation, QString(), QString());
  layout->addWidget(validation);
  auto clearValidation = [&]() {
    UiPolish::SetValidationMessage(validation, QString(), QString());
    UiPolish::SetFieldState(name, QString());
    UiPolish::SetFieldState(provider, QString());
  };
  QObject::connect(name, &QLineEdit::textChanged, &dialog,
                   [&]() { clearValidation(); });
  QObject::connect(provider->lineEdit(), &QLineEdit::textChanged, &dialog,
                   [&]() { clearValidation(); });

  auto resolveProviderIndex = [&]() -> int {
    const QString text = provider->currentText().trimmed();
    int index = provider->findText(text, Qt::MatchFixedString);
    if (index >= 0) {
      return index;
    }
    for (int i = 0; i < provider->count(); ++i) {
      const QString prefix = provider->itemData(i).toString();
      if (prefix.compare(text, Qt::CaseInsensitive) == 0) {
        return i;
      }
    }
    return -1;
  };

  auto buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                           &dialog);
  if (auto ok = buttons->button(QDialogButtonBox::Ok)) {
    ok->setText("Create");
    UiPolish::SetPrimaryButton(ok);
  }
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
    const QString remoteName = name->text().trimmed();
    if (remoteName.isEmpty() || remoteName.contains(':') ||
        remoteName.contains('\n') || remoteName.contains('\r')) {
      UiPolish::SetFieldState(name, "error");
      UiPolish::SetValidationMessage(
          validation, "error",
          "Enter a remote name without ':' or line breaks.");
      name->setFocus(Qt::OtherFocusReason);
      return;
    }

    const int providerIndex = resolveProviderIndex();
    if (providerIndex < 0) {
      UiPolish::SetFieldState(provider, "error");
      UiPolish::SetValidationMessage(
          validation, "error",
          "Select a provider from the list, or enter an exact rclone provider "
          "prefix such as s3, drive, sftp, or local.");
      provider->setFocus(Qt::OtherFocusReason);
      return;
    }

    provider->setCurrentIndex(providerIndex);
    clearValidation();
    dialog.accept();
  });

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const int providerIndex = resolveProviderIndex();
  const QString providerPrefix = provider->itemData(providerIndex).toString();
  const QString remoteName = name->text().trimmed();
  const QDateTime configBefore = rcloneConfigLastModified();
  if (startDetachedTerminalCommand(QStringList()
                                       << "config"
                                       << "create" << remoteName
                                       << providerPrefix << "--all",
                                   configBefore, "Create remote")) {
    setStatusMessage(QString("Creating remote %1 (%2)...")
                         .arg(remoteName, providerPrefix));
  }
}

QString MainWindow::terminalRcloneConfigCommand(const QStringList &args) const {
  QStringList fullArgs;
  fullArgs << GetRclone() << GetRcloneConf() << args;

  QStringList quoted;
  quoted.reserve(fullArgs.size());
  for (const QString &arg : fullArgs) {
    quoted << shellQuote(arg);
  }
  return quoted.join(' ');
}

bool MainWindow::startDetachedTerminalCommand(const QStringList &args,
                                              const QDateTime &configBefore,
                                              const QString &errorTitle) {
  const QStringList fullArgs = GetRcloneConf() + args;

  QProcess *p = new QProcess(this);

  QObject::connect(p,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   this, [=](int code, QProcess::ExitStatus) {
                     if (code == 0) {
                       noteConfigReloadIfChanged(configBefore);
                       emit rcloneListRemotes();
                     }
                     p->deleteLater();
                   });

#if defined(Q_OS_WIN32)
  p->setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NEW_CONSOLE;
        args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
      });
  p->setProgram(GetRclone());
  p->setArguments(fullArgs);

#elif defined(Q_OS_MACOS)
  // use a unique file in the per-user temp dir rather than a fixed
  // world-writable /tmp path (predictable-name/symlink hazard)
  auto tmp = new QTemporaryFile(
      QDir::tempPath() + "/rclone_config_XXXXXX.command", p);
  tmp->setAutoRemove(false); // Terminal reads it after we return
  if (!tmp->open()) {
    QMessageBox::critical(
        this, errorTitle,
        "Cannot create temporary file to launch rclone config.");
    p->deleteLater();
    return false;
  }
  QTextStream(tmp) << "#!/bin/sh\n" << terminalRcloneConfigCommand(args)
                   << "\n";
  tmp->close();
  tmp->setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser |
                      QFileDevice::ExeUser);
  p->setProgram("open");
  p->setArguments(QStringList() << tmp->fileName());
#else
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  QString terminal = env.value("TERMINAL");
  QString execFlag = "-e";

  struct TerminalDef {
    const char *name;
    const char *flag;
  };
  static const TerminalDef terminals[] = {
      {"gnome-terminal", "--"},
      {"konsole", "-e"},
      {"xfce4-terminal", "-e"},
      {"mate-terminal", "-e"},
      {"tilix", "-e"},
      {"kitty", "--"},
      {"alacritty", "-e"},
      {"foot", "--"},
      {"wezterm", "start"},
      {"xterm", "-e"},
      {"x-terminal-emulator", "-e"},
      {"lxterminal", "-e"},
  };

  if (terminal.isEmpty()) {
    for (const auto &t : terminals) {
      terminal = QStandardPaths::findExecutable(t.name);
      if (!terminal.isEmpty()) {
        execFlag = t.flag;
        break;
      }
    }
    if (terminal.isEmpty()) {
      QMessageBox::critical(this, "Error",
                            "Not sure how to launch terminal!\n"
                            "Please set path to terminal executable in "
                            "$TERMINAL environment variable.",
                            QMessageBox::Ok);
      p->deleteLater();
      return false;
    }
  }

  QStringList termArgs;
  termArgs << execFlag;
  termArgs << GetRclone() << fullArgs;
  p->setArguments(termArgs);
  p->setProgram(terminal);
#endif

  UseRclonePassword(p);
  p->start(QIODevice::NotOpen);
  return true;
}

bool MainWindow::confirmConfigMutation(const QString &action) {
  if (mJobCount == 0) {
    return true;
  }

  QMessageBox box(this);
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(qApp->applicationDisplayName());
  box.setText(action + " while jobs, mounts, or streams are active can leave "
              "running rclone processes using stale configuration.");
  box.setInformativeText(
      QString("Active processes: %1\n\nStop the active process first, defer "
              "the config change, or continue anyway if you understand that "
              "running processes will not pick up the change.")
          .arg(mJobCount));

  QPushButton *continueButton = box.addButton("Continue Anyway",
                                              QMessageBox::AcceptRole);
  QPushButton *showJobsButton =
      box.addButton("Show Jobs", QMessageBox::ActionRole);
  box.addButton(QMessageBox::Cancel);
  box.exec();

  if (box.clickedButton() == continueButton) {
    setStatusMessage(
        "Config edit continued while active rclone processes are running.");
    return true;
  }

  if (box.clickedButton() == showJobsButton) {
    ui.tabs->setCurrentIndex(1);
    showNormal();
  }

  setStatusMessage(
      "Config edit deferred while active rclone processes are running.");
  return false;
}

QDateTime MainWindow::rcloneConfigLastModified() const {
  const QStringList configArgs = GetRcloneConf();
  const int configIndex = configArgs.indexOf("--config");
  if (configIndex < 0 || configIndex + 1 >= configArgs.count()) {
    return QDateTime();
  }

  const QFileInfo info(configArgs.at(configIndex + 1));
  return info.exists() ? info.lastModified() : QDateTime();
}

void MainWindow::noteConfigReloadIfChanged(const QDateTime &before) {
  if (!before.isValid()) {
    return;
  }

  const QDateTime after = rcloneConfigLastModified();
  if (after.isValid() && after != before) {
    setStatusMessage("rclone config changed; remotes reloaded.");
  }
}

void MainWindow::setStatusMessage(const QString &message) {
  mStatusMessage->setText(message);
  mStatusMessage->setToolTip(message);
}

void MainWindow::rcloneListRemotes() {
  if (mRemotesFilter) {
    const bool wasBlocked = mRemotesFilter->blockSignals(true);
    mRemotesFilter->clear();
    mRemotesFilter->blockSignals(wasBlocked);
  }
  ui.remotes->clear();
  mRemotesFilterEmptyItem = nullptr;

  QProcess *p = new QProcess();

  QObject::connect(
      p,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [=](int code, QProcess::ExitStatus) {
        if (code == 0) {
          QStyle *style = qApp->style();

          QString bytes = p->readAllStandardOutput().trimmed();
          QStringList items = bytes.split('\n');

          auto settings = GetSettings();
          bool darkModeIni = settings->value("Settings/darkModeIni").toBool();
          QString iconSize = settings->value("Settings/iconSize").toString();

          for (const QString &line : items) {
            if (line.isEmpty()) {
              continue;
            }

            QStringList parts = line.split(':');
            if (parts.count() != 2) {
              continue;
            }

            QString name = parts[0].trimmed();
            QString type = parts[1].trimmed();
            QString tooltip = type;

            int size;

            // medium scale by default
            double darkModeIconScale = 1.333;
            double lightModeiconScale = 2;

            // set icons scale based on iconSize value
            if (iconSize == "small") {
              lightModeiconScale = 1.5;
              darkModeIconScale = 1;
            }

            if (iconSize == "medium") {
              lightModeiconScale = 2;
              darkModeIconScale = 1.333;
            }

            if (iconSize == "large") {
              lightModeiconScale = 3;
              darkModeIconScale = 2;
            }

            // only the Windows and older-macOS size paths consume these
            Q_UNUSED(darkModeIni);
            Q_UNUSED(darkModeIconScale);

            // use the inverted (light) icon set whenever the effective UI
            // is dark - covers the app's own dark mode AND OS-level dark
            // themes (Windows 10/11, macOS Mojave+), which the old
            // setting-based check missed
            bool darkUi =
                qApp->palette().color(QPalette::Base).lightness() < 128;
            QString img_add = darkUi ? "_inv" : "";

#if !defined(Q_OS_MACOS)
#if defined(Q_OS_WIN)
            // the Fusion-based dark style changes PM_ListViewIconSize,
            // so the scale has to follow the style actually in use
            if (darkModeIni) {
              size = darkModeIconScale *
                     style->pixelMetric(QStyle::PM_ListViewIconSize);
            } else {
              size = lightModeiconScale *
                     style->pixelMetric(QStyle::PM_ListViewIconSize);
            }
#else
             // for Linux/BSD PM_ListViewIconSize stays the same
             size = lightModeiconScale * style->pixelMetric(QStyle::PM_ListViewIconSize);
#endif
#else
             // macOS 11+ (deployment target): native dark mode does not change IconSize base
             size = 1.5 * lightModeiconScale * style->pixelMetric(QStyle::PM_ListViewIconSize);
#endif
            const int displaySize = qBound(22, size, 34);
            ui.remotes->setIconSize(QSize(displaySize, displaySize));

            const QString iconType = QString(type).replace(' ', '_');
            QString path =
                ":/remotes/images/" + iconType + img_add + ".png";
            QIcon icon(QFile(path).exists()
                           ? path
                           : ":/remotes/images/unknown" + img_add + ".png");

            QListWidgetItem *item = new QListWidgetItem(icon, name);
            item->setData(Qt::UserRole, type);
            item->setToolTip(tooltip);
            item->setSizeHint(QSize(0, displaySize + 14));
            ui.remotes->addItem(item);
          }

          bool hasCrypt = false;
          for (int i = 0; i < ui.remotes->count(); ++i) {
            if (ui.remotes->item(i)->data(Qt::UserRole).toString() == "crypt")
              hasCrypt = true;
          }
          if (hasCrypt) {
            auto *dump = new QProcess(this);
            QObject::connect(
                dump,
                static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                    &QProcess::finished),
                this, [this, dump](int code, QProcess::ExitStatus) {
                  dump->deleteLater();
                  if (code != 0)
                    return;
                  QJsonDocument doc =
                      QJsonDocument::fromJson(dump->readAllStandardOutput());
                  if (!doc.isObject())
                    return;
                  QSet<QString> cryptBackends;
                  QJsonObject root = doc.object();
                  for (auto it = root.begin(); it != root.end(); ++it) {
                    QJsonObject cfg = it.value().toObject();
                    if (cfg.value("type").toString() != "crypt")
                      continue;
                    QString backing = cfg.value("remote").toString();
                    int colon = backing.indexOf(':');
                    if (colon > 0)
                      cryptBackends.insert(backing.left(colon));
                  }
                  if (cryptBackends.isEmpty())
                    return;
                  for (int i = 0; i < ui.remotes->count(); ++i) {
                    auto *item = ui.remotes->item(i);
                    if (cryptBackends.contains(item->text())) {
                      item->setHidden(true);
                      item->setData(Qt::UserRole + 1, true);
                    }
                  }
                });
            UseRclonePassword(dump);
            dump->start(GetRclone(),
                        QStringList() << "config" << "dump" << GetRcloneConf()
                                      << "--ask-password=false",
                        QIODevice::ReadOnly);
          }

          if (ui.remotes->count() == 0) {
            auto *empty = new QListWidgetItem(
                "No remotes configured yet. Use New Remote or Config to add one.");
            empty->setFlags(Qt::NoItemFlags);
            empty->setForeground(qApp->palette().color(QPalette::Disabled,
                                                       QPalette::Text));
            empty->setSizeHint(QSize(0, 54));
            ui.remotes->addItem(empty);

            if (!mTabsRestored) {
              QTimer::singleShot(0, this, &MainWindow::createRemote);
            }
          }

          if (!mTabsRestored) {
            mTabsRestored = true;
            auto tabSettings = GetSettings();
            QStringList saved =
                tabSettings->value("MainWindow/openTabs").toStringList();
            for (const QString &tabName : saved) {
              for (int i = 0; i < ui.remotes->count(); ++i) {
                auto *item = ui.remotes->item(i);
                if (item->text() == tabName &&
                    (item->flags() & Qt::ItemIsEnabled)) {
                  QString type = item->data(Qt::UserRole).toString();
                  auto *remote = makeRemoteWidget(tabName, type, ui.tabs);
                  ui.tabs->addTab(remote, tabName);
                  break;
                }
              }
            }

            auto autoSettings = GetSettings();
            QStringList autoMounts =
                autoSettings->value("Settings/autoMountRemotes")
                    .toStringList();
            for (const QString &name : autoMounts) {
              addMount(name + ":", QString());
            }
          }
        } else {
          if (p->error() != QProcess::FailedToStart) {
            if (getConfigPassword(p)) {
              rcloneListRemotes();
            }
          }
        }
        p->deleteLater();
      });

  UseRclonePassword(p);
  p->start(GetRclone(),
           QStringList() << "listremotes" << GetRcloneConf() << "--long"
                         << "--ask-password=false",
           QIODevice::ReadOnly);
}

bool MainWindow::getConfigPassword(QProcess *p) {
  QString output = p->readAllStandardError().trimmed();
  if (output.contains("RCLONE_CONFIG_PASS")) {
    bool ok;
    QString password = QInputDialog::getText(
        this, qApp->applicationDisplayName(),
        "Enter password for your encrypted rclone configuration file:",
        QLineEdit::Password, QString(), &ok);
    if (ok) {
      SetRclonePassword(password);
      return true;
    }
  } else if (output.contains("unknown command \"listremotes\"")) {
    QMessageBox::critical(this, qApp->applicationDisplayName(),
                          "The rclone version you are using is too old.\n"
                          "Please upgrade to the latest version.");
    return false;
  }
  return false;
}

bool MainWindow::canClose() {
  if (mJobCount == 0) {
    return true;
  }

  bool wasVisible = isVisible();

  ui.tabs->setCurrentIndex(1);
  showNormal();

  int button = QMessageBox::question(
      this, qApp->applicationDisplayName(),
      QString("There are %1 job(s) running.\n"
              "Do you want to stop them and quit?")
          .arg(mJobCount),
      QMessageBox::Yes | QMessageBox::No);

  if (!wasVisible) {
    hide();
  }

  if (button == QMessageBox::Yes) {
    // collect first - cancel() emits closed() which removes widgets from
    // the layout and would shift indices mid-iteration, skipping jobs
    QList<QWidget *> widgets;
    for (int i = 0; i < ui.jobs->count(); i++) {
      if (QWidget *widget = ui.jobs->itemAt(i)->widget()) {
        widgets.append(widget);
      }
    }
    for (QWidget *widget : widgets) {
      if (auto mount = qobject_cast<MountWidget *>(widget)) {
        mount->cancel();
      } else if (auto transfer = qobject_cast<JobWidget *>(widget)) {
        transfer->cancel();
      } else if (auto rcTransfer = qobject_cast<RcJobWidget *>(widget)) {
        rcTransfer->cancel();
      } else if (auto stream = qobject_cast<StreamWidget *>(widget)) {
        stream->cancel();
      }
    }
    return true;
  }

  return false;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
  if (obj == ui.tabs->tabBar() &&
      event->type() == QEvent::MouseButtonRelease) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::MiddleButton) {
      int index = ui.tabs->tabBar()->tabAt(mouseEvent->pos());
      if (index >= 3) {
        ui.tabs->removeTab(index);
        return true;
      }
    }
  }
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::closeEvent(QCloseEvent *ev) {
  if (mCloseToTray && isVisible()) {
#ifdef Q_OS_MACOS
    osxHideDockIcon();
#endif
    mSystemTray.show();
    hide();
    ev->ignore();
    return;
  }

  if (canClose()) {
    QApplication::quit();
  } else {
    ev->ignore();
  }
}

void MainWindow::listTasks() {
  if (mTasksFilter) {
    const bool wasBlocked = mTasksFilter->blockSignals(true);
    mTasksFilter->clear();
    mTasksFilter->blockSignals(wasBlocked);
  }
  ui.tasksListWidget->clear();

  ListOfJobOptions *ljo = ListOfJobOptions::getInstance();

  for (JobOptions *jo : ljo->getTasks()) {
    JobOptionsListWidgetItem *item = new JobOptionsListWidgetItem(
        jo,
        jo->jobType == JobOptions::JobType::Download ? mDownloadIcon
                                                     : mUploadIcon,
        jo->description);
    const QString direction =
        jo->jobType == JobOptions::JobType::Download ? "Download" : "Upload";
    const QString operation = jo->sync ? "Sync"
                              : jo->operation == JobOptions::Move ? "Move"
                                                                  : "Copy";
    item->setToolTip(QString("%1 %2%3\n%4 -> %5")
                         .arg(direction, operation,
                              jo->watchFolder ? " (watching)" : QString(),
                              jo->source, jo->dest));
    item->setSizeHint(QSize(0, 44));
    ui.tasksListWidget->addItem(item);
  }

  if (ui.tasksListWidget->count() == 0) {
    auto *empty = new JobOptionsListWidgetItem(
        nullptr, QIcon(),
        "No saved tasks yet. Save a transfer from Upload or Download to reuse it.");
    empty->setFlags(Qt::NoItemFlags);
    empty->setForeground(qApp->palette().color(QPalette::Disabled,
                                               QPalette::Text));
    empty->setSizeHint(QSize(0, 58));
    ui.tasksListWidget->addItem(empty);
  }

  ui.buttonDeleteTask->setEnabled(false);
  ui.buttonEditTask->setEnabled(false);
  ui.buttonRunTask->setEnabled(false);
  ui.buttonDryrunTask->setEnabled(false);
  ui.buttonCopyTaskCmd->setEnabled(false);
}

void MainWindow::runItem(JobOptionsListWidgetItem *item, bool dryrun) {
  if (item == nullptr || item->GetData() == nullptr)
    return;
  runJobOptions(item->GetData(), dryrun, true);
}

void MainWindow::runJobOptions(JobOptions *jo, bool dryrun, bool confirmSync) {
  if (jo == nullptr) {
    return;
  }

  if (confirmSync && jo->sync && !dryrun) {
    int button = QMessageBox::question(
        this, "Run Task",
        QString("This Sync task may delete files at the destination.\n"
                "Are you sure you want to run \"%1\"?")
            .arg(jo->description),
        QMessageBox::Yes | QMessageBox::No);
    if (button != QMessageBox::Yes)
      return;
  }

  jo->dryRun = dryrun;
  QStringList args = jo->getOptions();
  QString heartbeatUrl = jo->heartbeatUrl;
  QString postCommand = jo->postCommand;
  QString webhookUrl = jo->webhookUrl;
  QString taskName = jo->description;
  QString message =
      QString("%1 %2").arg(jo->operation).arg(jo->source);

  if (!jo->preCommand.isEmpty()) {
    QProcess pre;
    pre.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    pre.start("cmd.exe", QStringList() << "/c" << jo->preCommand);
#else
    pre.start("/bin/sh", QStringList() << "-c" << jo->preCommand);
#endif
    pre.waitForFinished(30000);
    if (pre.exitCode() != 0) {
      QString output = QString::fromUtf8(pre.readAll()).trimmed();
      int button = QMessageBox::warning(
          this, "Pre-job command failed",
          QString("The pre-job command exited with status %1.\n\n%2\n\n"
                  "Run the transfer anyway?")
              .arg(pre.exitCode())
              .arg(output.left(500)),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (button != QMessageBox::Yes) {
        return;
      }
    }
  }

  bool hasHooks =
      !heartbeatUrl.isEmpty() || !postCommand.isEmpty() || !webhookUrl.isEmpty();
  if (!args.isEmpty() && hasHooks) {
    if (!mRcEngine) {
      mRcEngine = new RcloneRcEngine(this);
    }
    mRcEngine->runCommand(
        args, this, [=](int jobId, const QString &) {
          if (jobId >= 0) {
            auto *widget = new RcJobWidget(
                mRcEngine, jobId, message,
                mRcEngine->rcCommandForDisplay(args), jo->source, jo->dest);
            addRcJobWidget(widget, heartbeatUrl, webhookUrl, taskName);
            if (!postCommand.isEmpty()) {
              QObject::connect(
                  widget, &RcJobWidget::finished, this, [=]() {
#ifdef Q_OS_WIN
                    QProcess::startDetached("cmd.exe",
                                            QStringList() << "/c" << postCommand);
#else
                    QProcess::startDetached("/bin/sh",
                                            QStringList() << "-c" << postCommand);
#endif
                  });
            }
            return;
          }
          addTransferViaProcess(message, jo->source, jo->dest, args,
                                heartbeatUrl, postCommand, webhookUrl,
                                taskName);
        });
  } else {
    addTransfer(message, jo->source, jo->dest, args);
  }
}

void MainWindow::refreshTaskWatchers() {
  qDeleteAll(mWatchers);
  qDeleteAll(mWatchTimers);
  mWatchers.clear();
  mWatchTimers.clear();

  auto addWatchedDirectories = [](QFileSystemWatcher *watcher,
                                  const QString &rootPath) {
    QStringList directories;
    directories << rootPath;
    QDirIterator it(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      directories << it.next();
    }
    for (const QString &dir : directories) {
      if (!watcher->directories().contains(dir)) {
        watcher->addPath(dir);
      }
    }
  };

  for (JobOptions *jo : ListOfJobOptions::getInstance()->getTasks()) {
    if (!jo->watchFolder || jo->jobType != JobOptions::Upload ||
        jo->source.isEmpty()) {
      continue;
    }

    const QFileInfo sourceInfo(jo->source);
    if (!sourceInfo.isDir()) {
      continue;
    }

    const QUuid taskId = jo->uniqueId;
    const QString sourcePath = sourceInfo.absoluteFilePath();
    auto *watcher = new QFileSystemWatcher(this);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(2000);
    addWatchedDirectories(watcher, sourcePath);

    auto schedule = [=]() {
      if (mPausedWatchTasks.contains(taskId)) {
        return;
      }
      addWatchedDirectories(watcher, sourcePath);
      timer->start();
    };
    QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, this,
                     [schedule](const QString &) { schedule(); });
    QObject::connect(timer, &QTimer::timeout, this, [=]() {
      if (mPausedWatchTasks.contains(taskId)) {
        return;
      }
      for (JobOptions *task : ListOfJobOptions::getInstance()->getTasks()) {
        if (task->uniqueId == taskId) {
          runJobOptions(task, false, false);
          return;
        }
      }
    });

    mWatchers.insert(taskId, watcher);
    mWatchTimers.insert(taskId, timer);
  }

  rebuildWatchTrayMenu();
}

void MainWindow::rebuildWatchTrayMenu() {
  if (!mWatchMenu) {
    return;
  }

  mWatchMenu->clear();
  bool hasItems = false;
  for (JobOptions *jo : ListOfJobOptions::getInstance()->getTasks()) {
    if (!jo->watchFolder || !mWatchers.contains(jo->uniqueId)) {
      continue;
    }
    hasItems = true;
    const bool paused = mPausedWatchTasks.contains(jo->uniqueId);
    QAction *action = mWatchMenu->addAction(
        QString("%1 %2").arg(paused ? "Resume" : "Pause", jo->description));
    const QUuid taskId = jo->uniqueId;
    QObject::connect(action, &QAction::triggered, this, [this, taskId]() {
      if (mPausedWatchTasks.contains(taskId)) {
        mPausedWatchTasks.remove(taskId);
      } else {
        mPausedWatchTasks.insert(taskId);
      }
      rebuildWatchTrayMenu();
    });
  }

  if (!hasItems) {
    QAction *empty = mWatchMenu->addAction("No active folder watches");
    empty->setEnabled(false);
  }
}

void MainWindow::editSelectedTask() {
  auto selection = ui.tasksListWidget->selectionModel()->currentIndex();
  JobOptionsListWidgetItem *item = static_cast<JobOptionsListWidgetItem *>(
      ui.tasksListWidget->currentItem());
  if (!item || !item->GetData()) {
    return;
  }
  JobOptions *jo = item->GetData();
  bool isDownload = (jo->jobType == JobOptions::Download);
  QString remote = isDownload ? jo->source : jo->dest;
  QString path = isDownload ? jo->dest : jo->source;
  // qDebug() << "remote:" + remote;
  // qDebug() << "path:" + path;
  TransferDialog td(isDownload, false, remote, path, jo->isFolder, this, jo,
                    true);
  td.exec();
  // restore the selection to help user keep track of what s/he was doing
  ui.tasksListWidget->selectionModel()->select(selection,
                                               QItemSelectionModel::Select);
  // edit mode on the TransferDialog suppresses the usual Accept buttons
  // and the Save Task button closes it... so there is nothing more to do here
}

void MainWindow::addTransfer(const QString &message, const QString &source,
                             const QString &dest, const QStringList &args) {
  if (!args.isEmpty()) {
    if (!mRcEngine) {
      mRcEngine = new RcloneRcEngine(this);
    }

    mRcEngine->runCommand(
        args, this, [=](int jobId, const QString &) {
          if (jobId >= 0) {
            auto *widget = new RcJobWidget(
                mRcEngine, jobId, message,
                mRcEngine->rcCommandForDisplay(args), source, dest);
            addRcJobWidget(widget);
            return;
          }
          addTransferViaProcess(message, source, dest, args);
        });
    return;
  }

  addTransferViaProcess(message, source, dest, args);
}

void MainWindow::addRcJobWidget(RcJobWidget *widget,
                                const QString &heartbeatUrl,
                                const QString &webhookUrl,
                                const QString &taskName) {
  auto *line = new QFrame();
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);

  QObject::connect(widget, &RcJobWidget::finished, this,
                   [=](const QString &info) {
                     if (mNotifyFinishedTransfers) {
                       qApp->alert(this);
                       QApplication::beep();
                       mSystemTray.showMessage("Transfer finished", info);
                     }
                     if (!heartbeatUrl.isEmpty()) {
                       sendHeartbeat(heartbeatUrl,
                                     widget->wasSuccessful());
                     }
                     if (!webhookUrl.isEmpty()) {
                       sendWebhook(webhookUrl, taskName,
                                   widget->wasSuccessful());
                     }

                     persistJobHistory(widget->historyEntry());
                     noteJobFinished(widget->wasSuccessful());
                   });

  QObject::connect(widget, &RcJobWidget::closed, this, [=]() {
    ui.jobs->removeWidget(widget);
    ui.jobs->removeWidget(line);
    widget->deleteLater();
    delete line;
    if (ui.jobs->count() == 2) {
      ui.noJobsAvailable->show();
    }
  });

  if (ui.jobs->count() == 2) {
    ui.noJobsAvailable->hide();
  }

  ui.jobs->insertWidget(0, widget);
  ui.jobs->insertWidget(1, line);
  noteJobStarted();
}

void MainWindow::addTransferViaProcess(const QString &message,
                                       const QString &source,
                                       const QString &dest,
                                       const QStringList &args,
                                       const QString &heartbeatUrl,
                                       const QString &postCommand,
                                       const QString &webhookUrl,
                                       const QString &taskName) {
  auto settings = GetSettings();
  int maxConcurrent =
      settings->value("Settings/maxConcurrentTransfers", 0).toInt();
  if (maxConcurrent > 0 && mRunningTransfers >= maxConcurrent) {
    mTransferQueue.enqueue(
        {message, source, dest, args, heartbeatUrl, postCommand, webhookUrl,
         taskName});
    setStatusMessage(
        QString("Queued: %1 (%2 in queue)")
            .arg(message)
            .arg(mTransferQueue.size()));
    return;
  }
  ++mRunningTransfers;

  QProcess *transfer = new QProcess(this);
  transfer->setProcessChannelMode(QProcess::MergedChannels);
  QStringList processArgs = GetRcloneConf() + args;

  auto *widget = new JobWidget(transfer, message, args, source, dest);

  auto *line = new QFrame();
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);

  QObject::connect(
      widget, &JobWidget::finished, this, [=](const QString &info) {
        if (mNotifyFinishedTransfers) {
          qApp->alert(this);
          QApplication::beep();
          mLastFinished = widget;
          mSystemTray.showMessage("Transfer finished", info);
        }
        if (!heartbeatUrl.isEmpty()) {
          sendHeartbeat(heartbeatUrl, widget->wasSuccessful());
        }
        if (!webhookUrl.isEmpty()) {
          sendWebhook(webhookUrl, taskName, widget->wasSuccessful());
        }
        if (!postCommand.isEmpty()) {
#ifdef Q_OS_WIN
          QProcess::startDetached("cmd.exe",
                                  QStringList() << "/c" << postCommand);
#else
          QProcess::startDetached("/bin/sh",
                                  QStringList() << "-c" << postCommand);
#endif
        }

        persistJobHistory(widget->historyEntry());
        noteJobFinished(widget->wasSuccessful());
      });

  QObject::connect(widget, &JobWidget::retryRequested, this, [=]() {
    QStringList args = widget->retryArgs();
    addTransfer(widget->retryInfo(), widget->retrySource(),
                widget->retryDest(), args);
  });

  QObject::connect(widget, &JobWidget::closed, this, [=]() {
    if (widget == mLastFinished) {
      mLastFinished = nullptr;
    }
    ui.jobs->removeWidget(widget);
    ui.jobs->removeWidget(line);
    widget->deleteLater();
    delete line;
    if (ui.jobs->count() == 2) {
      ui.noJobsAvailable->show();
    }
  });

  if (ui.jobs->count() == 2) {
    ui.noJobsAvailable->hide();
  }

  ui.jobs->insertWidget(0, widget);
  ui.jobs->insertWidget(1, line);
  noteJobStarted();

  UseRclonePassword(transfer);
  transfer->start(GetRclone(), processArgs, QIODevice::ReadOnly);
}

void MainWindow::showJobHistory() {
  QString error;
  QVector<JobHistoryEntry> entries = JobHistoryStore::Load(&error);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, "Job History", error);
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle("Job History");
  dialog.resize(980, 420);
  QVBoxLayout *layout = new QVBoxLayout(&dialog);
  QTableWidget *table = new QTableWidget(&dialog);
  table->setColumnCount(9);
  table->setHorizontalHeaderLabels(QStringList()
                                   << "Finished"
                                   << "Status"
                                   << "Task"
                                   << "Source"
                                   << "Destination"
                                   << "Bytes"
                                   << "Files"
                                   << "Errors"
                                   << "Exit");
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->verticalHeader()->setVisible(false);
  table->horizontalHeader()->setStretchLastSection(false);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

  table->setRowCount(entries.size());
  for (int row = 0; row < entries.size(); ++row) {
    const JobHistoryEntry &entry = entries.at(entries.size() - row - 1);
    const QString status = entry.success ? "Success" : "Failed";
    QString finished = entry.finishedAt.toLocalTime().toString("yyyy-MM-dd hh:mm:ss");
    const QStringList values = QStringList()
                               << finished
                               << status
                               << entry.name
                               << entry.source
                               << entry.dest
                               << GetNiceSize(static_cast<quint64>(qMax<qint64>(0, entry.bytes)))
                               << QString::number(entry.files)
                               << QString::number(entry.errors)
                               << QString::number(entry.exitCode);
    for (int col = 0; col < values.size(); ++col) {
      auto *item = new QTableWidgetItem(values.at(col));
      item->setToolTip(values.at(col));
      table->setItem(row, col, item);
    }
  }

  layout->addWidget(table);
  if (entries.isEmpty()) {
    QLabel *empty = new QLabel("No completed jobs have been recorded yet.", &dialog);
    UiPolish::SetMuted(empty);
    layout->addWidget(empty);
  }
  QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.exec();
}

void MainWindow::noteJobStarted() {
  if (mJobCount == 0) {
    mLastJobFailed = false;
  }
  ++mJobCount;
  updateJobIndicators();
}

void MainWindow::noteJobFinished(bool success) {
  if (mJobCount > 0) {
    --mJobCount;
  }
  if (mRunningTransfers > 0) {
    --mRunningTransfers;
  }
  if (!success) {
    mLastJobFailed = true;
  }
  updateJobIndicators();
  drainTransferQueue();
}

void MainWindow::drainTransferQueue() {
  auto settings = GetSettings();
  int maxConcurrent =
      settings->value("Settings/maxConcurrentTransfers", 0).toInt();
  while (!mTransferQueue.isEmpty() &&
         (maxConcurrent <= 0 || mRunningTransfers < maxConcurrent)) {
    auto queued = mTransferQueue.dequeue();
    addTransferViaProcess(queued.message, queued.source, queued.dest,
                          queued.args, queued.heartbeatUrl,
                          queued.postCommand, queued.webhookUrl,
                          queued.taskName);
  }
}

void MainWindow::updateJobIndicators() {
  if (mJobCount == 0) {
    ui.tabs->setTabText(1, "Jobs");
  } else {
    ui.tabs->setTabText(1, QString("Jobs (%1)").arg(mJobCount));
  }

  if (mJobCount > 0) {
    mSystemTray.setIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserReload));
    mSystemTray.setToolTip(QString("Rclone Browser NG - %1 transfer(s) running").arg(mJobCount));
  } else if (mLastJobFailed) {
    mSystemTray.setIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning));
    mSystemTray.setToolTip("Rclone Browser NG - last transfer needs attention");
  } else {
    mSystemTray.setIcon(qApp->windowIcon());
    mSystemTray.setToolTip("Rclone Browser NG");
  }
}

void MainWindow::persistJobHistory(const JobHistoryEntry &entry) {
  QString error;
  if (!JobHistoryStore::Append(entry, &error)) {
    setStatusMessage("Job history could not be saved: " + error);
  }
  if (entry.success && entry.bytes > 0) {
    auto settings = GetSettings();
    qint64 totalBytes =
        settings->value("Stats/totalBytes", 0).toLongLong() + entry.bytes;
    int totalFiles =
        settings->value("Stats/totalFiles", 0).toInt() + entry.files;
    int totalJobs = settings->value("Stats/totalJobs", 0).toInt() + 1;
    settings->setValue("Stats/totalBytes", totalBytes);
    settings->setValue("Stats/totalFiles", totalFiles);
    settings->setValue("Stats/totalJobs", totalJobs);
    updateGlobalStats();
  }
}

void MainWindow::updateGlobalStats() {
  auto settings = GetSettings();
  qint64 totalBytes = settings->value("Stats/totalBytes", 0).toLongLong();
  int totalFiles = settings->value("Stats/totalFiles", 0).toInt();
  int totalJobs = settings->value("Stats/totalJobs", 0).toInt();
  if (totalJobs == 0) {
    mStatsLabel->clear();
    return;
  }
  mStatsLabel->setText(
      QString("%1 transferred | %2 files | %3 jobs")
          .arg(GetNiceSize(static_cast<quint64>(totalBytes)))
          .arg(totalFiles)
          .arg(totalJobs));
  mStatsLabel->setToolTip("Cumulative transfer statistics across all sessions.");
}

void MainWindow::sendHeartbeat(const QString &url, bool success) {
  if (url.isEmpty()) {
    return;
  }

  if (!mNetworkManager) {
    mNetworkManager = new QNetworkAccessManager(this);
  }

  QString endpoint = url;
  if (!success) {
    if (endpoint.endsWith('/')) {
      endpoint += "fail";
    } else {
      endpoint += "/fail";
    }
  }

  QNetworkRequest req{QUrl(endpoint)};
  QNetworkReply *reply = mNetworkManager->get(req);
  QTimer::singleShot(15000, reply, [reply]() {
    if (reply->isRunning()) {
      reply->abort();
    }
  });
  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

void MainWindow::sendWebhook(const QString &url, const QString &taskName,
                             bool success, const QString &error) {
  if (url.isEmpty()) {
    return;
  }

  if (!mNetworkManager) {
    mNetworkManager = new QNetworkAccessManager(this);
  }

  QJsonObject payload;
  payload.insert("app", QStringLiteral("Rclone Browser NG"));
  payload.insert("task", taskName);
  payload.insert("status", success ? "success" : "failed");
  if (!error.isEmpty()) {
    payload.insert("error", error);
  }

  // Discord webhook compatibility: wrap in "content" for plain text
  QString summary = success
      ? QString("Task \"%1\" completed successfully.").arg(taskName)
      : QString("Task \"%1\" failed: %2").arg(taskName, error);
  payload.insert("content", summary);

  QNetworkRequest req{QUrl(url)};
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  QNetworkReply *reply = mNetworkManager->post(
      req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
  QTimer::singleShot(15000, reply, [reply]() {
    if (reply->isRunning()) {
      reply->abort();
    }
  });
  QObject::connect(reply, &QNetworkReply::finished, reply,
                   &QNetworkReply::deleteLater);
}

void MainWindow::checkRcloneUpdate(const QString &currentVersion) {
  auto settings = GetSettings();
  bool enabled =
      settings->value("Settings/checkRcloneUpdates", true).toBool();
  if (!enabled)
    return;

  QString last =
      settings->value("Settings/lastRcloneUpdateCheck").toString();
  QString today = QDate::currentDate().toString();
  if (last == today)
    return;

  if (!mNetworkManager)
    mNetworkManager = new QNetworkAccessManager(this);

  QNetworkReply *reply = mNetworkManager->get(QNetworkRequest(
      QUrl("https://api.github.com/repos/rclone/rclone/releases/latest")));

  QTimer::singleShot(10000, reply, [reply]() {
    if (reply->isRunning())
      reply->abort();
  });

  connect(reply, &QNetworkReply::finished, this,
          [this, reply, currentVersion]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError)
              return;

            auto settings = GetSettings();
            settings->setValue("Settings/lastRcloneUpdateCheck",
                               QDate::currentDate().toString());

            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject() || !doc.object().contains("tag_name"))
              return;

            QString latest = doc.object().value("tag_name").toString();
            latest.replace("v", "");
            latest.replace("-DEV", "");
            latest = latest.trimmed();

            if (compareVersion(latest.toStdString(),
                               currentVersion.toStdString()) == 1) {
              QMessageBox::information(
                  this, "rclone update available",
                  QString(R"(<p>A newer version of rclone is available.</p>)"
                          R"(<p>Installed: v%1<br />Available: v%2</p>)"
                          R"(<p>Visit the rclone <a href="https://rclone.org/downloads/">downloads</a> page to upgrade.</p>)")
                      .arg(currentVersion, latest));
            }
          });
}

void MainWindow::checkBrowserUpdate() {
  auto settings = GetSettings();
  bool enabled =
      settings->value("Settings/checkRcloneBrowserUpdates", true).toBool();
  if (!enabled)
    return;

  QString last =
      settings->value("Settings/lastRcloneBrowserUpdateCheck").toString();
  QString today = QDate::currentDate().toString();
  if (last == today)
    return;

  if (!mNetworkManager)
    mNetworkManager = new QNetworkAccessManager(this);

  QNetworkReply *reply = mNetworkManager->get(QNetworkRequest(QUrl(
      "https://api.github.com/repos/SysAdminDoc/RcloneBrowserNG/releases/"
      "latest")));

  QTimer::singleShot(10000, reply, [reply]() {
    if (reply->isRunning())
      reply->abort();
  });

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
      return;

    auto settings = GetSettings();
    settings->setValue("Settings/lastRcloneBrowserUpdateCheck",
                       QDate::currentDate().toString());

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject() || !doc.object().contains("tag_name"))
      return;

    QString latest = doc.object().value("tag_name").toString().trimmed();

    if (compareVersion(latest.toStdString(), RCLONE_BROWSER_VERSION) == 1) {
      QMessageBox::information(
          this, "App update available",
          QString(
              R"(<p>A newer version of Rclone Browser NG is available.</p>)"
              R"(<p>Installed: v)" RCLONE_BROWSER_VERSION
              R"(<br />Available: v%1</p>)"
              R"(<p>Visit <a href="https://github.com/SysAdminDoc/RcloneBrowserNG/releases/latest">releases</a> to download.</p>)")
              .arg(latest));
    }
  });
}

void MainWindow::addMount(const QString &remote, const QString &folder) {
  startMount(remote, folder, true, 0);
}

void MainWindow::startMount(const QString &remote, const QString &folder,
                            bool keepMounted, int restartAttempt) {
  auto settings = GetSettings();
  const QString opt = settings->value("Settings/mount").toString();
  const bool driveShared = settings->value("Settings/driveShared").toBool();

#if defined(Q_OS_WIN32)
  const QString winFspDll = findWinFspDll();
  if (winFspDll.isEmpty()) {
    QMessageBox box(QMessageBox::Warning, "WinFsp required",
                    "Mounting requires WinFsp, which is not installed.\n\n"
                    "Install it with:\n  winget install WinFsp.WinFsp\n\n"
                    "Or download from https://winfsp.dev",
                    QMessageBox::Ok | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Ok);
    if (box.exec() == QMessageBox::Ok) {
      QDesktopServices::openUrl(QUrl("https://winfsp.dev/rel/"));
    }
    return;
  }

  const QString winFspVersion = windowsFileVersion(winFspDll);
  if (!winFspVersion.isEmpty() &&
      compareVersion(winFspVersion.toStdString(), "2.1.25156") != 1) {
    if (settings->value("Settings/winFspWarnedVersion").toString() !=
        winFspVersion) {
      settings->setValue("Settings/winFspWarnedVersion", winFspVersion);
      QMessageBox::warning(
          this, "WinFsp update recommended",
          QString("Detected WinFsp %1.\n\nWinFsp 2.1.25156 and older are "
                  "affected by CVE-2026-3006, a local privilege escalation in "
                  "the kernel driver.\n\nUpdate to WinFsp 2026 Beta1 or newer "
                  "before using mounts on untrusted systems.")
              .arg(winFspVersion));
    }
  }
#elif defined(Q_OS_MACOS)
  MacMountBackendFacts macMountFacts;
  macMountFacts.macFuseVersion = DetectMacFuseVersion();
  macMountFacts.fuseTInstalled = DetectFuseTInstalled();
  macMountFacts.nfsMountSupported =
      RcloneCommandSupported(GetRclone(), "nfsmount");
  macMountFacts.macOsMajorVersion =
      IsMacOs26OrNewer() ? 26 : QOperatingSystemVersion::current().majorVersion();
  if (!opt.isEmpty()) {
    macMountFacts.userMountOptions = opt.split(' ', Qt::SkipEmptyParts);
  }
  const MountBackendPlan macMountPlan = PlanMacMountBackend(macMountFacts);
  if (!macMountPlan.warningText.isEmpty()) {
    if (settings->value(macMountPlan.warningKey).toString() !=
        macMountPlan.warningVersion) {
      settings->setValue(macMountPlan.warningKey, macMountPlan.warningVersion);
      QMessageBox::warning(this, macMountPlan.warningTitle,
                           macMountPlan.warningText);
    }
  }
#endif

  QProcess *mount = new QProcess(this);
  mount->setProcessChannelMode(QProcess::MergedChannels);

  // Windows unmounts via the rclone remote-control endpoint. Authenticate it
  // with a random per-mount credential so the (otherwise unauthenticated)
  // loopback rc can't be driven by another local process or a browser page
  // (CVE-2026-41176 / CVE-2026-49980). The same credential is handed to the
  // MountWidget so it can issue the authenticated core/quit on unmount.
  QString rcAddr, rcUser, rcPass;
#if defined(Q_OS_WIN32)
  rcAddr = "localhost:" + QString::number(GetRcMountPort(folder));
  rcUser = "rclonebrowser";
  rcPass = MakeRcPassword();
#endif

  auto widget =
      new MountWidget(mount, remote, folder, rcAddr, rcUser, rcPass,
                      keepMounted);

  auto line = new QFrame();
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  const QDateTime mountStarted = QDateTime::currentDateTimeUtc();

  QObject::connect(widget, &MountWidget::finished, this, [=]() {
    if (--mJobCount == 0) {
      ui.tabs->setTabText(1, "Jobs");
    } else {
      ui.tabs->setTabText(1, QString("Jobs (%1)").arg(mJobCount));
    }
  });

  QObject::connect(widget, &MountWidget::stopped, this,
                   [=](bool requestedUnmount, bool) {
                     if (requestedUnmount || !widget->keepMounted()) {
                       return;
                     }

                     const int nextAttempt =
                         mountStarted.secsTo(QDateTime::currentDateTimeUtc()) >=
                                 300
                             ? 1
                             : restartAttempt + 1;
                     if (nextAttempt > 5) {
                       QMessageBox::warning(
                           this, "Mount stopped",
                           QString("The mount %1 on %2 stopped unexpectedly "
                                   "and automatic remount gave up after 5 "
                                   "attempts.")
                               .arg(remote)
                               .arg(folder));
                       return;
                     }

                     const int delayMs =
                         qMin(60000, 5000 * (1 << qMin(nextAttempt - 1, 4)));
                     widget->setRemountScheduled(delayMs, nextAttempt);
                     QPointer<MountWidget> widgetGuard(widget);
                     QTimer::singleShot(delayMs, this, [=]() {
                       if (!widgetGuard) {
                         return;
                       }
                       startMount(remote, folder, true, nextAttempt);
                     });
                   });

  QObject::connect(widget, &MountWidget::closed, this, [=]() {
    ui.jobs->removeWidget(widget);
    ui.jobs->removeWidget(line);
    widget->deleteLater();
    delete line;
    if (ui.jobs->count() == 2) {
      ui.noJobsAvailable->show();
    }
  });

  if (ui.jobs->count() == 2) {
    ui.noJobsAvailable->hide();
  }

  ui.jobs->insertWidget(0, widget);
  ui.jobs->insertWidget(1, line);
  ui.tabs->setTabText(1, QString("Jobs (%1)").arg(++mJobCount));

  QStringList args;
  QString mountCommand = "mount";
  QStringList mountBackendArgs;
#if defined(Q_OS_MACOS)
  mountCommand = macMountPlan.command;
  mountBackendArgs = macMountPlan.argsBeforeRemote;
#endif

  args << mountCommand;

#if defined(Q_OS_WIN32)
  args << "--rc";
  args << "--rc-addr" << rcAddr;
  args << "--rc-user" << rcUser;
  args << "--rc-pass" << rcPass;
#endif

  // for google drive "shared with me" without --read-only writes go created in
  // main google drive it is more logical to mount it as read only so there is
  // no confusion
  if (driveShared) {
    args << "--drive-shared-with-me";
    args << "--read-only";
  };

  //	 default mount is now more generic. all options can be passed via
  // preferences mount field
  //       args << "--vfs-cache-mode";
  //       args << "writes";

  args.append(GetRcloneConf());
  if (!opt.isEmpty()) {
    args.append(opt.split(' ', Qt::SkipEmptyParts));
  }
  args.append(mountBackendArgs);
  args << remote << folder;

  UseRclonePassword(mount);
  mount->start(GetRclone(), args, QIODevice::ReadOnly);
}

void MainWindow::addStream(const QString &remote, const QString &stream) {
  auto player = new QProcess(this);
  auto rclone = new QProcess(this);
  rclone->setStandardOutputProcess(player);

  QObject::connect(
      player,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [=](int, QProcess::ExitStatus) { player->deleteLater(); });

  // finished() is never emitted when the process fails to start, so a
  // broken player command has to be caught here
  QObject::connect(
      player, &QProcess::errorOccurred, this, [=](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
          rclone->kill();
          rclone->deleteLater();
          player->deleteLater();
          QMessageBox::critical(
              this, "Error",
              QString("Failed to start the player command:\n%1\n\nYou will "
                      "be asked for a new command on the next stream.")
                  .arg(stream));
          auto settings = GetSettings();
          settings->remove("Settings/streamConfirmed");
        }
      });

  auto widget = new StreamWidget(rclone, player, remote, stream);

  auto line = new QFrame();
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);

  QObject::connect(widget, &StreamWidget::finished, this, [=]() {
    if (--mJobCount == 0) {
      ui.tabs->setTabText(1, "Jobs");
    } else {
      ui.tabs->setTabText(1, QString("Jobs (%1)").arg(mJobCount));
    }
  });

  QObject::connect(widget, &StreamWidget::closed, this, [=]() {
    ui.jobs->removeWidget(widget);
    ui.jobs->removeWidget(line);
    widget->deleteLater();
    delete line;
    if (ui.jobs->count() == 2) {
      ui.noJobsAvailable->show();
    }
  });

  if (ui.jobs->count() == 2) {
    ui.noJobsAvailable->hide();
  }

  ui.jobs->insertWidget(0, widget);
  ui.jobs->insertWidget(1, line);
  ui.tabs->setTabText(1, QString("Jobs (%1)").arg(++mJobCount));

  auto streamParts = QProcess::splitCommand(stream);
  if (streamParts.isEmpty()) {
    streamParts << stream;
  }

  QString fileName =
      QFileInfo(remote.mid(remote.indexOf(':') + 1)).fileName();
  if (!fileName.isEmpty()) {
    QString playerBase =
        QFileInfo(streamParts.first()).baseName().toLower();
    if (playerBase.contains("mpv")) {
      streamParts.insert(1, "--force-media-title=" + fileName);
    } else if (playerBase.contains("vlc")) {
      streamParts.insert(1, "--meta-title=" + fileName);
    }
  }

  player->start(streamParts.first(), streamParts.mid(1), QProcess::ReadOnly);
  UseRclonePassword(rclone);
  rclone->start(GetRclone(),
                QStringList() << "cat" << GetRcloneConf() << remote,
                QProcess::WriteOnly);
}
