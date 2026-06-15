#pragma once

#include "pch.h"
#include "ui_export_dialog.h"

class ExportDialog : public QDialog {
  Q_OBJECT

public:
  ExportDialog(const QString &remote, const QDir &path,
               QWidget *parent = nullptr);
  ~ExportDialog();

  QString getDestination() const;
  bool onlyFilenames() const;
  QStringList getOptions() const;

private:
  Ui::ExportDialog ui;
  QLabel *mValidation = nullptr;
  QString mTarget;

  void clearValidation();
  void showValidation(QWidget *field, const QString &message);

  void done(int r) override;
};
