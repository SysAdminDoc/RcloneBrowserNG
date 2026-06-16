#pragma once

#include "job_options.h"
#include "pch.h"
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

private:
  Ui::TransferDialog ui;
  QLabel *mValidation = nullptr;
  QLineEdit *mHeartbeatUrl = nullptr;
  QLineEdit *mNameTransform = nullptr;
  QLineEdit *mPreCommand = nullptr;
  QLineEdit *mPostCommand = nullptr;
  QLineEdit *mWebhookUrl = nullptr;
  QCheckBox *mWatchFolder = nullptr;
  QPlainTextEdit *mPreview = nullptr;
  QPushButton *mPreviewButton = nullptr;

  bool mIsDownload;
  bool mDryRun = false;
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
