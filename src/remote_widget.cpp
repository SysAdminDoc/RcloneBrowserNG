#include "remote_widget.h"
#include "export_list_writer.h"
#include "export_dialog.h"
#include "icon_cache.h"
#include "item_model.h"
#include "list_of_job_options.h"
#include "progress_dialog.h"
#include "remote_path.h"
#include "transfer_dialog.h"
#include "interface_polish.h"
#include "utils.h"

QStringList RemoteWidget::getDriveSharedArgs() const {
  if (ui.checkBoxShared->isChecked())
    return QStringList() << "--drive-shared-with-me";
  return QStringList();
}

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
  UiPolish::SetNavigationView(ui.tree, "Remote file browser");
  ui.tree->setRootIsDecorated(true);
  ui.tree->setIndentation(18);

QString root = isLocal ? "/" : QString();

#ifndef Q_OS_WIN
  isLocal = false;
#endif

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

  ui.tree->sortByColumn(0, Qt::AscendingOrder);
  ui.tree->header()->setSectionsMovable(false);
  ui.tree->header()->setHighlightSections(false);
  ui.tree->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  ItemModel *model = new ItemModel(iconCache, remote, isGooglePhotos, this);
  QObject::connect(ui.checkBoxShared, &QCheckBox::toggled, model,
                   &ItemModel::setDriveShared);
  ui.tree->setModel(model);
  QTimer::singleShot(0, ui.tree, SLOT(setFocus()));

  // selection helper - actions can fire with nothing selected (shortcuts,
  // programmatic toggles), so never call .front() on an empty list
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
        QMessageBox::warning(
            this, "Listing failed",
            QString("rclone could not list \"%1:%2\".\n\n%3")
                .arg(remote, path, error.left(600)));
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
      [=](const QItemSelection &selection) {
        for (auto child : findChildren<QAction *>()) {
          child->setDisabled(selection.isEmpty());
        }

        if (selection.isEmpty()) {
          ui.path->clear();
          return;
        }

        QModelIndex index = selection.indexes().front();

        bool topLevel = model->isTopLevel(index);
        bool isFolder = model->isFolder(index);

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
          ui.path->setText("Loading " +
                           (isLocal ? QDir::toNativeSeparators(path.path())
                                    : path.path()));
        } else {
          ui.refresh->setDisabled(false);
          bool driveShared = ui.checkBoxShared->checkState();
          ui.mkdir->setDisabled(driveShared);
          ui.rename->setDisabled(topLevel || driveShared);
          ui.move->setDisabled(topLevel || driveShared);
          ui.purge->setDisabled(topLevel || driveShared);
          ui.upload->setDisabled(driveShared);

#if defined(Q_OS_WIN32)
          // check if required version
          unsigned int result =
              compareVersion(rcloneVersion.toStdString(), "1.50");
          if (result == 2) {
            ui.mount->setDisabled(true);
          } else {
            ui.mount->setDisabled(!isFolder);
          };
#else
// mount is not supported by rclone on these systems
#if defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
          ui.mount->setDisabled(true);
#else
          ui.mount->setDisabled(!isFolder);
#endif
#endif

          ui.stream->setDisabled(isFolder);
          ui.checkBoxShared->setDisabled(!isGoogle);
          path = model->path(index);
          ui.path->setText(isLocal ? QDir::toNativeSeparators(path.path())
                                   : path.path());
        }

        ui.getSize->setDisabled(!isFolder);
        ui.getTree->setDisabled(!isFolder);
        ui.export_->setDisabled(!isFolder);
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


    QModelIndex index = selectedIndex();
    if (!index.isValid()) {
      return;
    }
    if (!confirmUnambiguousDestructiveAction(index, "Delete")) {
      return;
    }

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    int button = QMessageBox::question(
        this, "Delete",
        QString("Delete %1?\n\nThis starts a rclone delete job. Recovery "
                "depends on the remote backend's trash or versioning support.")
            .arg(pathMsg),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (button == QMessageBox::Yes) {
      QStringList args;
      args << (model->isFolder(index) ? "purge" : "delete");
      args << getDriveSharedArgs();
      args << GetDefaultRcloneOptionsList();
      args << "--verbose";
      args << "--use-json-log";
      args << "--stats" << "1s";
      args << remote + ":" + path;

      QModelIndex parent = index.parent();
      QModelIndex next = parent.model()->index(index.row() + 1, 0);
      ui.tree->selectionModel()->select(next.isValid() ? next : parent,
                                        QItemSelectionModel::SelectCurrent);
      model->removeRow(index.row(), parent);

      emit addTransfer(
          QString("Delete %1").arg(pathMsg),
          remote + ":" + path, QString(), args);
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

  QObject::connect(
      ui.tree, &QWidget::customContextMenuRequested, this,
      [=](const QPoint &pos) {
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
        menu.exec(ui.tree->viewport()->mapToGlobal(pos));
      });

  if (isLocal) {
    QHash<QString, QPersistentModelIndex> drives;

    // QDir::drives is fast
    for (const auto &drive : QDir::drives()) {
      QString path = drive.path();
      QModelIndex index = model->addRoot(QDir::toNativeSeparators(path), path);
      drives.insert(path, index);
    }

#if (QT_VERSION >= QT_VERSION_CHECK(5, 4, 0)) && !(defined Q_OS_WIN)
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

  ui.tree->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(
      ui.tree, &QWidget::customContextMenuRequested, this,
      [=](const QPoint &pos) {
        QModelIndex index = ui.tree->indexAt(pos);
        if (!index.isValid()) {
          return;
        }
        QMenu menu;
        if (model->isFolder(index)) {
          QAction *archiveAction = menu.addAction("Archive…");
          archiveAction->setToolTip(
              "Move files older than a threshold to a dated archive folder.");
          QAction *speedAction = menu.addAction("Speed Test…");
          speedAction->setToolTip(
              "Run upload/download speed probes against this remote.");

          QAction *chosen = menu.exec(ui.tree->viewport()->mapToGlobal(pos));
          if (!chosen) {
            return;
          }

          QString path = model->path(index).path();
          QString target = remote + ":" + path;

          if (chosen == archiveAction) {
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
            ProgressDialog progress("Archive", "Archiving…", target, &process,
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
            ProgressDialog progress("Speed Test", "Testing…", target, &process,
                                    this, false);
            progress.expand();
            progress.allowToClose();
            progress.exec();
          }
        }
      });

  QShortcut *close = new QShortcut(QKeySequence::Close, this);
  QObject::connect(close, &QShortcut::activated, this, [=]() {
    if (auto tabs = qobject_cast<QTabWidget *>(parent)) {
      tabs->removeTab(tabs->indexOf(this));
    }
  });
}

void RemoteWidget::refreshCurrentDir() { ui.refresh->trigger(); }

void RemoteWidget::focusPathBar() {
  ui.path->setFocus();
  ui.path->selectAll();
}

RemoteWidget::~RemoteWidget() {}
