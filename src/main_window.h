#pragma once

#include "icon_cache.h"
#include "job_history.h"
#include "job_options.h"
#include "pch.h"
#include "ui_main_window.h"

class JobWidget;
class RcJobWidget;
class RcloneRcEngine;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow();
  ~MainWindow();

public slots:
  void bringToFront();

private slots:
  void rcloneGetVersion();
  void rcloneConfig();
  void createRemote();
  void rcloneListRemotes();
  void listTasks();

  void addTransfer(const QString &message, const QString &source,
                   const QString &dest, const QStringList &args);
  void addMount(const QString &remote, const QString &folder);
  void addStream(const QString &remote, const QString &stream);

private:
  Ui::MainWindow ui;

  QSystemTrayIcon mSystemTray;
  JobWidget *mLastFinished = nullptr;
  RcloneRcEngine *mRcEngine = nullptr;

  bool mAlwaysShowInTray;
  bool mCloseToTray;
  bool mNotifyFinishedTransfers;

  QLabel *mStatusMessage;
  QLineEdit *mRemotesFilter = nullptr;
  QListWidgetItem *mRemotesFilterEmptyItem = nullptr;

  IconCache mIcons;

  bool mFirstTime = true;
  bool mTabsRestored = false;
  int mJobCount = 0;
  bool mLastJobFailed = false;

  bool canClose();
  void closeEvent(QCloseEvent *ev) override;
  bool eventFilter(QObject *obj, QEvent *event) override;
  bool getConfigPassword(QProcess *p);
  bool confirmConfigMutation(const QString &action);
  QDateTime rcloneConfigLastModified() const;
  void noteConfigReloadIfChanged(const QDateTime &before);
  void setStatusMessage(const QString &message);
  QString terminalRcloneConfigCommand(const QStringList &args) const;
  bool startDetachedTerminalCommand(const QStringList &args,
                                    const QDateTime &configBefore,
                                    const QString &errorTitle);
  void startMount(const QString &remote, const QString &folder,
                  bool keepMounted, int restartAttempt);

  void addEmptyJobsMessage();
  void addTransferViaProcess(const QString &message, const QString &source,
                             const QString &dest, const QStringList &args,
                             const QString &heartbeatUrl = QString(),
                             const QString &postCommand = QString(),
                             const QString &webhookUrl = QString(),
                             const QString &taskName = QString());
  void addRcJobWidget(RcJobWidget *widget,
                      const QString &heartbeatUrl = QString(),
                      const QString &webhookUrl = QString(),
                      const QString &taskName = QString());
  void showJobHistory();
  void noteJobStarted();
  void noteJobFinished(bool success);
  void updateJobIndicators();
  void persistJobHistory(const JobHistoryEntry &entry);
  void sendHeartbeat(const QString &url, bool success);
  void sendWebhook(const QString &url, const QString &taskName, bool success,
                   const QString &error = QString());
  void checkRcloneUpdate(const QString &currentVersion);
  void checkBrowserUpdate();
  QNetworkAccessManager *mNetworkManager = nullptr;

  void runItem(JobOptionsListWidgetItem *item, bool dryrun = false);
  void editSelectedTask();
  QIcon mUploadIcon;
  QIcon mDownloadIcon;
};
