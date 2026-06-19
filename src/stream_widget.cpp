#include "stream_widget.h"
#include "interface_polish.h"

StreamWidget::StreamWidget(QProcess *rclone, QProcess *player,
                           const QString &remote, const QString &stream,
                           QWidget *parent)
    : QWidget(parent), mRclone(rclone), mPlayer(player) {
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
  ui.cancel->setStyleSheet(QString());
  UiPolish::SetCard(this);
  UiPolish::SetToolbarSurface(ui.widget);
  UiPolish::SetDisclosureButton(ui.showDetails, "Show stream details");
  UiPolish::SetDisclosureButton(ui.showOutput, "Show stream output");
  UiPolish::SetPathField(ui.remote, "Streaming remote file");
  UiPolish::SetPathField(ui.stream, "Player command");

  ui.remote->setText(remote);
  ui.remote->setToolTip(remote);
  ui.stream->setText(stream);
  ui.stream->setToolTip(stream);
  ui.info->setTextFormat(Qt::PlainText);
  ui.info->setText(remote);
  ui.info->setToolTip(remote);
  ui.info->setMinimumWidth(0);
  ui.info->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

  ui.details->setVisible(false);

  UiPolish::SetOutputView(ui.output);
  ui.output->setVisible(false);
  // streams can run for hours - bound memory growth
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

  ui.cancel->setIcon(
      QApplication::style()->standardIcon(QStyle::SP_DialogCloseButton));
  UiPolish::SetCompactToolButton(ui.cancel, "Stop stream",
                                 "Stop this stream and close the player pipe.");

  QObject::connect(ui.cancel, &QToolButton::clicked, this, [=]() {
    if (mRunning) {
      cancel();
    } else {
      emit closed();
    }
  });

  QObject::connect(mRclone, &QProcess::readyRead, this, [=]() {
    while (mRclone->canReadLine()) {
      ui.output->appendPlainText(mRclone->readLine().trimmed());
    }
  });

  QObject::connect(mRclone,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   this, [=](int status, QProcess::ExitStatus) {
                     mRclone->deleteLater();
                     mRunning = false;
                     ui.cancel->setEnabled(true);
                     UiPolish::SetCompactToolButton(
                         ui.cancel, "Close stream card",
                         "Remove this stream from the jobs list.");
                     if (mUserStopped) {
                       UiPolish::SetStatus(ui.showDetails, "warning",
                                           "Stopped");
                     } else if (status == 0) {
                       UiPolish::SetStatus(ui.showDetails, "success",
                                           "Finished");
                     } else {
                       UiPolish::SetStatus(ui.showDetails, "error",
                                           "Needs attention");
                       ui.showDetails->setChecked(true);
                       ui.showOutput->setChecked(true);
                     }
                     emit finished();
                   });

  UiPolish::SetStatus(ui.showDetails, "running", "Streaming");
}

StreamWidget::~StreamWidget() {}

void StreamWidget::cancel() {
  if (!mRunning) {
    return;
  }
  if (mStopping) {
    return;
  }

  mStopping = true;
  mUserStopped = true;
  UiPolish::SetStatus(ui.showDetails, "warning", "Stopping");
  ui.cancel->setEnabled(false);
  ui.cancel->setToolTip("Stopping stream...");
  ui.output->appendPlainText("Stop requested; closing stream processes...");
  mPlayer->terminate();
  mRclone->terminate();
  QPointer<QProcess> rcloneGuard(mRclone);
  QPointer<QProcess> playerGuard(mPlayer);
  QTimer::singleShot(5000, this, [rcloneGuard, playerGuard]() {
    if (rcloneGuard && rcloneGuard->state() != QProcess::NotRunning) {
      rcloneGuard->kill();
    }
    if (playerGuard && playerGuard->state() != QProcess::NotRunning) {
      playerGuard->kill();
    }
  });
}
