#include "mount_widget.h"
#include "utils.h"

MountWidget::MountWidget(QProcess *process, const QString &remote,
                         const QString &folder, QWidget *parent)
    : QWidget(parent), mProcess(process) {
  ui.setupUi(this);

  ui.remote->setText(remote);
  ui.folder->setText(folder);
  ui.info->setText(QString("%1 on %2").arg(remote).arg(folder));

  ui.details->setVisible(false);

  ui.output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
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

  QObject::connect(ui.cancel, &QToolButton::clicked, this, [=]() {
    if (mRunning) {
      int button = QMessageBox::question(
          this, "Unmount",
#if defined(Q_OS_WIN)
          QString("Do you want to unmount %1 drive?").arg(folder),
#else
          QString("Do you want to unmount %1 folder?").arg(folder),
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
      ui.output->appendPlainText(mProcess->readLine().trimmed());
    }
  });

  QObject::connect(mProcess,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   this, [=](int status, QProcess::ExitStatus) {
                     mProcess->deleteLater();
                     mRunning = false;
                     if (status == 0) {
                       ui.showDetails->setStyleSheet(
                           "QToolButton { border: 0; }");
                       ui.showDetails->setText("Unmounted");
                     } else {
                       ui.showDetails->setStyleSheet(
                           "QToolButton { border: 0; color: #e53935; }");
                       ui.showDetails->setText("Error");
                       ui.showDetails->setChecked(true);
                       ui.showOutput->setChecked(true);
                     }
                     ui.cancel->setToolTip("Close");
                     emit finished();
                   });

  ui.showDetails->setStyleSheet(
      "QToolButton { border: 0; color: #43a047; }");
  ui.showDetails->setText("Mounted");
}

MountWidget::~MountWidget() {}

void MountWidget::cancel() {
  if (!mRunning) {
    return;
  }

#if defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD)
  QProcess::startDetached("umount", QStringList() << ui.folder->text());
#elif defined(Q_OS_WIN32)
  QProcess *p = new QProcess();
  QStringList args;
  args << "rc";
  args << "core/quit";
  args << "--rc-addr";
  QString folder = ui.folder->text();
  unsigned short int rclone_rc_port = 19000 + (qHash(folder) % 10000);
  args << "localhost:" + QString::number(rclone_rc_port);
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
