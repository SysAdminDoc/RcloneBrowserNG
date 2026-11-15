#include "combine_remote_dialog.h"

#include "interface_polish.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

CombineRemoteDialog::CombineRemoteDialog(
    const QList<CombineCandidate> &candidates, const QStringList &existingNames,
    const QString &initialName, const QString &preselected, QWidget *parent)
    : QDialog(parent), mExistingNames(existingNames) {
  setWindowTitle("Combine Remotes");
  UiPolish::SetWindowDefaults(this, QSize(640, 480));
  auto layout = new QVBoxLayout(this);
  layout->setSpacing(12);
  layout->setContentsMargins(12, 12, 12, 12);

  auto intro = new QLabel(this);
  UiPolish::SetNotice(
      intro, "Tick the remotes to put together. Each one shows up as a folder "
             "in the new remote, so you can browse or mount them all as one "
             "drive. Double-click a folder name or path to change it. An empty "
             "path uses the whole remote. Nothing is copied or moved.");
  layout->addWidget(intro);

  auto form = new QFormLayout();
  form->setLabelAlignment(Qt::AlignRight);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setHorizontalSpacing(10);
  layout->addLayout(form);

  mName = new QLineEdit(this);
  mName->setPlaceholderText("e.g. all-drives");
  mName->setClearButtonEnabled(true);
  mName->setText(initialName.trimmed());
  UiPolish::SetPathField(mName, "Combined remote name");
  form->addRow("New remote name:", mName);

  mTable = new QTableWidget(static_cast<int>(candidates.size()), 3, this);
  mTable->setHorizontalHeaderLabels(
      {"Remote", "Folder name", "Path inside the remote"});
  UiPolish::SetTableView(mTable, "Remotes to combine");
  mTable->verticalHeader()->hide();
  mTable->setSelectionMode(QAbstractItemView::SingleSelection);
  mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  mTable->setEditTriggers(QAbstractItemView::DoubleClicked |
                          QAbstractItemView::EditKeyPressed |
                          QAbstractItemView::AnyKeyPressed);
  mTable->horizontalHeader()->setSectionResizeMode(
      IncludeColumn, QHeaderView::ResizeToContents);
  mTable->horizontalHeader()->setSectionResizeMode(FolderColumn,
                                                   QHeaderView::Interactive);
  mTable->horizontalHeader()->setStretchLastSection(true);
  mTable->horizontalHeaderItem(FolderColumn)
      ->setToolTip("What the remote is called inside the combined one. "
                   "Double-click to change it.");
  mTable->horizontalHeaderItem(PathColumn)
      ->setToolTip("Leave empty to use the whole remote, or enter a folder "
                   "inside it such as Photos/2024. Double-click to change it.");
  for (int row = 0; row < candidates.size(); ++row) {
    const CombineCandidate &candidate = candidates.at(row);
    auto include = new QTableWidgetItem(candidate.name);
    include->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                      Qt::ItemIsUserCheckable);
    include->setCheckState(candidate.name == preselected ? Qt::Checked
                                                         : Qt::Unchecked);
    if (!candidate.type.isEmpty()) {
      include->setToolTip(candidate.type);
    }
    mTable->setItem(row, IncludeColumn, include);
    mTable->setItem(row, FolderColumn, new QTableWidgetItem(candidate.name));
    mTable->setItem(row, PathColumn, new QTableWidgetItem(QString()));
  }
  mTable->resizeColumnToContents(FolderColumn);
  layout->addWidget(mTable, 1);

  mPreview = new QLabel(this);
  mPreview->setWordWrap(true);
  mPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
  UiPolish::SetMuted(mPreview);
  layout->addWidget(mPreview);

  mValidation = new QLabel(this);
  UiPolish::SetValidationMessage(mValidation, QString(), QString());
  layout->addWidget(mValidation);

  auto buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  UiPolish::SetDialogButtonBox(buttons);
  if (auto ok = buttons->button(QDialogButtonBox::Ok)) {
    ok->setText("Create");
    UiPolish::SetPrimaryButton(ok);
  }
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &CombineRemoteDialog::tryAccept);

  auto clearValidation = [this]() {
    UiPolish::SetValidationMessage(mValidation, QString(), QString());
    UiPolish::SetFieldState(mName, QString());
  };
  connect(mName, &QLineEdit::textChanged, this, [this, clearValidation]() {
    clearValidation();
    refreshPreview();
  });
  connect(mTable, &QTableWidget::itemChanged, this,
          [this, clearValidation](QTableWidgetItem *) {
            clearValidation();
            refreshPreview();
          });
  refreshPreview();
  (mName->text().isEmpty() ? static_cast<QWidget *>(mName)
                           : static_cast<QWidget *>(mTable))
      ->setFocus(Qt::OtherFocusReason);
}

QString CombineRemoteDialog::remoteName() const {
  return mName->text().trimmed();
}

QList<CombineUpstream> CombineRemoteDialog::upstreams() const {
  QList<CombineUpstream> result;
  for (int row = 0; row < mTable->rowCount(); ++row) {
    const QTableWidgetItem *include = mTable->item(row, IncludeColumn);
    if (!include || include->checkState() != Qt::Checked) {
      continue;
    }
    const QTableWidgetItem *folder = mTable->item(row, FolderColumn);
    const QTableWidgetItem *path = mTable->item(row, PathColumn);
    result << CombineUpstream{folder ? folder->text().trimmed() : QString(),
                              include->text(),
                              path ? path->text().trimmed() : QString()};
  }
  return result;
}

QString CombineRemoteDialog::problem() const {
  const QString name = remoteName();
  if (name.isEmpty() || name.contains(':') || name.contains('\n') ||
      name.contains('\r')) {
    return "Enter a name for the new remote without ':' or line breaks.";
  }
  if (mExistingNames.contains(name)) {
    return QString("A remote called \"%1\" already exists. Pick another name "
                   "so it isn't replaced.")
        .arg(name);
  }
  return CombineUpstreamsProblem(upstreams());
}

void CombineRemoteDialog::refreshPreview() {
  const QList<CombineUpstream> selected = upstreams();
  if (selected.isEmpty()) {
    mPreview->setText("Nothing ticked yet.");
    return;
  }
  const QString name = remoteName().isEmpty() ? "new" : remoteName();
  QStringList lines;
  for (const CombineUpstream &upstream : selected) {
    lines << QString("%1:%2 shows %3:%4")
                 .arg(name, upstream.folder, upstream.remote, upstream.path);
  }
  mPreview->setText(lines.join('\n'));
}

void CombineRemoteDialog::tryAccept() {
  const QString text = problem();
  if (text.isEmpty()) {
    accept();
    return;
  }
  UiPolish::SetValidationMessage(mValidation, "error", text);
  const QString name = remoteName();
  if (name.isEmpty() || name.contains(':') || mExistingNames.contains(name)) {
    UiPolish::SetFieldState(mName, "error");
    mName->setFocus(Qt::OtherFocusReason);
  }
}
