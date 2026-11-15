#pragma once

#include "pch.h"
#include "run_conditions.h"

class ScheduleDialog : public QDialog {
  Q_OBJECT

public:
  explicit ScheduleDialog(const QString &taskName,
                          QWidget *parent = nullptr);

  QString interval() const;
  QString time() const;
  RunConditions::Conditions conditions() const;

private:
  QComboBox *mIntervalCombo = nullptr;
  QLineEdit *mTimeEdit = nullptr;
  QLineEdit *mCronEdit = nullptr;
  QLabel *mTimeLabel = nullptr;
  QLabel *mCronLabel = nullptr;
  QLabel *mPreview = nullptr;
  QCheckBox *mRequireAc = nullptr;
  QCheckBox *mRequireUnmetered = nullptr;

  void updatePreview();
};
