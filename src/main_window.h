#pragma once

#include "icon_cache.h"
#include "job_history.h"
#include "job_options.h"
#include "pch.h"
#include "remote_provider.h"
#include <QQueue>
#include "staged_transfer.h"
#include "ui_main_window.h"

class JobWidget;
class RcJobWidget;
class RcloneRcEngine;
class RemoteWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(bool initializeRuntime = true);
  ~MainWindow();

public slots:
  void bringToFront();
  void handleSendToFiles(const QStringList &files);

private slots:
  void rcloneGetVersion();
  void rcloneConfig();
  void createRemote();
  void rcloneListRemotes();
  void listTasks();

  void addTransfer(const QString &message, const QString &source,
                   const QString &dest, const QStringList &args,
                   const QString &backupDirTemplate = QString(),
                   int backupRetainCount = 0);
  void addMount(const QString &remote, const QString &folder);
  void addStream(const QString &remote, const QString &stream);

private:
  Ui::MainWindow ui;

  QSystemTrayIcon mSystemTray;
  QMenu *mWatchMenu = nullptr;
  JobWidget *mLastFinished = nullptr;
  RcloneRcEngine *mRcEngine = nullptr;

  bool mAlwaysShowInTray;
  bool mCloseToTray;
  bool mNotifyFinishedTransfers;

  QLabel *mStatusMessage;
  QLabel *mStatsLabel = nullptr;
  QLineEdit *mBandwidthLimit = nullptr;
  QPlainTextEdit *mErrorLog = nullptr;
  QToolButton *mErrorLogToggle = nullptr;
  QLineEdit *mRemotesFilter = nullptr;
  QLabel *mRemotesEmptyState = nullptr;
  QLineEdit *mTasksFilter = nullptr;
  QLabel *mTasksEmptyState = nullptr;

  IconCache mIcons;

  bool mFirstTime = true;
  bool mInitializeRuntime = true;
  bool mTabsRestored = false;
  int mJobCount = 0;
  int mRunningTransfers = 0;
  bool mLastJobFailed = false;
  bool mScheduleCheckInFlight = false;

  QQueue<StagedTransfer> mTransferQueue;
  QListWidget *mStagingList = nullptr;
  QLabel *mStagingEmptyState = nullptr;
  QToolButton *mStagingDisclosure = nullptr;
  QWidget *mStagingBar = nullptr;
  QPushButton *mRunStagedButton = nullptr;
  QPushButton *mClearStagedButton = nullptr;
  QHash<QUuid, QFileSystemWatcher *> mWatchers;
  QHash<QUuid, QTimer *> mWatchTimers;
  QSet<QUuid> mPausedWatchTasks;

  bool canClose();
  void closeEvent(QCloseEvent *ev) override;
  bool eventFilter(QObject *obj, QEvent *event) override;
  bool getConfigPassword(QProcess *p);
  bool confirmConfigMutation(const QString &action);
  QDateTime rcloneConfigLastModified() const;
  void noteConfigReloadIfChanged(const QDateTime &before);
  RemoteWidget *createRemoteWidgetInstance(const QString &name,
                                           const QString &type,
                                           QWidget *parent);
  void setStatusMessage(const QString &message);
  void showCreateRemoteDialog(const QVector<RemoteProvider> &providers);
  void showRemotesEmptyState(const QString &title, const QString &detail);
  void hideRemotesEmptyState();
  void showTasksEmptyState(const QString &title, const QString &detail);
  void hideTasksEmptyState();
  void updateStagingEmptyState();
  void runStagedTransfer(const StagedTransfer &transfer);
  QString terminalRcloneConfigCommand(const QStringList &args) const;
  bool startDetachedTerminalCommand(const QStringList &args,
                                    const QDateTime &configBefore,
                                    const QString &errorTitle);
  void startMount(const QString &remote, const QString &folder,
                  bool keepMounted, int restartAttempt);
  void launchMount(const QString &remote, const QString &folder,
                   bool keepMounted, int restartAttempt, const QString &opt,
                   bool driveShared, const QString &mountCommand,
                   const QStringList &mountBackendArgs);

  void addEmptyJobsMessage();
  void addTransferViaProcess(const QString &message, const QString &source,
                             const QString &dest, const QStringList &args,
                             const QString &heartbeatUrl = QString(),
                             const QString &postCommand = QString(),
                             const QString &webhookUrl = QString(),
                             const QString &taskName = QString(),
                             const QString &backupDirTemplate = QString(),
                             int backupRetainCount = 0,
                             bool verifyAfterTransfer = false);
  void addRcJobWidget(RcJobWidget *widget,
                      const QString &heartbeatUrl = QString(),
                      const QString &webhookUrl = QString(),
                      const QString &taskName = QString(),
                      const QString &backupDirTemplate = QString(),
                      int backupRetainCount = 0);
  void showJobHistory();
  void noteJobStarted();
  void noteJobFinished(bool success);
  void updateJobIndicators();
  void persistJobHistory(const JobHistoryEntry &entry);
  void updateGlobalStats();
  void pruneBackupRetention(const QString &backupDirTemplate,
                            int backupRetainCount);
  void drainTransferQueue();
  void checkStaleness();
  void sendHeartbeat(const QString &url, bool success);
  void sendWebhook(const QString &url, const QString &taskName, bool success,
                   const QString &error = QString());
  void checkRcloneUpdate(const QString &currentVersion);
  void checkBrowserUpdate();
  QNetworkAccessManager *mNetworkManager = nullptr;

  struct BackgroundError {
    QDateTime timestamp;
    QString jobName;
    QString message;
    bool reviewed = false;
  };
  QList<BackgroundError> mErrorQueue;
  QToolButton *mErrorBadge = nullptr;
  void appendBackgroundError(const QString &jobName, const QString &message);
  void showErrorQueue();

  void runItem(JobOptionsListWidgetItem *item, bool dryrun = false);
  void runJobOptions(JobOptions *jo, bool dryrun = false,
                     bool confirmSync = true);
  void refreshTaskWatchers();
  void rebuildWatchTrayMenu();
  void editSelectedTask();
  void saveStagedTransfers();
  void restoreStagedTransfers();
  QIcon mUploadIcon;
  QIcon mDownloadIcon;
};
