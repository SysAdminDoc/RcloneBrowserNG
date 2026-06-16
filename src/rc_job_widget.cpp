#include "rc_job_widget.h"
#include "rclone_rc_engine.h"
#include "interface_polish.h"
#include "rclone_capabilities.h"
#include "utils.h"


RcJobWidget::RcJobWidget(RcloneRcEngine *engine, int jobId, const QString &info,
                         const QStringList &displayArgs, const QString &source,
                         const QString &dest, QWidget *parent)
    : QWidget(parent), mEngine(engine), mJobId(jobId), mGroup("job/" + QString::number(jobId)),
      mDisplayArgs(displayArgs), mInfo(info), mSource(source), mDest(dest) {
  mStartedAt = QDateTime::currentDateTimeUtc();
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
  UiPolish::SetOutputView(ui.output);
  ui.output->setVisible(false);
  ui.output->setMaximumBlockCount(10000);
  ui.output->appendPlainText(QString("Started through rclone rc job %1.")
                                 .arg(jobId));

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
                                 "Copy the rclone rc command to the clipboard.");
  QObject::connect(ui.copy, &QToolButton::clicked, this, [=]() {
    QStringList quotedArgs;
    for (const auto &arg : mDisplayArgs) {
      if (arg.contains(' ') || arg.contains('"')) {
        quotedArgs << '"' + QString(arg).replace('"', "\\\"") + '"';
      } else {
        quotedArgs << arg;
      }
    }
    QGuiApplication::clipboard()->setText(quotedArgs.join(" "));
  });

  QObject::connect(&mPollTimer, &QTimer::timeout, this, &RcJobWidget::poll);
  mPollTimer.start(1000);

  UiPolish::SetStatus(ui.showDetails, "running", "Running");
  poll();
}

RcJobWidget::~RcJobWidget() {}

void RcJobWidget::showDetails() { ui.showDetails->setChecked(true); }

void RcJobWidget::poll() {
  if (mPollInFlight || !mRunning) {
    return;
  }
  mPollInFlight = true;

  mEngine->coreStats(
      mGroup, this,
      [this](const QJsonObject &stats, const QString &statsError) {
        if (statsError.isEmpty()) {
          applyStats(stats);
        }

        mEngine->jobStatus(
            mJobId, this,
            [this](const QJsonObject &status, const QString &error) {
              mPollInFlight = false;
              if (!error.isEmpty()) {
                ui.output->appendPlainText(error);
                Diagnostics::appendLog("rc-job", error);
                return;
              }
              if (status.value("finished").toBool()) {
                const bool success = status.value("success").toBool();
                const QString jobError = status.value("error").toString();
                const QJsonValue output = status.value("output");
                if (output.isString() && !output.toString().isEmpty()) {
                  QString text = output.toString().trimmed();
                  ui.output->appendPlainText(text);
                  Diagnostics::appendLog("rc-job", text);
                }
                finish(success, jobError);
              }
            });
      });
}

void RcJobWidget::applyStats(const QJsonObject &stats) {
  const double bytes = stats.value("bytes").toDouble();
  const double totalBytes = stats.value("totalBytes").toDouble();
  const double speed = stats.value("speed").toDouble();
  const int pct =
      totalBytes > 0 ? static_cast<int>(bytes / totalBytes * 100) : 0;
  mBytes = static_cast<qint64>(bytes);
  ui.size->setText(QString("%1, %2%")
                       .arg(GetNiceSize(static_cast<quint64>(bytes)))
                       .arg(pct));
  ui.totalsize->setText(GetNiceSize(static_cast<quint64>(totalBytes)));
  ui.bandwidth->setText(GetNiceSize(static_cast<quint64>(speed)) + "/s");
  mErrors = stats.value("errors").toInt();
  ui.errors->setText(QString::number(mErrors));
  ui.checks->setText(QString::number(stats.value("checks").toInt()));
  mFiles = stats.value("transfers").toInt();
  ui.transferred->setText(QString::number(mFiles));

  const double elapsed = stats.value("elapsedTime").toDouble();
  if (elapsed > 0) {
    const int seconds = static_cast<int>(elapsed);
    ui.elapsed->setText(QString("%1s").arg(seconds));
  }
}

void RcJobWidget::finish(bool success, const QString &error) {
  mPollTimer.stop();
  mRunning = false;
  mSuccess = success;
  mFinishedAt = QDateTime::currentDateTimeUtc();
  mExitCode = success ? 0 : 1;
  ui.cancel->setEnabled(true);
  UiPolish::SetCompactToolButton(ui.cancel, "Close transfer card",
                                 "Remove this transfer from the jobs list.");
  if (mUserCancelled) {
    UiPolish::SetStatus(ui.showDetails, "warning", "Cancelled");
  } else if (success) {
    UiPolish::SetStatus(ui.showDetails, "success", "Finished");
  } else if (error == "Cancelled.") {
    UiPolish::SetStatus(ui.showDetails, "warning", "Cancelled");
  } else {
    if (!error.isEmpty()) {
      ui.output->appendPlainText(error);
    }
    UiPolish::SetStatus(ui.showDetails, "error", "Needs attention");
    ui.showDetails->setChecked(true);
    ui.showOutput->setChecked(true);
  }
  emit finished(ui.info->text());
}

JobHistoryEntry RcJobWidget::historyEntry() const {
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
  return entry;
}

void RcJobWidget::cancel() {
  if (!mRunning) {
    return;
  }
  if (mStopping) {
    return;
  }
  mStopping = true;
  mUserCancelled = true;
  UiPolish::SetStatus(ui.showDetails, "warning", "Stopping");
  ui.cancel->setEnabled(false);
  ui.cancel->setToolTip("Stopping rclone rc job...");
  ui.output->appendPlainText(
      QString("Cancel requested for rclone rc job %1.").arg(mJobId));
  mEngine->stopJob(mJobId, this,
                   [this](bool, const QString &error) {
                     if (!error.isEmpty()) {
                       ui.output->appendPlainText(error);
                       Diagnostics::appendLog("rc-job", error);
                       mStopping = false;
                       mUserCancelled = false;
                       ui.cancel->setEnabled(true);
                       UiPolish::SetStatus(ui.showDetails, "error",
                                           "Stop failed");
                       ui.showDetails->setChecked(true);
                       ui.showOutput->setChecked(true);
                     }
                   });
  QTimer::singleShot(5000, this, [this]() {
    if (mRunning && mStopping) {
      ui.output->appendPlainText(
          "Stop request accepted; marking this rc job cancelled locally.");
      finish(false, "Cancelled.");
    }
  });
}
