#include "remote_widget.h"
#include "export_list_writer.h"
#include "export_dialog.h"
#include "folder_compare.h"
#include "icon_cache.h"
#include "item_model.h"
#include "list_of_job_options.h"
#include "progress_dialog.h"
#include "remote_path.h"
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
  }
  ui.path->installEventFilter(this);
  ui.path->hide();
  UiPolish::SetNavigationView(ui.tree, "Remote file browser");
  ui.tree->setRootIsDecorated(true);
  ui.tree->setIndentation(18);
  ui.tree->setSelectionMode(QAbstractItemView::ExtendedSelection);

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
  // hide checkBoxShared for non Google remotes
  if (!isGoogle) {
    ui.checkBoxShared->hide();
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

  ui.refresh->setToolTip("Reload the selected folder.");
  ui.mkdir->setToolTip("Create a folder in the selected location.");
  ui.rename->setToolTip("Rename the selected file or folder.");
  ui.move->setToolTip("Move the selected file or folder to another remote path.");
  ui.purge->setToolTip("Delete the selected file or folder.");
  ui.mount->setToolTip("Mount the selected folder locally.");
  ui.stream->setToolTip("Stream the selected file to an external player.");
  ui.upload->setToolTip("Upload local files or folders to this remote.");
  ui.download->setToolTip("Download the selected item locally.");
  ui.getSize->setToolTip("Calculate total size for the selected folder.");
  ui.getTree->setToolTip("Show the directory tree for the selected folder.");
  ui.export_->setToolTip("Export a file list for the selected folder.");
  ui.link->setToolTip("Create a public link when the backend supports it.");
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
  mFileFilter->setPlaceholderText("Filter files in current folder...");
  mFileFilter->setClearButtonEnabled(true);
  mFileFilter->setAccessibleName("Filter files");
  mFileFilter->setVisible(false);
  UiPolish::SetPathField(mFileFilter, "Filter files");
  if (auto *layout =
          qobject_cast<QVBoxLayout *>(ui.tree->parentWidget()->layout())) {
    layout->insertWidget(layout->indexOf(ui.tree), mFileFilter);
  }

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

  QObject::connect(mFileFilter, &QLineEdit::textChanged, this,
                   [this, model](const QString &text) {
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
                   });

  QAction *filterAction = new QAction(this);
  filterAction->setShortcut(QKeySequence("Ctrl+F"));
  filterAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  addAction(filterAction);
  QObject::connect(filterAction, &QAction::triggered, this, [this]() {
    mFileFilter->setVisible(true);
    mFileFilter->setFocus(Qt::ShortcutFocusReason);
    mFileFilter->selectAll();
  });

  QAction *editPathAction = new QAction(this);
  editPathAction->setShortcut(QKeySequence("Ctrl+L"));
  editPathAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  addAction(editPathAction);
  QObject::connect(editPathAction, &QAction::triggered, this,
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

        for (auto child : findChildren<QAction *>()) {
          child->setDisabled(count == 0);
        }

        if (count == 0) {
          showPathMessage(QString());
          return;
        }

        bool multiSelect = (count > 1);
        QModelIndex index = rows.front();

        bool topLevel = model->isTopLevel(index);
        bool isFolder = model->isFolder(index);

        ui.rename->setDisabled(multiSelect || topLevel);
        ui.move->setDisabled(multiSelect || topLevel);
        ui.mount->setDisabled(multiSelect || !isFolder);
        ui.stream->setDisabled(multiSelect || isFolder);
        ui.link->setDisabled(multiSelect);
        ui.getTree->setDisabled(multiSelect || !isFolder);
        ui.export_->setDisabled(multiSelect || !isFolder);

        QDir path;
        if (model->isLoading(index)) {
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
          bool driveShared = ui.checkBoxShared->checkState();
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
        if (isGoogle) {
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
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

#if defined(Q_OS_WIN32)
    QString lastMount = settings->value("Settings/lastMountPoint", "Z:").toString();
    QString folder =
        QInputDialog::getText(this, "Mount",
                              QString("Drive letter or mount point for %1")
                                  .arg(remote),
                              QLineEdit::Normal, lastMount);
#else
    QString lastMount = settings->value("Settings/lastMountPoint").toString();
    QString folder = QFileDialog::getExistingDirectory(
        this, QString("Mount %1").arg(remote), lastMount);
#endif

    if (!folder.isEmpty()) {
      settings->setValue("Settings/lastMountPoint", folder);
      settings->setValue("Settings/driveShared", ui.checkBoxShared->isChecked());
      emit addMount(remote + ":" + path, folder);
    }
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
      emit addTransfer(QString("%1 from %2").arg(t.getMode()).arg(src), src,
                       dst, args);
    }
  });

  QObject::connect(ui.download, &QAction::triggered, this, [=]() {
    auto settings = GetSettings();


    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }
    QDir path = model->path(index);

    {auto s = GetSettings(); s->setValue("Settings/driveShared", ui.checkBoxShared->isChecked());}
    TransferDialog t(true, false, remote, path, model->isFolder(index), this);
    if (t.exec() == QDialog::Accepted) {
      QString src = t.getSource();
      QString dst = t.getDest();

      QStringList args = t.getOptions();
      emit addTransfer(QString("%1 %2").arg(t.getMode()).arg(src), src, dst,
                       args);
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
        } else {
          menu.addSeparator();
          editAction = menu.addAction("Open/Edit...");
          editAction->setToolTip(
              "Download, open with the default app, and re-upload on save.");
        }

        QAction *chosen = menu.exec(ui.tree->viewport()->mapToGlobal(pos));
        if (!chosen || (chosen != editAction && chosen != compareAction &&
                        chosen != archiveAction && chosen != speedAction &&
                        chosen != copyUrlAction && chosen != dedupeAction)) {
          return;
        }

        QString path = model->path(index).path();
        QString target = remote + ":" + path;

        if (chosen == editAction) {
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
              this, "Download URL", "URL to download to this remote folder:",
              QLineEdit::Normal, QString(), &ok);
          if (!ok || url.trimmed().isEmpty()) {
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
