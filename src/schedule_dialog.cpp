#include "schedule_dialog.h"

#include "interface_polish.h"
#include "schedule_manager.h"

ScheduleDialog::ScheduleDialog(const QString &taskName, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(QString("Schedule: %1").arg(taskName));
  resize(480, 320);
  UiPolish::SetWindowDefaults(this, QSize(400, 260));

  auto *layout = new QVBoxLayout(this);

  mIntervalCombo = new QComboBox(this);
  mIntervalCombo->setObjectName("scheduleInterval");
  mIntervalCombo->addItems(
      QStringList() << "Every 15 minutes" << "Every 30 minutes" << "Hourly"
                    << "Daily" << "Weekly" << "Custom (cron expression)");
  mIntervalCombo->setCurrentIndex(3);
  UiPolish::SetAccessibleFormField(
      mIntervalCombo, "Schedule interval",
      "Choose how often the saved task should run.");
  auto *intervalLabel = new QLabel("Interval:", this);
  intervalLabel->setBuddy(mIntervalCombo);
  layout->addWidget(intervalLabel);
  layout->addWidget(mIntervalCombo);

  mTimeEdit = new QLineEdit("02:00", this);
  mTimeEdit->setObjectName("scheduleStartTime");
  mTimeEdit->setPlaceholderText("HH:MM (24h)");
  UiPolish::SetAccessibleFormField(mTimeEdit, "Start time",
                                   "Use 24-hour HH:MM notation.");
  mTimeLabel = new QLabel("Start time:", this);
  mTimeLabel->setBuddy(mTimeEdit);
  layout->addWidget(mTimeLabel);
  layout->addWidget(mTimeEdit);

  mCronEdit = new QLineEdit(this);
  mCronEdit->setObjectName("scheduleCronExpression");
  mCronEdit->setPlaceholderText("min hour dom mon dow (e.g. 0 2 * * 1-5)");
  UiPolish::SetAccessibleFormField(
      mCronEdit, "Cron expression",
      "Enter a five-field cron expression for a custom schedule.");
  mCronLabel = new QLabel("Cron expression:", this);
  mCronLabel->setBuddy(mCronEdit);
  mCronLabel->hide();
  mCronEdit->hide();
  layout->addWidget(mCronLabel);
  layout->addWidget(mCronEdit);

  mPreview = new QLabel(this);
  mPreview->setObjectName("schedulePreview");
  mPreview->setWordWrap(true);
  UiPolish::SetMuted(mPreview);
  mPreview->setAccessibleName("Schedule preview");
  mPreview->setAccessibleDescription(
      "Shows the next scheduled run times or validation errors.");
  layout->addWidget(mPreview);
  layout->addStretch();

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setObjectName("scheduleButtons");
  UiPolish::SetDialogButtonBox(buttons);
  if (auto *ok = buttons->button(QDialogButtonBox::Ok)) {
    UiPolish::SetPrimaryButton(ok);
  }
  QObject::connect(buttons, &QDialogButtonBox::accepted, this,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this,
                   &QDialog::reject);
  layout->addWidget(buttons);

  QObject::connect(mIntervalCombo,
                   static_cast<void (QComboBox::*)(int)>(
                       &QComboBox::currentIndexChanged),
                   this, &ScheduleDialog::updatePreview);
  QObject::connect(mCronEdit, &QLineEdit::textChanged, this,
                   &ScheduleDialog::updatePreview);
  setTabOrder(mIntervalCombo, mTimeEdit);
  setTabOrder(mTimeEdit, mCronEdit);
  setTabOrder(mCronEdit, buttons->button(QDialogButtonBox::Ok));
  updatePreview();
}

QString ScheduleDialog::interval() const {
  static const QStringList intervalMap = {"15m", "30m", "hourly", "daily",
                                          "weekly"};
  const int index = mIntervalCombo ? mIntervalCombo->currentIndex() : -1;
  return index >= 0 && index < intervalMap.size()
             ? intervalMap.at(index)
             : (mCronEdit ? mCronEdit->text().trimmed() : QString());
}

QString ScheduleDialog::time() const {
  const int index = mIntervalCombo ? mIntervalCombo->currentIndex() : -1;
  return (index == 3 || index == 4) && mTimeEdit ? mTimeEdit->text().trimmed()
                                                  : QString();
}

void ScheduleDialog::updatePreview() {
  if (!mIntervalCombo || !mTimeLabel || !mTimeEdit || !mCronLabel ||
      !mCronEdit || !mPreview) {
    return;
  }
  const int index = mIntervalCombo->currentIndex();
  const bool isCron = index == 5;
  mTimeLabel->setVisible(!isCron && index >= 3);
  mTimeEdit->setVisible(!isCron && index >= 3);
  mCronLabel->setVisible(isCron);
  mCronEdit->setVisible(isCron);

  if (!isCron) {
    mPreview->clear();
    return;
  }

  const QString expression = mCronEdit->text().trimmed();
  if (expression.isEmpty()) {
    mPreview->setText("Enter a 5-field cron expression.");
    return;
  }
  if (!ScheduleManager::isValidCronExpr(expression)) {
    mPreview->setText(
        "Invalid cron expression (need 5 fields: min hour dom mon dow).");
    return;
  }
  const QList<QDateTime> runs = ScheduleManager::nextCronRuns(expression, 5);
  if (runs.isEmpty()) {
    mPreview->setText("No upcoming runs found (check expression).");
    return;
  }
  QStringList lines;
  lines << "Next runs:";
  for (const QDateTime &dateTime : runs) {
    lines << "  " + dateTime.toString("ddd yyyy-MM-dd HH:mm");
  }
  mPreview->setText(lines.join('\n'));
}
