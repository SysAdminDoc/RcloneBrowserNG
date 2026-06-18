#include "remote_widget.h"
#include "export_list_writer.h"
#include "export_dialog.h"
#include "folder_compare.h"
#include "icon_cache.h"
#include "item_model.h"
#include "list_of_job_options.h"
#include "progress_dialog.h"
#include "rclone_capabilities.h"
#include "remote_path.h"
#include "stream_widget.h"
#include "transfer_dialog.h"
#include "interface_polish.h"
#include "utils.h"

#include <functional>

QStringList RemoteWidget::getDriveSharedArgs() const {
  if (ui.checkBoxShared->isChecked())
    return QStringList() << "--drive-shared-with-me";
  return QStringList();
}

namespace {
QString fingerprintFromLsjson(const QByteArray &data) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    return QString();
  }

  QJsonObject obj;
  if (doc.isArray() && !doc.array().isEmpty() && doc.array().first().isObject()) {
    obj = doc.array().first().toObject();
  } else if (doc.isObject()) {
    obj = doc.object();
  }
  if (obj.isEmpty()) {
    return QString();
  }

  return obj.value("ModTime").toString() + "|" +
         QString::number(obj.value("Size").toVariant().toLongLong());
}

class RemoteEditSession : public QObject {
public:
  RemoteEditSession(const QString &remoteFile, const QString &fileName,
                    const QStringList &driveSharedArgs, QWidget *dialogParent,
                    QObject *parent)
      : QObject(parent), mRemoteFile(remoteFile),
        mDriveSharedArgs(driveSharedArgs), mDialogParent(dialogParent) {
    mTempDir.setAutoRemove(true);
    mLocalFile = QDir(mTempDir.path()).filePath(fileName);
    mUploadTimer.setSingleShot(true);
    mUploadTimer.setInterval(1200);

    QObject::connect(&mWatcher, &QFileSystemWatcher::fileChanged, this,
                     [this](const QString &path) {
                       if (QFileInfo::exists(path) &&
                           !mWatcher.files().contains(path)) {
                         mWatcher.addPath(path);
                       }
                       mUploadTimer.start();
                     });
    QObject::connect(&mUploadTimer, &QTimer::timeout, this,
                     [this]() { uploadNow(); });
  }

  bool downloadAndOpen() {
    if (!mTempDir.isValid()) {
      QMessageBox::warning(mDialogParent, "Open/Edit",
                           "Could not create a temporary edit folder.");
      return false;
    }

    QProcess process;
    UseRclonePassword(&process);
    process.setProgram(GetRclone());
    process.setArguments(QStringList()
                         << "copyto" << GetRcloneConf() << mDriveSharedArgs
                         << GetDefaultRcloneOptionsList() << mRemoteFile
                         << mLocalFile);
    process.setProcessChannelMode(QProcess::MergedChannels);

    ProgressDialog progress("Open/Edit", "Downloading...", mRemoteFile,
                            &process, mDialogParent);
    if (progress.exec() != QDialog::Accepted ||
        process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
      return false;
    }

    mRemoteFingerprint = remoteFingerprint();
    mWatcher.addPath(mLocalFile);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(mLocalFile))) {
      QMessageBox::warning(mDialogParent, "Open/Edit",
                           "Could not open the downloaded file.");
      return false;
    }
    return true;
  }

private:
  QTemporaryDir mTempDir;
  QFileSystemWatcher mWatcher;
  QTimer mUploadTimer;
  QString mRemoteFile;
  QString mLocalFile;
  QString mRemoteFingerprint;
  QStringList mDriveSharedArgs;
  QWidget *mDialogParent = nullptr;

  QString remoteFingerprint() const {
    QProcess process;
    UseRclonePassword(&process);
    process.setProgram(GetRclone());
    process.setArguments(QStringList()
                         << "lsjson" << GetRcloneConf() << mDriveSharedArgs
                         << GetDefaultRcloneOptionsList() << mRemoteFile);
    process.start(QIODevice::ReadOnly);
    if (!process.waitForFinished(30000) ||
        process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
      return QString();
    }
    return fingerprintFromLsjson(process.readAllStandardOutput());
  }

  void uploadNow() {
    if (!QFileInfo::exists(mLocalFile)) {
      return;
    }
    if (!mWatcher.files().contains(mLocalFile)) {
      mWatcher.addPath(mLocalFile);
    }

    const QString currentFingerprint = remoteFingerprint();
    if (!mRemoteFingerprint.isEmpty() && !currentFingerprint.isEmpty() &&
        currentFingerprint != mRemoteFingerprint) {
      QMessageBox box(mDialogParent);
      box.setIcon(QMessageBox::Warning);
      box.setWindowTitle("Remote file changed");
      box.setText("The remote file changed after it was opened.");
      box.setInformativeText(
          "Overwrite the remote file with your local edit?");
      QPushButton *overwrite =
          box.addButton("Overwrite Remote", QMessageBox::AcceptRole);
      box.addButton(QMessageBox::Cancel);
      box.exec();
      if (box.clickedButton() != overwrite) {
        return;
      }
    }

    QProcess process;
    UseRclonePassword(&process);
    process.setProgram(GetRclone());
    process.setArguments(QStringList()
                         << "copyto" << GetRcloneConf() << mDriveSharedArgs
                         << GetDefaultRcloneOptionsList() << "--verbose"
                         << "--use-json-log"
                         << "--stats" << "1s"
                         << "--stats-file-name-length" << "0" << mLocalFile
                         << mRemoteFile);
    process.setProcessChannelMode(QProcess::MergedChannels);

    ProgressDialog progress("Open/Edit", "Uploading saved changes...",
                            mRemoteFile, &process, mDialogParent);
    if (progress.exec() == QDialog::Accepted &&
        process.exitStatus() == QProcess::NormalExit &&
        process.exitCode() == 0) {
      mRemoteFingerprint = remoteFingerprint();
    }
  }
};
} // namespace

RemoteWidget::RemoteWidget(IconCache *iconCache, const QString &remote,
                           bool isLocal, bool isGoogle, bool isGooglePhotos,
                           QWidget *parent)
    : QWidget(parent) {
  ui.setupUi(this);
  ui.horizontalLayout->setContentsMargins(10, 10, 10, 10);
  ui.verticalLayout->setSpacing(8);
  ui.buttonsGrid->setContentsMargins(10, 8, 10, 8);
  ui.buttonsGrid->setHorizontalSpacing(6);
  ui.buttonsGrid->setVerticalSpacing(6);
  ui.splitter->setHandleWidth(6);
  UiPolish::SetToolbarSurface(ui.buttons);
  UiPolish::SetPathField(ui.path, "Current remote path");
  ui.path->setPlaceholderText("Select a folder or file");
  mBackButton = new QToolButton(this);
  mBackButton->setIcon(qApp->style()->standardIcon(QStyle::SP_ArrowBack));
  mBackButton->setToolTip("Go back");
  mBackButton->setAccessibleName("Navigate back");
  mBackButton->setAutoRaise(true);
  mBackButton->setEnabled(false);
  QObject::connect(mBackButton, &QToolButton::clicked, this,
                   &RemoteWidget::goBack);

  mForwardButton = new QToolButton(this);
  mForwardButton->setIcon(
      qApp->style()->standardIcon(QStyle::SP_ArrowForward));
  mForwardButton->setToolTip("Go forward");
  mForwardButton->setAccessibleName("Navigate forward");
  mForwardButton->setAutoRaise(true);
  mForwardButton->setEnabled(false);
  QObject::connect(mForwardButton, &QToolButton::clicked, this,
                   &RemoteWidget::goForward);

  mBreadcrumbBar = new QWidget(this);
  mBreadcrumbBar->setObjectName("breadcrumbBar");
  mBreadcrumbBar->installEventFilter(this);
  mBreadcrumbLayout = new QHBoxLayout(mBreadcrumbBar);
  mBreadcrumbLayout->setContentsMargins(0, 0, 0, 0);
  mBreadcrumbLayout->setSpacing(4);
  if (auto *pathLayout =
          qobject_cast<QBoxLayout *>(ui.path->parentWidget()->layout())) {
    const int pathIndex = pathLayout->indexOf(ui.path);
    pathLayout->insertWidget(pathIndex, mBreadcrumbBar);
    pathLayout->insertWidget(pathIndex, mForwardButton);
    pathLayout->insertWidget(pathIndex, mBackButton);
  }
  ui.path->installEventFilter(this);
  ui.path->hide();
  ui.tree->installEventFilter(this);
  UiPolish::SetNavigationView(ui.tree, "Remote file browser");
  ui.tree->setRootIsDecorated(true);
  ui.tree->setIndentation(18);
  ui.tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  ui.tree->setDragEnabled(true);
  ui.tree->setDragDropMode(QAbstractItemView::DragDrop);
  ui.tree->setDefaultDropAction(Qt::CopyAction);

  QString root = isLocal ? "/" : QString();

#ifndef Q_OS_WIN
  isLocal = false;
#endif
  mIsLocal = isLocal;

  auto settings = GetSettings();
  QString rcloneVersion = settings->value("Settings/rcloneVersion").toString();
  ui.tree->setAlternatingRowColors(
      settings->value("Settings/rowColors", false).toBool());
  ui.checkBoxShared->setChecked(false);
  ui.checkBoxShared->setDisabled(!isGoogle);
  if (!isGoogle) {
    ui.checkBoxShared->hide();
  }
  {
    auto *trashButton = new QPushButton("Trash", this);
    trashButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_TrashIcon));
    trashButton->setMaximumHeight(28);
    if (isGoogle) {
      trashButton->setToolTip("List trashed files in this Google Drive remote.");
      trashButton->setAccessibleName("Browse Google Drive trash");
    } else {
      trashButton->setToolTip(
          "Clean up trash or old versions for this remote (rclone cleanup).");
      trashButton->setAccessibleName("Clean up remote trash");
    }
    if (!isGoogle) {
      trashButton->hide();
    }
    if (auto *layout =
            qobject_cast<QHBoxLayout *>(ui.checkBoxShared->parentWidget()->layout())) {
      layout->addWidget(trashButton);
    }
    QObject::connect(trashButton, &QPushButton::clicked, this, [this, remote, trashButton, isGoogle]() {
      if (isGoogle) {
        trashButton->setEnabled(false);
        trashButton->setText("Loading...");
        auto *proc = new QProcess(this);
        UseRclonePassword(proc);
        proc->setProgram(GetRclone());
        proc->setArguments(QStringList()
                          << "lsjson" << GetRcloneConf() << "--drive-trashed-only"
                          << getDriveSharedArgs() << "-R" << "--no-mimetype"
                          << remote + ":");
        proc->setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(
            proc,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                &QProcess::finished),
            this, [this, proc, remote, trashButton](int code, QProcess::ExitStatus) {
              proc->deleteLater();
              trashButton->setEnabled(true);
              trashButton->setText("Trash");
              if (code != 0) {
                QString err =
                    QString::fromUtf8(proc->readAllStandardError()).trimmed();
                QMessageBox::warning(this, "Trash",
                                     "Could not list trashed files:\n" +
                                         err.left(500));
                return;
              }
              QJsonDocument doc =
                  QJsonDocument::fromJson(proc->readAllStandardOutput());
              QJsonArray arr = doc.array();
              if (arr.isEmpty()) {
                QMessageBox::information(this, "Trash",
                                         "No trashed files found.");
                return;
              }
              QStringList items;
              for (const QJsonValue &val : arr) {
                QJsonObject obj = val.toObject();
                QString path = obj.value("Path").toString();
                qint64 size = obj.value("Size").toVariant().toLongLong();
                items << QString("%1  (%2)")
                             .arg(path,
                                  GetNiceSize(static_cast<quint64>(size)));
              }
              QDialog dlg(this);
              dlg.setWindowTitle(
                  QString("Trashed files in %1").arg(remote));
              dlg.resize(600, 400);
              UiPolish::SetWindowDefaults(&dlg, QSize(520, 320));
              auto *dlgLayout = new QVBoxLayout(&dlg);
              auto *list = new QListWidget(&dlg);
              UiPolish::SetNavigationView(list, "Trashed files");
              list->addItems(items);
              dlgLayout->addWidget(list);
              auto *hint = new QLabel(
                  QString("%1 trashed file(s). Use the Google Drive web "
                          "interface to restore.")
                      .arg(arr.size()),
                  &dlg);
              UiPolish::SetMuted(hint);
              dlgLayout->addWidget(hint);
              auto *close = new QPushButton("Close", &dlg);
              QObject::connect(close, &QPushButton::clicked, &dlg,
                               &QDialog::accept);
              dlgLayout->addWidget(close);
              dlg.exec();
            });
        QTimer::singleShot(30000, proc, [proc]() {
          if (proc->state() != QProcess::NotRunning)
            proc->kill();
        });
        proc->start();
      } else {
        int btn = QMessageBox::question(
            this, "Clean Up",
            QString("Run rclone cleanup on %1?\n\n"
                    "This permanently deletes trashed files, old versions, "
                    "and other backend-specific garbage. This cannot be undone.")
                .arg(remote),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes)
          return;
        trashButton->setEnabled(false);
        trashButton->setText("Cleaning...");
        auto *proc = new QProcess(this);
        UseRclonePassword(proc);
        proc->setProgram(GetRclone());
        proc->setArguments(QStringList()
                          << "cleanup" << GetRcloneConf()
                          << remote + ":");
        proc->setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(
            proc,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                &QProcess::finished),
            this, [this, proc, trashButton](int code, QProcess::ExitStatus) {
              proc->deleteLater();
              trashButton->setEnabled(true);
              trashButton->setText("Trash");
              if (code == 0) {
                QMessageBox::information(this, "Clean Up",
                                         "Cleanup completed successfully.");
              } else {
                QString err =
                    QString::fromUtf8(proc->readAllStandardError()).trimmed();
                QMessageBox::warning(this, "Clean Up",
                                     "Cleanup failed:\n" + err.left(500));
              }
            });
        QTimer::singleShot(60000, proc, [proc]() {
          if (proc->state() != QProcess::NotRunning)
            proc->kill();
        });
        proc->start();
      }
    });

    mTrashButton = trashButton;
  }

  QStyle *style = QApplication::style();
  ui.refresh->setIcon(style->standardIcon(QStyle::SP_BrowserReload));
  ui.mkdir->setIcon(style->standardIcon(QStyle::SP_FileDialogNewFolder));
  ui.rename->setIcon(style->standardIcon(QStyle::SP_FileIcon));
  ui.move->setIcon(style->standardIcon(QStyle::SP_DirOpenIcon));
  ui.purge->setIcon(style->standardIcon(QStyle::SP_TrashIcon));
  ui.mount->setIcon(style->standardIcon(QStyle::SP_DriveNetIcon));
  ui.stream->setIcon(style->standardIcon(QStyle::SP_MediaPlay));
  ui.upload->setIcon(style->standardIcon(QStyle::SP_ArrowUp));
  ui.download->setIcon(style->standardIcon(QStyle::SP_ArrowDown));
  ui.getSize->setIcon(style->standardIcon(QStyle::SP_FileDialogInfoView));
  ui.getTree->setIcon(style->standardIcon(QStyle::SP_FileDialogListView));
  ui.export_->setIcon(style->standardIcon(QStyle::SP_FileDialogDetailedView));
  ui.link->setIcon(style->standardIcon(QStyle::SP_FileLinkIcon));

  ui.buttonRefresh->setDefaultAction(ui.refresh);
  ui.buttonMkdir->setDefaultAction(ui.mkdir);
  ui.buttonRename->setDefaultAction(ui.rename);
  ui.buttonMove->setDefaultAction(ui.move);
  ui.buttonPurge->setDefaultAction(ui.purge);
  ui.buttonMount->setDefaultAction(ui.mount);
  ui.buttonStream->setDefaultAction(ui.stream);
  ui.buttonUpload->setDefaultAction(ui.upload);
  ui.buttonDownload->setDefaultAction(ui.download);
  ui.buttonTree->setDefaultAction(ui.getTree);
  ui.buttonLink->setDefaultAction(ui.link);
  ui.buttonSize->setDefaultAction(ui.getSize);
  ui.buttonExport->setDefaultAction(ui.export_);
  ui.upload->setText("Upload");
  ui.download->setText("Download");
  ui.getSize->setText("Size");
  ui.getTree->setText("Tree");
  ui.export_->setText("Export");
  ui.link->setText("Public Link");
  const QList<QToolButton *> browserButtons = {
      ui.buttonRefresh, ui.buttonMkdir, ui.buttonRename, ui.buttonMove,
      ui.buttonPurge,   ui.buttonMount, ui.buttonStream, ui.buttonUpload,
      ui.buttonDownload, ui.buttonSize, ui.buttonTree, ui.buttonLink,
      ui.buttonExport};
  for (QToolButton *button : browserButtons) {
    button->setMinimumHeight(34);
    button->setIconSize(QSize(18, 18));
  }
  UiPolish::SetPrimaryButton(ui.buttonUpload);
  UiPolish::SetPrimaryButton(ui.buttonDownload);
  UiPolish::SetDestructiveButton(ui.buttonPurge);

  auto setActionTooltip = [](QAction *action, const QString &text) {
    action->setToolTip(text);
    action->setStatusTip(text);
  };
  const QString refreshTip = "Reload the selected folder.";
  const QString mkdirTip = "Create a folder in the selected location.";
  const QString renameTip = "Rename the selected file or folder.";
  const QString moveTip =
      "Move the selected file or folder to another remote path.";
  const QString purgeTip = "Delete the selected file or folder.";
  const QString mountTip = "Mount the selected folder locally.";
  const QString streamTip = "Stream the selected file to an external player.";
  const QString uploadTip = "Upload local files or folders to this remote.";
  const QString downloadTip = "Download the selected item locally.";
  const QString sizeTip = "Calculate total size for the selected folder.";
  const QString treeTip = "Show the directory tree for the selected folder.";
  const QString exportTip = "Export a file list for the selected folder.";
  const QString linkTip = "Create a public link when the backend supports it.";
  setActionTooltip(ui.refresh, refreshTip);
  setActionTooltip(ui.mkdir, mkdirTip);
  setActionTooltip(ui.rename, renameTip);
  setActionTooltip(ui.move, moveTip);
  setActionTooltip(ui.purge, purgeTip);
  setActionTooltip(ui.mount, mountTip);
  setActionTooltip(ui.stream, streamTip);
  setActionTooltip(ui.upload, uploadTip);
  setActionTooltip(ui.download, downloadTip);
  setActionTooltip(ui.getSize, sizeTip);
  setActionTooltip(ui.getTree, treeTip);
  setActionTooltip(ui.export_, exportTip);
  setActionTooltip(ui.link, linkTip);
  ui.buttonRefresh->setAccessibleName("Refresh folder");
  ui.buttonMkdir->setAccessibleName("Create folder");
  ui.buttonRename->setAccessibleName("Rename selected item");
  ui.buttonMove->setAccessibleName("Move selected item");
  ui.buttonPurge->setAccessibleName("Delete selected item");
  ui.buttonMount->setAccessibleName("Mount selected folder");
  ui.buttonStream->setAccessibleName("Stream selected file");
  ui.buttonUpload->setAccessibleName("Upload to this remote");
  ui.buttonDownload->setAccessibleName("Download selected item");
  ui.buttonSize->setAccessibleName("Calculate selected size");
  ui.buttonTree->setAccessibleName("Show selected directory tree");
  ui.buttonExport->setAccessibleName("Export selected file list");
  ui.buttonLink->setAccessibleName("Create public link");

  mFileFilter = new QLineEdit(this);
  mFileFilter->setPlaceholderText("Filter files in current folder");
  mFileFilter->setClearButtonEnabled(true);
  mFileFilter->setAccessibleName("Filter files");
  mFileFilter->setVisible(false);
  UiPolish::SetPathField(mFileFilter, "Filter files");
  if (auto *layout =
          qobject_cast<QVBoxLayout *>(ui.tree->parentWidget()->layout())) {
    layout->insertWidget(layout->indexOf(ui.tree), mFileFilter);
  }

  auto *pathTools = new QWidget(this);
  auto *pathToolsLayout = new QHBoxLayout(pathTools);
  pathToolsLayout->setContentsMargins(0, 0, 0, 0);
  pathToolsLayout->setSpacing(4);
  auto *filterFilesButton = new QToolButton(pathTools);
  filterFilesButton->setIcon(style->standardIcon(QStyle::SP_FileDialogContentsView));
  filterFilesButton->setText("Filter");
  filterFilesButton->setCheckable(true);
  filterFilesButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  UiPolish::SetCompactToolButton(
      filterFilesButton, "Show file filter",
      "Show or hide the filter field for the current folder.");
  auto *editPathButton = new QToolButton(pathTools);
  editPathButton->setIcon(style->standardIcon(QStyle::SP_DirLinkIcon));
  editPathButton->setText("Path");
  editPathButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  UiPolish::SetCompactToolButton(
      editPathButton, "Edit current path",
      "Type a loaded remote path directly.");
  pathToolsLayout->addWidget(filterFilesButton);
  pathToolsLayout->addWidget(editPathButton);
  ui.buttonsGrid->addWidget(pathTools, 1, 6);

  ui.tree->sortByColumn(0, Qt::AscendingOrder);
  ui.tree->header()->setSectionsMovable(false);
  ui.tree->header()->setHighlightSections(false);
  ui.tree->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  ItemModel *model = new ItemModel(iconCache, remote, isGooglePhotos, this);
  mModel = model;
  QObject::connect(ui.checkBoxShared, &QCheckBox::toggled, model,
                   &ItemModel::setDriveShared);
  ui.tree->setModel(model);
  QTimer::singleShot(0, ui.tree, SLOT(setFocus()));

  auto applyFileFilter = [this, model]() {
    QString text = mFileFilter->text();
    if (!mFileFilter->isVisible())
      return;
    QModelIndex root = ui.tree->rootIndex();
    int rows = model->rowCount(root);
    for (int i = 0; i < rows; ++i) {
      QModelIndex idx = model->index(i, 0, root);
      bool match =
          text.isEmpty() ||
          model->data(idx, Qt::DisplayRole)
              .toString()
              .contains(text, Qt::CaseInsensitive);
      ui.tree->setRowHidden(i, root, !match);
    }
  };

  QObject::connect(mFileFilter, &QLineEdit::textChanged, this, applyFileFilter);
  QObject::connect(model, &QAbstractItemModel::layoutChanged, this,
                   applyFileFilter);

  QObject::connect(filterFilesButton, &QToolButton::toggled, this,
                   [this](bool checked) {
                     mFileFilter->setVisible(checked);
                     if (checked) {
                       mFileFilter->setFocus(Qt::OtherFocusReason);
                       mFileFilter->selectAll();
                     } else {
                       mFileFilter->clear();
                       ui.tree->setFocus(Qt::OtherFocusReason);
                     }
                   });
  QObject::connect(editPathButton, &QToolButton::clicked, this,
                   [this]() { showPathEditor(); });
  QObject::connect(ui.path, &QLineEdit::returnPressed, this, [this]() {
    const QModelIndex index = findLoadedPath(ui.path->text());
    if (index.isValid()) {
      selectIndex(index);
      hidePathEditor();
    } else {
      QMessageBox::information(
          this, "Path not loaded",
          "That path is not loaded in this tab yet. Use the browser tree to "
          "open it first.");
    }
  });

  // selection helper - actions can fire while nothing is selected, so never
  // call .front() on an empty list
  auto selectedIndex = [=]() -> QModelIndex {
    auto rows = ui.tree->selectionModel()->selectedRows();
    return rows.isEmpty() ? QModelIndex() : rows.front();
  };

  auto confirmUnambiguousDestructiveAction =
      [=](const QModelIndex &index, const QString &operation) -> bool {
    if (!model->hasDuplicateSiblingName(index)) {
      return true;
    }

    const QString name = model->data(index, Qt::DisplayRole).toString();
    const QString path = model->path(index).path();
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(operation);
    box.setText(QString("More than one item named \"%1\" exists in this "
                        "folder.")
                    .arg(name));
    box.setInformativeText(
        QString("rclone path operations address this selection as:\n\n%1:%2\n\n"
                "On remotes that allow duplicate names, such as Google Drive, "
                "that path can match more than the row currently selected.")
            .arg(remote, path));
    QPushButton *continueButton = box.addButton("Continue Anyway",
                                                QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == continueButton;
  };

  // rclone failures used to leave a silently empty folder, which reads as
  // "no files here" - surface them instead
  auto errorShowing = std::make_shared<bool>(false);
  QObject::connect(
      model, &ItemModel::loadFailed, this,
      [=](const QString &path, const QString &error) {
        if (*errorShowing) {
          return;
        }
        *errorShowing = true;

        bool tokenExpired = false;
        static const QStringList tokenPatterns = {
            "token expired", "oauth2: cannot fetch token",
            "invalid_grant", "authError", "unauthorized",
            "403 Forbidden", "401 Unauthorized",
            "token has been revoked", "token has been expired"};
        for (const auto &p : tokenPatterns) {
          if (error.contains(p, Qt::CaseInsensitive)) {
            tokenExpired = true;
            break;
          }
        }

        if (tokenExpired) {
          QMessageBox box(this);
          box.setIcon(QMessageBox::Warning);
          box.setWindowTitle("Authentication expired");
          box.setText(
              QString("The token for \"%1\" appears to have expired.")
                  .arg(remote));
          box.setInformativeText(
              "Reconnect opens a terminal to re-authenticate this remote "
              "with rclone.\n\n" +
              error.left(400));
          auto *reconnect =
              box.addButton("Reconnect", QMessageBox::AcceptRole);
          box.addButton(QMessageBox::Cancel);
          box.exec();
          if (box.clickedButton() == reconnect) {
            emit requestReconnect(remote);
          }
        } else {
          QMessageBox::warning(
              this, "Listing failed",
              QString("rclone could not list \"%1:%2\".\n\n%3")
                  .arg(remote, path, error.left(600)));
        }
        *errorShowing = false;
      },
      Qt::QueuedConnection);

  QObject::connect(model, &QAbstractItemModel::layoutChanged, this, [=]() {
    ui.tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui.tree->resizeColumnToContents(1);
    ui.tree->resizeColumnToContents(2);
  });

  QObject::connect(
      ui.tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
      [=]() {
        auto rows = ui.tree->selectionModel()->selectedRows();
        int count = rows.size();
        const QString selectTip = "Select a file or folder first.";

        for (auto child : findChildren<QAction *>()) {
          child->setDisabled(count == 0);
        }

        if (count == 0) {
          setActionTooltip(ui.refresh, "Select a loaded folder to refresh.");
          setActionTooltip(ui.mkdir, "Select a folder before creating a child folder.");
          setActionTooltip(ui.rename, selectTip);
          setActionTooltip(ui.move, selectTip);
          setActionTooltip(ui.purge, selectTip);
          setActionTooltip(ui.mount, "Select a folder to mount.");
          setActionTooltip(ui.stream, "Select a file to stream.");
          setActionTooltip(ui.upload, "Select a folder before uploading.");
          setActionTooltip(ui.download, selectTip);
          setActionTooltip(ui.getSize, "Select a folder to calculate its size.");
          setActionTooltip(ui.getTree, "Select a folder to show its tree.");
          setActionTooltip(ui.export_, "Select a folder to export a file list.");
          setActionTooltip(ui.link, selectTip);
          showPathMessage(QString());
          return;
        }

        bool multiSelect = (count > 1);
        QModelIndex index = rows.front();

        bool topLevel = model->isTopLevel(index);
        bool isFolder = model->isFolder(index);
        bool loading = model->isLoading(index);
        bool driveShared = false;

        ui.rename->setDisabled(multiSelect || topLevel);
        ui.move->setDisabled(multiSelect || topLevel);
        ui.mount->setDisabled(multiSelect || !isFolder);
        ui.stream->setDisabled(multiSelect || isFolder);
        ui.link->setDisabled(multiSelect);
        ui.getTree->setDisabled(multiSelect || !isFolder);
        ui.export_->setDisabled(multiSelect || !isFolder);

        QDir path;
        if (loading) {
          ui.refresh->setDisabled(true);
          ui.move->setDisabled(true);
          ui.rename->setDisabled(true);
          ui.purge->setDisabled(true);
          ui.mount->setDisabled(true);
          ui.stream->setDisabled(true);
          ui.upload->setDisabled(true);
          ui.download->setDisabled(true);
          ui.checkBoxShared->setDisabled(true);
          path = model->path(model->parent(index));
          showPathMessage("Loading " + displayPath(path.path()));
        } else {
          ui.refresh->setDisabled(false);
          driveShared = ui.checkBoxShared->checkState();
          ui.mkdir->setDisabled(driveShared);
          if (!multiSelect) {
            ui.rename->setDisabled(topLevel || driveShared);
            ui.move->setDisabled(topLevel || driveShared);
          }
          ui.purge->setDisabled(topLevel || driveShared);
          ui.upload->setDisabled(driveShared);

#if defined(Q_OS_WIN32)
          unsigned int result =
              compareVersion(rcloneVersion.toStdString(), "1.50");
          if (result == 2) {
            ui.mount->setDisabled(true);
          } else if (!multiSelect) {
            ui.mount->setDisabled(!isFolder);
          }
#else
#if defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
          ui.mount->setDisabled(true);
#else
          if (!multiSelect) {
            ui.mount->setDisabled(!isFolder);
          }
#endif
#endif

          if (!multiSelect) {
            ui.stream->setDisabled(isFolder);
          }
          ui.checkBoxShared->setDisabled(!isGoogle);
          path = model->path(index);
          if (multiSelect) {
            showPathMessage(QString("%1 items selected").arg(count));
          } else {
            showBreadcrumbForIndex(index);
          }
        }

        if (!multiSelect) {
          ui.getSize->setDisabled(!isFolder);
          ui.getTree->setDisabled(!isFolder);
          ui.export_->setDisabled(!isFolder);
        }

        const QString singleOnly = "Select one item to use this action.";
        const QString folderOnly = "Select a folder to use this action.";
        const QString fileOnly = "Select a file to use this action.";
        const QString rootLocked =
            "This action is not available at the remote root.";
        const QString sharedLocked =
            "Not available while browsing Google Drive Shared with Me.";
        const QString loadingLocked = "Wait for the current listing to finish.";
        auto tipFor = [&](QAction *action, const QString &enabledTip,
                          const QString &disabledTip) {
          setActionTooltip(action, action->isEnabled() ? enabledTip
                                                       : disabledTip);
        };
        const QString renameDisabled =
            loading ? loadingLocked
                    : (multiSelect ? singleOnly
                                   : (topLevel ? rootLocked : sharedLocked));
        const QString moveDisabled = renameDisabled;
        const QString folderDisabled =
            loading ? loadingLocked : (multiSelect ? singleOnly : folderOnly);
        const QString streamDisabled =
            loading ? loadingLocked : (multiSelect ? singleOnly : fileOnly);
        const QString mutableDisabled =
            loading ? loadingLocked : (driveShared ? sharedLocked : rootLocked);
        tipFor(ui.refresh, refreshTip, loadingLocked);
        tipFor(ui.mkdir, mkdirTip, driveShared ? sharedLocked : folderOnly);
        tipFor(ui.rename, renameTip, renameDisabled);
        tipFor(ui.move, moveTip, moveDisabled);
        tipFor(ui.purge, purgeTip, mutableDisabled);
        tipFor(ui.mount, mountTip, folderDisabled);
        tipFor(ui.stream, streamTip, streamDisabled);
        tipFor(ui.upload, uploadTip, driveShared ? sharedLocked : folderOnly);
        tipFor(ui.download, downloadTip, loading ? loadingLocked : selectTip);
        tipFor(ui.getSize, sizeTip, folderDisabled);
        tipFor(ui.getTree, treeTip, folderDisabled);
        tipFor(ui.export_, exportTip, folderDisabled);
        tipFor(ui.link, linkTip,
               loading ? loadingLocked : (multiSelect ? singleOnly : linkTip));
      });

  QObject::connect(ui.refresh, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();


    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }
    model->refresh(index);
  });

  QObject::connect(ui.mkdir, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();


    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }

    if (!model->isFolder(index)) {
      index = index.parent();
    }
    QDir path = model->path(index);
    QString pathMsg =
        isLocal ? QDir::toNativeSeparators(path.path()) : path.path();
    QString name = QInputDialog::getText(
        this, "New Folder", QString("Folder name for:\n%1").arg(pathMsg));
    if (!name.isEmpty()) {
      QString folder = isLocal ? path.filePath(name)
                               : JoinRemotePath(path.path(), name);
      QString folderMsg = isLocal ? QDir::toNativeSeparators(folder) : folder;

      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      process.setArguments(QStringList() << "mkdir" << GetRcloneConf()
                                         << getDriveSharedArgs()
                                         << GetDefaultRcloneOptionsList()
                                         << remote + ":" + folder);
      process.setProcessChannelMode(QProcess::MergedChannels);

      ProgressDialog progress("New Folder", "Creating...", folderMsg, &process,
                              this);
      if (progress.exec() == QDialog::Accepted) {
        model->refresh(index);
      }
    }
  });

  QObject::connect(ui.rename, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();


    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }
    if (!confirmUnambiguousDestructiveAction(index, "Rename")) {
      return;
    }

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QString name = model->data(index, Qt::DisplayRole).toString();
    name = QInputDialog::getText(this, "Rename",
                                 QString("Rename:\n%1\n\nto").arg(pathMsg),
                                 QLineEdit::Normal, name);
    if (!name.isEmpty()) {
      const QString targetPath =
          isLocal ? model->path(index.parent()).filePath(name)
                  : JoinRemotePath(model->path(index.parent()).path(), name);

      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      process.setArguments(
          QStringList() << "moveto" << GetRcloneConf() << getDriveSharedArgs()
                        << GetDefaultRcloneOptionsList() << remote + ":" + path
                        << remote + ":" + targetPath);
      process.setProcessChannelMode(QProcess::MergedChannels);

      ProgressDialog progress("Rename", "Renaming...", pathMsg, &process, this);
      if (progress.exec() == QDialog::Accepted) {
        model->rename(index, name);
      }
    }
  });

  QObject::connect(ui.move, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();


    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }
    if (!confirmUnambiguousDestructiveAction(index, "Move")) {
      return;
    }

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QString name = path;
    name = QInputDialog::getText(
        this, "Move", QString("Move:\n%1\n\nto remote path").arg(pathMsg),
        QLineEdit::Normal, name);
    if (!name.isEmpty()) {
      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      process.setArguments(
          QStringList() << "moveto" << GetRcloneConf() << getDriveSharedArgs()
                        << GetDefaultRcloneOptionsList() << remote + ":" + path
                        << remote + ":" + name);
      process.setProcessChannelMode(QProcess::MergedChannels);

      ProgressDialog progress("Move", "Moving...", pathMsg, &process, this);
      if (progress.exec() == QDialog::Accepted) {
        model->refresh(index);
      }
    }
  });

  QObject::connect(ui.purge, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();

    auto rows = ui.tree->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
      return;
    }

    for (const auto &idx : rows) {
      if (!confirmUnambiguousDestructiveAction(idx, "Delete")) {
        return;
      }
    }

    QString confirmMsg;
    if (rows.size() == 1) {
      QString path = model->path(rows.front()).path();
      QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;
      confirmMsg = QString("Delete %1?").arg(pathMsg);
    } else {
      confirmMsg = QString("Delete %1 items?").arg(rows.size());
    }

    int button = QMessageBox::question(
        this, "Delete",
        confirmMsg +
            "\n\nThis starts rclone delete jobs. Recovery "
            "depends on the remote backend's trash or versioning support.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (button == QMessageBox::Yes) {
      QList<QPersistentModelIndex> persistentRows;
      for (const auto &idx : rows) {
        persistentRows.append(QPersistentModelIndex(idx));
      }

      for (const auto &pidx : persistentRows) {
        if (!pidx.isValid()) {
          continue;
        }
        QModelIndex idx = QModelIndex(pidx);
        QString path = model->path(idx).path();
        QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

        QStringList args;
        args << (model->isFolder(idx) ? "purge" : "delete");
        args << getDriveSharedArgs();
        if (!mFeatures.trashFlag.isEmpty()) {
          args << mFeatures.trashFlag;
        } else if (isGoogle) {
          args << "--drive-use-trash";
        }
        args << GetDefaultRcloneOptionsList();
        args << "--verbose";
        args << "--use-json-log";
        args << "--stats" << "1s";
        args << remote + ":" + path;

        emit addTransfer(QString("Delete %1").arg(pathMsg),
                         remote + ":" + path, QString(), args);
      }

      for (int i = persistentRows.size() - 1; i >= 0; i--) {
        if (persistentRows[i].isValid()) {
          QModelIndex idx = QModelIndex(persistentRows[i]);
          model->removeRow(idx.row(), idx.parent());
        }
      }
    }
  });

  QObject::connect(ui.mount, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();

    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }

    QString path = model->path(index).path();

    QDialog dlg(this);
    dlg.setWindowTitle(QString("Mount %1").arg(remote));
    UiPolish::SetWindowDefaults(&dlg, QSize(460, 220));
    auto *layout = new QFormLayout(&dlg);
    layout->setSpacing(10);
    layout->setContentsMargins(12, 12, 12, 12);

    auto *mountPoint = new QLineEdit(&dlg);
#if defined(Q_OS_WIN32)
    mountPoint->setText(
        settings->value("Settings/lastMountPoint", "Z:").toString());
    mountPoint->setPlaceholderText("Drive letter (Z:) or folder path");
#else
    mountPoint->setText(
        settings->value("Settings/lastMountPoint").toString());
    mountPoint->setPlaceholderText("/mnt/remote");
#endif
    mountPoint->setAccessibleName("Mount point");
    UiPolish::SetPathField(mountPoint, "Mount point");
    layout->addRow("Mount point:", mountPoint);

#if !defined(Q_OS_WIN32)
    auto *browseBtn = new QPushButton("Browse...", &dlg);
    QObject::connect(browseBtn, &QPushButton::clicked, &dlg, [&]() {
      QString dir = QFileDialog::getExistingDirectory(
          &dlg, "Select mount point", mountPoint->text());
      if (!dir.isEmpty())
        mountPoint->setText(dir);
    });
    layout->addRow("", browseBtn);
#endif

    auto *cacheMode = new QComboBox(&dlg);
    cacheMode->addItems({"off", "minimal", "writes", "full"});
    cacheMode->setCurrentText(
        settings->value("Settings/mountCacheMode", "writes").toString());
    cacheMode->setToolTip("--vfs-cache-mode setting for the mount.");
    cacheMode->setAccessibleName("VFS cache mode");
    layout->addRow("Cache mode:", cacheMode);

    auto *readOnly = new QCheckBox("Read-only mount", &dlg);
    readOnly->setChecked(
        settings->value("Settings/mountReadOnly", false).toBool());
    readOnly->setAccessibleName("Read-only mount");
    layout->addRow("", readOnly);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             &dlg);
    UiPolish::SetDialogButtonBox(buttons);
    if (auto ok = buttons->button(QDialogButtonBox::Ok)) {
      UiPolish::SetPrimaryButton(ok);
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg,
                     &QDialog::reject);
    layout->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted || mountPoint->text().isEmpty())
      return;

    QString folder = mountPoint->text();
    settings->setValue("Settings/lastMountPoint", folder);
    settings->setValue("Settings/mountCacheMode", cacheMode->currentText());
    settings->setValue("Settings/mountReadOnly", readOnly->isChecked());
    settings->setValue("Settings/driveShared", ui.checkBoxShared->isChecked());

    QString mountOpts = "--vfs-cache-mode " + cacheMode->currentText();
    if (readOnly->isChecked())
      mountOpts += " --read-only";
    settings->setValue("Settings/mount", mountOpts);

    emit addMount(remote + ":" + path, folder);
  });

  QObject::connect(ui.stream, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();


    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }
    QString path = model->path(index).path();

    bool streamConfirmed =
        settings->value("Settings/streamConfirmed", false).toBool();
    QString stream = settings->value("Settings/stream", "mpv -").toString();
    if (!streamConfirmed) {
      QString result = QInputDialog::getText(
          this, "Stream",
          "Player command. Rclone will stream the file to the command's stdin:",
          QLineEdit::Normal, stream);
      if (result.isEmpty()) {
        return;
      }

      stream = result;

      settings->setValue("Settings/stream", stream);
      settings->setValue("Settings/streamConfirmed", true);
    }

    emit addStream(remote + ":" + path, stream);
  });

  QObject::connect(ui.checkBoxShared, &QCheckBox::toggled, ui.shared,
                   &QAction::toggled);

  QObject::connect(ui.shared, &QAction::toggled, this, [=](const bool checked) {
    ui.checkBoxShared->setChecked(checked);

    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }
    QModelIndex top = index;
    while (!model->isTopLevel(top)) {
      top = top.parent();
    }
    ui.tree->selectionModel()->clear();
    ui.tree->selectionModel()->select(top, QItemSelectionModel::Select |
                                               QItemSelectionModel::Rows);
    model->refresh(top);
  });

  QObject::connect(ui.link, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();


    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QProcess process;
    UseRclonePassword(&process);
    process.setProgram(GetRclone());
    process.setArguments(
        QStringList() << "link" << GetRcloneConf() << getDriveSharedArgs()
                      << GetDefaultRcloneOptionsList() << remote + ":" + path);
    process.setProcessChannelMode(QProcess::MergedChannels);
    ProgressDialog progress("Fetch Public Link", "Fetching link for...",
                            pathMsg, &process, this, false, true);
    progress.expand();
    progress.allowToClose();
    progress.exec();
  });

  QObject::connect(ui.upload, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();


    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }

    if (!model->isFolder(index)) {
      index = index.parent();
    }
    QDir path = model->path(index);

    {auto s = GetSettings(); s->setValue("Settings/driveShared", ui.checkBoxShared->isChecked());}
    TransferDialog t(false, false, remote, path, true, this);
    if (t.exec() == QDialog::Accepted) {
      QString src = t.getSource();
      QString dst = t.getDest();
      QStringList args = t.getOptions();
      QString msg = QString("%1 from %2").arg(t.getMode()).arg(src);
      if (t.wasEnqueued()) {
        emit enqueueTransfer(msg, src, dst, args);
      } else {
        emit addTransfer(msg, src, dst, args);
      }
    }
  });

  QObject::connect(ui.download, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();

    auto rows = ui.tree->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
      return;
    }

    {auto s = GetSettings(); s->setValue("Settings/driveShared", ui.checkBoxShared->isChecked());}

    if (rows.size() > 1) {
      QModelIndex parentIdx = rows.front().parent();
      QDir parentPath = model->path(parentIdx);
      QStringList includeFilters;
      for (const auto &idx : rows) {
        QString name = model->data(idx, Qt::DisplayRole).toString();
        if (!name.isEmpty()) {
          includeFilters << name;
        }
      }
      TransferDialog t(true, false, remote, parentPath, true, this);
      if (t.exec() == QDialog::Accepted) {
        QStringList args = t.getOptions();
        for (const QString &name : includeFilters) {
          args.prepend(name);
          args.prepend("--include");
        }
        QString src = t.getSource();
        QString dst = t.getDest();
        QString msg = QString("%1 %2 (%3 items)").arg(t.getMode(), src).arg(rows.size());
        if (t.wasEnqueued()) {
          emit enqueueTransfer(msg, src, dst, args);
        } else {
          emit addTransfer(msg, src, dst, args);
        }
      }
    } else {
      QModelIndex index = rows.front();
      QDir path = model->path(index);
      TransferDialog t(true, false, remote, path, model->isFolder(index), this);
      if (t.exec() == QDialog::Accepted) {
        QString src = t.getSource();
        QString dst = t.getDest();
        QStringList args = t.getOptions();
        QString msg = QString("%1 %2").arg(t.getMode()).arg(src);
        if (t.wasEnqueued()) {
          emit enqueueTransfer(msg, src, dst, args);
        } else {
          emit addTransfer(msg, src, dst, args);
        }
      }
    }
  });

  QObject::connect(ui.getTree, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();

    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QProcess process;
    UseRclonePassword(&process);
    process.setProgram(GetRclone());
    process.setArguments(
        QStringList() << "tree"
                      << "-d" << GetRcloneConf() << getDriveSharedArgs()
                      << GetDefaultRcloneOptionsList() << remote + ":" + path);
    process.setProcessChannelMode(QProcess::MergedChannels);
    ProgressDialog progress("Show directories tree", "Processing...", pathMsg,
                            &process, this, false);
    progress.expand();
    progress.allowToClose();
    progress.resize(1000, 600);
    progress.exec();
  });

  QObject::connect(ui.getSize, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();

    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QProcess process;
    UseRclonePassword(&process);
    process.setProgram(GetRclone());
    process.setArguments(
        QStringList() << "size" << GetRcloneConf() << getDriveSharedArgs()
                      << GetDefaultRcloneOptionsList() << remote + ":" + path);
    process.setProcessChannelMode(QProcess::MergedChannels);
    ProgressDialog progress("Get Size", "Calculating...", pathMsg, &process,
                            this, false);
    progress.expand();
    progress.allowToClose();
    progress.exec();
  });

  QObject::connect(ui.export_, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();


    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }
    QDir path = model->path(index);
    ExportDialog e(remote, path, this);
    if (e.exec() == QDialog::Accepted) {
      QString dst = e.getDestination();
      bool txt = e.onlyFilenames();

      QSaveFile file(dst);
      if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(
            this, "Error",
            QString("Cannot open file '%1' for writing!").arg(dst));
        return;
      }

      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      process.setArguments(QStringList()
                           << GetRcloneConf() << getDriveSharedArgs()
                           << GetDefaultRcloneOptionsList() << e.getOptions());
      process.setProcessChannelMode(QProcess::MergedChannels);

      ProgressDialog progress("Export", "Exporting...", dst, &process, this);
      QByteArray exportOutput;

      QObject::connect(&progress, &ProgressDialog::outputAvailable, &progress,
                       [&exportOutput](const QString &output) {
                         exportOutput.append(output.toUtf8());
                       });

      if (progress.exec() != QDialog::Accepted ||
          process.exitStatus() != QProcess::NormalExit ||
          process.exitCode() != 0) {
        return;
      }

      QString error;
      const auto format = txt ? ExportListFormat::Text : ExportListFormat::Csv;
      if (!WriteExportListFromLsjson(&file, exportOutput, format, &error)) {
        QMessageBox::warning(this, "Error", error);
        return;
      }
      if (!file.commit()) {
        QMessageBox::warning(
            this, "Error",
            QString("Cannot save file '%1': %2").arg(dst, file.errorString()));
      }
    }
  });

  QObject::connect(
      model, &ItemModel::drop, this,
      [=](const QDir &path, const QModelIndex &parent) {
        activateWindow();
        QDir destPath = model->path(parent);
        QString dest = QFileInfo(path.path()).isDir()
                           ? destPath.filePath(path.dirName())
                           : destPath.path();

        {auto s = GetSettings(); s->setValue("Settings/driveShared", ui.checkBoxShared->isChecked());}
        TransferDialog t(false, true, remote, dest, true, this);
        t.setSource(path.path());

        if (t.exec() == QDialog::Accepted) {
          QString src = t.getSource();
          QString dst = t.getDest();

          QStringList args = t.getOptions();
          emit addTransfer(QString("%1 from %2").arg(t.getMode()).arg(src), src,
                           dst, args);
        }
      });

  ui.tree->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(
      ui.tree, &QWidget::customContextMenuRequested, this,
      [=](const QPoint &pos) {
        QModelIndex index = ui.tree->indexAt(pos);
        if (!index.isValid()) {
          return;
        }
        ui.tree->selectionModel()->select(
            index, QItemSelectionModel::ClearAndSelect |
                       QItemSelectionModel::Rows);

        QMenu menu;
        menu.addAction(ui.refresh);
        menu.addAction(ui.getSize);
        menu.addAction(ui.getTree);
        menu.addAction(ui.export_);
        menu.addSeparator();
        menu.addAction(ui.mkdir);
        menu.addAction(ui.rename);
        menu.addAction(ui.move);
        menu.addAction(ui.purge);
        menu.addSeparator();
        menu.addAction(ui.mount);
        menu.addAction(ui.stream);
        menu.addAction(ui.upload);
        menu.addAction(ui.download);
        menu.addAction(ui.link);

        QAction *editAction = nullptr;
        QAction *archiveAction = nullptr;
        QAction *compareAction = nullptr;
        QAction *speedAction = nullptr;
        QAction *copyUrlAction = nullptr;
        QAction *dedupeAction = nullptr;
        QAction *bookmarkAction = menu.addAction("Bookmark this path");
        bookmarkAction->setToolTip(
            "Save this path as a bookmark for quick access.");
        QAction *copyToRemote = nullptr;
        copyToRemote = menu.addAction("Copy to Remote...");
        copyToRemote->setToolTip(
            "Copy this item directly to another remote without downloading locally.");
        if (model->isFolder(index)) {
          menu.addSeparator();
          compareAction = menu.addAction("Compare Folders...");
          compareAction->setToolTip(
              "Run rclone check --combined against another folder.");
          dedupeAction = menu.addAction("Dedupe...");
          dedupeAction->setToolTip(
              "Find and remove duplicate files in this folder.");
          archiveAction = menu.addAction("Archive...");
          archiveAction->setToolTip(
              "Move files older than a threshold to a dated archive folder.");
          speedAction = menu.addAction("Speed Test...");
          speedAction->setToolTip(
              "Run upload/download speed probes against this remote.");
          copyUrlAction = menu.addAction("Download URL here...");
          copyUrlAction->setToolTip(
              "Download a file from a URL directly to this remote folder.");
        }

        QAction *serveAction = nullptr;
        if (model->isFolder(index)) {
          serveAction = menu.addAction("Serve...");
          serveAction->setToolTip(
              "Serve this folder over HTTP, WebDAV, FTP, or other protocols.");
        }

        QAction *propsAction = nullptr;
        QAction *previewAction = nullptr;
        QAction *versionsAction = nullptr;
        if (!model->isFolder(index)) {
          menu.addSeparator();
          editAction = menu.addAction("Open/Edit...");
          editAction->setToolTip(
              "Download, open with the default app, and re-upload on save.");
          previewAction = menu.addAction("Preview...");
          previewAction->setToolTip(
              "Download and preview this file inline (images, text, audio, video up to 50 MB).");
          propsAction = menu.addAction("Properties...");
          propsAction->setToolTip(
              "Show file size, modification time, and available hashes.");
          versionsAction = menu.addAction("Versions...");
          versionsAction->setToolTip(
              "Browse prior versions of this file (requires backend versioning support).");
        }

        QAction *chosen = menu.exec(ui.tree->viewport()->mapToGlobal(pos));
        if (!chosen || (chosen != editAction && chosen != compareAction &&
                        chosen != archiveAction && chosen != speedAction &&
                        chosen != copyUrlAction && chosen != dedupeAction &&
                        chosen != copyToRemote && chosen != serveAction &&
                        chosen != propsAction && chosen != bookmarkAction &&
                        chosen != previewAction && chosen != versionsAction)) {
          return;
        }

        QString path = model->path(index).path();
        QString target = remote + ":" + path;

        if (chosen == bookmarkAction) {
          auto settings = GetSettings();
          QStringList bookmarks =
              settings->value("Settings/bookmarks").toStringList();
          if (!bookmarks.contains(target)) {
            bookmarks.append(target);
            settings->setValue("Settings/bookmarks", bookmarks);
          }
        } else if (chosen == editAction) {
          auto *session = new RemoteEditSession(
              target, QFileInfo(path).fileName(), getDriveSharedArgs(), this,
              this);
          if (!session->downloadAndOpen()) {
            session->deleteLater();
          }
        } else if (chosen == compareAction) {
          auto compareSettings = GetSettings();
          QString lastCompare =
              compareSettings->value("Settings/lastCompareTarget").toString();
          FolderCompareDialog dialog(
              target, lastCompare, getDriveSharedArgs(),
              [this](const QString &message, const QString &source,
                     const QString &dest, const QStringList &args) {
                emit addTransfer(message, source, dest, args);
              },
              this);
          dialog.exec();
          if (!dialog.destinationPath().isEmpty()) {
            compareSettings->setValue("Settings/lastCompareTarget",
                                      dialog.destinationPath());
          }
        } else if (chosen == archiveAction) {
          bool ok;
          QString age = QInputDialog::getText(
              this, "Archive", "Move files older than:", QLineEdit::Normal,
              "30d", &ok);
          if (!ok || age.trimmed().isEmpty()) {
            return;
          }
          QProcess process;
          UseRclonePassword(&process);
          process.setProgram(GetRclone());
          process.setArguments(QStringList()
                               << "archive" << GetRcloneConf()
                               << "--min-age" << age.trimmed()
                               << GetDefaultRcloneOptionsList() << target);
          process.setProcessChannelMode(QProcess::MergedChannels);
          ProgressDialog progress("Archive", "Archiving...", target, &process,
                                  this, false);
          progress.expand();
          progress.allowToClose();
          progress.exec();
        } else if (chosen == speedAction) {
          QProcess process;
          UseRclonePassword(&process);
          process.setProgram(GetRclone());
          process.setArguments(QStringList()
                               << "test" << "speed" << GetRcloneConf()
                               << GetDefaultRcloneOptionsList() << target);
          process.setProcessChannelMode(QProcess::MergedChannels);
          ProgressDialog progress("Speed Test", "Testing...", target, &process,
                                  this, false);
          progress.expand();
          progress.allowToClose();
          progress.exec();
        } else if (chosen == copyUrlAction) {
          bool ok;
          QString url = QInputDialog::getText(
              this, "Download URL",
              "HTTP/HTTPS URL to download to this remote folder:",
              QLineEdit::Normal, QString(), &ok);
          if (!ok || url.trimmed().isEmpty()) {
            return;
          }
          QUrl parsed(url.trimmed());
          if (parsed.scheme() != "http" && parsed.scheme() != "https") {
            QMessageBox::warning(
                this, "Invalid URL",
                "Only http:// and https:// URLs are supported.");
            return;
          }
          QProcess process;
          UseRclonePassword(&process);
          process.setProgram(GetRclone());
          process.setArguments(QStringList()
                               << "copyurl" << GetRcloneConf()
                               << url.trimmed() << target
                               << GetDefaultRcloneOptionsList());
          process.setProcessChannelMode(QProcess::MergedChannels);
          ProgressDialog progress("Download URL", "Downloading...",
                                  url.trimmed() + " -> " + target, &process,
                                  this, false);
          progress.expand();
          progress.allowToClose();
          progress.exec();
          if (process.exitCode() == 0) {
            refreshCurrentDir();
          }
        } else if (chosen == dedupeAction) {
          QStringList modes;
          modes << "interactive" << "skip" << "first" << "newest" << "oldest"
                << "largest" << "smallest" << "rename";
          bool ok;
          QString mode = QInputDialog::getItem(
              this, "Dedupe",
              "Dedupe mode (how to resolve duplicates):", modes, 1,
              false, &ok);
          if (!ok) {
            return;
          }
          QProcess process;
          UseRclonePassword(&process);
          process.setProgram(GetRclone());
          process.setArguments(QStringList()
                               << "dedupe" << GetRcloneConf() << "--dedupe-mode"
                               << mode << getDriveSharedArgs()
                               << GetDefaultRcloneOptionsList() << "--verbose"
                               << target);
          process.setProcessChannelMode(QProcess::MergedChannels);
          ProgressDialog progress("Dedupe", "Deduplicating...", target,
                                  &process, this, false);
          progress.expand();
          progress.allowToClose();
          progress.exec();
          if (process.exitCode() == 0) {
            refreshCurrentDir();
          }
        } else if (chosen == copyToRemote) {
          auto copySettings = GetSettings();
          QString lastDest =
              copySettings->value("Settings/lastRemoteToRemoteDest").toString();
          bool ok;
          QString dest = QInputDialog::getText(
              this, "Copy to Remote",
              "Destination remote:path (e.g. backupremote:folder):",
              QLineEdit::Normal, lastDest, &ok);
          if (!ok || dest.trimmed().isEmpty()) {
            return;
          }
          copySettings->setValue("Settings/lastRemoteToRemoteDest",
                                dest.trimmed());
          QStringList args;
          args << "copy" << getDriveSharedArgs()
               << GetDefaultRcloneOptionsList() << "--verbose"
               << "--use-json-log" << "--stats" << "1s"
               << "--stats-file-name-length" << "0"
               << target << dest.trimmed();
          emit addTransfer(
              QString("Copy %1 -> %2").arg(target, dest.trimmed()),
              target, dest.trimmed(), args);
        } else if (chosen == propsAction) {
          QProcess proc;
          UseRclonePassword(&proc);
          proc.setProgram(GetRclone());
          proc.setArguments(QStringList()
                            << "lsjson" << GetRcloneConf() << "--stat"
                            << "--hash" << getDriveSharedArgs() << "-M"
                            << target);
          proc.setProcessChannelMode(QProcess::SeparateChannels);
          proc.start();
          proc.waitForFinished(15000);

          QStringList lines;
          lines << QString("<b>%1</b>").arg(QFileInfo(path).fileName());

          if (proc.exitCode() == 0) {
            QJsonDocument doc =
                QJsonDocument::fromJson(proc.readAllStandardOutput());
            QJsonArray arr = doc.array();
            if (!arr.isEmpty()) {
              QJsonObject obj = arr.first().toObject();
              qint64 size = obj.value("Size").toVariant().toLongLong();
              lines << "Size: " +
                        GetNiceSize(static_cast<quint64>(size)) +
                        QString(" (%L1 bytes)").arg(size);
              QString modTime = obj.value("ModTime").toString();
              if (!modTime.isEmpty()) {
                QDateTime dt =
                    QDateTime::fromString(modTime, Qt::ISODateWithMs);
                if (dt.isValid())
                  lines << "Modified: " +
                              dt.toLocalTime().toString(
                                  "yyyy-MM-dd HH:mm:ss");
                else
                  lines << "Modified: " + modTime;
              }
              QString mimeType = obj.value("MimeType").toString();
              if (!mimeType.isEmpty())
                lines << "Type: " + mimeType;
              QJsonObject hashes = obj.value("Hashes").toObject();
              for (auto it = hashes.begin(); it != hashes.end(); ++it) {
                lines << it.key() + ": " + it.value().toString();
              }
              QJsonObject meta = obj.value("Metadata").toObject();
              if (!meta.isEmpty()) {
                lines << "<br><b>Metadata</b>";
                for (auto it = meta.begin(); it != meta.end(); ++it) {
                  lines << it.key() + ": " + it.value().toString();
                }
              }
            }
          } else {
            lines << "Could not retrieve properties.";
            QString err =
                QString::fromUtf8(proc.readAll()).trimmed();
            if (!err.isEmpty())
              lines << err.left(300);
          }

          QMessageBox::information(this, "Properties", lines.join("<br>"));
        } else if (chosen == serveAction) {
          QStringList protocols;
          protocols << "http" << "webdav" << "ftp" << "dlna" << "s3" << "nfs";
          bool ok;
          QString protocol = QInputDialog::getItem(
              this, "Serve", "Protocol:", protocols, 0, false, &ok);
          if (!ok)
            return;
          QString addrDefault = (protocol == "dlna") ? "" : ":8080";
          QString addr = QInputDialog::getText(
              this, "Serve",
              QString("Listen address for %1 serve:").arg(protocol),
              QLineEdit::Normal, addrDefault, &ok);
          if (!ok)
            return;
          QStringList args;
          args << "serve" << protocol << GetRcloneConf();
          if (!addr.trimmed().isEmpty()) {
            args << "--addr" << addr.trimmed();
          }
          args << getDriveSharedArgs() << GetDefaultRcloneOptionsList()
               << target;
          QProcess *proc = new QProcess(this);
          proc->setProcessChannelMode(QProcess::MergedChannels);
          UseRclonePassword(proc);
          proc->start(GetRclone(), args, QIODevice::ReadOnly);
          QString url =
              (protocol == "http" || protocol == "webdav")
                  ? QString("http://localhost%1").arg(
                        addr.trimmed().isEmpty() ? ":8080" : addr.trimmed())
                  : QString();

          auto *widget = new StreamWidget(proc, new QProcess(this), target,
                                          "rclone serve " + protocol);
          auto *line = new QFrame();
          line->setFrameShape(QFrame::HLine);
          line->setFrameShadow(QFrame::Sunken);
          QObject::connect(widget, &StreamWidget::closed, this, [=]() {
            proc->terminate();
            if (!proc->waitForFinished(3000))
              proc->kill();
          });
          if (!url.isEmpty()) {
            QMessageBox::information(
                this, "Serve",
                QString("Serving %1 via %2\n\n%3\n\nThe serve runs in the "
                        "Jobs tab until stopped.")
                    .arg(target, protocol, url));
          }
        } else if (chosen == previewAction) {
          Item *item = model->get(index);
          if (!item)
            return;
          quint64 size = item->size;
          constexpr quint64 kMaxPreviewBytes = 50 * 1024 * 1024;
          if (size > kMaxPreviewBytes) {
            QMessageBox::information(
                this, "Preview",
                QString("File is too large for preview (%1).\nMaximum: 50 MB.")
                    .arg(GetNiceSize(size)));
            return;
          }

          QString name = QFileInfo(path).fileName();
          QString suffix = QFileInfo(name).suffix().toLower();
          static const QSet<QString> textExts = {
              "txt", "log", "csv", "json", "xml", "yaml", "yml",
              "ini", "cfg", "conf", "md", "rst", "html", "htm",
              "css", "js", "ts", "py", "c", "cpp", "h", "hpp",
              "java", "sh", "bat", "ps1", "toml"};
          static const QSet<QString> imageExts = {
              "jpg", "jpeg", "png", "gif", "bmp", "webp", "svg", "ico"};
          static const QSet<QString> audioExts = {
              "mp3", "wav", "flac", "ogg", "aac", "m4a"};
          static const QSet<QString> videoExts = {
              "mp4", "mkv", "avi", "mov", "webm"};

          bool isText = textExts.contains(suffix);
          bool isImage = imageExts.contains(suffix);
          bool isMedia = audioExts.contains(suffix) || videoExts.contains(suffix);

          if (!isText && !isImage && !isMedia && size > 0) {
            QMessageBox::information(
                this, "Preview",
                QString("No preview available for .%1 files.\n"
                        "Supported: text, images, audio, video.")
                    .arg(suffix));
            return;
          }

          QString tempDir = QDir::tempPath() + "/rclonebrowserng-preview";
          QDir().mkpath(tempDir);
          QString safeName = QFileInfo(name).fileName();
          if (safeName.isEmpty())
            safeName = "preview";
          QString tempFile = tempDir + "/" + safeName;

          auto *proc = new QProcess(this);
          UseRclonePassword(proc);
          proc->setProgram(GetRclone());
          proc->setArguments(QStringList()
                            << "copyto" << GetRcloneConf()
                            << getDriveSharedArgs()
                            << target
                            << tempFile);
          proc->setProcessChannelMode(QProcess::SeparateChannels);

          QObject::connect(
              proc,
              static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                  &QProcess::finished),
              this, [this, proc, tempFile, name, isText, isImage](
                        int code, QProcess::ExitStatus) {
                proc->deleteLater();
                if (code != 0) {
                  QString err =
                      QString::fromUtf8(proc->readAllStandardError()).trimmed();
                  QMessageBox::warning(this, "Preview",
                                       "Could not download file:\n" +
                                           err.left(500));
                  QFile::remove(tempFile);
                  return;
                }

                QFileInfo fi(tempFile);
                if (fi.size() == 0) {
                  QMessageBox::information(this, "Preview",
                                           "File is empty (0 bytes).");
                  QFile::remove(tempFile);
                  return;
                }

                QDialog dlg(this);
                dlg.setWindowTitle(QString("Preview: %1").arg(name));
                dlg.resize(800, 600);
                UiPolish::SetWindowDefaults(&dlg, QSize(600, 400));
                auto *layout = new QVBoxLayout(&dlg);

                if (isImage) {
                  auto *label = new QLabel(&dlg);
                  QPixmap pix(tempFile);
                  if (!pix.isNull()) {
                    label->setPixmap(
                        pix.scaled(780, 560, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation));
                  } else {
                    label->setText("Could not load image.");
                  }
                  label->setAlignment(Qt::AlignCenter);
                  layout->addWidget(label);
                } else if (isText) {
                  auto *text = new QPlainTextEdit(&dlg);
                  text->setReadOnly(true);
                  QFile f(tempFile);
                  if (f.open(QIODevice::ReadOnly)) {
                    text->setPlainText(
                        QString::fromUtf8(f.read(2 * 1024 * 1024)));
                  }
                  UiPolish::SetOutputView(text, "File preview");
                  layout->addWidget(text);
                } else {
                  auto *label = new QLabel(&dlg);
                  label->setText(
                      QString("Media file downloaded to:\n%1\n\n"
                              "Use your system media player to play.")
                          .arg(tempFile));
                  label->setTextInteractionFlags(
                      Qt::TextSelectableByMouse);
                  layout->addWidget(label);
                  QDesktopServices::openUrl(
                      QUrl::fromLocalFile(tempFile));
                }

                auto *close = new QPushButton("Close", &dlg);
                QObject::connect(close, &QPushButton::clicked, &dlg,
                                 &QDialog::accept);
                layout->addWidget(close);
                dlg.exec();

                QFile::remove(tempFile);
              });
          QTimer::singleShot(60000, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning)
              proc->kill();
          });
          proc->start();
        } else if (chosen == versionsAction) {
          auto *proc = new QProcess(this);
          UseRclonePassword(proc);
          proc->setProgram(GetRclone());
          proc->setArguments(QStringList()
                            << "lsjson" << GetRcloneConf()
                            << getDriveSharedArgs()
                            << "--versions" << "--no-mimetype"
                            << target);
          proc->setProcessChannelMode(QProcess::SeparateChannels);
          QObject::connect(
              proc,
              static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                  &QProcess::finished),
              this, [this, proc, remote, path, target](int code, QProcess::ExitStatus) {
                proc->deleteLater();
                if (code != 0) {
                  QString err =
                      QString::fromUtf8(proc->readAllStandardError()).trimmed();
                  if (err.contains("not supported") || err.contains("unknown flag")) {
                    QMessageBox::information(
                        this, "Versions",
                        "This backend does not support file versioning.");
                  } else {
                    QMessageBox::warning(
                        this, "Versions",
                        "Could not list versions:\n" + err.left(500));
                  }
                  return;
                }

                QJsonDocument doc =
                    QJsonDocument::fromJson(proc->readAllStandardOutput());
                QJsonArray arr = doc.array();
                if (arr.isEmpty()) {
                  QMessageBox::information(this, "Versions",
                                           "No versions found for this file.");
                  return;
                }

                QDialog dlg(this);
                dlg.setWindowTitle(
                    QString("Versions: %1").arg(QFileInfo(path).fileName()));
                dlg.resize(700, 400);
                UiPolish::SetWindowDefaults(&dlg, QSize(560, 320));
                auto *layout = new QVBoxLayout(&dlg);

                auto *table = new QTableWidget(&dlg);
                table->setColumnCount(4);
                table->setHorizontalHeaderLabels(
                    QStringList() << "Modified" << "Size" << "Version ID" << "");
                UiPolish::SetTableView(table, "File versions");
                table->setEditTriggers(QAbstractItemView::NoEditTriggers);
                table->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::Stretch);
                table->setRowCount(arr.size());

                for (int i = 0; i < arr.size(); ++i) {
                  QJsonObject obj = arr[i].toObject();
                  QString modTime = obj.value("ModTime").toString();
                  QDateTime dt =
                      QDateTime::fromString(modTime, Qt::ISODateWithMs);
                  QString displayTime = dt.isValid()
                      ? dt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss")
                      : modTime;
                  qint64 size = obj.value("Size").toVariant().toLongLong();
                  QString versionId = obj.value("ID").toString();
                  if (versionId.isEmpty())
                    versionId = obj.value("VersionID").toString();

                  table->setItem(i, 0, new QTableWidgetItem(displayTime));
                  table->setItem(i, 1, new QTableWidgetItem(
                      GetNiceSize(static_cast<quint64>(size))));
                  table->setItem(i, 2, new QTableWidgetItem(
                      versionId.isEmpty() ? "(current)" : versionId));

                  auto *restoreBtn = new QPushButton(
                      i == 0 ? "Current" : "Restore", &dlg);
                  restoreBtn->setEnabled(i > 0);
                  table->setCellWidget(i, 3, restoreBtn);

                  QString vPath = obj.value("Path").toString();
                  if (vPath.isEmpty())
                    vPath = path;
                  QObject::connect(restoreBtn, &QPushButton::clicked, &dlg,
                      [this, remote, vPath, target, &dlg]() {
                        QString src = remote + ":" + vPath;
                        QStringList args;
                        args << "copyto" << "--verbose" << "--use-json-log"
                             << "--stats" << "1s"
                             << GetDefaultRcloneOptionsList()
                             << src << target;
                        emit addTransfer(
                            QString("Restore %1").arg(vPath), src, target, args);
                        dlg.accept();
                      });
                }

                layout->addWidget(table);

                auto *hint = new QLabel(
                    QString("%1 version(s). Select Restore to copy an older "
                            "version back to the current path.")
                        .arg(arr.size()),
                    &dlg);
                UiPolish::SetMuted(hint);
                layout->addWidget(hint);

                auto *close = new QPushButton("Close", &dlg);
                QObject::connect(close, &QPushButton::clicked, &dlg,
                                 &QDialog::accept);
                layout->addWidget(close);
                dlg.exec();
              });
          QTimer::singleShot(30000, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning)
              proc->kill();
          });
          proc->start();
        }
      });

  if (isLocal) {
    QHash<QString, QPersistentModelIndex> drives;

    // QDir::drives is fast
    for (const auto &drive : QDir::drives()) {
      QString path = drive.path();
      QModelIndex index = model->addRoot(QDir::toNativeSeparators(path), path);
      drives.insert(path, index);
    }

#if !(defined Q_OS_WIN)
    QThread *thread = new QThread(this);
    thread->start();

    QObject *worker = new QObject();
    worker->moveToThread(thread);

    QTimer::singleShot(0, worker, [=]() {
      QStorageInfo info;
      info.refresh();

      // QStorageInfo::mountedVolumes is slow :(
      for (const auto &volume : info.mountedVolumes()) {
        QString name = volume.name();
        if (!name.isEmpty()) {
          QString path = volume.rootPath();
          QString item =
              QString("%1 (%2)").arg(QDir::toNativeSeparators(path)).arg(name);
          QTimer::singleShot(0, this,
                             [=]() { model->rename(drives[path], item); });
        }
      }

      thread->quit();
      thread->deleteLater();
      worker->deleteLater();
    });
#endif

    ui.tree->selectionModel()->selectionChanged(QItemSelection(),
                                                QItemSelection());
  } else {
    QModelIndex index = model->addRoot("/", root);
    ui.tree->selectionModel()->select(
        index, QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    ui.tree->expand(index);
  }

}

void RemoteWidget::refreshCurrentDir() { ui.refresh->trigger(); }

void RemoteWidget::focusPathBar() { showPathEditor(); }

bool RemoteWidget::eventFilter(QObject *obj, QEvent *event) {
  if (obj == mBreadcrumbBar && event->type() == QEvent::MouseButtonPress) {
    showPathEditor();
    return true;
  }

  if (obj == ui.path && event->type() == QEvent::KeyPress) {
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape) {
      hidePathEditor();
      return true;
    }
  }

  if (event->type() == QEvent::KeyPress) {
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->modifiers() == Qt::AltModifier) {
      if (key->key() == Qt::Key_Left) {
        goBack();
        return true;
      }
      if (key->key() == Qt::Key_Right) {
        goForward();
        return true;
      }
    }
  }

  return QWidget::eventFilter(obj, event);
}

void RemoteWidget::showBreadcrumbForIndex(const QModelIndex &index) {
  if (!index.isValid() || !mModel || !mBreadcrumbLayout) {
    showPathMessage(QString());
    return;
  }

  while (QLayoutItem *item = mBreadcrumbLayout->takeAt(0)) {
    if (QWidget *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }

  QVector<QPersistentModelIndex> chain;
  QModelIndex current = index;
  while (current.isValid()) {
    chain.prepend(QPersistentModelIndex(current));
    current = mModel->parent(current);
  }

  for (int i = 0; i < chain.size(); ++i) {
    if (i > 0) {
      auto *separator = new QLabel("/", mBreadcrumbBar);
      separator->setAlignment(Qt::AlignCenter);
      mBreadcrumbLayout->addWidget(separator);
    }

    QPersistentModelIndex persistent = chain.at(i);
    auto *button = new QToolButton(mBreadcrumbBar);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setAutoRaise(true);
    QString text = mModel->data(persistent, Qt::DisplayRole).toString();
    if (text.isEmpty()) {
      text = "/";
    }
    button->setText(text);
    button->setToolTip(displayPath(mModel->path(persistent).path()));

    auto *menu = new QMenu(button);
    QModelIndex parentIndex = mModel->parent(persistent);
    const int rows = mModel->rowCount(parentIndex);
    for (int row = 0; row < rows; ++row) {
      QModelIndex sibling = mModel->index(row, 0, parentIndex);
      if (!sibling.isValid() || !mModel->isFolder(sibling)) {
        continue;
      }
      QPersistentModelIndex siblingIndex(sibling);
      QAction *action =
          menu->addAction(mModel->data(sibling, Qt::DisplayRole).toString());
      QObject::connect(action, &QAction::triggered, this,
                       [this, siblingIndex]() {
                         selectIndex(QModelIndex(siblingIndex));
                       });
    }
    if (!menu->isEmpty()) {
      button->setMenu(menu);
      button->setPopupMode(QToolButton::MenuButtonPopup);
    }

    QObject::connect(button, &QToolButton::clicked, this,
                     [this, persistent]() {
                       selectIndex(QModelIndex(persistent));
                     });
    mBreadcrumbLayout->addWidget(button);
  }
  mBreadcrumbLayout->addStretch(1);

  ui.path->setReadOnly(true);
  ui.path->hide();
  mBreadcrumbBar->show();
}

void RemoteWidget::showPathMessage(const QString &message) {
  if (mBreadcrumbBar) {
    mBreadcrumbBar->hide();
  }
  ui.path->setReadOnly(true);
  ui.path->setText(message);
  ui.path->show();
}

void RemoteWidget::showPathEditor(const QString &text) {
  QString value = text;
  if (value.isEmpty() && mModel) {
    const QModelIndexList rows = ui.tree->selectionModel()->selectedRows();
    if (!rows.isEmpty()) {
      value = displayPath(mModel->path(rows.front()).path());
    } else {
      value = ui.path->text();
    }
  }

  if (mBreadcrumbBar) {
    mBreadcrumbBar->hide();
  }
  ui.path->setReadOnly(false);
  ui.path->setText(value);
  ui.path->show();
  ui.path->setFocus(Qt::ShortcutFocusReason);
  ui.path->selectAll();
}

void RemoteWidget::hidePathEditor() {
  ui.path->setReadOnly(true);
  const QModelIndexList rows = ui.tree->selectionModel()->selectedRows();
  if (rows.size() == 1) {
    showBreadcrumbForIndex(rows.front());
  } else if (rows.size() > 1) {
    showPathMessage(QString("%1 items selected").arg(rows.size()));
  } else {
    showPathMessage(QString());
  }
}

void RemoteWidget::selectIndex(const QModelIndex &index) {
  if (!index.isValid()) {
    return;
  }
  ui.tree->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect |
                                               QItemSelectionModel::Rows);
  ui.tree->setCurrentIndex(index);
  ui.tree->scrollTo(index);
  if (mModel && mModel->isFolder(index)) {
    ui.tree->expand(index);
  }
  if (!mNavInProgress) {
    pushNavHistory(index);
  }
}

void RemoteWidget::navigateTo(const QModelIndex &index) {
  mNavInProgress = true;
  selectIndex(index);
  mNavInProgress = false;
}

void RemoteWidget::pushNavHistory(const QModelIndex &index) {
  if (!index.isValid())
    return;
  if (mNavPos >= 0 && mNavPos < mNavHistory.size() &&
      mNavHistory[mNavPos] == index)
    return;
  while (mNavHistory.size() > mNavPos + 1)
    mNavHistory.removeLast();
  mNavHistory.append(QPersistentModelIndex(index));
  mNavPos = mNavHistory.size() - 1;
  updateNavButtons();
}

void RemoteWidget::goBack() {
  if (mNavPos <= 0)
    return;
  --mNavPos;
  if (mNavHistory[mNavPos].isValid()) {
    navigateTo(QModelIndex(mNavHistory[mNavPos]));
  }
  updateNavButtons();
}

void RemoteWidget::goForward() {
  if (mNavPos >= mNavHistory.size() - 1)
    return;
  ++mNavPos;
  if (mNavHistory[mNavPos].isValid()) {
    navigateTo(QModelIndex(mNavHistory[mNavPos]));
  }
  updateNavButtons();
}

void RemoteWidget::updateNavButtons() {
  if (mBackButton)
    mBackButton->setEnabled(mNavPos > 0);
  if (mForwardButton)
    mForwardButton->setEnabled(mNavPos < mNavHistory.size() - 1);
}

QModelIndex RemoteWidget::findLoadedPath(const QString &path) const {
  if (!mModel) {
    return QModelIndex();
  }

  QString target = QDir::fromNativeSeparators(path.trimmed());
  if (!mIsLocal && target == "/") {
    target.clear();
  }

  std::function<QModelIndex(const QModelIndex &)> search =
      [&](const QModelIndex &parent) -> QModelIndex {
    const int rows = mModel->rowCount(parent);
    for (int row = 0; row < rows; ++row) {
      QModelIndex child = mModel->index(row, 0, parent);
      if (!child.isValid()) {
        continue;
      }
      if (QDir::fromNativeSeparators(mModel->path(child).path()) == target) {
        return child;
      }
      if (mModel->isFolder(child)) {
        QModelIndex found = search(child);
        if (found.isValid()) {
          return found;
        }
      }
    }
    return QModelIndex();
  };

  return search(QModelIndex());
}

QString RemoteWidget::displayPath(const QString &path) const {
  if (mIsLocal) {
    return QDir::toNativeSeparators(path);
  }
  return path.isEmpty() ? "/" : path;
}

RemoteWidget::~RemoteWidget() {}

void RemoteWidget::applyBackendFeatures(const BackendFeatures &features) {
  mFeatures = features;

  if (!features.publicLink) {
    ui.link->setEnabled(false);
    ui.link->setToolTip("This backend does not support public links.");
    ui.link->setStatusTip(ui.link->toolTip());
  }
  if (!features.about) {
    ui.getSize->setToolTip(
        ui.getSize->toolTip() +
        " (Quota unavailable for this backend.)");
  }
  if (mTrashButton && (features.trashSupported || features.cleanUp)) {
    mTrashButton->show();
    if (features.cleanUp && !features.trashSupported) {
      mTrashButton->setText("Clean Up");
      mTrashButton->setToolTip(
          "Clean up old versions and backend garbage (rclone cleanup).");
    }
  }
}
