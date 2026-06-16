#include "transfer_dialog.h"
#include "list_of_job_options.h"
#include "interface_polish.h"
#include "rclone_capabilities.h"
#include "utils.h"

TransferDialog::TransferDialog(bool isDownload, bool isDrop,
                               const QString &remote, const QDir &path,
                               bool isFolder, QWidget *parent, JobOptions *task,
                               bool editMode)
    : QDialog(parent), mIsDownload(isDownload), mIsFolder(isFolder),
      mIsEditMode(editMode), mJobOptions(task) {
  ui.setupUi(this);
  if (layout()) {
    layout()->setSizeConstraint(QLayout::SetDefaultConstraint);
    layout()->setSpacing(10);
    layout()->setContentsMargins(12, 12, 12, 12);
  }
  UiPolish::SetWindowDefaults(this, QSize(780, 580));
  resize(840, 620);
  setWindowTitle(isDownload ? "Download from remote" : "Upload to remote");
  ui.tabWidget->setDocumentMode(true);
  UiPolish::SetToolbarSurface(ui.pathGroup);
  UiPolish::SetPathField(ui.textSource, "Transfer source");
  UiPolish::SetPathField(ui.textDest, "Transfer destination");
  UiPolish::SetOutputView(ui.textExclude, "Exclude patterns");
  ui.textSource->setPlaceholderText(isDownload ? "remote:path" : "Local file or folder");
  ui.textDest->setPlaceholderText(isDownload ? "Local destination folder"
                                             : "remote:path");
  ui.textExtra->setPlaceholderText("Additional rclone flags for this transfer");
  ui.textDescription->setPlaceholderText("Name this task if you want to save it");
  ui.textBandwidth->setPlaceholderText("off, 10M, or timetable syntax");
  auto *bwEditBtn = new QToolButton(this);
  bwEditBtn->setText("...");
  bwEditBtn->setToolTip("Open the bandwidth timetable editor.");
  bwEditBtn->setAccessibleName("Edit bandwidth timetable");
  bwEditBtn->setMaximumWidth(28);
  if (auto *form =
          qobject_cast<QFormLayout *>(ui.textBandwidth->parentWidget()->layout())) {
    int row = -1;
    QFormLayout::ItemRole role;
    form->getWidgetPosition(ui.textBandwidth, &row, &role);
    if (row >= 0) {
      auto *bwRow = new QHBoxLayout();
      form->removeWidget(ui.textBandwidth);
      bwRow->addWidget(ui.textBandwidth, 1);
      bwRow->addWidget(bwEditBtn);
      form->setLayout(row, QFormLayout::FieldRole, bwRow);
    }
  }
  QObject::connect(bwEditBtn, &QToolButton::clicked, this, [this]() {
    QDialog dlg(this);
    dlg.setWindowTitle("Bandwidth Timetable");
    dlg.resize(500, 350);
    auto *layout = new QVBoxLayout(&dlg);
    layout->setSpacing(8);
    auto *hint = new QLabel(
        "Define time-of-day bandwidth limits. Format: HH:MM,speed "
        "(e.g. 08:00,512k). Use 'off' for unlimited.", &dlg);
    hint->setWordWrap(true);
    UiPolish::SetMuted(hint);
    layout->addWidget(hint);

    auto *table = new QTableWidget(0, 2, &dlg);
    table->setHorizontalHeaderLabels({"Time (HH:MM)", "Bandwidth"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setAccessibleName("Bandwidth timetable entries");
    layout->addWidget(table, 1);

    QString current = ui.textBandwidth->text().trimmed();
    QStringList entries = current.split(' ', Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
      int comma = entry.indexOf(',');
      if (comma > 0) {
        int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0,
                       new QTableWidgetItem(entry.left(comma)));
        table->setItem(row, 1,
                       new QTableWidgetItem(entry.mid(comma + 1)));
      }
    }

    auto *btnRow = new QHBoxLayout();
    auto *addBtn = new QPushButton("Add Row", &dlg);
    auto *removeBtn = new QPushButton("Remove Row", &dlg);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    QObject::connect(addBtn, &QPushButton::clicked, &dlg, [table]() {
      int row = table->rowCount();
      table->insertRow(row);
      table->setItem(row, 0, new QTableWidgetItem("00:00"));
      table->setItem(row, 1, new QTableWidgetItem("off"));
    });
    QObject::connect(removeBtn, &QPushButton::clicked, &dlg, [table]() {
      int row = table->currentRow();
      if (row >= 0)
        table->removeRow(row);
    });

    auto *preview = new QLabel(&dlg);
    UiPolish::SetMuted(preview);
    layout->addWidget(preview);

    auto updatePreview = [table, preview]() {
      QStringList parts;
      for (int i = 0; i < table->rowCount(); ++i) {
        auto *t = table->item(i, 0);
        auto *b = table->item(i, 1);
        if (t && b && !t->text().isEmpty())
          parts << t->text() + "," + b->text();
      }
      preview->setText("Preview: --bwlimit \"" + parts.join(' ') + "\"");
    };
    QObject::connect(table, &QTableWidget::cellChanged, &dlg,
                     [updatePreview](int, int) { updatePreview(); });
    updatePreview();

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg,
                     &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() == QDialog::Accepted) {
      QStringList parts;
      for (int i = 0; i < table->rowCount(); ++i) {
        auto *t = table->item(i, 0);
        auto *b = table->item(i, 1);
        if (t && b && !t->text().isEmpty())
          parts << t->text() + "," + b->text();
      }
      ui.textBandwidth->setText(parts.join(' '));
    }
  });

  ui.textMinSize->setPlaceholderText("100M");
  ui.textMinAge->setPlaceholderText("1d");
  ui.textMaxAge->setPlaceholderText("30d");
  ui.textExclude->setPlaceholderText("One --exclude pattern per line");

  auto *excludeQuickBar = new QWidget(this);
  auto *quickLayout = new QHBoxLayout(excludeQuickBar);
  quickLayout->setContentsMargins(0, 0, 0, 0);
  quickLayout->setSpacing(4);
  auto *quickLabel = new QLabel("Quick add:", excludeQuickBar);
  UiPolish::SetMuted(quickLabel);
  quickLayout->addWidget(quickLabel);
  struct QuickPattern {
    const char *label;
    const char *pattern;
  };
  const QuickPattern quickPatterns[] = {
      {"Temp files", "*.tmp\n~*\n*.bak"},
      {"OS junk", ".DS_Store\nThumbs.db\ndesktop.ini\n.Spotlight-V100/**"},
      {"Git", ".git/**"},
      {"Node", "node_modules/**"},
      {"Hidden", ".*"},
  };
  for (const auto &qp : quickPatterns) {
    auto *btn = new QPushButton(qp.label, excludeQuickBar);
    btn->setMaximumHeight(24);
    btn->setToolTip(QString("Add: %1").arg(QString(qp.pattern).replace('\n', ", ")));
    const QString pattern = qp.pattern;
    QObject::connect(btn, &QPushButton::clicked, this, [this, pattern]() {
      QString existing = ui.textExclude->toPlainText().trimmed();
      if (!existing.isEmpty() && !existing.endsWith('\n'))
        existing += '\n';
      ui.textExclude->setPlainText(existing + pattern);
    });
    quickLayout->addWidget(btn);
  }
  quickLayout->addStretch();
  if (auto *parentLayout =
          qobject_cast<QVBoxLayout *>(ui.textExclude->parentWidget()->layout())) {
    int idx = parentLayout->indexOf(ui.textExclude);
    if (idx >= 0)
      parentLayout->insertWidget(idx + 1, excludeQuickBar);
  }

  auto *heartbeatLabel = new QLabel("Heartbeat URL:", this);
  heartbeatLabel->setToolTip(
      "Healthchecks.io, ntfy.sh, or any URL to ping on job completion.\n"
      "On success the app sends GET to this URL; on failure it appends /fail.");
  mHeartbeatUrl = new QLineEdit(this);
  mHeartbeatUrl->setPlaceholderText("https://hc-ping.com/your-uuid or https://ntfy.sh/your-topic");
  mHeartbeatUrl->setAccessibleName("Heartbeat URL for monitoring");
  ui.gridLayout->addWidget(heartbeatLabel, 8, 0);
  ui.gridLayout->addWidget(mHeartbeatUrl, 8, 1);
  auto *webhookLabel = new QLabel("Webhook URL:", this);
  webhookLabel->setToolTip(
      "Discord, Gotify, Shoutrrr, or any URL to POST a JSON status on "
      "job completion.\nPayload includes task name, status, duration, "
      "bytes transferred, and error message (if any).");
  mWebhookUrl = new QLineEdit(this);
  mWebhookUrl->setPlaceholderText("https://discord.com/api/webhooks/... or https://gotify.example.com/message");
  mWebhookUrl->setAccessibleName("Webhook URL for notifications");
  ui.gridLayout->addWidget(webhookLabel, 9, 0);
  ui.gridLayout->addWidget(mWebhookUrl, 9, 1);
  auto *nameTransformLabel = new QLabel("Name transform:", this);
  nameTransformLabel->setToolTip(
      "rclone --name-transform pattern (requires rclone >= 1.74).\n"
      "Example: lowercase or s/regex/replacement/");
  mNameTransform = new QLineEdit(this);
  mNameTransform->setPlaceholderText("lowercase, s/regex/replacement/, or blank to skip");
  mNameTransform->setAccessibleName("Name transform pattern");
  {
    auto caps = RcloneCapabilities::detect();
    if (!caps.hasNameTransform()) {
      mNameTransform->setDisabled(true);
      mNameTransform->setPlaceholderText("Requires rclone >= 1.74");
      nameTransformLabel->setEnabled(false);
    }
  }
  ui.gridLayout->addWidget(nameTransformLabel, 10, 0);
  ui.gridLayout->addWidget(mNameTransform, 10, 1);
  auto *preCommandLabel = new QLabel("Pre-job command:", this);
  preCommandLabel->setToolTip("Shell command to run before the transfer starts.");
  mPreCommand = new QLineEdit(this);
  mPreCommand->setPlaceholderText("Command to run before the transfer");
  mPreCommand->setAccessibleName("Pre-job command");
  ui.gridLayout->addWidget(preCommandLabel, 11, 0);
  ui.gridLayout->addWidget(mPreCommand, 11, 1);

  auto *postCommandLabel = new QLabel("Post-job command:", this);
  postCommandLabel->setToolTip("Shell command to run after the transfer finishes.");
  mPostCommand = new QLineEdit(this);
  mPostCommand->setPlaceholderText("Command to run after the transfer");
  mPostCommand->setAccessibleName("Post-job command");
  ui.gridLayout->addWidget(postCommandLabel, 12, 0);
  ui.gridLayout->addWidget(mPostCommand, 12, 1);

  mRbBisync = new QRadioButton("Bisync", this);
  mRbBisync->setToolTip("Bidirectional sync between source and destination.");
  mRbBisync->setAccessibleName("Bisync operation");
  {
    auto caps = RcloneCapabilities::detect();
    if (!caps.hasBisync()) {
      mRbBisync->setDisabled(true);
      mRbBisync->setToolTip("Requires rclone >= 1.58 with bisync support.");
    }
  }
  if (auto *layout =
          qobject_cast<QHBoxLayout *>(ui.rbSync->parentWidget()->layout())) {
    int idx = layout->indexOf(ui.rbSync);
    if (idx >= 0)
      layout->insertWidget(idx + 1, mRbBisync);
  }

  auto *conflictLabel = new QLabel("Conflict resolution:", this);
  conflictLabel->setToolTip(
      "How bisync should resolve conflicts when the same file changed "
      "on both sides.");
  mConflictResolve = new QComboBox(this);
  mConflictResolve->addItem("none (report only)", "none");
  mConflictResolve->addItem("newer wins", "newer");
  mConflictResolve->addItem("older wins", "older");
  mConflictResolve->addItem("larger wins", "larger");
  mConflictResolve->addItem("smaller wins", "smaller");
  mConflictResolve->addItem("path1 wins", "path1");
  mConflictResolve->addItem("path2 wins", "path2");
  mConflictResolve->setAccessibleName("Bisync conflict resolution strategy");
  conflictLabel->setVisible(false);
  mConflictResolve->setVisible(false);
  ui.gridLayout->addWidget(conflictLabel, 14, 0);
  ui.gridLayout->addWidget(mConflictResolve, 14, 1);
  QObject::connect(mRbBisync, &QRadioButton::toggled, this,
                   [conflictLabel, this](bool checked) {
                     conflictLabel->setVisible(checked);
                     mConflictResolve->setVisible(checked);
                   });

  auto *backupDirLabel = new QLabel("Backup dir:", this);
  backupDirLabel->setToolTip(
      "rclone --backup-dir path. Use {date} for auto-dated folders.\n"
      "Example: remote:backups/{date}");
  mBackupDir = new QLineEdit(this);
  mBackupDir->setPlaceholderText("remote:backups/{date} or blank to skip");
  mBackupDir->setAccessibleName("Backup directory pattern");
  ui.gridLayout->addWidget(backupDirLabel, 15, 0);
  ui.gridLayout->addWidget(mBackupDir, 15, 1);

  auto *retainLabel = new QLabel("Retain backups:", this);
  retainLabel->setToolTip(
      "Maximum number of backup-dir snapshots to keep. 0 = keep all.");
  mBackupRetain = new QSpinBox(this);
  mBackupRetain->setMinimum(0);
  mBackupRetain->setMaximum(9999);
  mBackupRetain->setSpecialValueText("Keep all");
  mBackupRetain->setAccessibleName("Backup retention count");
  ui.gridLayout->addWidget(retainLabel, 16, 0);
  ui.gridLayout->addWidget(mBackupRetain, 16, 1);

  mWatchFolder = new QCheckBox("Watch local source and rerun on changes", this);
  mWatchFolder->setToolTip(
      "Saved upload tasks can watch the local source folder and rerun after "
      "filesystem changes settle.");
  mWatchFolder->setAccessibleName("Watch local source folder");
  if (mIsDownload) {
    mWatchFolder->setDisabled(true);
    mWatchFolder->setToolTip(
        "Watch-folder mode is available for upload tasks with a local source.");
  }
  ui.gridLayout->addWidget(mWatchFolder, 17, 1);

  mValidation = new QLabel(this);
  UiPolish::SetValidationMessage(mValidation, QString(), QString());
  ui.gridLayout->addWidget(mValidation, 18, 0, 1, 2);
  QObject::connect(ui.textSource, &QLineEdit::textChanged, this,
                   &TransferDialog::clearValidation);
  QObject::connect(ui.textDest, &QLineEdit::textChanged, this,
                   &TransferDialog::clearValidation);
  QObject::connect(ui.textDescription, &QLineEdit::textChanged, this,
                   &TransferDialog::clearValidation);

  QStyle *style = qApp->style();
  ui.buttonSourceFile->setIcon(style->standardIcon(QStyle::SP_FileIcon));
  ui.buttonSourceFolder->setIcon(style->standardIcon(QStyle::SP_DirIcon));
  ui.buttonDest->setIcon(style->standardIcon(QStyle::SP_DirIcon));

  ui.buttonDefaultSource->setIcon(style->standardIcon(QStyle::SP_DirHomeIcon));
  ui.buttonDefaultDest->setIcon(style->standardIcon(QStyle::SP_DirHomeIcon));
  ui.buttonSourceFile->setStyleSheet(QString());
  ui.buttonSourceFolder->setStyleSheet(QString());
  ui.buttonDefaultSource->setStyleSheet(QString());
  ui.buttonDest->setStyleSheet(QString());
  ui.buttonDefaultDest->setStyleSheet(QString());
  UiPolish::SetCompactToolButton(ui.buttonSourceFile, "Choose source file");
  UiPolish::SetCompactToolButton(ui.buttonSourceFolder, "Choose source folder");
  UiPolish::SetCompactToolButton(ui.buttonDefaultSource,
                                 "Use default source folder");
  UiPolish::SetCompactToolButton(ui.buttonDest, "Choose destination folder");
  UiPolish::SetCompactToolButton(ui.buttonDefaultDest,
                                 "Use default destination folder");

  if (!mIsEditMode) {
    QPushButton *dryRun =
        ui.buttonBox->addButton("Dry Run", QDialogButtonBox::AcceptRole);
    QPushButton *run =
        ui.buttonBox->addButton("Run", QDialogButtonBox::AcceptRole);
    UiPolish::SetPrimaryButton(run);
    dryRun->setToolTip(
        "Preview the transfer with --dry-run; no files are changed.");
    run->setToolTip("Start the transfer and apply changes.");
    dryRun->setAccessibleName("Preview transfer with dry run");
    run->setAccessibleName("Run transfer");
    QObject::connect(dryRun, &QPushButton::clicked, this,
                     [=]() { mDryRun = true; });
    // reset the flag in case a prior "Dry run" click was rejected by
    // validation and the user then clicks "Run"
    QObject::connect(run, &QPushButton::clicked, this,
                     [=]() { mDryRun = false; });
  }

  if (!mIsEditMode) {
    QPushButton *enqueue =
        ui.buttonBox->addButton("Enqueue", QDialogButtonBox::AcceptRole);
    enqueue->setToolTip(
        "Add to the staging queue for batch review instead of running now.");
    enqueue->setAccessibleName("Enqueue transfer for later");
    QObject::connect(enqueue, &QPushButton::clicked, this,
                     [this]() { mEnqueued = true; });
  }

  QPushButton *saveTask = ui.buttonBox->addButton(
      "Save Task", QDialogButtonBox::ButtonRole::ActionRole);
  saveTask->setToolTip("Save these settings as a reusable task.");
  saveTask->setAccessibleName("Save transfer as task");
  if (auto restore = ui.buttonBox->button(QDialogButtonBox::RestoreDefaults)) {
    restore->setToolTip("Restore recommended transfer defaults.");
  }

  QObject::connect(
      ui.buttonBox->button(QDialogButtonBox::RestoreDefaults),
      &QPushButton::clicked, this, [=]() {
        ui.cbSyncDelete->setCurrentIndex(0);
        // set combobox tooltips
        ui.cbSyncDelete->setItemData(0, "--delete-during", Qt::ToolTipRole);
        ui.cbSyncDelete->setItemData(1, "--delete-after", Qt::ToolTipRole);
        ui.cbSyncDelete->setItemData(2, "--delete-before", Qt::ToolTipRole);
        ui.checkSkipNewer->setChecked(false);
        ui.checkSkipExisting->setChecked(false);
        ui.checkCompare->setChecked(true);
        ui.cbCompare->setCurrentIndex(0);
        // set combobox tooltips
        ui.cbCompare->setItemData(0, "default", Qt::ToolTipRole);
        ui.cbCompare->setItemData(1, "--checksum", Qt::ToolTipRole);
        ui.cbCompare->setItemData(2, "--ignore-size", Qt::ToolTipRole);
        ui.cbCompare->setItemData(3, "--size-only", Qt::ToolTipRole);
        ui.cbCompare->setItemData(4, "--checksum --ignore-size",
                                  Qt::ToolTipRole);
        //      ui.checkVerbose->setChecked(false);
        ui.checkSameFilesystem->setChecked(false);
        ui.checkDontUpdateModified->setChecked(false);
        ui.spinTransfers->setValue(4);
        ui.spinCheckers->setValue(8);
        ui.textBandwidth->clear();
        ui.textMinSize->clear();
        ui.textMinAge->clear();
        ui.textMaxAge->clear();
        ui.spinMaxDepth->setValue(0);
        ui.spinConnectTimeout->setValue(60);
        ui.spinIdleTimeout->setValue(300);
        ui.spinRetries->setValue(3);
        ui.spinLowLevelRetries->setValue(10);
        ui.checkDeleteExcluded->setChecked(false);
        ui.textExclude->clear();
        auto settings = GetSettings();
        if (isDownload) {
          // download
          ui.textExtra->setText(
              settings->value("Settings/defaultDownloadOptions").toString());
        } else {
          // upload
          ui.textExtra->setText(
              settings->value("Settings/defaultUploadOptions").toString());
        }
      });

  ui.buttonBox->button(QDialogButtonBox::RestoreDefaults)->click();

  QObject::connect(saveTask, &QPushButton::clicked, this, [=]() {
    // validate before saving task...
    if (ui.textDescription->text().trimmed().isEmpty()) {
      showValidation(ui.textDescription,
                     "Add a task name before saving this reusable transfer.");
      return;
    }
    // even though the below does not match the condition on the Run buttons
    // it SEEMS like blanking either one would be a problem, right?
    if (ui.textSource->text().trimmed().isEmpty()) {
      showValidation(ui.textSource, "Saved tasks need a source path.");
      return;
    }
    if (ui.textDest->text().trimmed().isEmpty()) {
      showValidation(ui.textDest, "Saved tasks need a destination path.");
      return;
    }
    JobOptions *jobo = getJobOptions();
    ListOfJobOptions::getInstance()->Persist(jobo);
    // always close on save
    // if (mIsEditMode)
    this->close();
  });

  if (!mIsEditMode) {
    mPreview = new QPlainTextEdit(this);
    mPreview->setReadOnly(true);
    mPreview->setVisible(false);
    mPreview->setMaximumHeight(200);
    mPreview->setAccessibleName("Dry-run preview output");
    UiPolish::SetOutputView(mPreview, "Transfer preview");
    mPreviewButton = new QPushButton("Preview Changes", this);
    mPreviewButton->setToolTip(
        "Run with --dry-run to see what would change, without modifying "
        "any files.");
    ui.gridLayout->addWidget(mPreviewButton, 19, 0);
    ui.gridLayout->addWidget(mPreview, 19, 1);
    QObject::connect(mPreviewButton, &QPushButton::clicked, this, [=]() {
      if (mIsDownload) {
        if (ui.textDest->text().trimmed().isEmpty()) {
          showValidation(ui.textDest,
                         "Choose a destination before previewing.");
          return;
        }
      } else {
        if (ui.textSource->text().trimmed().isEmpty()) {
          showValidation(ui.textSource,
                         "Choose a source before previewing.");
          return;
        }
      }
      mPreview->clear();
      mPreview->setVisible(true);
      mPreview->setPlainText("Running dry-run preview...");
      mPreviewButton->setEnabled(false);

      JobOptions *opts = getJobOptions();
      opts->dryRun = true;
      QStringList args = GetRcloneConf() + opts->getOptions();
      opts->dryRun = false;

      auto *proc = new QProcess(this);
      proc->setProcessChannelMode(QProcess::MergedChannels);
      UseRclonePassword(proc);
      proc->start(GetRclone(), args, QIODevice::ReadOnly);

      QObject::connect(
          proc,
          static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
              &QProcess::finished),
          this, [=](int code, QProcess::ExitStatus) {
            proc->deleteLater();
            mPreviewButton->setEnabled(true);
            QByteArray raw = proc->readAll();
            QStringList output;
            for (const QByteArray &line : raw.split('\n')) {
              QByteArray trimmed = line.trimmed();
              if (trimmed.isEmpty())
                continue;
              QJsonDocument doc = QJsonDocument::fromJson(trimmed);
              if (doc.isObject()) {
                QString msg = doc.object().value("msg").toString();
                if (!msg.isEmpty())
                  output << msg;
              } else {
                output << QString::fromUtf8(trimmed);
              }
            }
            if (output.isEmpty() && code == 0) {
              mPreview->setPlainText("No changes detected.");
            } else {
              mPreview->setPlainText(output.join('\n'));
            }
          });
    });
  }

  QObject::connect(ui.buttonBox, &QDialogButtonBox::accepted, this,
                   &QDialog::accept);
  QObject::connect(ui.buttonBox, &QDialogButtonBox::rejected, this,
                   &QDialog::reject);

  QObject::connect(ui.buttonSourceFile, &QToolButton::clicked, this, [=]() {
    QString file = QFileDialog::getOpenFileName(this, "Choose file to upload");
    if (!file.isEmpty()) {
      ui.textSource->setText(QDir::toNativeSeparators(file));
      ui.textDest->setText(remote + ":" + path.path());
    }
  });

  QObject::connect(ui.buttonSourceFolder, &QToolButton::clicked, this, [=]() {
    auto settings = GetSettings();
    QString last_used_source_folder =
        (settings->value("Settings/lastUsedSourceFolder").toString());
    QString folder = QFileDialog::getExistingDirectory(
        this, "Choose folder to upload", last_used_source_folder,
        QFileDialog::ShowDirsOnly);
    if (!folder.isEmpty()) {
      // store new folder in lastUsedSourceFolder
      settings->setValue("Settings/lastUsedSourceFolder", folder);
      ui.textSource->setText(QDir::toNativeSeparators(folder));
      ui.textDest->setText(remote + ":" +
                           path.filePath(QFileInfo(folder).fileName()));
    }
  });

  QObject::connect(ui.buttonDefaultSource, &QToolButton::clicked, this, [=]() {
    auto settings = GetSettings();
    QString default_folder =
        (settings->value("Settings/defaultUploadDir").toString());
    // store default folder in lastUsedSourceFolder
    settings->setValue("Settings/lastUsedSourceFolder", default_folder);
    ui.textSource->setText(QDir::toNativeSeparators(default_folder));
    if (!default_folder.isEmpty()) {
      ui.textDest->setText(remote + ":" +
                           path.filePath(QFileInfo(default_folder).fileName()));
    } else {
      ui.textDest->setText(remote + ":" + path.path());
    };
  });

  QObject::connect(ui.buttonDest, &QToolButton::clicked, this, [=]() {
    auto settings = GetSettings();
    QString last_used_dest_folder =
        (settings->value("Settings/lastUsedDestFolder").toString());
    QString folder = QFileDialog::getExistingDirectory(
        this, "Choose destination folder", last_used_dest_folder,
        QFileDialog::ShowDirsOnly);
    if (!folder.isEmpty()) {
      // store new folder in lastUsedDestFolder
      settings->setValue("Settings/lastUsedDestFolder", folder);
      if (isFolder) {
        ui.textDest->setText(
            QDir::toNativeSeparators(folder + "/" + path.dirName()));
      } else {
        ui.textDest->setText(QDir::toNativeSeparators(folder));
      }
    }
  });

  QObject::connect(ui.buttonDefaultDest, &QToolButton::clicked, this, [=]() {
    auto settings = GetSettings();
    QString default_folder =
        (settings->value("Settings/defaultDownloadDir").toString());
    // store default_folder in lastUsedDestFolder
    settings->setValue("Settings/lastUsedDestFolder", default_folder);
    if (!default_folder.isEmpty()) {
      if (isFolder) {
        ui.textDest->setText(
            QDir::toNativeSeparators(default_folder + "/" + path.dirName()));
      } else {
        ui.textDest->setText(QDir::toNativeSeparators(default_folder));
      }
    } else {
      ui.textDest->setText("");
    };
  });

  auto settings = GetSettings();
  settings->beginGroup("Transfer");
  ReadSettings(settings.get(), this);
  settings->endGroup();

  ui.buttonSourceFile->setVisible(!isDownload);
  ui.buttonSourceFolder->setVisible(!isDownload);
  ui.buttonDefaultSource->setVisible(!isDownload);

  ui.buttonDest->setVisible(isDownload);
  ui.buttonDefaultDest->setVisible(isDownload);

  // Info only - should not be edited
  // would be nice to display it only for Google Drive - todo
  ui.checkisDriveSharedWithMe->setDisabled(true);
  ui.checkisDriveSharedWithMe->setToolTip(
      "Inherited from the current Google Drive tab.");

  ui.checkisDriveSharedWithMe->setChecked(
      settings->value("Settings/driveShared", false).toBool());
  // always clear for new jobs
  ui.textDescription->clear();

  if (mIsEditMode && mJobOptions != nullptr) {
    // it's not really valid for only one of these things to be true.
    // when operating on an existing instance i.e. a saved task,
    // changing the dest or src seems to have problems so we
    // will not allow it.  simple enough, and better, to make a
    // new task for different pairings anyway.  that will make
    // a lot more sense when/if scheduling and history are added...
    ui.buttonSourceFile->setVisible(false);
    ui.buttonSourceFolder->setVisible(false);
    ui.buttonDefaultSource->setVisible(false);

    ui.buttonDest->setVisible(false);
    ui.buttonDefaultDest->setVisible(false);

    ui.textDest->setDisabled(true);
    ui.textSource->setDisabled(true);
    putJobOptions();
  } else {

    // set source and destination using defaults
    if (isDownload) {
      // download
      ui.textExtra->setText(
          settings->value("Settings/defaultDownloadOptions").toString());
      ui.textSource->setText(remote + ":" + path.path());
      QString folder;
      QString default_folder =
          (settings->value("Settings/defaultDownloadDir").toString());
      QString last_used_dest_folder =
          (settings->value("Settings/lastUsedDestFolder").toString());

      if (last_used_dest_folder.isEmpty()) {
        folder = default_folder;
      } else {
        folder = last_used_dest_folder;
      };

      if (!folder.isEmpty()) {
        if (isFolder) {
          ui.textDest->setText(
              QDir::toNativeSeparators(folder + "/" + path.dirName()));
        } else {
          ui.textDest->setText(QDir::toNativeSeparators(folder));
        }
      }

    } else {
      // upload
      ui.textExtra->setText(
          settings->value("Settings/defaultUploadOptions").toString());
      QString folder;
      QString default_folder =
          (settings->value("Settings/defaultUploadDir").toString());
      QString last_used_source_folder =
          (settings->value("Settings/lastUsedSourceFolder").toString());

      if (last_used_source_folder.isEmpty()) {
        folder = default_folder;
      } else {
        folder = last_used_source_folder;
      };

      // if upload initiated from drag and drop we dont use default upload
      // folder
      if (!isDrop) {
        ui.textSource->setText(QDir::toNativeSeparators(folder));
        if (!folder.isEmpty()) {
          ui.textDest->setText(remote + ":" +
                               path.filePath(QFileInfo(folder).fileName()));
        } else {
          ui.textDest->setText(remote + ":" + path.path());
        }
      } else {
        // when dropping to root folder
        if (path.path() == ".") {
          ui.textDest->setText(remote + ":");
        } else {
          ui.textDest->setText(remote + ":" + path.path());
        }
      };
    };
  }
}

TransferDialog::~TransferDialog() {
  if (result() == QDialog::Accepted) {
    auto settings = GetSettings();
    settings->beginGroup("Transfer");
    WriteSettings(settings.get(), this);
    settings->remove("textSource");
    settings->remove("textDest");
    settings->remove("textDescription");
    settings->endGroup();
  }
  // a JobOptions created for a plain run is owned by nobody once the
  // dialog closes; saved tasks live in (and are owned by) the task list
  if (mJobOptions != nullptr && !mIsEditMode &&
      !ListOfJobOptions::getInstance()->getTasks().contains(mJobOptions)) {
    delete mJobOptions;
  }
}

void TransferDialog::setSource(const QString &path) {
  ui.textSource->setText(QDir::toNativeSeparators(path));
}

QString TransferDialog::getMode() const {
  if (ui.rbCopy->isChecked()) {
    return "Copy";
  } else if (ui.rbMove->isChecked()) {
    return "Move";
  } else if (ui.rbSync->isChecked()) {
    return "Sync";
  } else if (mRbBisync->isChecked()) {
    return "Bisync";
  }

  return QString();
}

QString TransferDialog::getSource() const { return ui.textSource->text(); }

QString TransferDialog::getDest() const { return ui.textDest->text(); }

void TransferDialog::clearValidation() {
  UiPolish::SetValidationMessage(mValidation, QString(), QString());
  UiPolish::SetFieldState(ui.textSource, QString());
  UiPolish::SetFieldState(ui.textDest, QString());
  UiPolish::SetFieldState(ui.textDescription, QString());
}

void TransferDialog::showValidation(QWidget *field, const QString &message) {
  clearValidation();
  UiPolish::SetFieldState(field, "error");
  UiPolish::SetValidationMessage(mValidation, "error", message);
  if (field) {
    field->setFocus(Qt::OtherFocusReason);
  }
}

QStringList TransferDialog::getOptions() {
  JobOptions *jobo = getJobOptions();
  QStringList newWay = jobo->getOptions();
  return newWay;
}

/*
 * Apply the displayed/edited values on the UI to the
 * JobOptions object.
 *
 * This needs to be edited whenever options are added or changed.
 */
JobOptions *TransferDialog::getJobOptions() {
  if (mJobOptions == nullptr)
    mJobOptions = new JobOptions(mIsDownload);

  if (ui.rbCopy->isChecked()) {
    mJobOptions->operation = JobOptions::Copy;
    mJobOptions->sync = false;
  } else if (ui.rbMove->isChecked()) {
    mJobOptions->operation = JobOptions::Move;
    mJobOptions->sync = false;
  } else if (ui.rbSync->isChecked()) {
    mJobOptions->operation = JobOptions::Sync;
  } else if (mRbBisync->isChecked()) {
    mJobOptions->operation = JobOptions::Bisync;
    mJobOptions->sync = false;
  }

  mJobOptions->dryRun = mDryRun;

  if (ui.rbSync->isChecked()) {
    mJobOptions->sync = true;
    switch (ui.cbSyncDelete->currentIndex()) {
    case 0:
      mJobOptions->syncTiming = JobOptions::During;
      break;
    case 1:
      mJobOptions->syncTiming = JobOptions::After;
      break;
    case 2:
      mJobOptions->syncTiming = JobOptions::Before;
      break;
    }
  }

  mJobOptions->skipNewer = ui.checkSkipNewer->isChecked();
  mJobOptions->skipExisting = ui.checkSkipExisting->isChecked();

  if (ui.checkCompare->isChecked()) {
    mJobOptions->compare = true;
    switch (ui.cbCompare->currentIndex()) {
    case 0:
      mJobOptions->compareOption = JobOptions::SizeAndModTime;
      break;
    case 1:
      mJobOptions->compareOption = JobOptions::Checksum;
      break;
    case 2:
      mJobOptions->compareOption = JobOptions::IgnoreSize;
      break;
    case 3:
      mJobOptions->compareOption = JobOptions::SizeOnly;
      break;
    case 4:
      mJobOptions->compareOption = JobOptions::ChecksumIgnoreSize;
      break;
    }
  } else {
    mJobOptions->compare = false;
  };

  //    mJobOptions->verbose = ui.checkVerbose->isChecked();
  mJobOptions->sameFilesystem = ui.checkSameFilesystem->isChecked();
  mJobOptions->dontUpdateModified = ui.checkDontUpdateModified->isChecked();

  mJobOptions->transfers = ui.spinTransfers->text();
  mJobOptions->checkers = ui.spinCheckers->text();
  mJobOptions->bandwidth = ui.textBandwidth->text();
  mJobOptions->minSize = ui.textMinSize->text();
  mJobOptions->minAge = ui.textMinAge->text();
  mJobOptions->maxAge = ui.textMaxAge->text();
  mJobOptions->maxDepth = ui.spinMaxDepth->value();

  mJobOptions->connectTimeout = ui.spinConnectTimeout->text();
  mJobOptions->idleTimeout = ui.spinIdleTimeout->text();
  mJobOptions->retries = ui.spinRetries->text();
  mJobOptions->lowLevelRetries = ui.spinLowLevelRetries->text();
  mJobOptions->deleteExcluded = ui.checkDeleteExcluded->isChecked();

  mJobOptions->excluded = ui.textExclude->toPlainText().trimmed();
  mJobOptions->extra = ui.textExtra->text().trimmed();

  mJobOptions->source = ui.textSource->text();
  mJobOptions->dest = ui.textDest->text();
  mJobOptions->isFolder = mIsFolder;

  mJobOptions->description = ui.textDescription->text();
  //   auto settings = GetSettings();
  //   mJobOptions->DriveSharedWithMe = settings->value("Settings/driveShared",
  //   false).toBool();

  mJobOptions->DriveSharedWithMe = ui.checkisDriveSharedWithMe->isChecked();
  mJobOptions->heartbeatUrl = mHeartbeatUrl->text().trimmed();
  mJobOptions->nameTransform = mNameTransform->text().trimmed();
  mJobOptions->preCommand = mPreCommand->text().trimmed();
  mJobOptions->postCommand = mPostCommand->text().trimmed();
  mJobOptions->webhookUrl = mWebhookUrl->text().trimmed();
  mJobOptions->watchFolder =
      mWatchFolder->isChecked() && mJobOptions->jobType == JobOptions::Upload;
  mJobOptions->backupDir = mBackupDir->text().trimmed();
  mJobOptions->backupRetainCount = mBackupRetain->value();
  mJobOptions->conflictResolve =
      mConflictResolve->currentData().toString();

  return mJobOptions;
}

/*
 * Apply the JobOptions object to the displayed widget values.
 *
 * It could be "better" to use a two-way binding mechanism, but
 * if used that should be global to the app; and anyway doing
 * it this old primitive way makes it easier when the user wants
 * to not save changes...
 */
void TransferDialog::putJobOptions() {
  ui.rbCopy->setChecked(mJobOptions->operation == JobOptions::Copy);
  ui.rbMove->setChecked(mJobOptions->operation == JobOptions::Move);
  ui.rbSync->setChecked(mJobOptions->operation == JobOptions::Sync);
  mRbBisync->setChecked(mJobOptions->operation == JobOptions::Bisync);

  mDryRun = mJobOptions->dryRun;
  ui.rbSync->setChecked(mJobOptions->sync);

  ui.cbSyncDelete->setCurrentIndex((int)mJobOptions->syncTiming);
  // set combobox tooltips
  ui.cbSyncDelete->setItemData(0, "--delete-during", Qt::ToolTipRole);
  ui.cbSyncDelete->setItemData(1, "--delete-after", Qt::ToolTipRole);
  ui.cbSyncDelete->setItemData(2, "--delete-before", Qt::ToolTipRole);

  ui.checkSkipNewer->setChecked(mJobOptions->skipNewer);
  ui.checkSkipExisting->setChecked(mJobOptions->skipExisting);

  ui.checkCompare->setChecked(mJobOptions->compare);

  ui.cbCompare->setCurrentIndex(mJobOptions->compareOption);
  // set combobox tooltips
  ui.cbCompare->setItemData(0, "default", Qt::ToolTipRole);
  ui.cbCompare->setItemData(1, "--checksum", Qt::ToolTipRole);
  ui.cbCompare->setItemData(2, "--ignore-size", Qt::ToolTipRole);
  ui.cbCompare->setItemData(3, "--size-only", Qt::ToolTipRole);
  ui.cbCompare->setItemData(4, "--checksum --ignore-size", Qt::ToolTipRole);
  // ui.checkVerbose->setChecked(mJobOptions->verbose);
  ui.checkSameFilesystem->setChecked(mJobOptions->sameFilesystem);
  ui.checkDontUpdateModified->setChecked(mJobOptions->dontUpdateModified);

  ui.spinTransfers->setValue(mJobOptions->transfers.toInt());
  ui.spinCheckers->setValue(mJobOptions->checkers.toInt());
  ui.textBandwidth->setText(mJobOptions->bandwidth);
  ui.textMinSize->setText(mJobOptions->minSize);
  ui.textMinAge->setText(mJobOptions->minAge);
  ui.textMaxAge->setText(mJobOptions->maxAge);
  ui.spinMaxDepth->setValue(mJobOptions->maxDepth);

  ui.spinConnectTimeout->setValue(mJobOptions->connectTimeout.toInt());
  ui.spinIdleTimeout->setValue(mJobOptions->idleTimeout.toInt());
  ui.spinRetries->setValue(mJobOptions->retries.toInt());
  ui.spinLowLevelRetries->setValue(mJobOptions->lowLevelRetries.toInt());
  ui.checkDeleteExcluded->setChecked(mJobOptions->deleteExcluded);

  ui.textExclude->setPlainText(mJobOptions->excluded);
  ui.textExtra->setText(mJobOptions->extra);

  ui.textSource->setText(mJobOptions->source);
  ui.textDest->setText(mJobOptions->dest);
  ui.textDescription->setText(mJobOptions->description);
  // DDBB
  ui.checkisDriveSharedWithMe->setChecked(mJobOptions->DriveSharedWithMe);
  mHeartbeatUrl->setText(mJobOptions->heartbeatUrl);
  mNameTransform->setText(mJobOptions->nameTransform);
  mPreCommand->setText(mJobOptions->preCommand);
  mPostCommand->setText(mJobOptions->postCommand);
  mWebhookUrl->setText(mJobOptions->webhookUrl);
  mWatchFolder->setChecked(mJobOptions->watchFolder);
  mBackupDir->setText(mJobOptions->backupDir);
  mBackupRetain->setValue(mJobOptions->backupRetainCount);
  int conflictIdx =
      mConflictResolve->findData(mJobOptions->conflictResolve);
  if (conflictIdx >= 0)
    mConflictResolve->setCurrentIndex(conflictIdx);
}

void TransferDialog::done(int r) {
  if (r == QDialog::Accepted) {
    if (mIsDownload) {
      if (ui.textDest->text().trimmed().isEmpty()) {
        showValidation(ui.textDest,
                       "Choose a local destination before running.");
        return;
      }
    } else {
      if (ui.textSource->text().trimmed().isEmpty()) {
        showValidation(ui.textSource,
                       "Choose a local file or folder before running.");
        return;
      }
    }
  }
  QDialog::done(r);
}
