#include "rc_job_widget.h"
#include "adaptive_bandwidth_driver.h"
#include "rclone_rc_engine.h"
#include "interface_polish.h"
#include "rclone_capabilities.h"
#include "rc_job_result.h"
#include "rclone_exit_code.h"
#include "utils.h"

#include <limits>

RcJobWidget::RcJobWidget(RcloneRcEngine *engine, int jobId,
                         const QString &group, const QString &info,
                         const QStringList &displayArgs, const QString &source,
                         const QString &dest, QWidget *parent)
    : QWidget(parent), mEngine(engine), mJobId(jobId),
      mGroup(group.isEmpty() ? QString("job/%1").arg(jobId) : group),
      mDisplayArgs(displayArgs), mInfo(info), mSource(source), mDest(dest) {
  mStartedAt = QDateTime::currentDateTimeUtc();
  mAttemptClock.start();
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
  ui.info->setTextFormat(Qt::PlainText);
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

  buildBandwidthControl();

  QObject::connect(&mPollTimer, &QTimer::timeout, this, &RcJobWidget::poll);
  mPollTimer.start(1000);

  UiPolish::SetStatus(ui.showDetails, "running", "Running");
  poll();
}

RcJobWidget::~RcJobWidget() {}

void RcJobWidget::showDetails() { ui.showDetails->setChecked(true); }

void RcJobWidget::buildBandwidthControl() {
  // Only jobs running through the daemon can be retuned while they run. A
  // process-backed transfer is a plain rclone with its limit fixed on the
  // command line, and JobWidget has no equivalent of this for that reason.
  auto *panel = new QWidget(this);
  panel->setObjectName("bandwidthPanel");
  auto *lines = new QVBoxLayout(panel);
  lines->setContentsMargins(0, 0, 0, 0);
  lines->setSpacing(4);

  auto *row = new QWidget(panel);
  row->setObjectName("bandwidthRow");
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  auto *label = new QLabel("Bandwidth:", row);
  mBandwidth = new QLineEdit(row);
  mBandwidth->setObjectName("bandwidthLimit");
  mBandwidth->setAccessibleName("Bandwidth limit");
  mBandwidth->setPlaceholderText("off");
  mBandwidth->setMaximumWidth(120);
  mBandwidth->setToolTip(
      "Rate for all transfers running through the remote control daemon, "
      "in the same form as --bwlimit: 1M, 512k, 10M:1M for upload:download, "
      "or off. This is shared, not per transfer.");

  mBandwidthApply = new QPushButton("Apply", row);
  mBandwidthApply->setObjectName("bandwidthApply");
  mBandwidthApply->setAccessibleName("Apply bandwidth limit");

  mBandwidthNote = new QLabel(row);
  mBandwidthNote->setObjectName("bandwidthNote");
  mBandwidthNote->setTextFormat(Qt::PlainText);
  UiPolish::SetMuted(mBandwidthNote);

  layout->addWidget(label);
  layout->addWidget(mBandwidth);
  layout->addWidget(mBandwidthApply);
  layout->addWidget(mBandwidthNote);
  layout->addStretch();

  auto *adaptiveRow = new QWidget(panel);
  adaptiveRow->setObjectName("adaptiveBandwidthRow");
  auto *adaptiveLayout = new QHBoxLayout(adaptiveRow);
  adaptiveLayout->setContentsMargins(0, 0, 0, 0);
  adaptiveLayout->setSpacing(6);
  mAdaptive = new QCheckBox("Slow down while other apps use the network, to",
                            adaptiveRow);
  mAdaptive->setObjectName("adaptiveBandwidth");
  mAdaptive->setAccessibleName("Slow down while other apps use the network");
  mAdaptive->setToolTip(
      "Watches the computer's network traffic. While other apps use the "
      "network, transfers on this daemon drop to the rate on the right. "
      "When they stop, the limit above comes back.");
  mAdaptiveFloor = new QLineEdit(adaptiveRow);
  mAdaptiveFloor->setObjectName("adaptiveBandwidthFloor");
  mAdaptiveFloor->setAccessibleName("Rate while other apps use the network");
  mAdaptiveFloor->setPlaceholderText("1M");
  mAdaptiveFloor->setMaximumWidth(120);
  mAdaptiveFloor->setToolTip("Same form as the bandwidth limit, such as 1M "
                             "or 512k.");
  mAdaptiveFloor->setText(
      GetSettings()->value("Settings/adaptiveBandwidthFloor").toString());
  adaptiveLayout->addWidget(mAdaptive);
  adaptiveLayout->addWidget(mAdaptiveFloor);
  adaptiveLayout->addStretch();

  lines->addWidget(row);
  lines->addWidget(adaptiveRow);

  // Row 5 columns 0 and 1 hold Show Output; the rest of that row is free.
  // gridLayout_2 is a QGridLayout in the .ui and is used as one here, rather
  // than cast to something it is not.
  ui.gridLayout_2->addWidget(panel, 5, 2, 1, 6);

  QObject::connect(mBandwidthApply, &QPushButton::clicked, this,
                   &RcJobWidget::applyBandwidthLimit);
  QObject::connect(mBandwidth, &QLineEdit::returnPressed, this,
                   &RcJobWidget::applyBandwidthLimit);
  QObject::connect(mAdaptiveFloor, &QLineEdit::returnPressed, this,
                   &RcJobWidget::applyBandwidthLimit);
  QObject::connect(mAdaptive, &QCheckBox::toggled, this,
                   &RcJobWidget::setAdaptive);

  // The daemon's limit and its adaptive mode are shared by every card on it,
  // so a card opened while adaptive mode runs picks up that state.
  mAdaptiveDriver = AdaptiveBandwidthDriver::forEngine(mEngine);
  QObject::connect(mAdaptiveDriver, &AdaptiveBandwidthDriver::stateChanged,
                   this, &RcJobWidget::showAdaptiveState);
  if (mAdaptiveDriver->isActive()) {
    mBandwidth->setText(mAdaptiveDriver->ceiling() == QLatin1String("off")
                            ? QString()
                            : mAdaptiveDriver->ceiling());
    mAdaptiveFloor->setText(mAdaptiveDriver->floor());
    showAdaptiveState();
    return;
  }

  // Show what the daemon currently has rather than an empty box.
  mEngine->bandwidthLimit(
      QString(), this,
      [this](const QJsonObject &response, const QString &error) {
        showBandwidthLimit(response, error);
      });
}

QString RcJobWidget::floorRate() const {
  const QString rate = mAdaptiveFloor->text().trimmed();
  return rate.isEmpty() ? mAdaptiveFloor->placeholderText() : rate;
}

void RcJobWidget::setAdaptive(bool on) {
  if (!on) {
    mAdaptiveDriver->stop();
    return;
  }
  QString ceiling = mBandwidth->text().trimmed();
  if (ceiling.isEmpty()) {
    ceiling = QStringLiteral("off");
  }
  GetSettings()->setValue("Settings/adaptiveBandwidthFloor",
                          mAdaptiveFloor->text().trimmed());
  mAdaptiveDriver->start(ceiling, floorRate());
}

void RcJobWidget::showAdaptiveState() {
  {
    const QSignalBlocker blocker(mAdaptive);
    mAdaptive->setChecked(mAdaptiveDriver->isActive());
  }
  if (!mAdaptiveDriver->isActive()) {
    if (!mAdaptiveDriver->problem().isEmpty()) {
      mBandwidthNote->setText(mAdaptiveDriver->problem());
      UiPolish::SetNotice(mBandwidthNote, mAdaptiveDriver->problem());
      return;
    }
    // Adaptive mode put the ceiling back, so show the daemon's own answer.
    mEngine->bandwidthLimit(
        QString(), this,
        [this](const QJsonObject &response, const QString &error) {
          showBandwidthLimit(response, error);
        });
    return;
  }
  const QString ceiling = mAdaptiveDriver->ceiling() == QLatin1String("off")
                              ? QString("no limit")
                              : mAdaptiveDriver->ceiling();
  mBandwidthNote->setText(
      mAdaptiveDriver->isThrottled()
          ? QString("Other apps are using the network, so every transfer on "
                    "this daemon is held to %1.")
                .arg(mAdaptiveDriver->floor())
          : QString("%1 while the network is free, %2 while other apps use "
                    "it.")
                .arg(ceiling, mAdaptiveDriver->floor()));
  UiPolish::SetMuted(mBandwidthNote);
}

void RcJobWidget::applyBandwidthLimit() {
  if (!mBandwidth) {
    return;
  }
  QString rate = mBandwidth->text().trimmed();
  if (rate.isEmpty()) {
    rate = QStringLiteral("off");
  }
  if (mAdaptiveDriver && mAdaptiveDriver->isActive()) {
    // The daemon holds the floor while other apps are busy, so the new
    // ceiling goes to the driver, which applies it once the network is free.
    GetSettings()->setValue("Settings/adaptiveBandwidthFloor",
                            mAdaptiveFloor->text().trimmed());
    mAdaptiveDriver->setFloor(floorRate());
    mAdaptiveDriver->setCeiling(rate);
    return;
  }
  mBandwidthApply->setEnabled(false);
  mBandwidthNote->setText("Applying...");
  UiPolish::SetMuted(mBandwidthNote);

  mEngine->bandwidthLimit(
      rate, this, [this](const QJsonObject &response, const QString &error) {
        showBandwidthLimit(response, error);
      });
}

void RcJobWidget::showBandwidthLimit(const QJsonObject &response,
                                     const QString &error) {
  if (!mBandwidth) {
    return;
  }
  mBandwidthApply->setEnabled(true);
  if (!error.isEmpty()) {
    mBandwidthNote->setText(error);
    UiPolish::SetNotice(mBandwidthNote, error);
    Diagnostics::appendLog("rc-job", error);
    return;
  }

  // rclone rewrites what it was given, so "1M" comes back as "1Mi". Showing
  // the daemon's own answer is the only way the field can be trusted.
  const QString rate = response.value("rate").toString();
  mBandwidth->setText(rate == QLatin1String("off") ? QString() : rate);
  mBandwidthNote->setText(rate == QLatin1String("off")
                              ? QString("No limit, shared by every transfer "
                                        "on this daemon.")
                              : QString("%1 shared by every transfer on this "
                                        "daemon.")
                                    .arg(rate));
  UiPolish::SetMuted(mBandwidthNote);
}

void RcJobWidget::poll() {
  if (mPollInFlight || !mRunning) {
    return;
  }
  mPollInFlight = true;

  readGroup(
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
                // Cancel has nothing left to stop from here on, so a click
                // during the final stats read can't relabel the result. It
                // still keeps a failed attempt from being tried again.
                mJobEnded = true;
                // The job's own time, which ends where the job did, however
                // late this poll came.
                const double duration = status.value("duration").toDouble(-1);
                mElapsedDone += duration >= 0
                                    ? duration
                                    : mAttemptClock.elapsed() / 1000.0;
                mJobEndTime = status.value("endTime").toString();
                showElapsed();
                const RcJobResult::Result result =
                    RcJobResult::Read(status, mBatchLabels);
                // One append: a long run's log line by line stalls the window.
                if (!result.output.isEmpty()) {
                  ui.output->appendPlainText(result.output.join(QChar('\n')));
                }
                for (const QString &line : result.problems) {
                  Diagnostics::appendLog("rc-job", line);
                }
                noteDeleteCap(result.deleteCapLine);
                mFromCommand = result.fromCommand;
                mBisyncRan = status.value("output").toObject().contains(
                    QLatin1String("session"));
                const bool success = result.success;
                const QString jobError = result.error;
                const int exitCode = result.exitCode;
                // A limit stop reads as a success with no error, yet it's the
                // error the job returned.
                const QString returned = status.value("error").toString();
                // rclone can record lastError, and count the last transfers,
                // after the stats read at the top of this poll and before
                // the job reports finished. Read them once more so a
                // --max-delete stop is never missed and the transfer count
                // is final. Polling stays held off until finish().
                mPollInFlight = true;
                readGroup(
                    [this, success, jobError, exitCode,
                     returned](const QJsonObject &finalStats,
                               const QString &finalError) {
                      mPollInFlight = false;
                      const bool haveStats = finalError.isEmpty();
                      if (haveStats) {
                        applyStats(finalStats);
                        mFinalStats = true;
                      }
                      // A soft or cautious --max-transfer stop is the job's
                      // error only when the try had no plain one to return,
                      // yet rclone can have counted one it never returned
                      // (a destination file it couldn't hash, a modtime it
                      // couldn't set). cmd.Run retries any try whose group
                      // has a retry error, so the card does too. A flag an
                      // earlier try left in the group says nothing about
                      // this one (see noteLimitStop).
                      const bool retryableStop =
                          success && exitCode == 8 && haveStats &&
                          !finalStats.value("fatalError").toBool() &&
                          finalStats.value("retryError").toBool() &&
                          !mRetryFlagCarried;
                      // A try that returned nothing can still have counted
                      // errors of that kind with no limit in play. cmd.Run
                      // then takes the group's last error as the try's and
                      // retries or fails on it as on a returned one
                      // (cmd/cmd.go:256-259), so the run doesn't end at 0.
                      const bool countedError =
                          success && exitCode == 0 && haveStats &&
                          !mRetryEndpoint.isEmpty() &&
                          finalStats.value("errors").toInt() > mErrorBase;
                      const QString tryError =
                          countedError
                              ? finalStats.value("lastError").toString()
                              : (retryableStop ? returned : jobError);
                      if ((!success || retryableStop || countedError) &&
                          retryAfterFailure(finalStats, haveStats, tryError,
                                            retryableStop)) {
                        return;
                      }
                      if (countedError) {
                        finish(false, tryError);
                        return;
                      }
                      // A success with an exit code is a limit stop (8 or
                      // 10). The 9 of --error-on-no-transfer comes later.
                      if (success && exitCode > 0) {
                        noteLimitStop(finalStats, haveStats, exitCode);
                      } else if (success && mAttempt > 1) {
                        ui.output->appendPlainText(
                            QString("Attempt %1/%2 succeeded")
                                .arg(mAttempt)
                                .arg(mAttempts));
                      }
                      finish(success, jobError, exitCode);
                    });
              }
            });
      });
}

void RcJobWidget::readGroup(
    const std::function<void(const QJsonObject &, const QString &)> &done) {
  mEngine->coreStats(
      mGroup, this,
      [this, done](const QJsonObject &stats, const QString &statsError) {
        if (!mTrackDeletes || !statsError.isEmpty()) {
          done(stats, statsError);
          return;
        }
        // Read after the stats, so by the time the job has ended this lists
        // at least the deletes they count.
        mEngine->transferred(
            mGroup, this,
            [this, done, stats, statsError](const QJsonObject &listed,
                                            const QString &listError) {
              if (listError.isEmpty()) {
                RcJobResult::AddDeletes(listed, &mGroupDeletes);
              }
              done(stats, statsError);
            });
      });
}

qint64 RcJobWidget::deletedBytesSoFar() const {
  if (!mTrackDeletes || mCarriedDeletedBytes < 0 ||
      RcJobResult::DeleteCount(mGroupDeletes) != mThisAttempt.deletes) {
    return -1;
  }
  return mCarriedDeletedBytes + RcJobResult::DeletedBytes(mGroupDeletes);
}

void RcJobWidget::applyStats(const QJsonObject &stats) {
  mThisAttempt.bytes = stats.value("bytes").toDouble();
  mThisAttempt.totalBytes = stats.value("totalBytes").toDouble();
  mThisAttempt.transfers = stats.value("transfers").toInt();
  mThisAttempt.checks = stats.value("checks").toInt();
  mThisAttempt.deletes = stats.value("deletes").toInteger();
  const double bytes = mCarried.bytes + mThisAttempt.bytes;
  const double totalBytes = mCarried.totalBytes + mThisAttempt.totalBytes;
  const double speed = stats.value("speed").toDouble();
  const int pct =
      totalBytes > 0 ? static_cast<int>(bytes / totalBytes * 100) : 0;
  mBytes = static_cast<qint64>(bytes);
  ui.size->setText(QString("%1, %2%")
                       .arg(GetNiceSize(static_cast<quint64>(bytes)))
                       .arg(pct));
  ui.totalsize->setText(GetNiceSize(static_cast<quint64>(totalBytes)));
  ui.bandwidth->setText(GetNiceSize(static_cast<quint64>(speed)) + "/s");
  // The current attempt's errors, as cmd.Run counts them.
  mGroupErrors = stats.value("errors").toInt();
  mGroupRetryFlag = stats.value("retryError").toBool();
  mErrors = qMax(0, mGroupErrors - mErrorBase);
  ui.errors->setText(QString::number(mErrors));
  ui.checks->setText(
      QString::number(mCarried.checks + mThisAttempt.checks));
  mFiles = mCarried.transfers + mThisAttempt.transfers;
  ui.transferred->setText(QString::number(mFiles));
  noteDeleteCap(stats.value("lastError").toString());
  showElapsed();
}

void RcJobWidget::showElapsed() {
  const double elapsed =
      mElapsedDone + (mJobEnded ? 0 : mAttemptClock.elapsed() / 1000.0);
  if (elapsed > 0) {
    const int seconds = static_cast<int>(elapsed);
    ui.elapsed->setText(QString("%1s").arg(seconds));
  }
}

void RcJobWidget::noteDeleteCap(const QString &text) {
  if (mDeleteCapHit) {
    return;
  }
  const QString line = RcJobResult::DeleteCapLine(text);
  if (!line.isEmpty()) {
    mDeleteCapHit = true;
    mDeleteCapLine = line;
  }
}

void RcJobWidget::setStartedJob(const RcloneRcEngine::StartedJob &job) {
  mRetryEndpoint = job.endpoint;
  mRetryPayload = job.payload;
  mAttemptPayload = job.payload;
  mAttempts = qMax(1, job.attempts);
  mRetrySleepMs = qMax<qint64>(0, job.retrySleepMs);
  mDaemonMain = job.daemonMain;
  mTrackDeletes = mAttempts > 1 && !mRetryEndpoint.isEmpty() &&
                  mRetryEndpoint != QLatin1String("sync/bisync");
  if (!job.routeNote.isEmpty()) {
    ui.output->appendPlainText(
        QString("Live progress isn't shown for this run because %1. It runs "
                "as a separate rclone, and its results show here when it's "
                "done.")
            .arg(job.routeNote));
  }
}

bool RcJobWidget::retryAfterFailure(const QJsonObject &stats, bool haveStats,
                                    const QString &jobError,
                                    bool uncountedStop) {
  // core/command's child rclone runs this loop itself, and a batch delete
  // has nothing to post again. bisync runs cmd.Run with Retry off
  // (cmd/bisync/cmd.go), which tries once and logs none of these lines.
  if (mRetryEndpoint.isEmpty() || mUserCancelled || mStopping) {
    return false;
  }
  if (mRetryEndpoint == QLatin1String("sync/bisync")) {
    // The CLI exits on what bisync returned, and its command wraps "bisync
    // aborted" in a fatal error, which rc hands back bare. A path that
    // couldn't be opened stays 1, but the command refuses a path that's a
    // file before cmd.Run ("paths must be existing directories") and exits
    // 2, where rc fails opening it with fs.ErrorIsFile (measured on
    // v1.76.0).
    if (mBisyncRan) {
      mStopExit = RcJobResult::ExitCodeForError(
          jobError, jobError == QLatin1String("bisync aborted"), false);
    } else if (jobError == QLatin1String("is a file not a directory")) {
      mStopExit = 2;
    }
    return false;
  }
  // The current attempt's stats, as cmd.Run sees them. In a group an
  // earlier attempt used too, the fatal flag can't be that attempt's, since
  // a fatal error ends the run, but the retry flag can.
  int attemptErrors =
      haveStats ? qMax(0, stats.value("errors").toInt() - mErrorBase) : 0;
  bool fatal = haveStats && stats.value("fatalError").toBool();
  bool retryable = haveStats && stats.value("retryError").toBool();
  if (retryable && mRetryFlagCarried) {
    // The flag was already up when this attempt started, and rc can't say
    // what this one added. A sync returns a plain error ahead of a no-retry
    // one, so a no-retry error back means it had no plain one to return.
    // rclone also counts errors it never returns (a failed hash or modtime
    // update), which the CLI would retry and this can't see, which is why
    // an attempt only shares a group when the caps leave no other way.
    retryable = !RcJobResult::IsNoRetryError(jobError);
  }
  QString lastError =
      haveStats ? stats.value("lastError").toString() : QString();
  if (uncountedStop && haveStats) {
    // cmd.Run's CountError counts the stop the try returned, so it's one
    // error more and the last one (cmd/cmd.go:256).
    attemptErrors++;
    lastError = jobError;
  } else if (haveStats && attemptErrors == 0) {
    // cmd.Run counts the error the run returned when nothing counted it
    // yet, as a sync's own checks before it starts return theirs.
    switch (RcJobResult::ClassifyUncounted(jobError)) {
    case RcJobResult::Uncounted::Retry:
      attemptErrors = 1;
      retryable = true;
      lastError = jobError;
      break;
    case RcJobResult::Uncounted::Fatal:
      attemptErrors = 1;
      fatal = true;
      lastError = jobError;
      break;
    case RcJobResult::Uncounted::Unknown:
      break;
    }
  }
  // The lines below are cmd.Run's own, so the output reads like a CLI run.
  // --max-delete is a fatal error there too.
  if (mDeleteCapHit || RcloneExitCode::IsDeleteCapError(jobError) || fatal) {
    ui.output->appendPlainText(
        QStringLiteral("Fatal error received - not attempting retries"));
    // A sync returns its fatal error ahead of any other, and the CLI exits
    // on it (resolveExitCode in cmd/cmd.go): 7, unless its cause is a
    // missing folder or file or one worth retrying, as a --backup-dir on a
    // server that refused the connection is.
    mStopExit = RcJobResult::ExitCodeForError(jobError, true, false);
    return false;
  }
  if (!haveStats) {
    // Without the stats there's no telling what kind of errors these were.
    return false;
  }
  if (attemptErrors == 0) {
    // The job failed without counting an error, which is how a remote that
    // can't be opened fails (a missing section, a bad backend option). The
    // CLI fails those before its retry loop starts, so it never retries
    // them and prints no attempt line.
    return false;
  }
  if (!retryable) {
    ui.output->appendPlainText(QStringLiteral(
        "Can't retry any of the errors - not attempting retries"));
    // The error the CLI exits on is then a no-retry one, its own count
    // having taken any other as one to retry, and that's exit 6 unless its
    // cause comes first.
    mStopExit = RcJobResult::ExitCodeForError(jobError, false, true);
    return false;
  }
  if (lastError.isEmpty()) {
    lastError = jobError;
  }
  // cmd.Run sleeps out a backend's retry-after time before its attempt
  // line, then sleeps --retries-sleep. It does both after the last try as
  // well, where the card has no next try to wait for.
  mRetryAfterMs = 0;
  if (mAttempt < mAttempts && !mCancelBeforeRetry) {
    const RcJobResult::RetryAfter wait = RcJobResult::RetryAfterWait(
        QStringList{stats.value("lastError").toString(), lastError, jobError},
        mJobEndTime);
    if (wait.waitMs > 0) {
      mRetryAfterMs = wait.waitMs;
      const QString waitLine =
          QString("Received retry after error - sleeping until %1 (%2)")
              .arg(wait.until, wait.left);
      ui.output->appendPlainText(waitLine);
      Diagnostics::appendLog("rc-job", waitLine);
    }
  }
  const QString line =
      lastError.isEmpty()
          ? QString("Attempt %1/%2 failed with %3 errors")
                .arg(mAttempt)
                .arg(mAttempts)
                .arg(attemptErrors)
          : QString("Attempt %1/%2 failed with %3 errors and: %4")
                .arg(mAttempt)
                .arg(mAttempts)
                .arg(attemptErrors)
                .arg(lastError);
  ui.output->appendPlainText(line);
  Diagnostics::appendLog("rc-job", line);
  if (mAttempt >= mAttempts) {
    // The tries ran out, and the CLI exits on the error the last one
    // returned: 3 for a missing folder, 5 for a dropped connection, 6 for
    // a no-retry one.
    mStopExit = RcJobResult::ExitCodeForError(jobError, false, false);
    return false;
  }
  if (mCancelBeforeRetry) {
    // Cancel was clicked after the job ended and before its last stats
    // came back. There was no job left to stop, but there's a retry not
    // to start. A limit stop the job ended on stays the run's result, as
    // the history records it a success.
    if (!uncountedStop) {
      mUserCancelled = true;
    }
    ui.output->appendPlainText(
        QStringLiteral("Cancelled before the next attempt started."));
    return false;
  }
  startNextAttempt();
  return true;
}

void RcJobWidget::noteLimitStop(const QJsonObject &stats, bool haveStats,
                                int exitCode) {
  // A --max-transfer or --max-duration stop maps to a successful exit, but
  // cmd.Run still counted it as an error and says why it didn't retry, on
  // any attempt. core/command's child prints these lines itself, and
  // bisync's cmd.Run, with Retry off, prints none.
  if (mRetryEndpoint.isEmpty() ||
      mRetryEndpoint == QLatin1String("sync/bisync")) {
    return;
  }
  // A sync returns its --max-duration stop as a fatal error it never
  // counted, and cmd.Run counts it, so the CLI gives the fatal line in
  // every cutoff mode (measured on v1.76.0) whatever the group holds.
  if (exitCode == 10 || (haveStats && stats.value("fatalError").toBool())) {
    ui.output->appendPlainText(
        QStringLiteral("Fatal error received - not attempting retries"));
  } else if (haveStats && (!stats.value("retryError").toBool() ||
                           mRetryFlagCarried)) {
    // A --max-transfer stop with --cutoff-mode soft or cautious counts no
    // fatal error. A group's retry flag from an earlier attempt says nothing
    // here, and the job returned the stop, a no-retry error, so it had no
    // plain one to return (see retryAfterFailure for the ones it counts
    // without returning). A flag this attempt raised went through
    // retryAfterFailure first, and gets here only once the tries ran out or
    // a cancel stopped the next one.
    ui.output->appendPlainText(QStringLiteral(
        "Can't retry any of the errors - not attempting retries"));
  }
}

void RcJobWidget::startNextAttempt() {
  mRetryPending = true;
  // Holds off the poll timer until the next job is up.
  mPollInFlight = true;
  UiPolish::SetStatus(ui.showDetails, "warning", "Retrying");
  const int delay = static_cast<int>(
      qBound(qint64(0), mRetryAfterMs + mRetrySleepMs,
             qint64(std::numeric_limits<int>::max())));
  QTimer::singleShot(delay, this, [this]() {
    if (mFinished || !mRetryPending) {
      return;
    }
    // The caps the next attempt gets from the daemon are the ones it has
    // now, which another client can have changed since it connected. One
    // it won't say leaves them unknown.
    mEngine->daemonSettings(
        this, [this](const QJsonObject &values, const QString &error) {
          mDaemonMain = error.isEmpty() ? values.value("main").toObject()
                                        : QJsonObject();
          if (mFinished || !mRetryPending) {
            return;
          }
          submitNextAttempt();
        });
  });
}

void RcJobWidget::submitNextAttempt() {
  // A group of its own for the next attempt where the caps allow it (see
  // mCarried).
  const qint64 deletedBytes = deletedBytesSoFar();
  const RcJobResult::NextAttempt next = RcJobResult::PlanNextAttempt(
      mRetryPayload, mAttemptPayload, mAttempt + 1,
      static_cast<qint64>(mCarried.bytes + mThisAttempt.bytes),
      mCarried.deletes + mThisAttempt.deletes, mDaemonMain, deletedBytes,
      mCarriedDeletedBytes < 0);
  const QJsonObject payload = next.payload;
  const bool sameGroup = next.sameGroup;
  const QString group = payload.value(QStringLiteral("_group")).toString();
  // The engine is the context, so a job started after this card went
  // away is stopped rather than left running with nothing watching it.
  const QPointer<RcJobWidget> self(this);
  RcloneRcEngine *engine = mEngine;
  engine->resubmit(
      mRetryEndpoint, payload, engine,
      [self, engine, group, payload, sameGroup,
       deletedBytes](int jobId, const QString &error) {
        if (!self || self->mFinished) {
          if (jobId >= 0) {
            engine->stopJob(jobId, engine, nullptr);
          }
          return;
        }
        self->mRetryPending = false;
        if (jobId < 0) {
          self->mPollInFlight = false;
          self->finish(false, error);
          return;
        }
        self->mAttempt++;
        self->mJobId = jobId;
        self->mGroup =
            group.isEmpty() ? QString("job/%1").arg(jobId) : group;
        self->mAttemptPayload = payload;
        // cmd.Run's elapsed time takes in its retry-after and
        // --retries-sleep waits before a retry. It also waits after the last
        // attempt, when the card has nothing to wait for, so there the CLI's
        // time comes out one wait longer.
        self->mElapsedDone +=
            (self->mRetryAfterMs + self->mRetrySleepMs) / 1000.0;
        self->mAttemptClock.start();
        if (sameGroup) {
          // The group goes on counting, so only its errors need a base.
          self->mErrorBase = self->mGroupErrors;
          self->mRetryFlagCarried =
              self->mRetryFlagCarried || self->mGroupRetryFlag;
        } else {
          Totals &carried = self->mCarried;
          const Totals &done = self->mThisAttempt;
          carried.bytes += done.bytes;
          carried.totalBytes += done.totalBytes;
          carried.transfers += done.transfers;
          carried.checks += done.checks;
          carried.deletes += done.deletes;
          self->mCarriedDeletedBytes = deletedBytes;
          self->mGroupDeletes.clear();
          self->mThisAttempt = Totals();
          self->mErrorBase = 0;
          self->mGroupErrors = 0;
          self->mGroupRetryFlag = false;
          self->mRetryFlagCarried = false;
        }
        self->mJobEnded = false;
        self->mFinalStats = false;
        self->mPollInFlight = false;
        self->ui.output->appendPlainText(
            QString("Attempt %1/%2 started as rclone rc job %3.")
                .arg(self->mAttempt)
                .arg(self->mAttempts)
                .arg(jobId));
        UiPolish::SetStatus(self->ui.showDetails, "running", "Running");
        self->poll();
      });
}

void RcJobWidget::finish(bool success, const QString &error, int exitCode) {
  // The cancel timer and a poll still in flight can both get here.
  if (mFinished) {
    return;
  }
  mFinished = true;
  mPollTimer.stop();
  mRunning = false;
  mSuccess = success;
  mFinishedAt = QDateTime::currentDateTimeUtc();
  mExitCode = exitCode >= 0 ? exitCode : (success ? 0 : 1);
  ui.cancel->setEnabled(true);
  UiPolish::SetCompactToolButton(ui.cancel, "Close transfer card",
                                 "Remove this transfer from the jobs list.");
  // The card keeps its own wording. History and notifications get the
  // labels the CLI route records for the same outcome.
  if (mUserCancelled || (!success && error == "Cancelled.")) {
    mStatusLabel =
        RcloneExitCode::DescribeProcess(mExitCode, false, true, false).name;
    UiPolish::SetStatus(ui.showDetails, "warning", "Cancelled");
  } else if (success) {
    if (!mFromCommand && mFinalStats) {
      // core/command's child is its own process, so the daemon's stats
      // never count its transfers. Its log gave exit 9 already. Without
      // the final stats, the count may miss the last files copied.
      mExitCode =
          RcJobResult::WithNoTransferExit(mExitCode, mFiles, mHistoryArgs);
    }
    const RcloneExitCode::Meaning meaning = RcloneExitCode::Describe(mExitCode);
    mStatusLabel = meaning.name;
    if (meaning.outcome == RcloneExitCode::Outcome::CompletedWithLimit) {
      // A --max-transfer, --max-duration or --error-on-no-transfer stop:
      // calm, as the CLI route's card shows it, and says which limit.
      ui.output->appendPlainText(meaning.explanation);
      UiPolish::SetStatus(ui.showDetails, "warning", meaning.name);
      ui.showDetails->setToolTip(meaning.explanation);
    } else {
      UiPolish::SetStatus(ui.showDetails, "success", "Finished");
    }
  } else if (mDeleteCapHit || RcloneExitCode::IsDeleteCapError(error)) {
    // The same exit the CLI route reports for this stop.
    mExitCode = 7;
    const RcloneExitCode::Meaning cap = RcloneExitCode::DeleteCap();
    mStatusLabel = cap.name;
    if (!error.isEmpty()) {
      ui.output->appendPlainText(error);
    }
    if (!mDeleteCapLine.isEmpty() && mDeleteCapLine != error) {
      ui.output->appendPlainText(mDeleteCapLine);
    }
    ui.output->appendPlainText(cap.explanation);
    UiPolish::SetStatus(ui.showDetails, "error", mStatusLabel);
    ui.showDetails->setToolTip(cap.explanation);
    ui.showDetails->setChecked(true);
    ui.showOutput->setChecked(true);
  } else {
    if (!error.isEmpty()) {
      ui.output->appendPlainText(error);
    }
    if (mStopExit >= 0) {
      mExitCode = mStopExit;
    }
    mStatusLabel = RcloneExitCode::Describe(mExitCode).name;
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
  entry.statusLabel = mStatusLabel;
  entry.args = mHistoryArgs;
  return entry;
}

void RcJobWidget::cancel() {
  if (mRunning && mRetryPending) {
    // Between attempts there's no job to stop. A resubmit already posted
    // sees mFinished and stops the job it started.
    mUserCancelled = true;
    mRetryPending = false;
    ui.output->appendPlainText(
        QStringLiteral("Cancelled before the next attempt started."));
    finish(false, "Cancelled.");
    return;
  }
  if (mRunning && mJobEnded) {
    // The job ended and its last stats are being read. Nothing is left to
    // stop, so the result stands, but a failed attempt isn't retried.
    mCancelBeforeRetry = true;
    return;
  }
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
