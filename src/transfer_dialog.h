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
  ~TransferDialog();

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
  QPlainTextEdit *mPreview = nullptr;
  QPushButton *mPreviewButton = nullptr;
  QLabel *mExcludeExplanation = nullptr;
  bool mPreviewInFlight = false;

  bool wouldDeleteAtDestination() const;
  bool showDeletionSummary(const SyncPreview::Summary &summary);
  static QHash<QString, QString> RemoteTypes();

  bool mIsDownload;
  bool mDryRun = false;
  bool mEnqueued = false;
  bool mIsFolder;
  bool mIsEditMode;

  JobOptions *mJobOptions;

  void putJobOptions();
  void clearValidation();
  void showValidation(QWidget *field, const QString &message);

  void done(int r) override;

signals:
  void tasksListChanged();
};
