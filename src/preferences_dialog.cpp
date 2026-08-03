#include "preferences_dialog.h"
#include "interface_polish.h"
#include "mount_options.h"
#include "utils.h"

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
  ui.setupUi(this);
  if (layout()) {
    layout()->setSpacing(12);
    layout()->setContentsMargins(12, 12, 12, 12);
  }
  UiPolish::SetWindowDefaults(this, QSize(760, 560));
  ui.tabWidget->setDocumentMode(true);
  UiPolish::SetDialogButtonBox(ui.buttonBox);
  if (auto ok = ui.buttonBox->button(QDialogButtonBox::Ok)) {
    UiPolish::SetPrimaryButton(ok);
  }
  UiPolish::SetCompactToolButton(ui.rcloneBrowse, "Browse for rclone",
                                 "Choose the rclone executable.");
  UiPolish::SetCompactToolButton(ui.rcloneConfBrowse, "Browse for rclone.conf",
                                 "Choose a custom rclone.conf file.");
  UiPolish::SetCompactToolButton(ui.defaultDownloadDirBrowse,
                                 "Browse for default download folder");
  UiPolish::SetCompactToolButton(ui.defaultUploadDirBrowse,
                                 "Browse for default upload folder");
  QStyle *style = qApp->style();
  ui.rcloneBrowse->setText("Browse");
  ui.rcloneBrowse->setIcon(style->standardIcon(QStyle::SP_DialogOpenButton));
  ui.rcloneConfBrowse->setText("Browse");
  ui.rcloneConfBrowse->setIcon(
      style->standardIcon(QStyle::SP_DialogOpenButton));
  ui.defaultDownloadDirBrowse->setText("Browse");
  ui.defaultDownloadDirBrowse->setIcon(style->standardIcon(QStyle::SP_DirIcon));
  ui.defaultUploadDirBrowse->setText("Browse");
  ui.defaultUploadDirBrowse->setIcon(style->standardIcon(QStyle::SP_DirIcon));
  UiPolish::SetMuted(ui.info);
  UiPolish::SetMuted(ui.darkMode_info);
  UiPolish::SetMuted(ui.info_4);
  UiPolish::SetPathField(ui.rclone, "rclone executable path");
  UiPolish::SetPathField(ui.rcloneConf, "rclone configuration path");
  UiPolish::SetPathField(ui.stream, "Stream player command");
  UiPolish::SetPathField(ui.mount, "Expert mount options");
  UiPolish::SetPathField(ui.defaultDownloadDir, "Default download folder");
  UiPolish::SetPathField(ui.defaultUploadDir, "Default upload folder");
  UiPolish::SetPathField(ui.defaultDownloadOptions,
                         "Default download options");
  UiPolish::SetPathField(ui.defaultUploadOptions, "Default upload options");
  UiPolish::SetPathField(ui.defaultRcloneOptions, "Default rclone options");
  UiPolish::SetPathField(ui.http_proxy, "HTTP proxy");
  UiPolish::SetPathField(ui.https_proxy, "HTTPS proxy");
  UiPolish::SetPathField(ui.no_proxy, "No proxy list");
  UiPolish::SetPathField(ui.socks_proxy, "SOCKS proxy");
  const QList<QLineEdit *> editableFields = {
      ui.rclone, ui.rcloneConf, ui.stream, ui.mount, ui.defaultDownloadDir,
      ui.defaultUploadDir, ui.defaultDownloadOptions,
      ui.defaultUploadOptions, ui.defaultRcloneOptions, ui.http_proxy,
      ui.https_proxy, ui.no_proxy, ui.socks_proxy};
  for (QLineEdit *field : editableFields) {
    field->setClearButtonEnabled(true);
  }

  ui.rclone->setPlaceholderText("Use PATH lookup when empty");
  ui.rcloneConf->setPlaceholderText("Use rclone's default config path");
  ui.stream->setPlaceholderText("mpv -");
  ui.mount->setPlaceholderText("Additional flags, for example --network-mode");
  ui.defaultDownloadDir->setPlaceholderText("Ask each time when empty");
  ui.defaultUploadDir->setPlaceholderText("Ask each time when empty");
  ui.defaultDownloadOptions->setPlaceholderText("Extra flags for downloads");
  ui.defaultUploadOptions->setPlaceholderText("Extra flags for uploads");
  ui.defaultRcloneOptions->setPlaceholderText("--fast-list");

  // Issue #13: these controls used to be created at runtime and inserted
  // through qobject_cast<QFormLayout*>/<QVBoxLayout*> on layouts that are
  // QGridLayouts in the .ui, so every cast failed and the controls -- all
  // parented to the dialog itself -- painted over the tab bar at the
  // top-left corner. They are now regular .ui widgets in managed layouts;
  // this code only configures behaviour.
  const QString concurrentTip =
      "Maximum number of transfers that run simultaneously. "
      "0 = unlimited (all run at once). "
      "Excess jobs are queued and start when a slot frees up.";
  ui.maxConcurrentLabel->setToolTip(concurrentTip);
  ui.maxConcurrentTransfers->setToolTip(concurrentTip);
  ui.maxConcurrentTransfers->setAccessibleName("Max concurrent transfers");

  ui.defaultExcludeLabel->setToolTip(
      "One --exclude pattern per line. Applied to all transfers globally.");
  ui.defaultExclude->setPlaceholderText(
      "*.tmp\n.DS_Store\nThumbs.db\n*.partial");
  ui.defaultExclude->setAccessibleName("Default exclude patterns");
  UiPolish::SetOutputView(ui.defaultExclude, "Default exclude patterns");

  ui.http_proxy->setPlaceholderText("http://127.0.0.1:1087");
  ui.https_proxy->setPlaceholderText("https://127.0.0.1:1087");
  ui.no_proxy->setPlaceholderText("localhost,127.0.0.0/8");
  ui.socks_proxy->setPlaceholderText("socks5://127.0.0.1:1080");
  ui.socks_proxy->setAccessibleName("SOCKS proxy");

  QObject::connect(ui.rcloneBrowse, &QPushButton::clicked, this, [=]() {
    QString rclone = QFileDialog::getOpenFileName(
        this, "Select rclone executable", ui.rclone->text());
    if (rclone.isEmpty()) {
      return;
    }

    if (!QFileInfo(rclone).isExecutable()) {
      QMessageBox::critical(this, "Invalid rclone executable",
                            QString("%1 is not executable. Choose the rclone "
                                    "binary, not a folder or shortcut.")
                                .arg(rclone));
      return;
    }

    if (QFileInfo(rclone) == QFileInfo(qApp->applicationFilePath())) {
      QMessageBox::critical(this, "Invalid rclone executable",
                            "That is Rclone Browser NG itself. Choose the "
                            "rclone command-line executable instead.");
      return;
    }

    ui.rclone->setText(rclone);
  });

  QObject::connect(ui.rcloneConfBrowse, &QPushButton::clicked, this, [=]() {
    QString rcloneConf = QFileDialog::getOpenFileName(
        this, "Select .rclone.conf location", ui.rcloneConf->text());
    if (rcloneConf.isEmpty()) {
      return;
    }

    ui.rcloneConf->setText(rcloneConf);
  });

  ui.backupConfig->setAccessibleName("Backup rclone config");
  ui.restoreConfig->setAccessibleName("Restore rclone config");

  QObject::connect(ui.backupConfig, &QPushButton::clicked, this, [this]() {
    QStringList confArgs = GetRcloneConf();
    QString confPath;
    for (int i = 0; i < confArgs.size(); ++i) {
      if (confArgs[i] == "--config" && i + 1 < confArgs.size()) {
        confPath = confArgs[i + 1];
        break;
      }
    }
    if (confPath.isEmpty()) {
      confPath = QDir::home().filePath(".config/rclone/rclone.conf");
    }
    if (!QFile::exists(confPath)) {
      QMessageBox::warning(this, "Backup Config",
                           "Could not find rclone config at:\n" + confPath);
      return;
    }
    QString dest = QFileDialog::getSaveFileName(
        this, "Save config backup",
        QDir::home().filePath(
            "rclone.conf.backup-" +
            QDateTime::currentDateTime().toString("yyyy-MM-dd")),
        "Config files (*.conf *.conf.backup-*);;All files (*)");
    if (dest.isEmpty()) {
      return;
    }
    if (QFile::copy(confPath, dest)) {
      QMessageBox::information(
          this, "Backup Config",
          "Config backed up to:\n" + QDir::toNativeSeparators(dest));
    } else {
      QMessageBox::warning(this, "Backup Config",
                           "Failed to copy config to:\n" + dest);
    }
  });

  QObject::connect(ui.restoreConfig, &QPushButton::clicked, this, [this]() {
    QStringList confArgs = GetRcloneConf();
    QString confPath;
    for (int i = 0; i < confArgs.size(); ++i) {
      if (confArgs[i] == "--config" && i + 1 < confArgs.size()) {
        confPath = confArgs[i + 1];
        break;
      }
    }
    if (confPath.isEmpty()) {
      confPath = QDir::home().filePath(".config/rclone/rclone.conf");
    }

    QString source = QFileDialog::getOpenFileName(
        this, "Select config file to restore", QDir::homePath(),
        "Config files (*.conf *.conf.backup-*);;All files (*)");
    if (source.isEmpty()) {
      return;
    }

    if (QFile::exists(confPath)) {
      QString autoBackup = confPath + ".pre-restore-" +
                           QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
      if (!QFile::copy(confPath, autoBackup)) {
        QMessageBox::warning(
            this, "Restore Config",
            "Failed to back up current config before restore.\n"
            "Aborting to protect your existing configuration.");
        return;
      }
    }

    if (QFile::exists(confPath)) {
      QFile::remove(confPath);
    }
    if (QFile::copy(source, confPath)) {
      QMessageBox::information(
          this, "Restore Config",
          "Config restored. The previous config was backed up automatically.\n"
          "Close this dialog and refresh remotes to see the changes.");
    } else {
      QMessageBox::warning(this, "Restore Config",
                           "Failed to restore config from:\n" + source);
    }
  });

  QObject::connect(
      ui.defaultDownloadDirBrowse, &QPushButton::clicked, this, [=]() {
        QString defaultDownloadDir = QFileDialog::getExistingDirectory(
            this, "Select default download directory",
            ui.defaultDownloadDir->text());

        if (defaultDownloadDir.isEmpty()) {
          return;
        }

        ui.defaultDownloadDir->setText(defaultDownloadDir);
      });

  QObject::connect(
      ui.defaultUploadDirBrowse, &QPushButton::clicked, this, [=]() {
        QString defaultUploadDir = QFileDialog::getExistingDirectory(
            this, "Select default upload directory",
            ui.defaultUploadDir->text());

        if (defaultUploadDir.isEmpty()) {
          return;
        }

        ui.defaultUploadDir->setText(defaultUploadDir);
      });

  const QVector<MountPreset> mountPresets = MountPresets();
  for (const MountPreset &preset : mountPresets) {
    ui.mountPreset->addItem(preset.label, preset.id);
  }
  ui.mountPreset->setAccessibleName("Mount preset");
  ui.mountPresetFlags->setAccessibleName("Mount preset flags");
  auto updateMountPresetFlags = [this]() {
    ui.mountPresetFlags->setText(
        QString("Exact preset flags: %1")
            .arg(MountPresetFlags(ui.mountPreset->currentData().toString())
                     .isEmpty()
                 ? QStringLiteral("(none)")
                 : MountPresetFlags(ui.mountPreset->currentData().toString())));
    ui.mountPresetFlags->setToolTip(ui.mountPresetFlags->text());
  };
  QObject::connect(ui.mountPreset,
                   QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                   [updateMountPresetFlags](int) {
                     updateMountPresetFlags();
                   });

  auto settings = GetSettings();
  ui.rclone->setText(
      QDir::toNativeSeparators(settings->value("Settings/rclone").toString()));
  ui.rcloneConf->setText(QDir::toNativeSeparators(
      settings->value("Settings/rcloneConf").toString()));
  ui.usePasswordCommand->setChecked(IsRclonePasswordCommandEnabled());
#if !defined(Q_OS_WIN32)
  ui.usePasswordCommand->setChecked(false);
  ui.usePasswordCommand->setDisabled(true);
  ui.usePasswordCommand->setToolTip(
      "OS credential storage is not available in this build.");
#endif
  ui.stream->setText(settings->value("Settings/stream").toString());

  const MountOptionState mountState = LoadMountOptionState(*settings);
  ui.mount->setText(mountState.expertOptions);
  const int mountPresetIndex =
      ui.mountPreset->findData(mountState.presetId);
  if (mountPresetIndex >= 0) {
    ui.mountPreset->setCurrentIndex(mountPresetIndex);
  }
  updateMountPresetFlags();
#if defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
  ui.mount->setDisabled(true);
  ui.mountPreset->setDisabled(true);
#endif
#if defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
  ui.mount->setToolTip("Mount is not supported by rclone on this system.");
#endif

  ui.defaultDownloadDir->setText(QDir::toNativeSeparators(
      settings->value("Settings/defaultDownloadDir").toString()));
  ui.defaultUploadDir->setText(QDir::toNativeSeparators(
      settings->value("Settings/defaultUploadDir").toString()));
  ui.defaultDownloadOptions->setText(
      settings->value("Settings/defaultDownloadOptions").toString());
  ui.defaultUploadOptions->setText(
      settings->value("Settings/defaultUploadOptions").toString());
  ui.defaultRcloneOptions->setText(
      settings->value("Settings/defaultRcloneOptions").toString());
  ui.defaultExclude->setPlainText(
      settings->value("Settings/defaultExclude").toString());
  ui.maxConcurrentTransfers->setValue(
      settings->value("Settings/maxConcurrentTransfers", 0).toInt());

  ui.checkRcloneBrowserUpdates->setChecked(
      settings->value("Settings/checkRcloneBrowserUpdates", true).toBool());
  ui.checkRcloneUpdates->setChecked(
      settings->value("Settings/checkRcloneUpdates", true).toBool());

  ui.startMinimized->setAccessibleName("Start minimized to system tray");
  // Starting hidden only makes sense when a tray icon exists to bring the
  // window back, so Start Minimized implies Always Show in Tray.
  QObject::connect(ui.startMinimized, &QCheckBox::toggled, this,
                   [this](bool checked) {
                     if (checked) {
                       ui.alwaysShowInTray->setChecked(true);
                     }
                   });
  QObject::connect(ui.alwaysShowInTray, &QCheckBox::toggled, this,
                   [this](bool checked) {
                     if (!checked) {
                       ui.startMinimized->setChecked(false);
                     }
                   });

  if (QSystemTrayIcon::isSystemTrayAvailable()) {
    ui.alwaysShowInTray->setChecked(
        settings->value("Settings/alwaysShowInTray", false).toBool());
    ui.closeToTray->setChecked(
        settings->value("Settings/closeToTray", false).toBool());
    ui.notifyFinishedTransfers->setChecked(
        settings->value("Settings/notifyFinishedTransfers", true).toBool());
    ui.startMinimized->setChecked(
        settings->value("Settings/startMinimized", false).toBool());
  } else {
    const QString trayUnavailable =
        "System tray controls are unavailable in this desktop session.";
    ui.alwaysShowInTray->setChecked(false);
    ui.alwaysShowInTray->setDisabled(true);
    ui.alwaysShowInTray->setToolTip(trayUnavailable);
    ui.closeToTray->setChecked(false);
    ui.closeToTray->setDisabled(true);
    ui.closeToTray->setToolTip(trayUnavailable);
    ui.notifyFinishedTransfers->setChecked(false);
    ui.notifyFinishedTransfers->setDisabled(true);
    ui.notifyFinishedTransfers->setToolTip(trayUnavailable);
    ui.startMinimized->setChecked(false);
    ui.startMinimized->setDisabled(true);
    ui.startMinimized->setToolTip(trayUnavailable);
  }

  ui.showFolderIcons->setChecked(
      settings->value("Settings/showFolderIcons", true).toBool());
  ui.showFileIcons->setChecked(
      settings->value("Settings/showFileIcons", true).toBool());
  // default must match RemoteWidget's read (false), or the checkbox shows
  // a state the tree doesn't actually have
  ui.rowColors->setChecked(
      settings->value("Settings/rowColors", false).toBool());
  ui.showHidden->setChecked(
      settings->value("Settings/showHidden", true).toBool());
  ui.darkMode->setChecked(
      settings->value("Settings/darkMode", false).toBool());

#if defined(Q_OS_MACOS)
  ui.darkMode->hide();
  ui.darkMode_info->hide();
#endif

  if ((settings->value("Settings/iconSize").toString()) == "small") {
    ui.cb_small->setChecked(true);
  } else {
    if (settings->value("Settings/iconSize").toString() == "large") {
      ui.cb_large->setChecked(true);
    } else {
      ui.cb_medium->setChecked(true);
    }
  }

  ui.info_2->setText(
      "Manual proxy values are passed through HTTP_PROXY, HTTPS_PROXY and "
      "NO_PROXY. See the rclone <a "
      "href=\"https://github.com/rclone/rclone/blob/master/docs/content/"
      "faq.md#can-i-use-rclone-with-an-http-proxy\">proxy FAQ</a> for details.");
  ui.info_2->setTextFormat(Qt::RichText);
  ui.info_2->setTextInteractionFlags(Qt::TextBrowserInteraction);
  ui.info_2->setOpenExternalLinks(true);
  UiPolish::SetMuted(ui.info_2);

  if (settings->value("Settings/useProxy").toBool()) {
    ui.useProxy->setChecked(true);
  } else {
    ui.useSystemSettings->setChecked(true);
  }
  ui.http_proxy->setText(settings->value("Settings/http_proxy").toString());
  ui.https_proxy->setText(settings->value("Settings/https_proxy").toString());
  ui.no_proxy->setText(settings->value("Settings/no_proxy").toString());
  ui.socks_proxy->setText(settings->value("Settings/socksProxy").toString());

  auto updateProxyFields = [=]() {
    const bool manual = ui.useProxy->isChecked();
    ui.groupBox_8->setEnabled(manual);
    ui.groupBox_8->setToolTip(
        manual ? QString()
               : "Select manual proxy configuration to edit these fields.");
  };
  QObject::connect(ui.useProxy, &QRadioButton::toggled, this,
                   [=](bool) { updateProxyFields(); });
  updateProxyFields();

  // Issue #13: the fixed 760x560 minimum can sit below the tab content's
  // real minimum once the application stylesheet (dark theme) or larger
  // fonts inflate control metrics; the Interface page then gets compressed
  // until its checkboxes and help labels overlap. Track the layout's
  // computed minimum instead of trusting the constant.
  layout()->activate();
  setMinimumSize(QSize(760, 560).expandedTo(minimumSizeHint()));
  resize(minimumSize());
}

PreferencesDialog::~PreferencesDialog() {}

QString PreferencesDialog::getRclone() const {
  return QDir::fromNativeSeparators(ui.rclone->text());
}

QString PreferencesDialog::getRcloneConf() const {
  return QDir::fromNativeSeparators(ui.rcloneConf->text());
}

QString PreferencesDialog::getStream() const { return ui.stream->text(); }

QString PreferencesDialog::getMount() const { return ui.mount->text(); }

QString PreferencesDialog::getMountPreset() const {
  return ui.mountPreset->currentData().toString();
}

QString PreferencesDialog::getDefaultDownloadDir() const {
  return QDir::fromNativeSeparators(ui.defaultDownloadDir->text());
}

QString PreferencesDialog::getDefaultUploadDir() const {
  return QDir::fromNativeSeparators(ui.defaultUploadDir->text());
}

QString PreferencesDialog::getDefaultDownloadOptions() const {
  return ui.defaultDownloadOptions->text();
}

QString PreferencesDialog::getDefaultUploadOptions() const {
  return ui.defaultUploadOptions->text();
}

QString PreferencesDialog::getDefaultRcloneOptions() const {
  return ui.defaultRcloneOptions->text();
}

QString PreferencesDialog::getDefaultExclude() const {
  return ui.defaultExclude->toPlainText().trimmed();
}

bool PreferencesDialog::getStartMinimized() const {
  return ui.startMinimized->isChecked();
}

int PreferencesDialog::getMaxConcurrentTransfers() const {
  return ui.maxConcurrentTransfers->value();
}

bool PreferencesDialog::getCheckRcloneBrowserUpdates() const {
  return ui.checkRcloneBrowserUpdates->isChecked();
}

bool PreferencesDialog::getCheckRcloneUpdates() const {
  return ui.checkRcloneUpdates->isChecked();
}

bool PreferencesDialog::getAlwaysShowInTray() const {
  return ui.alwaysShowInTray->isChecked();
}

bool PreferencesDialog::getCloseToTray() const {
  return ui.closeToTray->isChecked();
}

bool PreferencesDialog::getNotifyFinishedTransfers() const {
  return ui.notifyFinishedTransfers->isChecked();
}

bool PreferencesDialog::getShowFolderIcons() const {
  return ui.showFolderIcons->isChecked();
}

bool PreferencesDialog::getShowFileIcons() const {
  return ui.showFileIcons->isChecked();
}

bool PreferencesDialog::getRowColors() const {
  return ui.rowColors->isChecked();
}

bool PreferencesDialog::getShowHidden() const {
  return ui.showHidden->isChecked();
}

bool PreferencesDialog::getDarkMode() const { return ui.darkMode->isChecked(); }

QString PreferencesDialog::getIconSize() const {
  if (ui.cb_small->isChecked()) {
    return "small";
  } else {
    if (ui.cb_large->isChecked()) {
      return "large";
    } else {
      return "medium";
    }
  }
}

QString PreferencesDialog::getHttpProxy() const {
  return ui.http_proxy->text();
}

QString PreferencesDialog::getHttpsProxy() const {
  return ui.https_proxy->text();
}

QString PreferencesDialog::getNoProxy() const { return ui.no_proxy->text(); }

QString PreferencesDialog::getSocksProxy() const {
  return ui.socks_proxy->text();
}

bool PreferencesDialog::getUseProxy() const {
  if (ui.useSystemSettings->isChecked()) {
    return false;
  } else {
    return true;
  }
}

bool PreferencesDialog::getUsePasswordCommand() const {
  return ui.usePasswordCommand->isChecked();
}
