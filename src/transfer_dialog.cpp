#include "transfer_dialog.h"
#include "bwlimit_timetable.h"
#include "sync_preview.h"
#include "remote_list_parser.h"
#include "filter_pattern.h"
#include "list_of_job_options.h"
#include "job_options_store.h"
#include "interface_polish.h"
#include "rclone_capabilities.h"
#include "remote_daemon.h"
#include "utils.h"
#include "vss_snapshot.h"

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
  auto *securityNotice = new QLabel(this);
  UiPolish::SetNotice(securityNotice, JobOptionsStoreSecurityNotice());
  securityNotice->setAccessibleName("Saved task security notice");
  ui.gridLayout->addWidget(securityNotice, 2, 0, 1, 2);
  ui.textSource->setPlaceholderText(isDownload ? "remote:path" : "Local file or folder");
  ui.textDest->setPlaceholderText(isDownload ? "Local destination folder"
                                             : "remote:path");
  ui.textExtra->setPlaceholderText("Additional rclone flags for this transfer");
  ui.textDescription->setPlaceholderText("Name this task if you want to save it");
  ui.textBandwidth->setPlaceholderText("off, 10M, or timetable syntax");
  auto *bwEditBtn = ui.buttonBandwidthEdit;
  bwEditBtn->setIcon(
      qApp->style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  bwEditBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
  bwEditBtn->setToolTip("Open the bandwidth timetable editor.");
  bwEditBtn->setAccessibleName("Edit bandwidth timetable");
  QObject::connect(bwEditBtn, &QToolButton::clicked, this, [this]() {
    QDialog dlg(this);
    dlg.setWindowTitle("Bandwidth Timetable");
    dlg.resize(500, 350);
    UiPolish::SetWindowDefaults(&dlg, QSize(500, 350));
    auto *layout = new QVBoxLayout(&dlg);
    layout->setSpacing(8);
    auto *hint = new QLabel(
        "Each row sets the limit from that time on: HH:MM, or a day and a "
        "time like Mon-08:00. The rate is a size like 512k or 10M, "
        "upload:download like 10M:1M, or off for no limit.", &dlg);
    hint->setWordWrap(true);
    UiPolish::SetMuted(hint);
    layout->addWidget(hint);

    auto *table = new QTableWidget(0, 2, &dlg);
    table->setHorizontalHeaderLabels({"Time (HH:MM)", "Bandwidth"});
    UiPolish::SetTableView(table, "Bandwidth timetable entries");
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);

    const auto addRow = [table](const QString &time, const QString &rate) {
      const int row = table->rowCount();
      table->insertRow(row);
      table->setItem(row, 0, new QTableWidgetItem(time));
      table->setItem(row, 1, new QTableWidgetItem(rate));
    };
    const QString current = ui.textBandwidth->text().trimmed();
    if (BwlimitTimetable::IsSingleRate(current)) {
      // A plain rate holds all day, the same as one row at 00:00. No limit
      // gets no rows.
      if (!current.isEmpty()) {
        addRow(QStringLiteral("00:00"), current);
      }
    } else {
      // An entry rclone would refuse still gets a row, marked, so it can be
      // fixed here instead of disappearing.
      for (const QString &entry : BwlimitTimetable::Entries(current)) {
        const qsizetype comma = entry.indexOf(QLatin1Char(','));
        addRow(comma < 0 ? entry : entry.left(comma),
               comma < 0 ? QString() : entry.mid(comma + 1));
      }
    }

    auto *btnRow = new QHBoxLayout();
    auto *addBtn = new QPushButton("Add Row", &dlg);
    auto *removeBtn = new QPushButton("Remove Row", &dlg);
    UiPolish::SetDestructiveButton(removeBtn);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    auto *preview = new QLabel(&dlg);
    preview->setObjectName("bandwidthPreview");
    preview->setWordWrap(true);
    UiPolish::SetMuted(preview);
    layout->addWidget(preview);
    auto *problem = new QLabel(&dlg);
    problem->setObjectName("bandwidthProblem");
    problem->setWordWrap(true);
    UiPolish::SetValidationMessage(problem, QString(), QString());
    layout->addWidget(problem);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    UiPolish::SetDialogButtonBox(buttons);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    if (ok) {
      UiPolish::SetPrimaryButton(ok);
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg,
                     &QDialog::reject);
    layout->addWidget(buttons);

    // rclone 1.76.0 refuses a whole timetable over one bad entry, and only
    // when the job starts. Each cell it would refuse is marked here, OK
    // stays off until they're fixed, and the value is built from the rows
    // that pass, so neither the preview nor the field gets a bad one.
    const QColor bad = UiPolish::BestContrastingColor(
        table->palette().color(QPalette::Base),
        {QColor("#cc0000"), QColor("#ff5f5f"), QColor("#7a0000")});
    const auto validate = [table, preview, problem, ok, bad]() {
      const QSignalBlocker blocker(table);
      QStringList parts;
      QString first;
      int badRows = 0;
      for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem *cells[2] = {table->item(row, 0),
                                      table->item(row, 1)};
        QString text[2];
        for (int col = 0; col < 2; ++col) {
          if (!cells[col]) {
            cells[col] = new QTableWidgetItem();
            table->setItem(row, col, cells[col]);
          }
          text[col] = cells[col]->text().trimmed();
        }
        // A row left empty is skipped, not refused.
        QString problems[2];
        if (!text[0].isEmpty() || !text[1].isEmpty()) {
          problems[0] = BwlimitTimetable::TimeProblem(text[0]);
          problems[1] = BwlimitTimetable::RateProblem(text[1]);
        }
        for (int col = 0; col < 2; ++col) {
          cells[col]->setData(Qt::ForegroundRole,
                              problems[col].isEmpty() ? QVariant()
                                                      : QVariant(QBrush(bad)));
          cells[col]->setToolTip(problems[col]);
          cells[col]->setData(Qt::AccessibleDescriptionRole, problems[col]);
        }
        if (!problems[0].isEmpty() || !problems[1].isEmpty()) {
          if (++badRows == 1) {
            first = QStringLiteral("Row %1: %2")
                        .arg(row + 1)
                        .arg(problems[0].isEmpty() ? problems[1]
                                                   : problems[0]);
          }
        } else if (!text[0].isEmpty()) {
          parts << text[0] + QLatin1Char(',') + text[1];
        }
      }
      // One row at 00:00 is the same limit as a plain rate.
      const QString value =
          parts.size() == 1 && parts.first().startsWith(QLatin1String("00:00,"))
              ? parts.first().mid(6)
              : parts.join(QLatin1Char(' '));
      preview->setText(value.isEmpty()
                           ? QStringLiteral("Preview: no limit")
                           : QStringLiteral("Preview: --bwlimit \"%1\"")
                                 .arg(value));
      UiPolish::SetValidationMessage(
          problem, badRows > 0 ? QStringLiteral("error") : QString(),
          badRows > 1 ? QStringLiteral("%1 %2 rows need fixing.")
                            .arg(first)
                            .arg(badRows)
                      : first);
      if (ok) {
        ok->setEnabled(badRows == 0);
      }
      return value;
    };
    QObject::connect(table, &QTableWidget::cellChanged, &dlg,
                     [validate](int, int) { validate(); });
    QObject::connect(addBtn, &QPushButton::clicked, &dlg,
                     [table, addRow, validate]() {
                       addRow(QStringLiteral("00:00"), QStringLiteral("off"));
                       table->setCurrentCell(table->rowCount() - 1, 0);
                       validate();
                     });
    QObject::connect(removeBtn, &QPushButton::clicked, &dlg,
                     [table, validate]() {
                       const int row = table->currentRow();
                       if (row >= 0) {
                         table->removeRow(row);
                       }
                       validate();
                     });
    validate();

    if (dlg.exec() == QDialog::Accepted) {
      ui.textBandwidth->setText(validate());
    }
  });

  // Only worth offering when both ends live on the same provider:
  // --server-side-across-configs is what lets it move the bytes
  // itself instead of streaming them through this machine.
  ui.checkServerSide->setVisible(false);
  ui.checkServerSide->setAccessibleName(
      "Copy on the server where possible");
  QObject::connect(ui.textSource, &QLineEdit::textChanged, this,
                   &TransferDialog::refreshServerSideOption);
  QObject::connect(ui.textDest, &QLineEdit::textChanged, this,
                   &TransferDialog::refreshServerSideOption);

  auto *presetCombo = ui.cbPreset;
  presetCombo->addItem("Custom");
  presetCombo->addItem("Default (4 transfers, 8 checkers)");
  presetCombo->addItem("Large files (2 transfers, 4 checkers, 256M buffer)");
  presetCombo->addItem("Many small files (16 transfers, 32 checkers)");
  presetCombo->addItem("Low bandwidth (2 transfers, 4 checkers, 1M limit)");
  presetCombo->setToolTip("Apply a performance preset to auto-fill transfers, "
                          "checkers, and bandwidth settings.");
  presetCombo->setAccessibleName("Performance preset");
  QObject::connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   this, [this, presetCombo](int idx) {
    if (idx == 0) return;
    switch (idx) {
    case 1: // Default
      ui.spinTransfers->setValue(4);
      ui.spinCheckers->setValue(8);
      ui.textBandwidth->clear();
      break;
    case 2: // Large files
      ui.spinTransfers->setValue(2);
      ui.spinCheckers->setValue(4);
      ui.textBandwidth->clear();
      ui.textExtra->setText(
          (ui.textExtra->text().trimmed().isEmpty() ? "" :
           ui.textExtra->text().trimmed() + " ") + "--buffer-size 256M");
      break;
    case 3: // Many small files
      ui.spinTransfers->setValue(16);
      ui.spinCheckers->setValue(32);
      ui.textBandwidth->clear();
      break;
    case 4: // Low bandwidth
      ui.spinTransfers->setValue(2);
      ui.spinCheckers->setValue(4);
      ui.textBandwidth->setText("1M");
      break;
    default:
      break;
    }
    presetCombo->setCurrentIndex(0);
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
  mExcludeExplanation = new QLabel(this);
  mExcludeExplanation->setObjectName("excludeExplanation");
  mExcludeExplanation->setWordWrap(true);
  mExcludeExplanation->setTextFormat(Qt::PlainText);
  mExcludeExplanation->setAccessibleName("Exclude pattern explanation");
  UiPolish::SetMuted(mExcludeExplanation);
  if (auto *parentLayout =
          qobject_cast<QVBoxLayout *>(ui.textExclude->parentWidget()->layout())) {
    int idx = parentLayout->indexOf(ui.textExclude);
    if (idx >= 0) {
      parentLayout->insertWidget(idx + 1, excludeQuickBar);
      parentLayout->insertWidget(idx + 2, mExcludeExplanation);
    }
  }
  QObject::connect(ui.textExclude, &QPlainTextEdit::textChanged, this,
                   &TransferDialog::refreshExcludeExplanation);
  QObject::connect(ui.checkDeleteExcluded, &QCheckBox::toggled, this,
                   &TransferDialog::refreshExcludeExplanation);
  refreshExcludeExplanation();

  auto *heartbeatLabel = new QLabel("Heartbeat URL:", this);
  heartbeatLabel->setToolTip(
      "Healthchecks.io, ntfy.sh, or any URL to ping on job completion.\n"
      "Use http:// or https:// only. On success the app sends GET to this "
      "URL; on failure it appends /fail.");
  mHeartbeatUrl = new QLineEdit(this);
  mHeartbeatUrl->setPlaceholderText("https://hc-ping.com/your-uuid or https://ntfy.sh/your-topic");
  mHeartbeatUrl->setAccessibleName("Heartbeat URL for monitoring");
  ui.gridLayout->addWidget(heartbeatLabel, 8, 0);
  ui.gridLayout->addWidget(mHeartbeatUrl, 8, 1);
  auto *webhookLabel = new QLabel("Webhook URL:", this);
  webhookLabel->setToolTip(
      "Discord, Gotify, Shoutrrr, or any URL to POST a JSON status on "
      "job completion.\nUse http:// or https:// only. Payload includes task "
      "name, status, duration, bytes transferred, and error message (if any).");
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

  mRbBisync = ui.rbBisync;
  mRbBisync->setToolTip("Bidirectional sync between source and destination.");
  mRbBisync->setAccessibleName("Bisync operation");
  {
    auto caps = RcloneCapabilities::detect();
    if (!caps.hasBisync()) {
      mRbBisync->setDisabled(true);
      mRbBisync->setToolTip("Requires rclone >= 1.58 with bisync support.");
    }
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
      "Maximum number of {date} backup-dir snapshots to keep after a "
      "successful transfer. 0 = keep all.");
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

  mVerifyAfter = new QCheckBox("Verify integrity after transfer", this);
  mVerifyAfter->setToolTip(
      "Run rclone check (or cryptcheck for crypt remotes) after a "
      "successful transfer to verify file integrity.");
  mVerifyAfter->setAccessibleName("Verify integrity after transfer");
  mVssSnapshot = new QCheckBox("Read locked files from a snapshot", this);
  mVssSnapshot->setAccessibleName("Read locked files from a snapshot");
#if !defined(Q_OS_WIN32)
  mVssSnapshot->hide();
#endif
  auto *afterRow = new QHBoxLayout();
  afterRow->setContentsMargins(0, 0, 0, 0);
  afterRow->addWidget(mVerifyAfter);
  afterRow->addWidget(mVssSnapshot);
  afterRow->addStretch();
  ui.gridLayout->addLayout(afterRow, 18, 1);
  QObject::connect(ui.textSource, &QLineEdit::textChanged, this,
                   &TransferDialog::updateSnapshotOption);
  for (QRadioButton *mode : {ui.rbCopy, ui.rbMove, ui.rbSync, ui.rbBisync}) {
    QObject::connect(mode, &QRadioButton::toggled, this,
                     &TransferDialog::updateSnapshotOption);
  }
  updateSnapshotOption();

  // The delete cap sits under the delete timing, the other Sync-only row.
  mDeleteCap = new QCheckBox("Delete at most", this);
  mDeleteCap->setAccessibleName("Cap how many files Sync deletes");
  mDeleteCapCount = new QSpinBox(this);
  mDeleteCapCount->setRange(0, 9999999);
  mDeleteCapCount->setAccessibleName("Most files Sync may delete");
  auto *capUnit = new QLabel("files, then stop", this);
  capUnit->setBuddy(mDeleteCapCount);
  auto *capRow = new QHBoxLayout();
  capRow->setContentsMargins(0, 0, 0, 0);
  capRow->addWidget(mDeleteCap);
  capRow->addWidget(mDeleteCapCount);
  capRow->addWidget(capUnit);
  capRow->addStretch();
  ui.gridLayout_5->removeWidget(ui.cbSyncDelete);
  auto *syncColumn = new QVBoxLayout();
  syncColumn->setContentsMargins(0, 0, 0, 0);
  syncColumn->addWidget(ui.cbSyncDelete);
  syncColumn->addLayout(capRow);
  ui.gridLayout_5->addLayout(syncColumn, 2, 1);
  QObject::connect(ui.rbSync, &QRadioButton::toggled, this,
                   &TransferDialog::updateDeleteCapOption);
  QObject::connect(mDeleteCap, &QCheckBox::toggled, this,
                   &TransferDialog::updateDeleteCapOption);
  updateDeleteCapOption();

  auto *profileLabel = new QLabel("Profile:", this);
  profileLabel->setToolTip(
      "Load saved rclone option defaults for this remote.");
  auto *profileCombo = new QComboBox(this);
  profileCombo->setAccessibleName("Operation profile");
  profileCombo->setToolTip(
      "Select a saved profile to auto-fill rclone flags for this remote.");
  profileCombo->addItem("(none)");
  {
    auto settings = GetSettings();
    settings->beginGroup("RemoteProfiles");
    for (const QString &key : settings->childKeys()) {
      profileCombo->addItem(key);
    }
    settings->endGroup();
  }
  auto *profileSaveBtn = new QPushButton("Save", this);
  profileSaveBtn->setToolTip(
      "Save the current Extra Options as a named profile.");
  profileSaveBtn->setAccessibleName("Save operation profile");
  auto *profileDeleteBtn = new QPushButton("Delete", this);
  profileDeleteBtn->setToolTip("Delete the selected profile.");
  profileDeleteBtn->setAccessibleName("Delete operation profile");
  profileDeleteBtn->setEnabled(false);

  auto *profileRow = new QHBoxLayout();
  profileRow->addWidget(profileLabel);
  profileRow->addWidget(profileCombo, 1);
  profileRow->addWidget(profileSaveBtn);
  profileRow->addWidget(profileDeleteBtn);
  ui.gridLayout->addLayout(profileRow, 19, 0, 1, 2);

  QObject::connect(profileCombo, &QComboBox::currentTextChanged, this,
                   [this, profileCombo, profileDeleteBtn](const QString &name) {
                     profileDeleteBtn->setEnabled(
                         profileCombo->currentIndex() > 0);
                     if (profileCombo->currentIndex() <= 0)
                       return;
                     auto settings = GetSettings();
                     QString extra = settings
                                         ->value("RemoteProfiles/" + name)
                                         .toString();
                     if (!extra.isEmpty()) {
                       ui.textExtra->setText(extra);
                     }
                   });

  QObject::connect(profileSaveBtn, &QPushButton::clicked, this,
                   [this, profileCombo]() {
                     bool ok = false;
                     QString name = QInputDialog::getText(
                         this, "Save Profile", "Profile name:",
                         QLineEdit::Normal, profileCombo->currentText(), &ok);
                     if (!ok || name.trimmed().isEmpty())
                       return;
                     name = name.trimmed();
                     auto settings = GetSettings();
                     settings->setValue("RemoteProfiles/" + name,
                                        ui.textExtra->text().trimmed());
                     if (profileCombo->findText(name) < 0) {
                       profileCombo->addItem(name);
                     }
                     profileCombo->setCurrentText(name);
                   });

  QObject::connect(profileDeleteBtn, &QPushButton::clicked, this,
                   [profileCombo]() {
                     QString name = profileCombo->currentText();
                     if (name.isEmpty() || profileCombo->currentIndex() <= 0)
                       return;
                     auto settings = GetSettings();
                     settings->remove("RemoteProfiles/" + name);
                     profileCombo->removeItem(profileCombo->currentIndex());
                   });

  mValidation = new QLabel(this);
  UiPolish::SetValidationMessage(mValidation, QString(), QString());
  ui.gridLayout->addWidget(mValidation, 20, 0, 1, 2);
  QObject::connect(ui.textSource, &QLineEdit::textChanged, this,
                   &TransferDialog::clearValidation);
  QObject::connect(ui.textDest, &QLineEdit::textChanged, this,
                   &TransferDialog::clearValidation);
  QObject::connect(ui.textDescription, &QLineEdit::textChanged, this,
                   &TransferDialog::clearValidation);
  QObject::connect(ui.textBandwidth, &QLineEdit::textChanged, this,
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

  UiPolish::SetDialogButtonBox(ui.buttonBox);

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
        // On for new syncs: a scheduled sync whose source comes back empty
        // would otherwise delete the whole destination with nobody there.
        mDeleteCap->setChecked(true);
        mDeleteCapCount->setValue(100);
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
    if (!checkBandwidth(true)) {
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
      if (!checkBandwidth(true)) {
        return;
      }
      mPreview->clear();
      mPreview->setVisible(true);
      mPreview->setPlainText("Running dry-run preview...");
      mPreviewButton->setEnabled(false);

      const QStringList command = getJobOptions()->getPreviewOptions();
      QStringList args = GetRcloneConf() + command;

      auto showOutput = [=](int code, const QByteArray &raw) {
            mPreviewButton->setEnabled(true);
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
          };

      if (RemoteDaemon::IsEnabled()) {
        RemoteDaemon::RunCommand(command, this, showOutput);
        return;
      }

      auto *proc = new QProcess(this);
      proc->setProcessChannelMode(QProcess::MergedChannels);
      UseRclonePassword(proc);
      proc->start(GetRclone(), args, QIODevice::ReadOnly);

      QObject::connect(
          proc,
          static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
              &QProcess::finished),
          this, [proc, showOutput](int code, QProcess::ExitStatus) {
            proc->deleteLater();
            showOutput(code, proc->readAll());
          });
    });
  }

  QObject::connect(ui.buttonBox, &QDialogButtonBox::accepted, this,
                   &TransferDialog::acceptWithDeletionPreview);
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
  // The performance preset is an action, not a remembered setting: restoring a
  // stored index would fire the handler and overwrite the transfers, checkers
  // and bandwidth values ReadSettings is putting back.
  const bool presetBlocked = ui.cbPreset->blockSignals(true);
  ReadSettings(settings.get(), this);
  ui.cbPreset->setCurrentIndex(0);
  ui.cbPreset->blockSignals(presetBlocked);
  settings->endGroup();

  ui.buttonSourceFile->setVisible(!isDownload);
  ui.buttonSourceFolder->setVisible(!isDownload);
  ui.buttonDefaultSource->setVisible(!isDownload);

  ui.buttonDest->setVisible(isDownload);
  ui.buttonDefaultDest->setVisible(isDownload);

  if (RemoteDaemon::IsEnabled()) {
    // The local side of the transfer is the remote rclone's computer. The
    // folder pickers, the check afterwards and the folder watcher only see
    // this one.
    const QString typePath = QStringLiteral(
        "Type a path on the remote rclone's computer. The folder pickers "
        "only see this one.");
    for (QToolButton *button :
         {ui.buttonSourceFile, ui.buttonSourceFolder, ui.buttonDefaultSource,
          ui.buttonDest, ui.buttonDefaultDest}) {
      button->setEnabled(false);
      button->setToolTip(typePath);
    }
    for (QCheckBox *option : {mVerifyAfter, mWatchFolder}) {
      option->setEnabled(false);
      option->setToolTip(RemoteDaemon::LocalOnlyReason());
    }
  }

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
    // A task saved before rclone 1.76.0 can hold a time it now refuses.
    checkBandwidth(false);
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

bool TransferDialog::wouldDeleteAtDestination() const {
  // rclone sync removes destination files the source no longer has, and
  // --delete-excluded removes whatever a filter skipped. Everything else
  // only adds or overwrites.
  return ui.rbSync->isChecked() || ui.checkDeleteExcluded->isChecked();
}

void TransferDialog::acceptWithDeletionPreview() {
  // The deletion preview runs rclone with this value too.
  if (!checkBandwidth(true)) {
    return;
  }
  // A dry run changes nothing, an edit is not a run, and a queued transfer
  // gets reviewed in the staging list before it goes anywhere.
  if (mDryRun || mIsEditMode || mEnqueued || mPreviewInFlight ||
      !wouldDeleteAtDestination()) {
    accept();
    return;
  }

  // The preview lists every delete, cap or not, and the summary says when
  // the cap will stop the real run.
  // The summary reads the caps and flags from the options the check runs
  // with. Its Run button runs what the dialog holds then, so nothing that
  // changes them stays live while the check runs.
  const QStringList planned = getJobOptions()->getOptions();
  QStringList command = getJobOptions()->getPreviewOptions();
  if (!command.contains("--use-json-log")) {
    command << "--use-json-log";
  }
  const QStringList args = GetRcloneConf() + command;

  mPreviewInFlight = true;
  ui.buttonBox->setEnabled(false);
  auto locked = std::make_shared<QList<QPointer<QWidget>>>();
  for (QWidget *input : findChildren<QWidget *>()) {
    if (input->isEnabled() &&
        (qobject_cast<QAbstractButton *>(input) != nullptr ||
         qobject_cast<QLineEdit *>(input) != nullptr ||
         qobject_cast<QComboBox *>(input) != nullptr ||
         qobject_cast<QAbstractSpinBox *>(input) != nullptr ||
         qobject_cast<QPlainTextEdit *>(input) != nullptr ||
         qobject_cast<QTextEdit *>(input) != nullptr)) {
      input->setEnabled(false);
      locked->append(input);
    }
  }
  const auto unlock = [this, locked]() {
    mPreviewInFlight = false;
    ui.buttonBox->setEnabled(true);
    for (const QPointer<QWidget> &input : std::as_const(*locked)) {
      if (input) {
        input->setEnabled(true);
      }
    }
    locked->clear();
  };

  const bool remoteDaemon = RemoteDaemon::IsEnabled();
  const SyncPreview::Command previewed =
      ui.rbMove->isChecked()     ? SyncPreview::Command::Move
      : ui.rbCopy->isChecked()   ? SyncPreview::Command::Copy
      : ui.rbBisync->isChecked() ? SyncPreview::Command::Bisync
                                 : SyncPreview::Command::Sync;
  auto finish = [this, remoteDaemon, previewed, planned,
                 unlock](int code, const QByteArray &raw) {
    unlock();

    const SyncPreview::Summary summary = SyncPreview::Parse(
        raw, 20, previewed, SyncPreview::FlagsIn(planned));
    if (code != 0 && !summary.deletesAnything()) {
      QString reason = summary.error;
      if (reason.isEmpty() && remoteDaemon) {
        // A remote rclone that couldn't be reached answers in plain text.
        reason = QString::fromUtf8(raw).trimmed().section(QChar('\n'), -1);
      }
      showValidation(ui.textSource,
                     reason.isEmpty()
                         ? QString("rclone could not check what would be "
                                   "deleted (exit %1).").arg(code)
                         : reason);
      return;
    }

    // The caps the run will use, from the cap field, the extra options or
    // the default options. The count is files: rclone logs a directory it
    // would remove as "remove directory", which the summary doesn't count.
    const JobOptions::DeleteCaps caps = JobOptions::DeleteCapsIn(planned);
    QStringList warnings = SyncPreview::CapWarnings(
        summary, caps.count, caps.bytes, [](qint64 bytes) {
          return GetNiceSize(static_cast<quint64>(bytes));
        });
    // A check that ended on an error may not have reached every file. A
    // bisync's dry run stops at its delete caps the way the real run does.
    if (code != 0 && !summary.error.isEmpty()) {
      warnings.prepend(QString("The check ended early with an error, so it "
                               "may not list everything. rclone said \"%1\".")
                           .arg(summary.error));
    }

    // Nothing is removed and no cap can stop the run, so there is nothing
    // to warn about. A run that only replaces, renames or moves files can
    // still trip a cap with those moves.
    if (!summary.deletesAnything() && warnings.isEmpty()) {
      accept();
      return;
    }

    if (showDeletionSummary(summary, warnings)) {
      accept();
    }
  };

  if (remoteDaemon) {
    // The paths are on the remote rclone's machine, so it runs the dry run.
    RemoteDaemon::RunCommand(command, this, finish);
    return;
  }

  auto *proc = new QProcess(this);
  proc->setProcessChannelMode(QProcess::MergedChannels);
  UseRclonePassword(proc);

  QObject::connect(proc, &QProcess::errorOccurred, this,
                   [this, proc, unlock](QProcess::ProcessError error) {
                     if (error != QProcess::FailedToStart) {
                       return;
                     }
                     proc->deleteLater();
                     unlock();
                     // Do not run a deleting sync we could not preview.
                     showValidation(ui.textSource,
                                    "Could not start rclone to check what "
                                    "would be deleted.");
                   });

  QObject::connect(
      proc,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [proc, finish](int code, QProcess::ExitStatus) {
        const QByteArray raw = proc->readAll();
        proc->deleteLater();
        finish(code, raw);
      });

  proc->start(GetRclone(), args, QIODevice::ReadOnly);
}

bool TransferDialog::showDeletionSummary(const SyncPreview::Summary &summary,
                                         const QStringList &warnings) {
  // --backup-dir or --suffix moves removed files aside instead. Usually
  // that's all of them, but a sync can do both. With neither, the summary
  // is only here for a cap warning about moves the run makes.
  const bool moves = summary.toBackup > 0;
  const bool deletes = summary.toDelete > summary.toBackup;
  const bool removes = summary.deletesAnything();
  QDialog dialog(this);
  dialog.setWindowTitle(SyncPreview::DeletionTitle(summary));
  UiPolish::SetWindowDefaults(&dialog, QSize(560, 380));
  auto *layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(8);

  auto *headline = new QLabel(SyncPreview::Headline(summary), &dialog);
  headline->setTextFormat(Qt::PlainText);
  headline->setWordWrap(true);
  layout->addWidget(headline);

  auto *detail = new QLabel(&dialog);
  detail->setTextFormat(Qt::PlainText);
  detail->setWordWrap(true);
  QString detailText;
  if (!removes) {
    detailText = SyncPreview::NoDeleteDetail(summary);
  } else if (!moves) {
    detailText = "These files exist at the destination and not at the "
                 "source, so Sync removes them. Recovering them depends on "
                 "whether the remote keeps a trash or versions. Freed: %1.";
  } else if (!deletes) {
    detailText = "These files exist at the destination and not at the "
                 "source, so Sync takes them out of it and keeps them in the "
                 "backup (--backup-dir or --suffix). rclone still counts "
                 "each one as a delete for the delete cap. Size: %1.";
  } else {
    detailText = "These files exist at the destination and not at the "
                 "source. Sync keeps some of them in the backup "
                 "(--backup-dir or --suffix) and removes the rest, and "
                 "rclone counts every one as a delete for the delete cap. "
                 "Size: %1.";
  }
  detail->setText(removes ? detailText.arg(GetNiceSize(
                                static_cast<quint64>(summary.deleteBytes)))
                          : detailText);
  UiPolish::SetMuted(detail);
  layout->addWidget(detail);

  if (removes) {
    auto *list = new QPlainTextEdit(&dialog);
    list->setReadOnly(true);
    list->setPlainText(summary.deletions.join('\n'));
    UiPolish::SetOutputView(
        list, !moves     ? "Files this sync will delete"
              : !deletes ? "Files this sync will move into the backup"
                         : "Files this sync will delete or move into the "
                           "backup");
    layout->addWidget(list, 1);
  }

  if (summary.moreDeletions > 0) {
    auto *more = new QLabel(
        QString("...and %1 more.").arg(summary.moreDeletions), &dialog);
    UiPolish::SetMuted(more);
    layout->addWidget(more);
  }

  for (const QString &warning : warnings) {
    auto *label = new QLabel(&dialog);
    label->setTextFormat(Qt::PlainText);
    UiPolish::SetValidationMessage(label, "warning", warning);
    layout->addWidget(label);
  }
  if (!removes) {
    layout->addStretch(1); // no list to take up the height
  }

  auto *buttons = new QDialogButtonBox(&dialog);
  QPushButton *cancel =
      buttons->addButton("Cancel", QDialogButtonBox::RejectRole);
  const QString command = SyncPreview::CommandName(summary.command);
  QPushButton *run =
      buttons->addButton("Run " + command, QDialogButtonBox::AcceptRole);
  UiPolish::SetDialogButtonBox(buttons);
  UiPolish::SetPrimaryButton(run);
  cancel->setToolTip("Go back to the transfer options without running.");
  const QString theRun = "Run the " + command.toLower();
  run->setToolTip(!removes   ? theRun + "."
                  : !moves   ? theRun + ", deleting the files listed above."
                  : !deletes ? theRun + ", moving the files listed above "
                                        "into the backup."
                             : theRun + ", deleting or moving the files "
                                        "listed above.");
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);

  return dialog.exec() == QDialog::Accepted;
}

namespace {
// Remote name to backend type, shared by every transfer dialog in the
// process. Populated once, and only from a probe that actually succeeded.
QHash<QString, QString> gRemoteTypes;
bool gRemoteTypesLoaded = false;
bool gRemoteTypesLoading = false;

// "remote:path" -> "remote". Empty for a local path, which can never be a
// server-side copy.
QString RemoteNameOf(const QString &path) {
  const QString trimmed = path.trimmed();
  const int colon = trimmed.indexOf(QChar(':'));
  if (colon <= 0) {
    return QString();
  }
#ifdef Q_OS_WIN
  // A Windows drive letter is not a remote. Elsewhere a one-character remote
  // name is perfectly legal, so this must not apply.
  if (colon == 1) {
    return QString();
  }
#endif
  return trimmed.left(colon);
}
} // namespace

void TransferDialog::refreshServerSideOption() {
  mServerSideOffered = false;
  const QString sourceRemote = RemoteNameOf(ui.textSource->text());
  const QString destRemote = RemoteNameOf(ui.textDest->text());
  if (sourceRemote.isEmpty() || destRemote.isEmpty() ||
      sourceRemote == destRemote) {
    // One end is local, or both are the same remote: rclone handles that
    // server-side already without the flag.
    ui.checkServerSide->setVisible(false);
    return;
  }

  if (!gRemoteTypesLoaded) {
    startRemoteTypesProbe();
    ui.checkServerSide->setVisible(false);
    return;
  }

  const QString sourceType = gRemoteTypes.value(sourceRemote);
  const QString destType = gRemoteTypes.value(destRemote);
  const bool sameProvider = !sourceType.isEmpty() && sourceType == destType;
  mServerSideOffered = sameProvider;
  ui.checkServerSide->setVisible(sameProvider);
  if (sameProvider) {
    ui.checkServerSide->setToolTip(
        QString("Both ends are %1 remotes, so the provider can copy between "
                "them directly instead of pulling every byte through this "
                "machine.")
            .arg(sourceType));
  }
}

void TransferDialog::startRemoteTypesProbe() {
  if (gRemoteTypesLoaded || gRemoteTypesLoading) {
    return;
  }
  gRemoteTypesLoading = true;

  // This used to be a blocking waitForFinished(5000) called straight from the
  // textChanged handler, so the first keystroke in a path field could freeze
  // the window for five seconds.
  auto *probe = new QProcess();
  UseRclonePassword(probe);
  const QPointer<TransferDialog> dialog(this);

  QObject::connect(
      probe, &QProcess::finished, probe,
      [probe, dialog](int exitCode, QProcess::ExitStatus exitStatus) {
        gRemoteTypesLoading = false;
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
          for (const RemoteListParser::Remote &remote :
               RemoteListParser::ParseJson(probe->readAllStandardOutput())) {
            gRemoteTypes.insert(remote.name, remote.type);
          }
          // Only a successful read is remembered. Marking it loaded before
          // the probe ran meant one slow or password-locked config hid the
          // option for the rest of the session, with nothing said.
          gRemoteTypesLoaded = true;
        }
        probe->deleteLater();
        if (dialog) {
          dialog->refreshServerSideOption();
        }
      });

  QObject::connect(probe, &QProcess::errorOccurred, probe,
                   [probe, dialog](QProcess::ProcessError processError) {
                     if (processError != QProcess::FailedToStart) {
                       return; // finished() still arrives for the rest
                     }
                     gRemoteTypesLoading = false;
                     probe->deleteLater();
                     if (dialog) {
                       dialog->refreshServerSideOption();
                     }
                   });

  QStringList args;
  args << "listremotes" << GetRcloneConf() << "--json"
       << "--ask-password=false";
  probe->start(GetRclone(), args, QIODevice::ReadOnly);
}

void TransferDialog::refreshExcludeExplanation() {
  if (!mExcludeExplanation) {
    return;
  }

  const QStringList lines =
      FilterPattern::DescribeAll(ui.textExclude->toPlainText());
  const bool deleting = ui.checkDeleteExcluded->isChecked();

  if (lines.isEmpty()) {
    mExcludeExplanation->setText(
        deleting ? QString("Delete excluded is on, but no patterns are set, "
                           "so nothing extra will be removed.")
                 : QString("Patterns match at any depth unless they start "
                           "with a slash."));
    UiPolish::SetMuted(mExcludeExplanation);
    return;
  }

  QString text;
  if (deleting) {
    // Not a warning about the exclude itself: about what it now deletes.
    text = QString("Delete excluded is on, so anything these patterns match "
                   "is REMOVED from the destination, not just skipped:\n");
  }
  text += lines.join(QChar('\n'));

  mExcludeExplanation->setText(text);
  if (deleting) {
    UiPolish::SetNotice(mExcludeExplanation, text);
  } else {
    UiPolish::SetMuted(mExcludeExplanation);
  }
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

void TransferDialog::updateSnapshotOption() {
  if (!mVssSnapshot) {
    return;
  }
  const QString about = QStringLiteral(
      "Windows makes a shadow copy of the source drive before the job "
      "starts, so files another program has open, like an Outlook data "
      "file or a database, still get copied. Windows asks for "
      "administrator approval each time the job starts, and the snapshot "
      "is deleted when the job ends.");
  QString reason =
      RemoteDaemon::IsEnabled()
          ? RemoteDaemon::LocalOnlyReason()
          : VssSnapshot::UnavailableReason(ui.textSource->text().trimmed());
  if (reason.isEmpty() && !ui.rbCopy->isChecked() &&
      !ui.rbSync->isChecked()) {
    reason = QStringLiteral(
        "Snapshots work with Copy and Sync. Move and Bisync change the "
        "source, and a snapshot can't be changed.");
  }
  mVssSnapshot->setEnabled(reason.isEmpty());
  mVssSnapshot->setToolTip(reason.isEmpty() ? about
                                            : reason + "\n\n" + about);
}

void TransferDialog::updateDeleteCapOption() {
  if (!mDeleteCap) {
    return;
  }
  const bool sync = ui.rbSync->isChecked();
  mDeleteCap->setEnabled(sync);
  mDeleteCapCount->setEnabled(sync && mDeleteCap->isChecked());
  const QString about = QStringLiteral(
      "--max-delete: rclone deletes up to this many files at the "
      "destination, then stops the sync with an error. It keeps a source "
      "that comes back empty, like an unplugged drive or an expired login, "
      "from wiping the destination while nobody is watching.");
  mDeleteCap->setToolTip(
      sync ? about
           : QStringLiteral("Only Sync deletes at the destination.\n\n") +
                 about);
  mDeleteCapCount->setToolTip(about);
}

int TransferDialog::deleteCapValue() const {
  if (!mDeleteCap || !ui.rbSync->isChecked() || !mDeleteCap->isChecked()) {
    return -1;
  }
  return mDeleteCapCount->value();
}

QString TransferDialog::getDest() const { return ui.textDest->text(); }

void TransferDialog::clearValidation() {
  UiPolish::SetValidationMessage(mValidation, QString(), QString());
  UiPolish::SetFieldState(ui.textSource, QString());
  UiPolish::SetFieldState(ui.textDest, QString());
  UiPolish::SetFieldState(ui.textDescription, QString());
  UiPolish::SetFieldState(ui.textBandwidth, QString());
}

bool TransferDialog::checkBandwidth(bool focus) {
  const QString problem =
      BwlimitTimetable::Problem(ui.textBandwidth->text().trimmed());
  if (problem.isEmpty()) {
    return true;
  }
  const QString message =
      QStringLiteral("rclone won't accept this bandwidth limit. %1")
          .arg(problem);
  if (focus) {
    ui.tabWidget->setCurrentWidget(ui.tab3);
    showValidation(ui.textBandwidth, message);
  } else {
    clearValidation();
    UiPolish::SetFieldState(ui.textBandwidth, "error");
    UiPolish::SetValidationMessage(mValidation, "error", message);
  }
  return false;
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
    default:
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
    default:
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
  mJobOptions->bandwidth = ui.textBandwidth->text().trimmed();
  mJobOptions->minSize = ui.textMinSize->text();
  mJobOptions->minAge = ui.textMinAge->text();
  mJobOptions->maxAge = ui.textMaxAge->text();
  mJobOptions->maxDepth = ui.spinMaxDepth->value();

  mJobOptions->connectTimeout = ui.spinConnectTimeout->text();
  mJobOptions->idleTimeout = ui.spinIdleTimeout->text();
  mJobOptions->retries = ui.spinRetries->text();
  mJobOptions->lowLevelRetries = ui.spinLowLevelRetries->text();
  mJobOptions->deleteExcluded = ui.checkDeleteExcluded->isChecked();
  // Not isVisible(): exec() hides the dialog before returning, and the
  // control lives on a tab that is usually not the current one, so both make
  // isVisible() false while the option is genuinely on offer. This tracks
  // whether it was offered, which is also what makes a tick stale when the
  // user edits a path afterwards.
  mJobOptions->serverSideAcrossConfigs =
      mServerSideOffered && ui.checkServerSide->isChecked();

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
  bool hasHooks = !mJobOptions->preCommand.isEmpty() ||
                  !mJobOptions->postCommand.isEmpty();
  if (hasHooks) {
    mJobOptions->hooksTrusted = true;
  }
  mJobOptions->watchFolder =
      mWatchFolder->isChecked() && mJobOptions->jobType == JobOptions::Upload;
  mJobOptions->backupDir = mBackupDir->text().trimmed();
  mJobOptions->backupRetainCount = mBackupRetain->value();
  mJobOptions->conflictResolve =
      mConflictResolve->currentData().toString();
  mJobOptions->verifyAfterTransfer = mVerifyAfter->isChecked();
  mJobOptions->vssSnapshot =
      mVssSnapshot->isChecked() && mVssSnapshot->isEnabled();
  mJobOptions->maxDelete = deleteCapValue();

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
  // A task with no operation saved falls back to Copy, since unchecking
  // the others leaves the mode remembered from the last transfer selected.
  ui.rbCopy->setChecked(mJobOptions->operation == JobOptions::Copy ||
                        mJobOptions->operation == JobOptions::UnknownOp);
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
  ui.checkServerSide->setChecked(mJobOptions->serverSideAcrossConfigs);
  refreshServerSideOption();

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
  mVerifyAfter->setChecked(mJobOptions->verifyAfterTransfer);
  mVssSnapshot->setChecked(mJobOptions->vssSnapshot);
  updateSnapshotOption();
  // A task saved without a cap opens without one. The count keeps its
  // default so ticking the box offers a sensible number.
  mDeleteCap->setChecked(mJobOptions->maxDelete >= 0);
  if (mJobOptions->maxDelete >= 0) {
    mDeleteCapCount->setValue(mJobOptions->maxDelete);
  }
  updateDeleteCapOption();
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
    if (!checkBandwidth(true)) {
      return;
    }
  }
  QDialog::done(r);
}
