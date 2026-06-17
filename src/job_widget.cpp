#include "job_widget.h"
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

      QJsonDocument doc = QJsonDocument::fromJson(raw);
      if (!doc.isObject()) {
        QString line = QString::fromUtf8(raw);
        ui.output->appendPlainText(line);
        Diagnostics::appendLog("job", line);
        continue;
      }

      QJsonObject obj = doc.object();
      QString msg = obj.value("msg").toString();
      QString level = obj.value("level").toString();

      if (!msg.isEmpty()) {
        ui.output->appendPlainText(msg);
        if (level == "error" || level == "warning") {
          Diagnostics::appendLog("job", msg);
        }
        if (msg.contains(':') && mTransferDetail.size() < 10000) {
          QString objectName = obj.value("object").toString();
          if (!objectName.isEmpty()) {
            QString ts = obj.value("time").toString();
            if (ts.isEmpty())
              ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            mTransferDetail.append(
                QString("%1 [%2] %3: %4")
                    .arg(ts, level, objectName, msg));
          }
        }
      }

      if (!obj.contains("stats"))
        continue;

      QJsonObject stats = obj.value("stats").toObject();

      // overall transfer progress
      double bytes = stats.value("bytes").toDouble();
      double totalBytes = stats.value("totalBytes").toDouble();
      double speed = stats.value("speed").toDouble();
      mBytes = static_cast<qint64>(bytes);
      int pct = totalBytes > 0
                    ? static_cast<int>(bytes / totalBytes * 100)
                    : 0;
      ui.size->setText(QString("%1, %2%")
                           .arg(GetNiceSize(static_cast<quint64>(bytes)))
                           .arg(pct));
      ui.totalsize->setText(
          GetNiceSize(static_cast<quint64>(totalBytes)));
      ui.bandwidth->setText(
          GetNiceSize(static_cast<quint64>(speed)) + "/s");
      mSparkline->addSample(speed);

      double eta = stats.value("eta").toDouble();
      if (eta > 0) {
        int h = static_cast<int>(eta) / 3600;
        int m = (static_cast<int>(eta) % 3600) / 60;
        int s = static_cast<int>(eta) % 60;
        if (h > 0)
          ui.eta->setText(
              QString("%1h%2m%3s").arg(h).arg(m).arg(s));
        else if (m > 0)
          ui.eta->setText(QString("%1m%2s").arg(m).arg(s));
        else
          ui.eta->setText(QString("%1s").arg(s));
      } else {
        ui.eta->setText("-");
      }

      int errors = stats.value("errors").toInt();
      mErrors = errors;
      ui.errors->setText(QString::number(errors));

      int checks = stats.value("checks").toInt();
      int totalChecks = stats.value("totalChecks").toInt();
      if (totalChecks > 0)
        ui.checks->setText(
            QString("%1 / %2").arg(checks).arg(totalChecks));
      else if (checks > 0)
        ui.checks->setText(QString::number(checks));

      int transfers = stats.value("transfers").toInt();
      int totalTransfers = stats.value("totalTransfers").toInt();
      mFiles = qMax(transfers, totalTransfers);
      if (totalTransfers > 0)
        ui.transferred->setText(
            QString("%1 / %2").arg(transfers).arg(totalTransfers));
      else if (transfers > 0)
        ui.transferred->setText(QString::number(transfers));

      double elapsed = stats.value("elapsedTime").toDouble();
      if (elapsed > 0) {
        int eh = static_cast<int>(elapsed) / 3600;
        int em = (static_cast<int>(elapsed) % 3600) / 60;
        int es = static_cast<int>(elapsed) % 60;
        if (eh > 0)
          ui.elapsed->setText(
              QString("%1h%2m%3s").arg(eh).arg(em).arg(es));
        else if (em > 0)
          ui.elapsed->setText(QString("%1m%2s").arg(em).arg(es));
        else
          ui.elapsed->setText(QString("%1s").arg(es));
      }

      // per-file progress from the "transferring" array
      QJsonArray xferring = stats.value("transferring").toArray();
      QSet<QLabel *> updated;
      int visibleRows = 0;
      int hiddenRows = 0;
      for (const QJsonValue &val : xferring) {
        QJsonObject f = val.toObject();
        QString name = f.value("name").toString();
        if (name.isEmpty())
          continue;
        if (visibleRows >= kMaxVisibleFileProgress) {
          hiddenRows++;
          continue;
        }
        visibleRows++;

        auto it = mActive.find(name);
        QLabel *label;
        QProgressBar *bar;
        if (it == mActive.end()) {
          label = new QLabel();
          QString display = name.length() > 47
                                ? name.left(25) + "..." + name.right(19)
                                : name;
          label->setText(display);

          bar = new QProgressBar();
          bar->setMinimum(0);
          bar->setMaximum(100);
          bar->setTextVisible(true);
          label->setBuddy(bar);

          ui.progress->addRow(label, bar);
          mActive.insert(name, label);
        } else {
          label = it.value();
          bar = static_cast<QProgressBar *>(label->buddy());
        }

        bar->setValue(f.value("percentage").toInt());
        double fSpeed = f.value("speed").toDouble();
        double fEta = f.value("eta").toDouble();
        bar->setToolTip(
            QString("File: %1\nSpeed: %2/s  ETA: %3s")
                .arg(name,
                     GetNiceSize(static_cast<quint64>(fSpeed)),
                     QString::number(static_cast<int>(fEta))));

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
                   this, [=](int status, QProcess::ExitStatus) {
                     mProcess->deleteLater();
                     clearFileProgress();

                     mRunning = false;
                     mFinishedAt = QDateTime::currentDateTimeUtc();
                     mExitCode = status;
                     mSuccess = (status == 0);

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

                     if (mUserCancelled) {
                       UiPolish::SetStatus(ui.showDetails, "warning",
                                           "Cancelled");
                     } else if (status == 0) {
                       UiPolish::SetStatus(ui.showDetails, "success",
                                           "Finished");
                     } else {
                       UiPolish::SetStatus(ui.showDetails, "error",
                                           "Needs attention");
                       ui.showDetails->setChecked(true);
                       ui.showOutput->setChecked(true);

                       auto *retry = new QToolButton(this);
                       retry->setIcon(QApplication::style()->standardIcon(
                           QStyle::SP_BrowserReload));
                       retry->setToolTip("Retry this transfer");
                       retry->setAccessibleName("Retry transfer");
                       UiPolish::SetCompactToolButton(retry, "Retry transfer",
                           "Re-run this transfer with the same arguments.");
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
  entry.transferDetail = mTransferDetail;
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
