#pragma once

#include "pch.h"
#include "ui_preferences_dialog.h"

class PreferencesDialog : public QDialog {
  Q_OBJECT

public:
  PreferencesDialog(QWidget *parent = nullptr);
  ~PreferencesDialog();

  QString getRclone() const;
  QString getRcloneConf() const;
  QString getStream() const;
  QString getMount() const;
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

private:
  Ui::PreferencesDialog ui;
  QPlainTextEdit *mDefaultExclude = nullptr;
  QCheckBox *mStartMinimized = nullptr;
  QSpinBox *mMaxConcurrent = nullptr;
  QLineEdit *mSocksProxy = nullptr;
};
