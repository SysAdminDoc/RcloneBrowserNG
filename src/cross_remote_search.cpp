#include "cross_remote_search.h"
#include "interface_polish.h"
#include "utils.h"

CrossRemoteSearchDialog::CrossRemoteSearchDialog(
    const QStringList &remoteNames, QWidget *parent)
    : QDialog(parent), mRemotes(remoteNames) {
  setWindowTitle("Search Across Remotes");
  resize(900, 550);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(8);

  auto *remoteRow = new QHBoxLayout();
  remoteRow->addWidget(new QLabel("Remotes:", this));
  for (const QString &r : remoteNames) {
    auto *cb = new QCheckBox(r, this);
    cb->setChecked(true);
    mRemoteChecks.insert(r, cb);
    remoteRow->addWidget(cb);
  }
  remoteRow->addStretch();
  layout->addLayout(remoteRow);

  auto *queryRow = new QHBoxLayout();
  mHistoryCombo = new QComboBox(this);
  mHistoryCombo->setEditable(true);
  mHistoryCombo->setInsertPolicy(QComboBox::NoInsert);
  mHistoryCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  mHistoryCombo->lineEdit()->setPlaceholderText(
      "Filename pattern (e.g. *.jpg, report*)");
  mHistoryCombo->setAccessibleName("Search query with history");
  mQueryEdit = mHistoryCombo->lineEdit();
  loadHistory();

  mCaseSensitive = new QCheckBox("Case-sensitive", this);
  mCaseSensitive->setToolTip("Match uppercase and lowercase exactly.");
  mSearchButton = new QPushButton("Search", this);
  UiPolish::SetPrimaryButton(mSearchButton);
  mSearchButton->setEnabled(false);
  mSearchButton->setAccessibleName("Start search");
  mCancelButton = new QPushButton("Cancel", this);
  mCancelButton->setEnabled(false);
  mCancelButton->setAccessibleName("Cancel search");
  queryRow->addWidget(mHistoryCombo, 1);
  queryRow->addWidget(mCaseSensitive);
  queryRow->addWidget(mSearchButton);
  queryRow->addWidget(mCancelButton);
  layout->addLayout(queryRow);

  auto *filterRow = new QHBoxLayout();
  filterRow->addWidget(new QLabel("Type:", this));
  mTypeFilter = new QComboBox(this);
  mTypeFilter->addItems(QStringList() << "All files" << "Images (*.jpg *.png *.gif *.bmp *.webp)"
                                      << "Documents (*.pdf *.doc* *.xls* *.ppt*)"
                                      << "Videos (*.mp4 *.mkv *.avi *.mov)"
                                      << "Audio (*.mp3 *.flac *.wav *.aac)");
  mTypeFilter->setAccessibleName("File type filter");
  filterRow->addWidget(mTypeFilter);
  filterRow->addWidget(new QLabel("Min size (KB):", this));
  mMinSize = new QSpinBox(this);
  mMinSize->setRange(0, 999999999);
  mMinSize->setSpecialValueText("Any");
  mMinSize->setAccessibleName("Minimum file size");
  filterRow->addWidget(mMinSize);
  filterRow->addWidget(new QLabel("Max size (MB):", this));
  mMaxSize = new QSpinBox(this);
  mMaxSize->setRange(0, 999999);
  mMaxSize->setSpecialValueText("Any");
  mMaxSize->setAccessibleName("Maximum file size");
  filterRow->addWidget(mMaxSize);
  filterRow->addStretch();
  layout->addLayout(filterRow);

  mStatus = new QLabel("Enter a pattern to search every configured remote.", this);
  UiPolish::SetMuted(mStatus);
  layout->addWidget(mStatus);

  mEmptyState = new QLabel(this);
  UiPolish::SetEmptyState(
      mEmptyState, "No search running",
      "Use a filename pattern such as *.jpg, invoice*, or report?.pdf.");
  layout->addWidget(mEmptyState);

  mResults = new QTableWidget(0, 4, this);
  mResults->setHorizontalHeaderLabels({"Remote", "Path", "Size", "Modified"});
  UiPolish::SetTableView(mResults, "Search results");
  mResults->horizontalHeader()->setStretchLastSection(true);
  mResults->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  mResults->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  mResults->setSelectionMode(QAbstractItemView::SingleSelection);
  mResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mResults->setSortingEnabled(true);
  layout->addWidget(mResults, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  UiPolish::SetDialogButtonBox(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this,
                   &QDialog::reject);
  layout->addWidget(buttons);

  QObject::connect(mSearchButton, &QPushButton::clicked, this,
                   &CrossRemoteSearchDialog::startSearch);
  QObject::connect(mCancelButton, &QPushButton::clicked, this,
                   &CrossRemoteSearchDialog::cancelSearch);
  QObject::connect(mQueryEdit, &QLineEdit::returnPressed, this,
                   &CrossRemoteSearchDialog::startSearch);
  QObject::connect(mQueryEdit, &QLineEdit::textChanged, this,
                   [this](const QString &text) {
                     UiPolish::SetFieldState(mQueryEdit, QString());
                     if (mRunning.isEmpty()) {
                       mSearchButton->setEnabled(!text.trimmed().isEmpty());
                     }
                   });

  QObject::connect(mResults, &QTableWidget::cellDoubleClicked, this,
                   [this](int row, int) {
                     auto *remoteItem = mResults->item(row, 0);
                     auto *pathItem = mResults->item(row, 1);
                     if (remoteItem && pathItem) {
                       emit openLocation(remoteItem->text() + ":" +
                                         pathItem->text());
                     }
                   });
}

CrossRemoteSearchDialog::~CrossRemoteSearchDialog() { cancelSearch(); }

void CrossRemoteSearchDialog::startSearch() {
  cancelSearch();

  QString query = mQueryEdit->text().trimmed();
  if (query.isEmpty()) {
    UiPolish::SetFieldState(mQueryEdit, "error");
    mStatus->setText("Enter a filename pattern before searching.");
    mEmptyState->setVisible(true);
    UiPolish::SetEmptyState(mEmptyState, "Search needs a pattern",
                            "Examples: *.jpg, invoice*, report?.pdf");
    return;
  }

  mResults->setRowCount(0);
  mTotalMatches = 0;
  mFailedRemotes = 0;
  mRemoteErrors.clear();
  mStatus->setToolTip(QString());
  mSearchButton->setEnabled(false);
  mCancelButton->setEnabled(true);
  UiPolish::SetFieldState(mQueryEdit, QString());
  mEmptyState->setVisible(true);
  UiPolish::SetEmptyState(mEmptyState, "Searching remotes",
                          "Matches will appear here as rclone returns them.");

  saveHistory(query);

  QStringList typeIncludes;
  int typeIdx = mTypeFilter->currentIndex();
  if (typeIdx == 1)
    typeIncludes << "*.jpg" << "*.jpeg" << "*.png" << "*.gif" << "*.bmp" << "*.webp";
  else if (typeIdx == 2)
    typeIncludes << "*.pdf" << "*.doc" << "*.docx" << "*.xls" << "*.xlsx" << "*.ppt" << "*.pptx";
  else if (typeIdx == 3)
    typeIncludes << "*.mp4" << "*.mkv" << "*.avi" << "*.mov" << "*.webm";
  else if (typeIdx == 4)
    typeIncludes << "*.mp3" << "*.flac" << "*.wav" << "*.aac" << "*.ogg";

  qint64 minBytes = static_cast<qint64>(mMinSize->value()) * 1024;
  qint64 maxBytes = mMaxSize->value() > 0
                        ? static_cast<qint64>(mMaxSize->value()) * 1024 * 1024
                        : 0;

  QStringList activeRemotes = selectedRemotes();
  int started = 0;
  for (const QString &remote : activeRemotes) {
    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    UseRclonePassword(proc);

    QStringList args;
    args << "lsjson" << GetRcloneConf() << "-R" << "--no-mimetype"
         << "--include" << query;
    for (const QString &inc : typeIncludes) {
      args << "--include" << inc;
    }
    if (minBytes > 0) {
      args << "--min-size" << QString::number(minBytes);
    }
    if (maxBytes > 0) {
      args << "--max-size" << QString::number(maxBytes);
    }
    if (!mCaseSensitive->isChecked()) {
      args << "--ignore-case";
    }
    args << remote + ":";

    mRunning.append(proc);
    ++started;

    QObject::connect(proc, &QProcess::readyReadStandardOutput, this,
                     [this, proc, remote]() {
                       while (proc->canReadLine()) {
                         QByteArray line = proc->readLine().trimmed();
                         if (line.isEmpty() || line.startsWith('[') ||
                             line.startsWith(']'))
                           continue;
                         if (line.endsWith(','))
                           line.chop(1);
                         QJsonDocument doc = QJsonDocument::fromJson(line);
                         if (!doc.isObject())
                           continue;
                         QJsonObject obj = doc.object();
                         if (obj.value("IsDir").toBool())
                           continue;
                         addResult(remote, obj.value("Path").toString(),
                                   obj.value("Size").toVariant().toLongLong(),
                                   obj.value("ModTime").toString());
                       }
                     });

    QObject::connect(
        proc,
        static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
            &QProcess::finished),
        this, [this, proc, remote](int code, QProcess::ExitStatus) {
          if (code != 0) {
            ++mFailedRemotes;
            const QString stderrText =
                QString::fromUtf8(proc->readAllStandardError()).trimmed();
            mRemoteErrors << QString("%1: %2")
                                 .arg(remote,
                                      stderrText.isEmpty()
                                          ? QString("rclone exited with status %1")
                                                .arg(code)
                                          : stderrText.left(500));
          }
          mRunning.removeOne(proc);
          proc->deleteLater();
          if (mRunning.isEmpty()) {
            mSearchButton->setEnabled(!mQueryEdit->text().trimmed().isEmpty());
            mCancelButton->setEnabled(false);
            QString status =
                QString("Done. %1 file(s) found across %2 remote(s).")
                    .arg(mTotalMatches)
                    .arg(selectedRemotes().size());
            if (mFailedRemotes > 0) {
              status += QString(" %1 remote(s) need attention.")
                            .arg(mFailedRemotes);
              mStatus->setToolTip(mRemoteErrors.join("\n\n"));
            }
            mStatus->setText(status);
            mEmptyState->setVisible(mTotalMatches == 0);
            if (mTotalMatches == 0) {
              UiPolish::SetEmptyState(
                  mEmptyState,
                  mFailedRemotes > 0 ? "No matches from completed remotes"
                                     : "No matching files",
                  mFailedRemotes > 0
                      ? "Check the status tooltip for remote errors, or refine the pattern."
                      : "Try a broader pattern or disable case-sensitive matching.");
            }
          }
        });

    proc->start(GetRclone(), args, QIODevice::ReadOnly);
  }

  mStatus->setText(
      QString("Searching %1 remote(s)...").arg(started));
}

void CrossRemoteSearchDialog::cancelSearch() {
  const bool hadRunning = !mRunning.isEmpty();
  for (auto *proc : mRunning) {
    proc->disconnect();
    proc->kill();
    proc->waitForFinished(2000);
    proc->deleteLater();
  }
  mRunning.clear();
  mSearchButton->setEnabled(!mQueryEdit->text().trimmed().isEmpty());
  mCancelButton->setEnabled(false);
  if (!hadRunning) {
    return;
  }
  if (mTotalMatches > 0) {
    mStatus->setText(
        QString("Cancelled. %1 file(s) found before cancellation.")
            .arg(mTotalMatches));
  } else {
    mStatus->setText("Search cancelled.");
    mEmptyState->setVisible(true);
    UiPolish::SetEmptyState(
        mEmptyState, "Search cancelled",
        "Start another search when you are ready.");
  }
}

QStringList CrossRemoteSearchDialog::selectedRemotes() const {
  QStringList result;
  for (const QString &r : mRemotes) {
    auto it = mRemoteChecks.find(r);
    if (it != mRemoteChecks.end() && it.value()->isChecked())
      result << r;
  }
  return result.isEmpty() ? mRemotes : result;
}

void CrossRemoteSearchDialog::loadHistory() {
  auto settings = GetSettings();
  QStringList history =
      settings->value("Search/history").toStringList();
  mHistoryCombo->clear();
  for (const QString &q : history) {
    mHistoryCombo->addItem(q);
  }
  mHistoryCombo->setCurrentText(QString());
}

void CrossRemoteSearchDialog::saveHistory(const QString &query) {
  auto settings = GetSettings();
  QStringList history =
      settings->value("Search/history").toStringList();
  history.removeAll(query);
  history.prepend(query);
  while (history.size() > 20)
    history.removeLast();
  settings->setValue("Search/history", history);
}

void CrossRemoteSearchDialog::addResult(const QString &remote,
                                         const QString &path, qint64 size,
                                         const QString &modTime) {
  mResults->setSortingEnabled(false);
  int row = mResults->rowCount();
  mResults->insertRow(row);
  mResults->setItem(row, 0, new QTableWidgetItem(remote));
  mResults->setItem(row, 1, new QTableWidgetItem(path));

  auto *sizeItem = new QTableWidgetItem(
      GetNiceSize(static_cast<quint64>(size)));
  sizeItem->setData(Qt::UserRole, size);
  mResults->setItem(row, 2, sizeItem);

  QString displayTime = modTime;
  QDateTime dt = QDateTime::fromString(modTime, Qt::ISODateWithMs);
  if (dt.isValid())
    displayTime = dt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
  mResults->setItem(row, 3, new QTableWidgetItem(displayTime));

  mResults->setSortingEnabled(true);
  ++mTotalMatches;
  mEmptyState->setVisible(false);
  mStatus->setText(
      QString("Searching... %1 found so far (%2 remote(s) pending)")
          .arg(mTotalMatches)
          .arg(mRunning.size()));
}
