#include "folder_compare.h"

#include "interface_polish.h"
#include "utils.h"

namespace {
constexpr int kEntryIndexRole = Qt::UserRole + 1;

FolderCompareStatus parseStatus(QChar marker, bool *ok) {
  if (ok) {
    *ok = true;
  }

  switch (marker.toLatin1()) {
  case '=':
    return FolderCompareStatus::Match;
  case '-':
    return FolderCompareStatus::MissingOnSource;
  case '+':
    return FolderCompareStatus::MissingOnDestination;
  case '*':
    return FolderCompareStatus::Different;
  case '!':
    return FolderCompareStatus::Error;
  default:
    if (ok) {
      *ok = false;
    }
    return FolderCompareStatus::Error;
  }
}

QString statusSymbol(FolderCompareStatus status) {
  switch (status) {
  case FolderCompareStatus::Match:
    return "=";
  case FolderCompareStatus::MissingOnSource:
    return "-";
  case FolderCompareStatus::MissingOnDestination:
    return "+";
  case FolderCompareStatus::Different:
    return "*";
  case FolderCompareStatus::Error:
    return "!";
  }
  return "!";
}

QString repairActionLabel(FolderCompareDialog::RepairAction action) {
  switch (action) {
  case FolderCompareDialog::RepairAction::CopyToDestination:
    return "Copy to destination";
  case FolderCompareDialog::RepairAction::CopyToSource:
    return "Copy to source";
  case FolderCompareDialog::RepairAction::DeleteFromDestination:
    return "Delete from destination";
  case FolderCompareDialog::RepairAction::DeleteFromSource:
    return "Delete from source";
  }
  return "Repair";
}
} // namespace

QVector<FolderCompareEntry> ParseRcloneCheckCombinedOutput(
    const QString &output) {
  QVector<FolderCompareEntry> entries;
  const QStringList lines = output.split('\n');
  for (QString line : lines) {
    if (line.endsWith('\r')) {
      line.chop(1);
    }
    if (line.size() < 3 || line.at(1) != ' ') {
      continue;
    }

    bool ok = false;
    const FolderCompareStatus status = parseStatus(line.at(0), &ok);
    if (!ok) {
      continue;
    }

    const QString path = line.mid(2);
    if (!path.isEmpty()) {
      entries.append(FolderCompareEntry{status, path});
    }
  }
  return entries;
}

QString FolderCompareStatusLabel(FolderCompareStatus status) {
  switch (status) {
  case FolderCompareStatus::Match:
    return "Match";
  case FolderCompareStatus::MissingOnSource:
    return "Missing on source";
  case FolderCompareStatus::MissingOnDestination:
    return "Missing on destination";
  case FolderCompareStatus::Different:
    return "Different";
  case FolderCompareStatus::Error:
    return "Error";
  }
  return "Error";
}

QString JoinFolderComparePath(const QString &root, const QString &relativePath) {
  if (relativePath.isEmpty()) {
    return root;
  }
  if (root.isEmpty()) {
    return relativePath;
  }
  if (root.endsWith('/') || root.endsWith('\\') || root.endsWith(':')) {
    return root + relativePath;
  }
  return root + "/" + relativePath;
}

FolderCompareDialog::FolderCompareDialog(
    const QString &sourcePath, const QString &destinationPath,
    const QStringList &driveSharedArgs,
    EnqueueTransferCallback enqueueTransfer, QWidget *parent)
    : QDialog(parent), mDriveSharedArgs(driveSharedArgs),
      mEnqueueTransfer(enqueueTransfer) {
  setWindowTitle("Compare Folders");
  resize(1000, 620);
  UiPolish::SetWindowDefaults(this, QSize(860, 520));

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(8);

  auto *paths = new QFormLayout();
  mSourceEdit = new QLineEdit(sourcePath, this);
  mDestinationEdit = new QLineEdit(destinationPath, this);
  mSourceEdit->setPlaceholderText("source:path or local path");
  mDestinationEdit->setPlaceholderText("destination:path or local path");
  UiPolish::SetPathField(mSourceEdit, "Compare source path");
  UiPolish::SetPathField(mDestinationEdit, "Compare destination path");
  paths->addRow("Source", mSourceEdit);
  paths->addRow("Destination", mDestinationEdit);
  layout->addLayout(paths);

  mCryptCheck = new QCheckBox("Use cryptcheck (for crypt remotes)", this);
  mCryptCheck->setToolTip(
      "Verify a crypt remote against its plaintext source using rclone "
      "cryptcheck instead of check.");
  layout->addWidget(mCryptCheck);

  auto *filters = new QHBoxLayout();
  mCompareButton = new QPushButton("Compare", this);
  UiPolish::SetPrimaryButton(mCompareButton);
  mCompareButton->setAccessibleName("Run folder comparison");
  mStatusFilter = new QComboBox(this);
  mStatusFilter->setAccessibleName("Compare status filter");
  mStatusFilter->addItem("All statuses", -1);
  mStatusFilter->addItem("Matches", static_cast<int>(FolderCompareStatus::Match));
  mStatusFilter->addItem(
      "Missing on source",
      static_cast<int>(FolderCompareStatus::MissingOnSource));
  mStatusFilter->addItem(
      "Missing on destination",
      static_cast<int>(FolderCompareStatus::MissingOnDestination));
  mStatusFilter->addItem("Different",
                         static_cast<int>(FolderCompareStatus::Different));
  mStatusFilter->addItem("Errors",
                         static_cast<int>(FolderCompareStatus::Error));
  mTextFilter = new QLineEdit(this);
  mTextFilter->setPlaceholderText("Filter paths");
  mTextFilter->setClearButtonEnabled(true);
  UiPolish::SetPathField(mTextFilter, "Filter compare paths");
  filters->addWidget(mCompareButton);
  filters->addWidget(mStatusFilter);
  filters->addWidget(mTextFilter, 1);
  layout->addLayout(filters);

  mSummary = new QLabel("No comparison has been run.", this);
  mSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  UiPolish::SetMuted(mSummary);
  layout->addWidget(mSummary);

  mEmptyState = new QLabel(this);
  UiPolish::SetEmptyState(
      mEmptyState, "No comparison yet",
      "Run Compare to review matches, differences, and repair actions.");
  layout->addWidget(mEmptyState);

  mTable = new QTableWidget(this);
  mTable->setColumnCount(3);
  mTable->setHorizontalHeaderLabels(QStringList() << "State"
                                                  << "Path"
                                                  << "Marker");
  UiPolish::SetTableView(mTable, "Folder comparison results");
  mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
  mTable->horizontalHeader()->setSectionResizeMode(0,
                                                   QHeaderView::ResizeToContents);
  mTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  mTable->horizontalHeader()->setSectionResizeMode(2,
                                                   QHeaderView::ResizeToContents);
  layout->addWidget(mTable, 1);

  auto *actionBar = new QWidget(this);
  UiPolish::SetActionBar(actionBar);
  auto *actions = new QHBoxLayout(actionBar);
  actions->setContentsMargins(4, 4, 4, 4);
  actions->setSpacing(4);
  mCopyToDestinationButton = new QPushButton("Copy to Destination", this);
  mCopyToSourceButton = new QPushButton("Copy to Source", this);
  mDeleteFromDestinationButton =
      new QPushButton("Delete from Destination", this);
  mDeleteFromSourceButton = new QPushButton("Delete from Source", this);
  auto *closeButton = new QPushButton("Close", this);
  UiPolish::SetDestructiveButton(mDeleteFromDestinationButton);
  UiPolish::SetDestructiveButton(mDeleteFromSourceButton);
  mCopyToDestinationButton->setAccessibleName("Queue copy to destination repair");
  mCopyToSourceButton->setAccessibleName("Queue copy to source repair");
  mDeleteFromDestinationButton->setAccessibleName(
      "Queue delete from destination repair");
  mDeleteFromSourceButton->setAccessibleName("Queue delete from source repair");
  actions->addWidget(mCopyToDestinationButton);
  actions->addWidget(mCopyToSourceButton);
  actions->addWidget(mDeleteFromDestinationButton);
  actions->addWidget(mDeleteFromSourceButton);
  actions->addStretch(1);
  actions->addWidget(closeButton);
  layout->addWidget(actionBar);

  QObject::connect(mCompareButton, &QPushButton::clicked, this,
                   [this]() { runCompare(); });
  QObject::connect(mSourceEdit, &QLineEdit::textChanged, this,
                   [this]() { UiPolish::SetFieldState(mSourceEdit, QString()); });
  QObject::connect(mDestinationEdit, &QLineEdit::textChanged, this, [this]() {
    UiPolish::SetFieldState(mDestinationEdit, QString());
  });
  QObject::connect(mStatusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   this, [this]() { refreshTable(); });
  QObject::connect(mTextFilter, &QLineEdit::textChanged, this,
                   [this]() { refreshTable(); });
  QObject::connect(mTable, &QTableWidget::itemSelectionChanged, this,
                   [this]() { updateRepairButtons(); });
  QObject::connect(mCopyToDestinationButton, &QPushButton::clicked, this,
                   [this]() { enqueueRepair(RepairAction::CopyToDestination); });
  QObject::connect(mCopyToSourceButton, &QPushButton::clicked, this,
                   [this]() { enqueueRepair(RepairAction::CopyToSource); });
  QObject::connect(mDeleteFromDestinationButton, &QPushButton::clicked, this,
                   [this]() {
                     enqueueRepair(RepairAction::DeleteFromDestination);
                   });
  QObject::connect(mDeleteFromSourceButton, &QPushButton::clicked, this,
                   [this]() { enqueueRepair(RepairAction::DeleteFromSource); });
  QObject::connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

  updateRepairButtons();
}

FolderCompareDialog::~FolderCompareDialog() {
  if (mProcess && mProcess->state() != QProcess::NotRunning) {
    mProcess->kill();
    mProcess->waitForFinished(1000);
  }
}

QString FolderCompareDialog::sourcePath() const {
  return mSourceEdit->text().trimmed();
}

QString FolderCompareDialog::destinationPath() const {
  return mDestinationEdit->text().trimmed();
}

void FolderCompareDialog::runCompare() {
  const QString source = sourcePath();
  const QString destination = destinationPath();
  if (source.isEmpty() || destination.isEmpty()) {
    UiPolish::SetFieldState(mSourceEdit, source.isEmpty() ? "error" : QString());
    UiPolish::SetFieldState(mDestinationEdit,
                            destination.isEmpty() ? "error" : QString());
    mSummary->setText("Enter both a source and destination path.");
    return;
  }
  if (mProcess && mProcess->state() != QProcess::NotRunning) {
    return;
  }

  mOutput.clear();
  mEntries.clear();
  refreshTable();
  mSummary->setText("Running rclone check...");
  mEmptyState->setVisible(true);
  UiPolish::SetEmptyState(mEmptyState, "Comparing folders",
                          "Results will appear as soon as rclone finishes.");
  mCompareButton->setEnabled(false);

  mProcess = new QProcess(this);
  UseRclonePassword(mProcess);
  mProcess->setProgram(GetRclone());
  QString command = mCryptCheck->isChecked() ? "cryptcheck" : "check";
  mProcess->setArguments(QStringList()
                         << command << GetRcloneConf() << mDriveSharedArgs
                         << GetDefaultRcloneOptionsList() << source
                         << destination << "--combined" << "-");
  mProcess->setProcessChannelMode(QProcess::MergedChannels);

  QObject::connect(mProcess, &QProcess::readyRead, this, [this]() {
    mOutput.append(mProcess->readAll());
  });
  QObject::connect(
      mProcess,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [this](int code, QProcess::ExitStatus status) {
        mOutput.append(mProcess->readAll());
        mEntries = ParseRcloneCheckCombinedOutput(QString::fromUtf8(mOutput));
        refreshTable();
        mCompareButton->setEnabled(true);

        if (status != QProcess::NormalExit || (code != 0 && mEntries.isEmpty())) {
          QMessageBox::warning(
              this, "Compare Folders",
              QString("rclone check failed with status %1.\n\n%2")
                  .arg(code)
                  .arg(QString::fromUtf8(mOutput).left(1200)));
        } else if (mEntries.isEmpty()) {
          mSummary->setText("No differences reported.");
          UiPolish::SetEmptyState(
              mEmptyState, "Folders match",
              "rclone did not report any paths that need attention.");
        }
        mProcess->deleteLater();
        mProcess = nullptr;
      });
  QObject::connect(mProcess, &QProcess::errorOccurred, this,
                   [this](QProcess::ProcessError) {
                     mCompareButton->setEnabled(true);
                     QMessageBox::warning(
                         this, "Compare Folders",
                         "Could not start rclone: " + mProcess->errorString());
                   });

  mProcess->start(QIODevice::ReadOnly);
}

void FolderCompareDialog::refreshTable() {
  mVisibleEntryIndexes.clear();
  for (int i = 0; i < mEntries.size(); ++i) {
    if (entryMatchesFilter(mEntries.at(i))) {
      mVisibleEntryIndexes.append(i);
    }
  }

  mTable->setRowCount(mVisibleEntryIndexes.size());
  mEmptyState->setVisible(mVisibleEntryIndexes.isEmpty());
  int matches = 0;
  int missingOnSource = 0;
  int missingOnDestination = 0;
  int different = 0;
  int errors = 0;

  for (const FolderCompareEntry &entry : mEntries) {
    switch (entry.status) {
    case FolderCompareStatus::Match:
      ++matches;
      break;
    case FolderCompareStatus::MissingOnSource:
      ++missingOnSource;
      break;
    case FolderCompareStatus::MissingOnDestination:
      ++missingOnDestination;
      break;
    case FolderCompareStatus::Different:
      ++different;
      break;
    case FolderCompareStatus::Error:
      ++errors;
      break;
    }
  }

  for (int row = 0; row < mVisibleEntryIndexes.size(); ++row) {
    const int entryIndex = mVisibleEntryIndexes.at(row);
    const FolderCompareEntry &entry = mEntries.at(entryIndex);
    auto *status = new QTableWidgetItem(FolderCompareStatusLabel(entry.status));
    status->setData(kEntryIndexRole, entryIndex);
    auto *path = new QTableWidgetItem(entry.path);
    path->setData(kEntryIndexRole, entryIndex);
    path->setToolTip(entry.path);
    auto *marker = new QTableWidgetItem(statusSymbol(entry.status));
    marker->setData(kEntryIndexRole, entryIndex);
    mTable->setItem(row, 0, status);
    mTable->setItem(row, 1, path);
    mTable->setItem(row, 2, marker);
  }

  if (mEntries.isEmpty()) {
    mSummary->setText("No comparison has been run.");
    UiPolish::SetEmptyState(
        mEmptyState, "No comparison yet",
        "Run Compare to review matches, differences, and repair actions.");
  } else {
    mSummary->setText(
        QString("%1 shown of %2 paths. %3 match, %4 missing on source, "
                "%5 missing on destination, %6 different, %7 errors.")
            .arg(mVisibleEntryIndexes.size())
            .arg(mEntries.size())
            .arg(matches)
            .arg(missingOnSource)
            .arg(missingOnDestination)
            .arg(different)
            .arg(errors));
    if (mVisibleEntryIndexes.isEmpty()) {
      UiPolish::SetEmptyState(
          mEmptyState, "No rows match the current filters",
          "Change the status or path filter to see more results.");
    }
  }

  updateRepairButtons();
}

void FolderCompareDialog::updateRepairButtons() {
  bool copyToDestination = false;
  bool copyToSource = false;
  bool deleteFromDestination = false;
  bool deleteFromSource = false;

  for (int entryIndex : selectedEntryIndexes()) {
    const FolderCompareEntry &entry = mEntries.at(entryIndex);
    copyToDestination |= entrySupportsAction(entry, RepairAction::CopyToDestination);
    copyToSource |= entrySupportsAction(entry, RepairAction::CopyToSource);
    deleteFromDestination |=
        entrySupportsAction(entry, RepairAction::DeleteFromDestination);
    deleteFromSource |= entrySupportsAction(entry, RepairAction::DeleteFromSource);
  }

  mCopyToDestinationButton->setEnabled(copyToDestination);
  mCopyToSourceButton->setEnabled(copyToSource);
  mDeleteFromDestinationButton->setEnabled(deleteFromDestination);
  mDeleteFromSourceButton->setEnabled(deleteFromSource);
  mCopyToDestinationButton->setToolTip(
      copyToDestination
          ? "Queue selected missing/different rows to copy to the destination."
          : "Select rows that are missing or different on the destination.");
  mCopyToSourceButton->setToolTip(
      copyToSource
          ? "Queue selected missing/different rows to copy to the source."
          : "Select rows that are missing or different on the source.");
  mDeleteFromDestinationButton->setToolTip(
      deleteFromDestination
          ? "Queue deletes for selected rows that only exist on the destination."
          : "Select rows that are missing on the source.");
  mDeleteFromSourceButton->setToolTip(
      deleteFromSource
          ? "Queue deletes for selected rows that only exist on the source."
          : "Select rows that are missing on the destination.");
}

void FolderCompareDialog::enqueueRepair(RepairAction action) {
  if (!mEnqueueTransfer) {
    return;
  }

  const QString sourceRoot = sourcePath();
  const QString destinationRoot = destinationPath();
  int queued = 0;

  for (int entryIndex : selectedEntryIndexes()) {
    const FolderCompareEntry &entry = mEntries.at(entryIndex);
    if (!entrySupportsAction(entry, action)) {
      continue;
    }

    if (action == RepairAction::CopyToDestination ||
        action == RepairAction::CopyToSource) {
      const bool sourceToDestination = action == RepairAction::CopyToDestination;
      const QString source = JoinFolderComparePath(
          sourceToDestination ? sourceRoot : destinationRoot, entry.path);
      const QString destination = JoinFolderComparePath(
          sourceToDestination ? destinationRoot : sourceRoot, entry.path);
      mEnqueueTransfer(QString("Repair copy %1").arg(entry.path), source,
                       destination, copyArgs(source, destination));
      ++queued;
    } else {
      const bool deleteFromDestination =
          action == RepairAction::DeleteFromDestination;
      const QString target = JoinFolderComparePath(
          deleteFromDestination ? destinationRoot : sourceRoot, entry.path);
      mEnqueueTransfer(QString("Repair delete %1").arg(entry.path), target,
                       QString(), deleteArgs(target));
      ++queued;
    }
  }

  mSummary->setText(QString("Queued %1 %2 operation(s).")
                        .arg(queued)
                        .arg(repairActionLabel(action).toLower()));
}

bool FolderCompareDialog::entryMatchesFilter(
    const FolderCompareEntry &entry) const {
  const int statusValue = mStatusFilter->currentData().toInt();
  if (statusValue >= 0 &&
      statusValue != static_cast<int>(entry.status)) {
    return false;
  }

  const QString filter = mTextFilter->text().trimmed();
  return filter.isEmpty() ||
         entry.path.contains(filter, Qt::CaseInsensitive);
}

bool FolderCompareDialog::entrySupportsAction(
    const FolderCompareEntry &entry, RepairAction action) const {
  switch (action) {
  case RepairAction::CopyToDestination:
    return entry.status == FolderCompareStatus::MissingOnDestination ||
           entry.status == FolderCompareStatus::Different;
  case RepairAction::CopyToSource:
    return entry.status == FolderCompareStatus::MissingOnSource ||
           entry.status == FolderCompareStatus::Different;
  case RepairAction::DeleteFromDestination:
    return entry.status == FolderCompareStatus::MissingOnSource;
  case RepairAction::DeleteFromSource:
    return entry.status == FolderCompareStatus::MissingOnDestination;
  }
  return false;
}

QVector<int> FolderCompareDialog::selectedEntryIndexes() const {
  QVector<int> indexes;
  const QModelIndexList rows = mTable->selectionModel()->selectedRows();
  for (const QModelIndex &row : rows) {
    QTableWidgetItem *item = mTable->item(row.row(), 0);
    if (!item) {
      continue;
    }
    const int entryIndex = item->data(kEntryIndexRole).toInt();
    if (entryIndex >= 0 && entryIndex < mEntries.size()) {
      indexes.append(entryIndex);
    }
  }
  return indexes;
}

QStringList FolderCompareDialog::copyArgs(const QString &source,
                                          const QString &dest) const {
  QStringList args;
  args << "copyto" << mDriveSharedArgs << GetDefaultRcloneOptionsList()
       << "--verbose"
       << "--use-json-log"
       << "--stats" << "1s"
       << "--stats-file-name-length" << "0" << source << dest;
  return args;
}

QStringList FolderCompareDialog::deleteArgs(const QString &target) const {
  QStringList args;
  args << "deletefile" << mDriveSharedArgs << GetDefaultRcloneOptionsList()
       << "--verbose"
       << "--use-json-log"
       << "--stats" << "1s"
       << "--stats-file-name-length" << "0" << target;
  return args;
}
