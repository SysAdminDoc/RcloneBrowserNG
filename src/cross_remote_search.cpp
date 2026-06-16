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

  auto *queryRow = new QHBoxLayout();
  mQueryEdit = new QLineEdit(this);
  mQueryEdit->setPlaceholderText("Filename pattern (e.g. *.jpg, report*)");
  mQueryEdit->setAccessibleName("Search query");
  UiPolish::SetPathField(mQueryEdit, "Search query");
  mCaseSensitive = new QCheckBox("Case-sensitive", this);
  mCaseSensitive->setToolTip("Match uppercase and lowercase exactly.");
  mSearchButton = new QPushButton("Search", this);
  UiPolish::SetPrimaryButton(mSearchButton);
  mSearchButton->setEnabled(false);
  mSearchButton->setAccessibleName("Start search");
  mCancelButton = new QPushButton("Cancel", this);
  mCancelButton->setEnabled(false);
  mCancelButton->setAccessibleName("Cancel search");
  queryRow->addWidget(mQueryEdit, 1);
  queryRow->addWidget(mCaseSensitive);
  queryRow->addWidget(mSearchButton);
  queryRow->addWidget(mCancelButton);
  layout->addLayout(queryRow);

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

  int started = 0;
  for (const QString &remote : mRemotes) {
    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    UseRclonePassword(proc);

    QStringList args;
    args << "lsjson" << GetRcloneConf() << "-R" << "--no-mimetype"
         << "--include" << query;
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
                    .arg(mRemotes.size());
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
