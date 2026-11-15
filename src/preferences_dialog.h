#pragma once

#include "pch.h"
#include "remote_daemon.h"
#include "ui_preferences_dialog.h"

class PreferencesDialog : public QDialog {
  Q_OBJECT

public:
  PreferencesDialog(QWidget *parent = nullptr);
  ~PreferencesDialog() override;

  QString getRclone() const;
  QString getRcloneConf() const;
  QString getStream() const;
  QString getMount() const;
  QString getMountPreset() const;
  QString getDefaultDownloadDir() const;
  QString getDefaultUploadDir() const;
  QString getDefaultDownloadOptions() const;
  QString getDefaultUploadOptions() const;
  QString getDefaultRcloneOptions() const;

  bool getCheckRcloneBrowserUpdates() const;
  bool getCheckRcloneUpdates() const;

  bool getAlwaysShowInTray() const;
  bool getCloseToTray() const;
  bool getNotifyFinishedTransfers() const;
  bool getStartMinimized() const;

  bool getShowFolderIcons() const;
  bool getShowFileIcons() const;
  bool getRowColors() const;
  bool getShowHidden() const;
  bool getHideCryptBackends() const;
  QString getLogLevel() const;
  bool getDarkMode() const;
  QString getIconSize() const;

  int getMaxConcurrentTransfers() const;
  bool getUseProxy() const;
  QString getHttpProxy() const;
  QString getHttpsProxy() const;
  QString getNoProxy() const;
  QString getSocksProxy() const;
  bool getUsePasswordCommand() const;
  QString getDefaultExclude() const;
  // As typed, not yet normalised. url can be empty or not an address. The
  // certificate fingerprint is normalised, empty when it isn't one.
  RemoteDaemon::Settings getRemoteDaemon() const;

private:
  Ui::PreferencesDialog ui;
  // The certificate the last Test Connection was refused over, which Trust
  // Certificate copies into the field.
  QString mPresentedCertificate;
};
