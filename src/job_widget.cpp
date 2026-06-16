#include "job_widget.h"
#include "interface_polish.h"
#include "rclone_capabilities.h"
#include "utils.h"

namespace {
constexpr int kMaxVisibleFileProgress = 12;
} // namespace

JobWidget::JobWidget(QProcess *process, const QString &info,
                     const QStringList &args, const QString &source,
                     const QString &dest, QWidget *parent)
    : QWidget(parent), mProcess(process) {
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
  UiPolish::SetCompactToolButton(ui.cancel, "Cancel transfer",
                                 "Cancel this running transfer.");

  QObject::connect(ui.cancel, &QToolButton::clicked, this, [=]() {
    if (mRunning) {
      int button = QMessageBox::question(
          this, "Transfer",
          QString("Cancel this transfer?\n\n"
                  "Rclone will stop as soon as the process can be terminated."),
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
      }

      if (!obj.contains("stats"))
        continue;

      QJsonObject stats = obj.value("stats").toObject();

      // overall transfer progress
      double bytes = stats.value("bytes").toDouble();
      double totalBytes = stats.value("totalBytes").toDouble();
      double speed = stats.value("speed").toDouble();
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
                     mSuccess = (status == 0);
                     if (status == 0) {
                       // no explicit colour - inherit the palette so the
                       // label stays readable in light and dark mode
                       UiPolish::SetStatus(ui.showDetails, "success",
                                           "Finished");
                     } else {
                       UiPolish::SetStatus(ui.showDetails, "error",
                                           "Needs attention");
                       // surface the rclone output so the user can see why
                       ui.showDetails->setChecked(true);
                       ui.showOutput->setChecked(true);
                     }

                     ui.cancel->setToolTip("Close");

                     emit finished(ui.info->text());
                   });

  UiPolish::SetStatus(ui.showDetails, "running", "Running");
}

JobWidget::~JobWidget() {}

void JobWidget::showDetails() { ui.showDetails->setChecked(true); }

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
