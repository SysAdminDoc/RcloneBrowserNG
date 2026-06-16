#include "mount_widget.h"
#include "interface_polish.h"
#include "rclone_capabilities.h"
#include "utils.h"
#include "vfs_upload_state.h"

MountWidget::MountWidget(QProcess *process, const QString &remote,
                         const QString &folder, const QString &rcAddr,
                         const QString &rcUser, const QString &rcPass,
                         bool keepMounted, QWidget *parent)
    : QWidget(parent), mProcess(process), mRcAddr(rcAddr), mRcUser(rcUser),
      mRcPass(rcPass) {
  ui.setupUi(this);
  ui.verticalLayout->setContentsMargins(0, 0, 0, 0);
  ui.verticalLayout->setSpacing(0);
  ui.horizontalLayout->setContentsMargins(12, 10, 12, 10);
  ui.horizontalLayout->setSpacing(8);
  ui.gridLayout_2->setContentsMargins(12, 10, 12, 12);
  ui.gridLayout_2->setHorizontalSpacing(10);
  ui.gridLayout_2->setVerticalSpacing(6);
  ui.showDetails->setStyleSheet(QString());
  ui.showOutput->setStyleSheet(QString());
  ui.copy->setStyleSheet(QString());
  ui.cancel->setStyleSheet(QString());
  UiPolish::SetCard(this);
  UiPolish::SetToolbarSurface(ui.widget);
  UiPolish::SetDisclosureButton(ui.showDetails, "Show mount details");
  UiPolish::SetDisclosureButton(ui.showOutput, "Show mount output");
  UiPolish::SetPathField(ui.remote, "Mounted remote");
  UiPolish::SetPathField(ui.folder, "Local mount point");

  ui.remote->setText(remote);
  ui.remote->setToolTip(remote);
  ui.folder->setText(folder);
  ui.folder->setToolTip(folder);
  ui.info->setText(QString("%1 on %2").arg(remote).arg(folder));
  ui.info->setToolTip(ui.info->text());
  ui.info->setMinimumWidth(0);
  ui.info->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  ui.keepMounted->setChecked(keepMounted);
  ui.keepMounted->setAccessibleName("Keep mounted");

  ui.details->setVisible(false);

  UiPolish::SetOutputView(ui.output);
  ui.output->setVisible(false);
  // long-lived mounts can log indefinitely - bound memory growth
  ui.output->setMaximumBlockCount(10000);

  QObject::connect(
      ui.showDetails, &QToolButton::toggled, this, [=](bool checked) {
        ui.details->setVisible(checked);
        ui.showDetails->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
      });

  QObject::connect(
      ui.showOutput, &QToolButton::toggled, this, [=](bool checked) {
        ui.output->setVisible(checked);
        ui.showOutput->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
      });

  ui.copy->setIcon(
      QApplication::style()->standardIcon(QStyle::SP_FileLinkIcon));
  UiPolish::SetCompactToolButton(ui.copy, "Copy mount command",
                                 "Copy the rclone mount command to the clipboard.");

  QObject::connect(ui.copy, &QToolButton::clicked, this, [=]() {
    QStringList args;
    args << QDir::toNativeSeparators(mProcess->program());
    for (const auto &arg : mProcess->arguments()) {
      if (arg.contains(' ') || arg.contains('"')) {
        args << '"' + QString(arg).replace('"', "\\\"") + '"';
      } else {
        args << arg;
      }
    }
    QGuiApplication::clipboard()->setText(args.join(" "));
  });

  ui.cancel->setIcon(
      QApplication::style()->standardIcon(QStyle::SP_DialogCloseButton));
  UiPolish::SetCompactToolButton(ui.cancel, "Unmount",
                                 "Unmount this remote.");

  QObject::connect(ui.cancel, &QToolButton::clicked, this, [=]() {
    if (mRunning) {
      int button = QMessageBox::question(
          this, "Unmount",
#if defined(Q_OS_WIN)
          QString("Unmount %1 drive?").arg(folder),
#else
          QString("Unmount %1 folder?").arg(folder),
#endif
          QMessageBox::Yes | QMessageBox::No);
      if (button == QMessageBox::Yes) {
        cancel();
      }
    } else {
      emit closed();
    }
  });

  QObject::connect(mProcess, &QProcess::readyRead, this, [=]() {
    while (mProcess->canReadLine()) {
      QString line = QString::fromUtf8(mProcess->readLine().trimmed());
      ui.output->appendPlainText(line);
      Diagnostics::appendLog("mount", line);
    }
  });

  QObject::connect(mProcess,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   this, [=](int status, QProcess::ExitStatus exitStatus) {
                     mProcess->deleteLater();
                     mRunning = false;
                     const bool cleanExit =
                         status == 0 && exitStatus == QProcess::NormalExit;
                     if (cleanExit) {
                       UiPolish::SetStatus(ui.showDetails, "idle",
                                           "Unmounted");
                     } else {
                       UiPolish::SetStatus(ui.showDetails, "error",
                                           "Needs attention");
                       ui.showDetails->setChecked(true);
                       ui.showOutput->setChecked(true);
                     }
                     ui.cancel->setToolTip("Close");
                     emit finished();
                     emit stopped(mUserRequestedUnmount, cleanExit);
                   });

  UiPolish::SetStatus(ui.showDetails, "success", "Mounted");
}

MountWidget::~MountWidget() {}

bool MountWidget::keepMounted() const { return ui.keepMounted->isChecked(); }

void MountWidget::setRemountScheduled(int delayMs, int attempt) {
  ui.keepMounted->setEnabled(false);
  UiPolish::SetStatus(
      ui.showDetails, "warning",
      QString("Remounting in %1 s").arg((delayMs + 999) / 1000));
  ui.showDetails->setChecked(true);
  ui.showOutput->setChecked(true);
  ui.output->appendPlainText(
      QString("Mount stopped unexpectedly; automatic remount attempt %1 is "
              "scheduled in %2 seconds.")
          .arg(attempt)
          .arg((delayMs + 999) / 1000));
}

QString MountWidget::rcAddr() const {
  if (!mRcAddr.isEmpty()) {
    return mRcAddr;
  }
  return "localhost:" + QString::number(GetRcMountPort(ui.folder->text()));
}

bool MountWidget::runRcCommand(const QString &command, QByteArray *output,
                               QString *error) const {
  if (!output) {
    return false;
  }

  QProcess process;
  QStringList args;
  args << "rc" << command << "--rc-addr" << rcAddr();
  if (!mRcUser.isEmpty()) {
    args << "--rc-user" << mRcUser;
  }
  if (!mRcPass.isEmpty()) {
    args << "--rc-pass" << mRcPass;
  }

  UseRclonePassword(&process);
  process.start(GetRclone(), args, QIODevice::ReadOnly);
  if (!process.waitForStarted(3000)) {
    if (error) {
      *error = "failed to start rclone rc";
    }
    return false;
  }
  if (!process.waitForFinished(5000)) {
    process.kill();
    process.waitForFinished();
    if (error) {
      *error = "rclone rc timed out";
    }
    return false;
  }

  *output = process.readAllStandardOutput();
  if (process.exitStatus() != QProcess::NormalExit ||
      process.exitCode() != 0) {
    const QString detail =
        QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (error) {
      *error = detail.isEmpty() ? QString("rclone rc exited with code %1")
                                      .arg(process.exitCode())
                                : detail;
    }
    return false;
  }

  return true;
}

bool MountWidget::confirmNoPendingVfsUploads() {
  QByteArray output;
  QString error;

  VfsUploadState state;
  if (runRcCommand("vfs/queue", &output, &error)) {
    state = ParseVfsQueueState(output);
    if (!state.valid) {
      error = state.error;
    }
  }

  if (!state.valid) {
    QByteArray statsOutput;
    QString statsError;
    if (runRcCommand("vfs/stats", &statsOutput, &statsError)) {
      state = ParseVfsStatsUploadState(statsOutput);
      if (!state.valid) {
        error = state.error;
      }
    } else if (error.isEmpty()) {
      error = statsError;
    }
  }

  if (!state.valid) {
    const int button = QMessageBox::warning(
        this, "Unmount",
        QString("Rclone Browser NG could not check whether the VFS cache has "
                "pending uploads.\n\n%1\n\nUnmount anyway?")
            .arg(error.isEmpty() ? "The rc endpoint did not return usable data."
                                 : error),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return button == QMessageBox::Yes;
  }

  if (!state.hasPendingUploads()) {
    return true;
  }

  const QString bytes =
      state.bytesKnown ? FormatUploadByteSize(state.pendingBytes)
                       : QString("unknown size");
  const int button = QMessageBox::warning(
      this, "Pending uploads",
      QString("The VFS cache still has %1 file(s) / %2 not yet uploaded.\n\n"
              "Unmount anyway?")
          .arg(state.pendingFiles)
          .arg(bytes),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  return button == QMessageBox::Yes;
}

void MountWidget::cancel() {
  if (!mRunning) {
    return;
  }

#if defined(Q_OS_WIN32)
  if (!confirmNoPendingVfsUploads()) {
    return;
  }
#endif

  mUserRequestedUnmount = true;

#if defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD)
  QProcess::startDetached("umount", QStringList() << ui.folder->text());
#elif defined(Q_OS_WIN32)
  QProcess *p = new QProcess();
  QStringList args;
  args << "rc";
  args << "core/quit";
  args << "--rc-addr" << rcAddr();
  // authenticate with the per-mount credential the endpoint was started with
  if (!mRcUser.isEmpty()) {
    args << "--rc-user" << mRcUser;
  }
  if (!mRcPass.isEmpty()) {
    args << "--rc-pass" << mRcPass;
  }
  // clean the process up when it finishes instead of leaking it
  QObject::connect(p,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   p, &QObject::deleteLater);
  UseRclonePassword(p);
  p->start(GetRclone(), args, QIODevice::ReadOnly);
#else
  QProcess::startDetached("fusermount", QStringList()
                                            << "-u" << ui.folder->text());
#endif

  if (!mProcess->waitForFinished(10000)) {
    mProcess->terminate();
    if (!mProcess->waitForFinished(5000)) {
      mProcess->kill();
      mProcess->waitForFinished();
    }
  }

  emit closed();
}
