#pragma once

#include "pch.h"

class ScheduleDialog : public QDialog {
  Q_OBJECT

public:
  explicit ScheduleDialog(const QString &taskName,
                          QWidget *parent = nullptr);

  QString interval() const;
  QString time() const;

private:
  QComboBox *mIntervalCombo = nullptr;
  QLineEdit *mTimeEdit = nullptr;
  QLineEdit *mCronEdit = nullptr;
  QLabel *mTimeLabel = nullptr;
  QLabel *mCronLabel = nullptr;
  QLabel *mPreview = nullptr;

  void updatePreview();
};
