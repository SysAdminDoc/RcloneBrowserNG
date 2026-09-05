#include "job_widget.h"
#include "job_stats.h"
#include "rclone_exit_code.h"
#include "interface_polish.h"
#include "rclone_capabilities.h"
#include "utils.h"
#if !defined(Q_OS_WIN32)
#include <csignal>
#endif

SparklineWidget::SparklineWidget(QWidget *parent) : QWidget(parent) {
  setFixedHeight(28);
  setMinimumWidth(120);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setToolTip("Transfer speed over time");
  setAccessibleName("Speed sparkline");
}

void SparklineWidget::addSample(double value) {
  mSamples.append(value);
  if (mSamples.size() > kMaxSamples)
    mSamples.removeFirst();
  update();
}

void SparklineWidget::paintEvent(QPaintEvent *) {
  if (mSamples.size() < 2)
    return;
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  double maxVal = *std::max_element(mSamples.begin(), mSamples.end());
  if (maxVal <= 0)
    maxVal = 1;

  const int w = width();
  const int h = height();
  const double stepX =
      static_cast<double>(w) / (kMaxSamples - 1);

  QPainterPath path;
  for (int i = 0; i < mSamples.size(); ++i) {
    double x = (kMaxSamples - mSamples.size() + i) * stepX;
    double y = h - (mSamples[i] / maxVal) * (h - 2) - 1;
    if (i == 0)
      path.moveTo(x, y);
    else
      path.lineTo(x, y);
  }

  QColor accent = palette().color(QPalette::Highlight);
  p.setPen(QPen(accent, 1.5));
  p.drawPath(path);

  QLinearGradient grad(0, 0, 0, h);
  grad.setColorAt(0, QColor(accent.red(), accent.green(), accent.blue(), 40));
  grad.setColorAt(1, QColor(accent.red(), accent.green(), accent.blue(), 5));
  QPainterPath fill = path;
  fill.lineTo(path.currentPosition().x(), h);
  fill.lineTo((kMaxSamples - mSamples.size()) * stepX, h);
  fill.closeSubpath();
  p.fillPath(fill, grad);
}

namespace {
constexpr int kMaxVisibleFileProgress = 12;
} // namespace

JobWidget::JobWidget(QProcess *process, const QString &info,
                     const QStringList &args, const QString &source,
                     const QString &dest, QWidget *parent)
    : QWidget(parent), mProcess(process),
      mStartedAt(QDateTime::currentDateTimeUtc()), mInfo(info),
      mSource(source), mDest(dest) {
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
  UiPolish::SetDisclosureButton(ui.showDetails, "Show transfer details");
  UiPolish::SetDisclosureButton(ui.showOutput, "Show transfer output");
  UiPolish::SetPathField(ui.source, "Transfer source");
  UiPolish::SetPathField(ui.dest, "Transfer destination");
  UiPolish::SetReadOnlyValue(ui.size, "Transferred size");
  UiPolish::SetReadOnlyValue(ui.elapsed, "Elapsed time");
  UiPolish::SetReadOnlyValue(ui.bandwidth, "Current bandwidth");
  UiPolish::SetReadOnlyValue(ui.transferred, "Transferred files");
  UiPolish::SetReadOnlyValue(ui.totalsize, "Total size");
  UiPolish::SetReadOnlyValue(ui.eta, "Remaining time");
  UiPolish::SetReadOnlyValue(ui.errors, "Transfer errors");
  UiPolish::SetReadOnlyValue(ui.checks, "Completed checks");

  mArgs.append(QDir::toNativeSeparators(GetRclone()));
  mArgs.append(GetRcloneConf());
  mArgs.append(args);
  mTransferArgs = args;

  ui.source->setText(source);
  ui.source->setToolTip(source);
  ui.dest->setText(dest);
  ui.dest->setToolTip(dest);
  ui.info->setTextFormat(Qt::PlainText);
  ui.info->setText(info);
  ui.info->setToolTip(info);
  ui.info->setMinimumWidth(0);
  ui.info->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  ui.info->setAccessibleName("Transfer summary");

  ui.details->setVisible(false);

  mSparkline = new SparklineWidget(this);
  ui.gridLayout_2->addWidget(mSparkline, ui.gridLayout_2->rowCount(), 0, 1, 2);

  UiPolish::SetOutputView(ui.output);
  ui.output->setVisible(false);
  // bound memory growth on long transfers; old lines scroll away instead
  // of the whole log being wiped at once
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

  auto *pauseBtn = new QToolButton(this);
  pauseBtn->setIcon(
      QApplication::style()->standardIcon(QStyle::SP_MediaPause));
  UiPolish::SetCompactToolButton(pauseBtn, "Pause transfer",
                                 "Pause or resume this running transfer.");
  ui.horizontalLayout->insertWidget(ui.horizontalLayout->indexOf(ui.cancel),
                                    pauseBtn);
  QObject::connect(pauseBtn, &QToolButton::clicked, this,
                   &JobWidget::togglePause);

  ui.cancel->setIcon(
      QApplication::style()->standardIcon(QStyle::SP_DialogCloseButton));
  UiPolish::SetCompactToolButton(ui.cancel, "Cancel transfer",
                                 "Cancel this running transfer.");

  QObject::connect(ui.cancel, &QToolButton::clicked, this, [=]() {
    if (mRunning) {
      cancel();
    } else {
      emit closed();
    }
  });

  ui.copy->setIcon(
      QApplication::style()->standardIcon(QStyle::SP_FileLinkIcon));
  UiPolish::SetCompactToolButton(ui.copy, "Copy transfer command",
                                 "Copy the rclone command to the clipboard.");

  QObject::connect(ui.copy, &QToolButton::clicked, this, [=]() {
    QStringList quotedArgs;
    for (const auto &arg : mArgs) {
      if (arg.contains(' ') || arg.contains('"')) {
        quotedArgs << '"' + QString(arg).replace('"', "\\\"") + '"';
      } else {
        quotedArgs << arg;
      }
    }
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(quotedArgs.join(" "));
  });

  QObject::connect(mProcess, &QProcess::readyRead, this, [=]() {
    while (mProcess->canReadLine()) {
      QByteArray raw = mProcess->readLine().trimmed();
      if (raw.isEmpty())
        continue;

      const JobStats::LogLine parsed = JobStats::ParseLogLine(raw);
      if (!parsed.isJson) {
        QString line = QString::fromUtf8(raw);
        ui.output->appendPlainText(line);
        Diagnostics::appendLog("job", line);
        continue;
      }

      const QString msg = parsed.message;
      const QString level = parsed.level;

      if (!msg.isEmpty()) {
        ui.output->appendPlainText(msg);
        if (level == "error" || level == "warning") {
          Diagnostics::appendLog("job", msg);
        }
        if (mTransferDetail.size() < 10000) {
          const QString objectName = parsed.object;
          if (!objectName.isEmpty()) {
            QString ts = parsed.time;
            if (ts.isEmpty())
              ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            mTransferDetail.append(
                QString("%1 [%2] %3: %4")
                    .arg(ts, level, objectName, msg));
          }
        }
      }

      if (!parsed.stats.present)
        continue;

      const JobStats::Stats &stats = parsed.stats;

      // overall transfer progress
      mBytes = static_cast<qint64>(stats.bytes);
      ui.size->setText(
          QString("%1, %2%")
              .arg(GetNiceSize(static_cast<quint64>(stats.bytes)))
              .arg(JobStats::PercentComplete(stats.bytes, stats.totalBytes)));
      ui.totalsize->setText(
          GetNiceSize(static_cast<quint64>(stats.totalBytes)));
      ui.bandwidth->setText(
          GetNiceSize(static_cast<quint64>(stats.speed)) + "/s");
      mSparkline->addSample(stats.speed);

      const QString etaText = JobStats::FormatDuration(stats.eta);
      ui.eta->setText(etaText.isEmpty() ? QString("-") : etaText);

      mErrors = stats.errors;
      ui.errors->setText(QString::number(stats.errors));

      const QString checksText =
          JobStats::FormatCount(stats.checks, stats.totalChecks);
      if (!checksText.isEmpty())
        ui.checks->setText(checksText);

      mFiles = qMax(stats.transfers, stats.totalTransfers);
      // Say when the provider did the work: it is the difference
      // between a copy that used the network and one that did not.
      if (stats.serverSideCopies > mServerSideCopies) {
        mServerSideCopies = stats.serverSideCopies;
        ui.output->appendPlainText(
            QString("%1 file(s) copied server-side (%2), without passing through this machine.")
                .arg(stats.serverSideCopies)
                .arg(GetNiceSize(static_cast<quint64>(
                    stats.serverSideCopyBytes))));
      }
      const QString transferredText =
          JobStats::FormatCount(stats.transfers, stats.totalTransfers);
      if (!transferredText.isEmpty())
        ui.transferred->setText(transferredText);

      const QString elapsedText = JobStats::FormatDuration(stats.elapsedTime);
      if (!elapsedText.isEmpty())
        ui.elapsed->setText(elapsedText);

      // per-file progress from the "transferring" array
      QSet<QLabel *> updated;
      int visibleRows = 0;
      int hiddenRows = 0;
      for (const JobStats::TransferringFile &file : stats.transferring) {
        if (visibleRows >= kMaxVisibleFileProgress) {
          hiddenRows++;
          continue;
        }
        visibleRows++;

        auto it = mActive.find(file.name);
        QLabel *label;
        QProgressBar *bar;
        if (it == mActive.end()) {
          label = new QLabel();
          label->setText(JobStats::ElideTransferName(file.name));

          bar = new QProgressBar();
          bar->setMinimum(0);
          bar->setMaximum(100);
          bar->setTextVisible(true);
          label->setBuddy(bar);

          ui.progress->addRow(label, bar);
          mActive.insert(file.name, label);
        } else {
          label = it.value();
          bar = static_cast<QProgressBar *>(label->buddy());
        }

        bar->setValue(file.percentage);
        bar->setToolTip(
            QString("File: %1\nSpeed: %2/s  ETA: %3s")
                .arg(file.name,
                     GetNiceSize(static_cast<quint64>(file.speed)),
                     QString::number(static_cast<int>(file.eta))));

        updated.insert(label);
      }
      setProgressOverflow(hiddenRows);

      // remove progress bars for files no longer in the transferring list
      for (auto it = mActive.begin(); it != mActive.end(); /* empty */) {
        auto label = it.value();
        if (updated.contains(label)) {
          ++it;
        } else {
          it = mActive.erase(it);
          ui.progress->removeWidget(label->buddy());
          ui.progress->removeWidget(label);
          delete label->buddy();
          delete label;
        }
      }
    }
  });

  QObject::connect(mProcess,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   this, [=](int status,
                             QProcess::ExitStatus exitStatus) {
                     mProcess->deleteLater();
                     clearFileProgress();

                     mRunning = false;
                     mFinishedAt = QDateTime::currentDateTimeUtc();
                     mExitCode = status;
                     // A cancelled or crashed process did not choose its exit
                     // code, so it must not be read as one. Cancelling on
                     // Windows lands on 62097 and on POSIX reports signal 9,
                     // which the rclone table calls "Nothing to transfer" and
                     // counts as a success.
                     const RcloneExitCode::ProcessOutcome processOutcome =
                         RcloneExitCode::DescribeProcess(
                             status, exitStatus == QProcess::CrashExit,
                             mUserCancelled);
                     mSuccess = processOutcome.success;
                     mCompletedFully = processOutcome.completedFully;
                     mStatusLabel = processOutcome.name;

                     for (int i = 0; i < ui.horizontalLayout->count(); ++i) {
                       auto *w = ui.horizontalLayout->itemAt(i)->widget();
                       if (auto *btn = qobject_cast<QToolButton *>(w)) {
                         if (btn != ui.showDetails && btn != ui.showOutput &&
                             btn != ui.cancel && btn != ui.copy)
                           btn->setVisible(false);
                       }
                     }

                     ui.cancel->setEnabled(true);
                     UiPolish::SetCompactToolButton(
                         ui.cancel, "Close transfer card",
                         "Remove this transfer from the jobs list.");

                     const RcloneExitCode::ProcessOutcome &meaning =
                         processOutcome;
                     if (mUserCancelled) {
                       UiPolish::SetStatus(ui.showDetails, "warning",
                                           meaning.name);
                       ui.showDetails->setToolTip(meaning.explanation);
                     } else if (processOutcome.completedFully) {
                       UiPolish::SetStatus(ui.showDetails, "success",
                                           meaning.name);
                       ui.showDetails->setToolTip(meaning.explanation);
                     } else if (meaning.outcome ==
                                RcloneExitCode::Outcome::CompletedWithLimit) {
                       // Not a failure: say what stopped it and leave the
                       // card calm rather than red.
                       UiPolish::SetStatus(ui.showDetails, "warning",
                                           meaning.name);
                       ui.showDetails->setToolTip(meaning.explanation);
                       ui.output->appendPlainText(meaning.explanation);
                     } else {
                       UiPolish::SetStatus(ui.showDetails, "error",
                                           meaning.name);
                       ui.showDetails->setToolTip(meaning.explanation);
                       ui.output->appendPlainText(meaning.explanation);
                       ui.showDetails->setChecked(true);
                       ui.showOutput->setChecked(true);

                       auto *retry = new QToolButton(this);
                       retry->setIcon(QApplication::style()->standardIcon(
                           QStyle::SP_BrowserReload));
                       retry->setToolTip("Retry this transfer");
                       retry->setAccessibleName("Retry transfer");
                       UiPolish::SetCompactToolButton(
                           retry, "Retry transfer",
                           RcloneExitCode::IsRetryable(status)
                               ? QString("%1 Retrying often helps here.")
                                     .arg(meaning.explanation)
                               : QString("Re-run this transfer with the same "
                                         "arguments."));
                       ui.horizontalLayout->insertWidget(
                           ui.horizontalLayout->indexOf(ui.cancel), retry);
                       QObject::connect(retry, &QToolButton::clicked, this,
                                        [this]() { emit retryRequested(); });

                       if (mTransferArgs.contains("bisync")) {
                         auto *resync = new QToolButton(this);
                         resync->setIcon(QApplication::style()->standardIcon(
                             QStyle::SP_DialogResetButton));
                         resync->setToolTip(
                             "Resync: re-run with --resync to reset bisync state.\n"
                             "Warning: this may overwrite changes on one side.");
                         resync->setAccessibleName("Resync bisync");
                         UiPolish::SetCompactToolButton(resync, "Resync",
                             "Reset bisync state and re-run. Use when bisync "
                             "enters an unrecoverable error state.");
                         ui.horizontalLayout->insertWidget(
                             ui.horizontalLayout->indexOf(ui.cancel), resync);
                         QObject::connect(resync, &QToolButton::clicked, this,
                                          [this]() { emit resyncRequested(); });
                       }
                     }

                     emit finished(ui.info->text());
                   });

  UiPolish::SetStatus(ui.showDetails, "running", "Running");
}

JobWidget::~JobWidget() {}

void JobWidget::showDetails() { ui.showDetails->setChecked(true); }

JobHistoryEntry JobWidget::historyEntry() const {
  JobHistoryEntry entry;
  entry.startedAt = mStartedAt;
  entry.finishedAt =
      mFinishedAt.isValid() ? mFinishedAt : QDateTime::currentDateTimeUtc();
  entry.name = mInfo;
  entry.source = mSource;
  entry.dest = mDest;
  entry.success = mSuccess;
  entry.bytes = mBytes;
  entry.files = mFiles;
  entry.errors = mErrors;
  entry.exitCode = mExitCode;
  entry.statusLabel = mStatusLabel;
  entry.transferDetail = mTransferDetail;
  entry.args = mTransferArgs;
  return entry;
}

void JobWidget::setProgressOverflow(int hiddenCount) {
  if (hiddenCount <= 0) {
    if (mOverflowLabel) {
      ui.progress->removeWidget(mOverflowLabel);
      delete mOverflowLabel;
      mOverflowLabel = nullptr;
    }
    return;
  }

  if (!mOverflowLabel) {
    mOverflowLabel = new QLabel();
    UiPolish::SetMuted(mOverflowLabel);
    ui.progress->addRow(mOverflowLabel);
  }
  mOverflowLabel->setText(
      QString("+%1 more active file(s) hidden").arg(hiddenCount));
}

void JobWidget::clearFileProgress() {
  for (auto label : mActive) {
    ui.progress->removeWidget(label->buddy());
    ui.progress->removeWidget(label);
    delete label->buddy();
    delete label;
  }
  mActive.clear();
  setProgressOverflow(0);
}

void JobWidget::togglePause() {
  if (!mRunning || mProcess->state() != QProcess::Running)
    return;

#if defined(Q_OS_WIN32)
  HANDLE hProcess =
      OpenProcess(PROCESS_SUSPEND_RESUME, FALSE,
                  static_cast<DWORD>(mProcess->processId()));
  if (!hProcess)
    return;
  using NtFunc = LONG(NTAPI *)(HANDLE);
  auto ntdll = GetModuleHandleW(L"ntdll.dll");
  if (mPaused) {
    auto NtResume = reinterpret_cast<NtFunc>(
        GetProcAddress(ntdll, "NtResumeProcess"));
    if (NtResume)
      NtResume(hProcess);
  } else {
    auto NtSuspend = reinterpret_cast<NtFunc>(
        GetProcAddress(ntdll, "NtSuspendProcess"));
    if (NtSuspend)
      NtSuspend(hProcess);
  }
  CloseHandle(hProcess);
#else
  ::kill(static_cast<pid_t>(mProcess->processId()),
         mPaused ? SIGCONT : SIGSTOP);
#endif

  mPaused = !mPaused;
  if (mPaused) {
    UiPolish::SetStatus(ui.showDetails, "warning", "Paused");
  } else {
    UiPolish::SetStatus(ui.showDetails, "running", "Running");
  }
}

void JobWidget::cancel() {
  if (!mRunning) {
    return;
  }
  if (mStopping) {
    return;
  }

  mStopping = true;
  mUserCancelled = true;
  if (mPaused) {
    togglePause();
  }
  UiPolish::SetStatus(ui.showDetails, "warning", "Stopping");
  ui.cancel->setEnabled(false);
  ui.cancel->setToolTip("Stopping transfer...");
  ui.output->appendPlainText("Cancel requested; stopping rclone...");

  mProcess->terminate();
  QPointer<QProcess> processGuard(mProcess);
  QTimer::singleShot(5000, this, [processGuard]() {
    if (processGuard && processGuard->state() != QProcess::NotRunning) {
      processGuard->kill();
    }
  });
}
