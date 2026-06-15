#include "export_dialog.h"
#include "interface_polish.h"
#include "utils.h"

ExportDialog::ExportDialog(const QString &remote, const QDir &path,
                           QWidget *parent)
    : QDialog(parent) {
  ui.setupUi(this);
  if (layout()) {
    layout()->setSizeConstraint(QLayout::SetDefaultConstraint);
    layout()->setSpacing(10);
    layout()->setContentsMargins(12, 12, 12, 12);
  }
  UiPolish::SetWindowDefaults(this, QSize(640, 390));
  ui.tabWidget->setDocumentMode(true);
  UiPolish::SetToolbarSurface(ui.pathGroup);
  UiPolish::SetPathField(ui.textFile, "Export destination file");
  UiPolish::SetOutputView(ui.textExclude, "Exclude patterns");
  UiPolish::SetCompactToolButton(ui.fileBrowse, "Choose export file",
                                 "Choose where to save the exported file list.");
  if (auto ok = ui.buttonBox->button(QDialogButtonBox::Ok)) {
    UiPolish::SetPrimaryButton(ok);
  }
  if (auto restore = ui.buttonBox->button(QDialogButtonBox::RestoreDefaults)) {
    restore->setToolTip("Restore export filters to defaults.");
  }

  setWindowTitle("Export File List");
  ui.textFile->setPlaceholderText("Choose a .txt or .csv destination");
  ui.textMinSize->setPlaceholderText("100M");
  ui.textMinAge->setPlaceholderText("1d");
  ui.textMaxAge->setPlaceholderText("30d");
  ui.textExtra->setPlaceholderText("Additional rclone flags");
  ui.textExclude->setPlaceholderText("One --exclude pattern per line");
  mValidation = new QLabel(this);
  UiPolish::SetValidationMessage(mValidation, QString(), QString());
  ui.gridLayout->addWidget(mValidation, 8, 0, 1, 2);
  QObject::connect(ui.textFile, &QLineEdit::textChanged, this,
                   &ExportDialog::clearValidation);

  mTarget = remote + ":" + path.path();

  QObject::connect(ui.buttonBox->button(QDialogButtonBox::RestoreDefaults),
                   &QPushButton::clicked, this, [=]() {
                     ui.rbText->setChecked(true);
                     ui.checkSameFilesystem->setChecked(false);
                     ui.textMinSize->clear();
                     ui.textMinAge->clear();
                     ui.textMaxAge->clear();
                     ui.spinMaxDepth->setValue(0);
                     ui.textExclude->clear();
                     ui.textExtra->clear();
                   });
  ui.buttonBox->button(QDialogButtonBox::RestoreDefaults)->click();

  QObject::connect(ui.buttonBox, &QDialogButtonBox::accepted, this,
                   &QDialog::accept);
  QObject::connect(ui.buttonBox, &QDialogButtonBox::rejected, this,
                   &QDialog::reject);

  QObject::connect(ui.fileBrowse, &QToolButton::clicked, this, [=]() {
    QString file =
        QFileDialog::getSaveFileName(this, "Choose destination file");
    if (!file.isEmpty()) {
      ui.textFile->setText(QDir::toNativeSeparators(file));
    }
  });

  auto settings = GetSettings();
  settings->beginGroup("Export");
  ReadSettings(settings.get(), this);
  settings->endGroup();
}

ExportDialog::~ExportDialog() {
  if (result() == QDialog::Accepted) {
    auto settings = GetSettings();
    settings->beginGroup("Export");
    WriteSettings(settings.get(), this);
    settings->remove("textFile");
    settings->endGroup();
  }
}

QString ExportDialog::getDestination() const { return ui.textFile->text(); }

bool ExportDialog::onlyFilenames() const { return ui.rbText->isChecked(); }

void ExportDialog::clearValidation() {
  UiPolish::SetValidationMessage(mValidation, QString(), QString());
  UiPolish::SetFieldState(ui.textFile, QString());
}

void ExportDialog::showValidation(QWidget *field, const QString &message) {
  clearValidation();
  UiPolish::SetFieldState(field, "error");
  UiPolish::SetValidationMessage(mValidation, "error", message);
  if (field) {
    field->setFocus(Qt::OtherFocusReason);
  }
}

QStringList ExportDialog::getOptions() const {
  QStringList list;
  list << "lsjson" << "--recursive" << "--files-only" << "--no-mimetype";
  if (ui.checkSameFilesystem->isChecked()) {
    list << "--one-file-system";
  }
  if (!ui.textMinSize->text().isEmpty()) {
    list << "--min-size" << ui.textMinSize->text();
  }
  if (!ui.textMinAge->text().isEmpty()) {
    list << "--min-age" << ui.textMinAge->text();
  }
  if (!ui.textMaxAge->text().isEmpty()) {
    list << "--max-age" << ui.textMaxAge->text();
  }
  if (ui.spinMaxDepth->value() != 0) {
    list << "--max-depth" << ui.spinMaxDepth->text();
  }

  QString excluded = ui.textExclude->toPlainText().trimmed();
  if (!excluded.isEmpty()) {
    for (const auto &line : excluded.split('\n')) {
      QString trimmed = line.trimmed();
      if (!trimmed.isEmpty()) {
        list << "--exclude" << trimmed;
      }
    }
  }

  QString extra = ui.textExtra->text().trimmed();
  if (!extra.isEmpty()) {
    for (const auto &arg : extra.split(' ', Qt::SkipEmptyParts)) {
      list << arg;
    }
  }

  list << mTarget;

  return list;
}

void ExportDialog::done(int r) {
  if (r == QDialog::Accepted) {
    if (ui.textFile->text().trimmed().isEmpty()) {
      showValidation(ui.textFile,
                     "Choose where to save the exported file list.");
      return;
    }
  }
  QDialog::done(r);
}
