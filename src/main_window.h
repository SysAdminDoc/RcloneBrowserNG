#pragma once

#include "icon_cache.h"
#include "job_history.h"
#include "job_options.h"
#include "pch.h"
#include "rc_delete_batch.h"
#include "remote_list_parser.h"
#include "remote_provider.h"
#include <QQueue>
#include "staged_transfer.h"
#include "ui_main_window.h"

class JobWidget;
class RcJobWidget;
class RcloneRcEngine;
class RemoteWidget;
class VssSession;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(bool initializeRuntime = true);
  ~MainWindow() override;

  // Shows the tray icon even when "Always show in tray" is off. Called
  // before the window is hidden at startup so a minimized start always
  // leaves a visible way to reach the application.
  void ensureTrayIconVisible();

  // Runs a saved task's post-transfer command. Public so the failure
  // path can be driven directly by tests: it used to be a detached
  // process whose exit code nobody looked at.
  void runPostCommand(const QString &command, const QString &taskLabel);
  void recordHookFailure(const QString &taskLabel, const QString &command,
                         const QString &message, int exitCode,
                         const QDateTime &startedAt);
  int backgroundErrorCount() const { return static_cast<int>(mErrorQueue.size()); }
  QString lastBackgroundErrorMessage() const {
    return mErrorQueue.isEmpty() ? QString() : mErrorQueue.last().message;
  }

public slots:
  void bringToFront();
  void handleSendToFiles(const QStringList &files);

private slots:
  void rcloneGetVersion();
  void offerRcloneDownload();
  void rcloneConfig();
  void createRemote();
  void rcloneListRemotes();
  void listTasks();

  void addTransfer(const QString &message, const QString &source,
                   const QString &dest, const QStringList &args,
                   const QString &backupDirTemplate = QString(),
                   int backupRetainCount = 0,
                   bool verifyAfterTransfer = false,
                   bool vssSnapshot = false);
  void deleteItems(const RcDeleteBatch::Plan &plan);
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
  QLabel *mRemotesHiddenNotice = nullptr;
  int mHiddenCryptBackends = 0;
  bool mListRemotesJson = false;
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
  // The tooltip shows detail when given, else the message itself.
  void setStatusMessage(const QString &message,
                        const QString &detail = QString());
  void showCreateRemoteDialog(const QVector<RemoteProvider> &providers);
  void createCombineRemote(const QString &initialName,
                           const QString &preselected);
  void showRemotesEmptyState(const QString &title, const QString &detail);
  // Fills the remotes list, from rclone listremotes or from the remote
  // rclone's config/dump.
  void showRemotes(const QVector<RemoteListParser::Remote> &parsed);
  bool shouldHideCryptBackends();
  void hideCryptBackends(const QJsonObject &dump);
  // The engine, made on first use and pointed at the remote rclone when one
  // is set in Preferences.
  RcloneRcEngine *rcEngine();
  void applyRemoteDaemonSettings();
  // Puts the window in or out of remote rclone mode: the config buttons and
  // the browser tabs belong to whichever rclone was in use before.
  void updateRemoteDaemonMode(bool closeBrowserTabs);
  void hideRemotesEmptyState();
  void updateRemotesHiddenNotice();
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
                   bool keepMounted, int restartAttempt,
                   const QStringList &mountOptions, const QString &mountCommand,
                   const QStringList &mountBackendArgs);

  void addEmptyJobsMessage();
  // With a remote rclone every job runs there as an rc job. There is no
  // rclone here to fall back to, so a job it refuses is reported instead.
  void startRemoteDaemonTransfer(const QString &message, const QString &source,
                                 const QString &dest, const QStringList &args,
                                 const QString &heartbeatUrl,
                                 const QString &postCommand,
                                 const QString &webhookUrl,
                                 const QString &taskName,
                                 const QString &backupDirTemplate,
                                 int backupRetainCount,
                                 bool verifyAfterTransfer, bool vssSnapshot);
  void addTransferViaProcess(const QString &message, const QString &source,
                             const QString &dest, const QStringList &args,
                             const QString &heartbeatUrl = QString(),
                             const QString &postCommand = QString(),
                             const QString &webhookUrl = QString(),
                             const QString &taskName = QString(),
                             const QString &backupDirTemplate = QString(),
                             int backupRetainCount = 0,
                             bool verifyAfterTransfer = false,
                             bool vssSnapshot = false);
  // addTransferViaProcess once DryRunCheck has passed a dry run.
  void launchTransferViaProcess(const QString &message, const QString &source,
                                const QString &dest, const QStringList &args,
                                const QString &heartbeatUrl,
                                const QString &postCommand,
                                const QString &webhookUrl,
                                const QString &taskName,
                                const QString &backupDirTemplate,
                                int backupRetainCount,
                                bool verifyAfterTransfer, bool vssSnapshot);
  // The part of addTransferViaProcess after the concurrency check. With a
  // snapshot, args already read through it and the job's card owns it.
  void startTransferProcess(const QString &message, const QString &source,
                            const QString &dest, const QStringList &args,
                            const QString &heartbeatUrl,
                            const QString &postCommand,
                            const QString &webhookUrl,
                            const QString &taskName,
                            const QString &backupDirTemplate,
                            int backupRetainCount, bool verifyAfterTransfer,
                            VssSession *snapshot);
  void snapshotFailed(const QString &message, const QString &source,
                      const QString &dest, const QString &error,
                      const QString &heartbeatUrl,
                      const QString &webhookUrl, const QString &taskName);
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
  void useRclone(const QString &path);
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
