#include "job_widget.h"
#include "utils.h"

JobWidget::JobWidget(QProcess *process, const QString &info,
                     const QStringList &args, const QString &source,
                     const QString &dest, QWidget *parent)
    : QWidget(parent), mProcess(process) {
  ui.setupUi(this);

  mArgs.append(QDir::toNativeSeparators(GetRclone()));
  mArgs.append(GetRcloneConf());
  mArgs.append(args);

  ui.source->setText(source);
  ui.dest->setText(dest);
  ui.info->setText(info);

  ui.details->setVisible(false);

  ui.output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
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

  ui.cancel->setIcon(
      QApplication::style()->standardIcon(QStyle::SP_DialogCloseButton));

  QObject::connect(ui.cancel, &QToolButton::clicked, this, [=]() {
    if (mRunning) {
      int button = QMessageBox::question(
          this, "Transfer",
          QString("rclone process is still running. Do you want to cancel it?"),
          QMessageBox::Yes | QMessageBox::No);
      if (button == QMessageBox::Yes) {
        cancel();
      }
    } else {
      emit closed();
    }
  });

  ui.copy->setIcon(
      QApplication::style()->standardIcon(QStyle::SP_FileLinkIcon));

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
    // Pre-1.42: "Transferred:    100 Bytes (50 Bytes/sec)"
    static const QRegularExpression rxSize(
        R"(^Transferred:\s+(\S+ \S+) \(([^)]+)\)$)");
    // 1.43+: "Transferred:   1.234G / 5.678 GBytes, 22%, 1.234 MBytes/s, ETA 1h2m3s"
    // 1.56+: "Transferred:   1.234 GiB / 5.678 GiB, 22%, 1.234 MiB/s, ETA 1h2m3s"
    static const QRegularExpression rxSize2(
        R"(^Transferred:\s+([\d.]+\s*\S*)\s+\/\s+([\d.]+\s*\S+),\s+(\S+),\s+([\d.]+\s*\S+),\s+ETA\s+(\S+)$)");
    static const QRegularExpression rxErrors(R"(^Errors:\s+(\d+))");
    static const QRegularExpression rxChecks(R"(^Checks:\s+(\S+)$)");
    static const QRegularExpression rxChecks2(
        R"(^Checks:\s+(\S+) \/ (\S+), ([0-9%-]+)$)");
    static const QRegularExpression rxTransferred(R"(^Transferred:\s+(\S+)$)");
    static const QRegularExpression rxTransferred2(
        R"(^Transferred:\s+(\S+) \/ (\S+), ([0-9%-]+)$)");
    static const QRegularExpression rxTime(R"(^Elapsed time:\s+(\S+)$)");
    // Pre-1.38: "*filename:   50% done.(ETA: 1h2m3s)"
    static const QRegularExpression rxProgress(
        R"(^\*([^:]+):\s*([^%]+)% done.+(ETA: [^)]+)$)");
    // 1.39+: "* filename:  50% /1.234GiB, 1.234MiB/s, 1h2m3s"
    static const QRegularExpression rxProgress2(
        R"(\*([^:]+):\s*([^%]+)% \/\S+, \S+\/s, (\S+)$)");

    while (mProcess->canReadLine()) {
      QString line = mProcess->readLine().trimmed();
      ui.output->appendPlainText(line);

      if (line.isEmpty()) {
        for (auto it = mActive.begin(), eit = mActive.end(); it != eit;
             /* empty */) {
          auto label = it.value();
          if (mUpdated.contains(label)) {
            ++it;
          } else {
            it = mActive.erase(it);
            ui.progress->removeWidget(label->buddy());
            ui.progress->removeWidget(label);
            delete label->buddy();
            delete label;
          }
        }
        mUpdated.clear();
        continue;
      }

      QRegularExpressionMatch m;

      m = rxSize.match(line);
      if (m.hasMatch()) {
        ui.size->setText(m.captured(1));
        ui.bandwidth->setText(m.captured(2));
        continue;
      }

      m = rxSize2.match(line);
      if (m.hasMatch()) {
        ui.size->setText(m.captured(1) + ", " + m.captured(3));
        ui.bandwidth->setText(m.captured(4));
        ui.eta->setText(m.captured(5));
        ui.totalsize->setText(m.captured(2));
        continue;
      }

      m = rxErrors.match(line);
      if (m.hasMatch()) {
        ui.errors->setText(m.captured(1));
        continue;
      }

      m = rxChecks.match(line);
      if (m.hasMatch()) {
        ui.checks->setText(m.captured(1));
        continue;
      }

      m = rxChecks2.match(line);
      if (m.hasMatch()) {
        ui.checks->setText(m.captured(1) + " / " + m.captured(2) + ", " +
                           m.captured(3));
        continue;
      }

      m = rxTransferred.match(line);
      if (m.hasMatch()) {
        ui.transferred->setText(m.captured(1));
        continue;
      }

      m = rxTransferred2.match(line);
      if (m.hasMatch()) {
        ui.transferred->setText(m.captured(1) + " / " +
                                m.captured(2) + ", " +
                                m.captured(3));
        continue;
      }

      m = rxTime.match(line);
      if (m.hasMatch()) {
        ui.elapsed->setText(m.captured(1));
        continue;
      }

      m = rxProgress.match(line);
      if (m.hasMatch()) {
        QString name = m.captured(1).trimmed();

        auto it = mActive.find(name);

        QLabel *label;
        QProgressBar *bar;
        if (it == mActive.end()) {
          label = new QLabel();
          label->setText(name);

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

        bar->setValue(m.captured(2).toInt());
        bar->setToolTip(m.captured(3));

        mUpdated.insert(label);
        continue;
      }

      m = rxProgress2.match(line);
      if (m.hasMatch()) {
        QString name = m.captured(1).trimmed();

        auto it = mActive.find(name);

        QLabel *label;
        QProgressBar *bar;
        if (it == mActive.end()) {
          label = new QLabel();

          QString nameTrimmed;

          if (name.length() > 47) {
            nameTrimmed = name.left(25) + "..." + name.right(19);
          } else {
            nameTrimmed = name;
          }

          label->setText(nameTrimmed);

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

        bar->setValue(m.captured(2).toInt());
        QString fullMatch = m.captured(0);
        bar->setToolTip("File name: " + name + "\nFile stats" + fullMatch.mid(fullMatch.indexOf(':')));

        mUpdated.insert(label);
      }
    }
  });

  QObject::connect(mProcess,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   this, [=](int status, QProcess::ExitStatus) {
                     mProcess->deleteLater();
                     for (auto label : mActive) {
                       ui.progress->removeWidget(label->buddy());
                       ui.progress->removeWidget(label);
                       delete label->buddy();
                       delete label;
                     }

                     mRunning = false;
                     if (status == 0) {
                       // no explicit colour - inherit the palette so the
                       // label stays readable in light and dark mode
                       ui.showDetails->setStyleSheet(
                           "QToolButton { border: 0; }");
                       ui.showDetails->setText("Finished");
                     } else {
                       ui.showDetails->setStyleSheet(
                           "QToolButton { border: 0; color: #e53935; }");
                       ui.showDetails->setText("Error");
                       // surface the rclone output so the user can see why
                       ui.showDetails->setChecked(true);
                       ui.showOutput->setChecked(true);
                     }

                     ui.cancel->setToolTip("Close");

                     emit finished(ui.info->text());
                   });

  ui.showDetails->setStyleSheet(
      "QToolButton { border: 0; color: #43a047; }");
  ui.showDetails->setText("Running");
}

JobWidget::~JobWidget() {}

void JobWidget::showDetails() { ui.showDetails->setChecked(true); }

void JobWidget::cancel() {
  if (!mRunning) {
    return;
  }

  mProcess->terminate();
  if (!mProcess->waitForFinished(5000)) {
    mProcess->kill();
    mProcess->waitForFinished();
  }

  emit closed();
}
