#pragma once

#include "job_options.h"
#include "pch.h"
#include "sync_preview.h"
#include "ui_transfer_dialog.h"

class TransferDialog : public QDialog {
  Q_OBJECT

public:
  TransferDialog(bool isDownload, bool isDrop, const QString &remote,
                 const QDir &path, bool isFolder, QWidget *parent = nullptr,
                 JobOptions *task = nullptr, bool editMode = false);
  ~TransferDialog() override;

  void setSource(const QString &path);

  QString getMode() const;
  QString getSource() const;
  QString getDest() const;
  QStringList getOptions();

  JobOptions *getJobOptions();
  bool wasEnqueued() const { return mEnqueued; }

private slots:
  // Sync and --delete-excluded remove files at the destination, so
  // they get a dry-run pass and a summary of what goes before the
  // transfer starts. Everything else accepts straight through.
  void acceptWithDeletionPreview();
  // Shows the server-side option only when both paths name remotes
  // of the same backend type.
  void refreshServerSideOption();
  // rclone patterns match at any depth unless anchored, which is
  // the mistake that cost an upstream reporter data when combined
  // with --delete-excluded.
  void refreshExcludeExplanation();

private:
  Ui::TransferDialog ui;
  QLabel *mValidation = nullptr;
  QLineEdit *mHeartbeatUrl = nullptr;
  QLineEdit *mNameTransform = nullptr;
  QLineEdit *mPreCommand = nullptr;
  QLineEdit *mPostCommand = nullptr;
  QLineEdit *mWebhookUrl = nullptr;
  QRadioButton *mRbBisync = nullptr;
  QComboBox *mConflictResolve = nullptr;
  QLineEdit *mBackupDir = nullptr;
  QSpinBox *mBackupRetain = nullptr;
  QCheckBox *mWatchFolder = nullptr;
  QCheckBox *mVerifyAfter = nullptr;
  QCheckBox *mVssSnapshot = nullptr;
  // Sync's --max-delete. Both only mean something while Sync is picked.
  QCheckBox *mDeleteCap = nullptr;
  QSpinBox *mDeleteCapCount = nullptr;
  QPlainTextEdit *mPreview = nullptr;
  QPushButton *mPreviewButton = nullptr;
  QLabel *mExcludeExplanation = nullptr;
  // Whether the server-side option was actually on offer the last time
  // the paths were checked. A tick left behind after the user retargets
  // the transfer must not reach the command line.
  bool mServerSideOffered = false;
  bool mPreviewInFlight = false;

  bool wouldDeleteAtDestination() const;
  // warnings are SyncPreview::CapWarnings for the caps this run uses.
  bool showDeletionSummary(const SyncPreview::Summary &summary,
                           const QStringList &warnings);
  void startRemoteTypesProbe();
  // Enables the snapshot option only for a source and operation it can
  // serve, and says why in the tooltip when it can't.
  void updateSnapshotOption();
  void updateDeleteCapOption();
  // The cap the dialog would run with, or -1 when there is none.
  int deleteCapValue() const;

  bool mIsDownload;
  bool mDryRun = false;
  bool mEnqueued = false;
  bool mIsFolder;
  bool mIsEditMode;

  JobOptions *mJobOptions;

  void putJobOptions();
  void clearValidation();
  void showValidation(QWidget *field, const QString &message);
  // Marks the bandwidth field and says what rclone would refuse in it.
  // focus also shows its tab and puts the cursor there.
  bool checkBandwidth(bool focus);

  void done(int r) override;

signals:
  void tasksListChanged();
};
