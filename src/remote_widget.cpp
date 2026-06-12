#include "remote_widget.h"
#include "export_list_writer.h"
#include "export_dialog.h"
#include "icon_cache.h"
#include "item_model.h"
#include "list_of_job_options.h"
#include "progress_dialog.h"
#include "transfer_dialog.h"
#include "utils.h"

QStringList RemoteWidget::getDriveSharedArgs() const {
  if (ui.checkBoxShared->isChecked())
    return QStringList() << "--drive-shared-with-me";
  return QStringList();
}

RemoteWidget::RemoteWidget(IconCache *iconCache, const QString &remote,
                           bool isLocal, bool isGoogle, QWidget *parent)
    : QWidget(parent) {
  ui.setupUi(this);

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

  ui.tree->sortByColumn(0, Qt::AscendingOrder);
  ui.tree->header()->setSectionsMovable(false);

  ItemModel *model = new ItemModel(iconCache, remote, this);
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
        }

        ui.getSize->setDisabled(!isFolder);
        ui.getTree->setDisabled(!isFolder);
        ui.export_->setDisabled(!isFolder);
        ui.path->setText(isLocal ? QDir::toNativeSeparators(path.path())
                                 : path.path());
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
        this, "New Folder", QString("Create folder in %1").arg(pathMsg));
    if (!name.isEmpty()) {
      QString folder = path.filePath(name);
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

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QString name = model->data(index, Qt::DisplayRole).toString();
    name = QInputDialog::getText(this, "Rename",
                                 QString("New name for %1").arg(pathMsg),
                                 QLineEdit::Normal, name);
    if (!name.isEmpty()) {
      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      process.setArguments(
          QStringList() << "moveto" << GetRcloneConf() << getDriveSharedArgs()
                        << GetDefaultRcloneOptionsList() << remote + ":" + path
                        << remote + ":" +
                               model->path(index.parent()).filePath(name));
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

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    QString name = model->path(index.parent()).path() + "/";
    name = QInputDialog::getText(this, "Move",
                                 QString("New location for %1").arg(pathMsg),
                                 QLineEdit::Normal, name);
    if (!name.isEmpty()) {
      QProcess process;
      UseRclonePassword(&process);
      process.setProgram(GetRclone());
      process.setArguments(
          QStringList() << "move" << GetRcloneConf() << getDriveSharedArgs()
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

    QString path = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;

    int button = QMessageBox::question(
        this, "Delete",
        QString("Are you sure you want to delete %1 ?").arg(pathMsg),
        QMessageBox::Yes | QMessageBox::No);
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
                              QString("(Make sure you have WinFsp-FUSE "
                                      "installed)\n\nDrive to mount %1 to")
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
          "Enter stream command (file will be passed in STDIN):",
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

  QShortcut *close = new QShortcut(QKeySequence::Close, this);
  QObject::connect(close, &QShortcut::activated, this, [=]() {
    if (auto tabs = qobject_cast<QTabWidget *>(parent)) {
      tabs->removeTab(tabs->indexOf(this));
    }
  });
}

RemoteWidget::~RemoteWidget() {}
