#include "progress_dialog.h"
#include "interface_polish.h"

ProgressDialog::ProgressDialog(const QString &title, const QString &operation,
                               const QString &message, QProcess *process,
                               QWidget *parent, bool close, bool trim)
    : QDialog(parent) {
  ui.setupUi(this);
  if (layout()) {
    layout()->setContentsMargins(12, 12, 12, 12);
    layout()->setSpacing(8);
  }
  UiPolish::SetWindowDefaults(this, QSize(620, 180));
  resize(width(), 0);

  setWindowTitle(title);
  ui.labelOperation->setText(operation);
  ui.labelInfo->setText(message);
  ui.labelInfo->setWordWrap(true);
  ui.labelInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
  UiPolish::SetMuted(ui.labelOperation);
  ui.buttonShowOutput->setStyleSheet(QString());
  UiPolish::SetDisclosureButton(ui.buttonShowOutput, "Show command output");
  UiPolish::SetStatus(ui.buttonShowOutput, "running", "Running");

  UiPolish::SetOutputView(ui.output);
  ui.output->setReadOnly(true);
  ui.output->setVisible(false);

  QObject::connect(ui.buttonBox, &QDialogButtonBox::rejected, this,
                   &QDialog::reject);

  QObject::connect(ui.buttonShowOutput, &QPushButton::toggled, this,
                   [=](bool checked) {
                     ui.output->setVisible(checked);
                     ui.buttonShowOutput->setArrowType(
                         checked ? Qt::DownArrow : Qt::RightArrow);
                     if (!checked) {
                       adjustSize();
                     }
                   });

  QObject::connect(process,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                    this, [=](int code, QProcess::ExitStatus status) {
                      if (status == QProcess::NormalExit && code == 0) {
                        UiPolish::SetStatus(ui.buttonShowOutput, "success",
                                            "Finished");
                        ui.buttonBox->setEnabled(true);
                        if (close) {
                          emit accept();
                        }
                      } else {
                        UiPolish::SetStatus(ui.buttonShowOutput, "error",
                                            "Failed");
                        if (ui.output->toPlainText().trimmed().isEmpty()) {
                          ui.output->appendPlainText(
                              QString("Command exited with status %1.")
                                  .arg(code));
                        }
                        ui.buttonShowOutput->setChecked(true);
                        ui.buttonBox->setEnabled(true);
                      }
                    });

  QObject::connect(process, &QProcess::errorOccurred, this,
                   [=](QProcess::ProcessError) {
                     UiPolish::SetStatus(ui.buttonShowOutput, "error",
                                         "Failed to start");
                     ui.output->appendPlainText(process->errorString());
                     ui.buttonShowOutput->setChecked(true);
                     ui.buttonBox->setEnabled(true);
                   });

  QObject::connect(process, &QProcess::readyRead, this, [=]() {
    QString output = process->readAll();
    if (trim) {
      output = output.trimmed();
    }
    ui.output->appendPlainText(output);
    emit outputAvailable(output);
  });

  process->setProcessChannelMode(QProcess::MergedChannels);
  process->start(QIODevice::ReadOnly);
}

ProgressDialog::~ProgressDialog() {}

void ProgressDialog::expand() { ui.buttonShowOutput->setChecked(true); }

void ProgressDialog::allowToClose() { ui.buttonBox->setEnabled(true); }
//
// QString ProgressDialog::getOutput() const
//{
//    return ui.output->toPlainText();
//}
