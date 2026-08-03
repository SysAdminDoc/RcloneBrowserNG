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
  ui.info->setTextFormat(Qt::PlainText);
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
      cancel();
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
                     mHealthTimer->stop();
                     mProcess->deleteLater();
                     mRunning = false;
                     ui.cancel->setEnabled(true);
                     UiPolish::SetCompactToolButton(
                         ui.cancel, "Close mount card",
                         "Remove this mount from the jobs list.");
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
                     emit finished();
                     emit stopped(mUserRequestedUnmount, cleanExit);
                   });

  mHealthTimer = new QTimer(this);
  mHealthTimer->setInterval(60000);
  mHealthTimer->setSingleShot(false);
  QObject::connect(mHealthTimer, &QTimer::timeout, this,
                   &MountWidget::startHealthProbe);
  // The mount needs time to establish its filesystem before the first probe.
  QTimer::singleShot(15000, this, &MountWidget::startHealthProbe);
  mHealthTimer->start();

  UiPolish::SetStatus(ui.showDetails, "success", "Mounted");
}

MountWidget::~MountWidget() {}

bool MountWidget::keepMounted() const { return ui.keepMounted->isChecked(); }

bool MountWidget::remountRequested() const { return mRemountRequested; }

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

void MountWidget::startHealthProbe() {
  if (!mRunning || mHealthProbeInFlight ||
      mProcess->state() != QProcess::Running) {
    return;
  }

  mHealthProbeInFlight = true;
  const QString folder = ui.folder->text();
  QPointer<MountWidget> widgetGuard(this);
  QCoreApplication *application = QCoreApplication::instance();
  QThread *thread = QThread::create(
      [widgetGuard, application, folder]() {
        const MountHealthProbeResult result = ProbeMountPoint(folder);
        if (!application) {
          return;
        }
        QMetaObject::invokeMethod(
            application,
            [widgetGuard, result]() {
              if (widgetGuard) {
                widgetGuard->finishHealthProbe(result);
              }
            },
            Qt::QueuedConnection);
      });
  QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}

void MountWidget::finishHealthProbe(const MountHealthProbeResult &result) {
  mHealthProbeInFlight = false;
  if (result.healthy) {
    mHealthFailures = 0;
    return;
  }

  ++mHealthFailures;
  if (mHealthFailures < 2 || mStaleNotified || !mRunning) {
    return;
  }

  mStaleNotified = true;
  ui.output->appendPlainText("Mount health probe failed: " + result.detail);
  UiPolish::SetStatus(ui.showDetails, "warning", "Mount needs attention");
  ui.showDetails->setChecked(true);
  ui.showOutput->setChecked(true);
  emit staleDetected(result.detail);
}

QString MountWidget::rcAddr() const {
  if (!mRcAddr.isEmpty()) {
    return mRcAddr;
  }
  return "localhost:" + QString::number(GetRcMountPort(ui.folder->text()));
}

void MountWidget::runRcCommandAsync(const QString &command,
                                    RcCommandCallback callback) {
  auto *process = new QProcess(this);
  process->setProperty("rcCommandFinished", false);

  QStringList args;
  args << "rc" << command << "--rc-addr" << rcAddr();
  if (!mRcUser.isEmpty()) {
    args << "--rc-user" << mRcUser;
  }
  if (!mRcPass.isEmpty()) {
    args << "--rc-pass" << mRcPass;
  }

  auto complete = [process, callback = std::move(callback)](
                      bool success, const QByteArray &output,
                      const QString &error) mutable {
    if (process->property("rcCommandFinished").toBool()) {
      return;
    }
    process->setProperty("rcCommandFinished", true);
    if (callback) {
      callback(success, output, error);
    }
    process->deleteLater();
  };

  QObject::connect(
      process,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      process, [process, complete](int exitCode,
                                   QProcess::ExitStatus exitStatus) mutable {
        const QByteArray output = process->readAllStandardOutput();
        const QString detail =
            QString::fromUtf8(process->readAllStandardError()).trimmed();
        const bool success = exitStatus == QProcess::NormalExit &&
                             exitCode == 0;
        complete(success, output,
                 success ? QString()
                         : (detail.isEmpty()
                                ? QString("rclone rc exited with code %1")
                                      .arg(exitCode)
                                : detail));
      });
  QObject::connect(process, &QProcess::errorOccurred, process,
                   [complete](QProcess::ProcessError error) mutable {
                     if (error == QProcess::FailedToStart) {
                       complete(false, QByteArray(),
                                QStringLiteral("failed to start rclone rc"));
                     }
                   });
  QTimer::singleShot(5000, process, [process, complete]() mutable {
    if (process->state() != QProcess::NotRunning) {
      process->kill();
      complete(false, QByteArray(), QStringLiteral("rclone rc timed out"));
    }
  });

  UseRclonePassword(process);
  process->start(GetRclone(), args, QIODevice::ReadOnly);
}

void MountWidget::confirmNoPendingVfsUploads(std::function<void(bool)> callback) {
  auto showDecision = [this, callback = std::move(callback)](
                          const VfsUploadState &state,
                          const QString &error) mutable {
    if (!state.valid) {
      const int button = QMessageBox::warning(
          this, "Unmount",
          QString("Rclone Browser NG could not check whether the VFS cache has "
                  "pending uploads.\n\n%1\n\nUnmount anyway?")
              .arg(error.isEmpty() ? "The rc endpoint did not return usable data."
                                   : error),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (callback) {
        callback(button == QMessageBox::Yes);
      }
      return;
    }

    if (!state.hasPendingUploads()) {
      if (callback) {
        callback(true);
      }
      return;
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
    if (callback) {
      callback(button == QMessageBox::Yes);
    }
  };

  runRcCommandAsync(
      "vfs/queue",
      [this, showDecision](bool success, const QByteArray &output,
                           const QString &commandError) mutable {
        VfsUploadState state;
        QString error = commandError;
        if (success) {
          state = ParseVfsQueueState(output);
          if (!state.valid) {
            error = state.error;
          }
        }
        if (state.valid) {
          showDecision(state, error);
          return;
        }

        runRcCommandAsync(
            "vfs/stats",
            [showDecision, error](bool statsSuccess,
                                   const QByteArray &statsOutput,
                                   const QString &statsError) mutable {
              VfsUploadState statsState;
              QString finalError = error;
              if (statsSuccess) {
                statsState = ParseVfsStatsUploadState(statsOutput);
                if (!statsState.valid) {
                  finalError = statsState.error;
                }
              } else if (finalError.isEmpty()) {
                finalError = statsError;
              }
              showDecision(statsState, finalError);
            });
      });
}

void MountWidget::beginUnmount() {
  if (!mRunning || !mStopping) {
    return;
  }

  mUserRequestedUnmount = true;
  mHealthTimer->stop();
  ui.keepMounted->setEnabled(false);
  ui.cancel->setEnabled(false);
  ui.cancel->setToolTip("Unmounting...");
  UiPolish::SetStatus(ui.showDetails, "warning", "Unmounting");
  ui.output->appendPlainText("Unmount requested; stopping rclone mount...");

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
  QObject::connect(p,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   p, &QObject::deleteLater);
  QObject::connect(p, &QProcess::errorOccurred, p, &QObject::deleteLater);
  UseRclonePassword(p);
  p->start(GetRclone(), args, QIODevice::ReadOnly);
#else
  QProcess::startDetached("fusermount", QStringList()
                                            << "-u" << ui.folder->text());
#endif

  QPointer<QProcess> processGuard(mProcess);
  QTimer::singleShot(10000, this, [processGuard]() {
    if (processGuard && processGuard->state() != QProcess::NotRunning) {
      processGuard->terminate();
      QTimer::singleShot(5000, processGuard, [processGuard]() {
        if (processGuard && processGuard->state() != QProcess::NotRunning) {
          processGuard->kill();
        }
      });
    }
  });
}

void MountWidget::cancel() {
  if (!mRunning) {
    return;
  }
  if (mStopping) {
    return;
  }

#if defined(Q_OS_WIN32)
  mStopping = true;
  ui.keepMounted->setEnabled(false);
  ui.cancel->setEnabled(false);
  ui.cancel->setToolTip("Checking pending uploads...");
  UiPolish::SetStatus(ui.showDetails, "warning", "Checking uploads");
  confirmNoPendingVfsUploads([this](bool allowUnmount) {
    if (!mRunning) {
      return;
    }
    if (!allowUnmount) {
      mRemountRequested = false;
      mStopping = false;
      ui.keepMounted->setEnabled(true);
      ui.cancel->setEnabled(true);
      UiPolish::SetCompactToolButton(ui.cancel, "Unmount",
                                     "Unmount this remote.");
      UiPolish::SetStatus(ui.showDetails, "success", "Mounted");
      return;
    }
    beginUnmount();
  });
  return;
#else
  mStopping = true;
  beginUnmount();
#endif
}

void MountWidget::requestRemount() {
  if (!mRunning || mRemountRequested) {
    return;
  }
  mRemountRequested = true;
  cancel();
}
