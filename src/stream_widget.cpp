#include "stream_widget.h"
#include "interface_polish.h"

StreamWidget::StreamWidget(QProcess *rclone, QProcess *player,
                           const QString &remote, const QString &stream,
                           QWidget *parent)
    : QWidget(parent), mRclone(rclone), mPlayer(player) {
  ui.setupUi(this);
  ui.verticalLayout->setContentsMargins(0, 0, 0, 0);
  ui.verticalLayout->setSpacing(0);
  ui.horizontalLayout->setContentsMargins(10, 8, 10, 8);
  ui.horizontalLayout->setSpacing(8);
  ui.gridLayout_2->setContentsMargins(10, 8, 10, 10);
  ui.gridLayout_2->setHorizontalSpacing(8);
  ui.gridLayout_2->setVerticalSpacing(6);
  ui.showDetails->setStyleSheet(QString());
  ui.showOutput->setStyleSheet(QString());
  ui.cancel->setStyleSheet(QString());
  UiPolish::SetCard(this);
  UiPolish::SetToolbarSurface(ui.widget);

  ui.remote->setText(remote);
  ui.remote->setToolTip(remote);
  ui.stream->setText(stream);
  ui.stream->setToolTip(stream);
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
      int button = QMessageBox::question(
          this, "Stop Stream", QString("Stop streaming %1?").arg(remote),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (button == QMessageBox::Yes) {
        cancel();
      }
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
                     if (status == 0) {
                       UiPolish::SetStatus(ui.showDetails, "success",
                                           "Finished");
                     } else {
                       UiPolish::SetStatus(ui.showDetails, "error",
                                           "Needs attention");
                       ui.showDetails->setChecked(true);
                       ui.showOutput->setChecked(true);
                     }
                     ui.cancel->setToolTip("Close");
                     emit finished();
                   });

  UiPolish::SetStatus(ui.showDetails, "running", "Streaming");
  ui.showOutput->setAccessibleName("Show stream output");
}

StreamWidget::~StreamWidget() {}

void StreamWidget::cancel() {
  if (!mRunning) {
    return;
  }

  mPlayer->terminate();
  mRclone->terminate();
  if (!mRclone->waitForFinished(5000)) {
    mRclone->kill();
    mRclone->waitForFinished();
  }
  if (mPlayer->state() != QProcess::NotRunning) {
    mPlayer->kill();
    mPlayer->waitForFinished(2000);
  }
}
