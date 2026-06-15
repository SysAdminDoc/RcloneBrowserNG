#include "preferences_dialog.h"
#include "interface_polish.h"
#include "utils.h"

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
  ui.setupUi(this);
  if (layout()) {
    layout()->setSpacing(12);
    layout()->setContentsMargins(12, 12, 12, 12);
  }
  UiPolish::SetWindowDefaults(this, QSize(760, 560));
  ui.tabWidget->setDocumentMode(true);
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
  UiPolish::SetMuted(ui.info);
  UiPolish::SetMuted(ui.darkMode_info);
  UiPolish::SetMuted(ui.info_4);
  UiPolish::SetPathField(ui.rclone, "rclone executable path");
  UiPolish::SetPathField(ui.rcloneConf, "rclone configuration path");
  UiPolish::SetPathField(ui.stream, "Stream player command");
  UiPolish::SetPathField(ui.mount, "Mount options");
  UiPolish::SetPathField(ui.defaultDownloadDir, "Default download folder");
  UiPolish::SetPathField(ui.defaultUploadDir, "Default upload folder");
  UiPolish::SetPathField(ui.defaultDownloadOptions,
                         "Default download options");
  UiPolish::SetPathField(ui.defaultUploadOptions, "Default upload options");
  UiPolish::SetPathField(ui.defaultRcloneOptions, "Default rclone options");
  UiPolish::SetPathField(ui.http_proxy, "HTTP proxy");
  UiPolish::SetPathField(ui.https_proxy, "HTTPS proxy");
  UiPolish::SetPathField(ui.no_proxy, "No proxy list");
  const QList<QLineEdit *> editableFields = {
      ui.rclone, ui.rcloneConf, ui.stream, ui.mount, ui.defaultDownloadDir,
      ui.defaultUploadDir, ui.defaultDownloadOptions,
      ui.defaultUploadOptions, ui.defaultRcloneOptions, ui.http_proxy,
      ui.https_proxy, ui.no_proxy};
  for (QLineEdit *field : editableFields) {
    field->setClearButtonEnabled(true);
  }

  ui.rclone->setPlaceholderText("Use PATH lookup when empty");
  ui.rcloneConf->setPlaceholderText("Use rclone's default config path");
  ui.stream->setPlaceholderText("mpv -");
  ui.mount->setPlaceholderText("--vfs-cache-mode writes");
  ui.defaultDownloadDir->setPlaceholderText("Ask each time when empty");
  ui.defaultUploadDir->setPlaceholderText("Ask each time when empty");
  ui.defaultDownloadOptions->setPlaceholderText("Extra flags for downloads");
  ui.defaultUploadOptions->setPlaceholderText("Extra flags for uploads");
  ui.defaultRcloneOptions->setPlaceholderText("--fast-list");
  ui.http_proxy->setPlaceholderText("http://127.0.0.1:1087");
  ui.https_proxy->setPlaceholderText("https://127.0.0.1:1087");
  ui.no_proxy->setPlaceholderText("localhost,127.0.0.0/8");

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

#if defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
  ui.mount->setText(
      settings
          ->value("Settings/mount",
                  "* mount is not supported by rclone on this system *")
          .toString());
  ui.mount->setDisabled(true);
#else
  ui.mount->setText(
      settings->value("Settings/mount", "--vfs-cache-mode writes").toString());
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

  ui.checkRcloneBrowserUpdates->setChecked(
      settings->value("Settings/checkRcloneBrowserUpdates", true).toBool());
  ui.checkRcloneUpdates->setChecked(
      settings->value("Settings/checkRcloneUpdates", true).toBool());

  if (QSystemTrayIcon::isSystemTrayAvailable()) {
    ui.alwaysShowInTray->setChecked(
        settings->value("Settings/alwaysShowInTray", false).toBool());
    ui.closeToTray->setChecked(
        settings->value("Settings/closeToTray", false).toBool());
    ui.notifyFinishedTransfers->setChecked(
        settings->value("Settings/notifyFinishedTransfers", true).toBool());
  } else {
    ui.alwaysShowInTray->setChecked(false);
    ui.alwaysShowInTray->setDisabled(true);
    ui.closeToTray->setChecked(false);
    ui.closeToTray->setDisabled(true);
    ui.notifyFinishedTransfers->setChecked(false);
    ui.notifyFinishedTransfers->setDisabled(true);
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
