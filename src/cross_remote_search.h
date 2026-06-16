#pragma once

#include "pch.h"

class CrossRemoteSearchDialog : public QDialog {
  Q_OBJECT

public:
  CrossRemoteSearchDialog(const QStringList &remoteNames, QWidget *parent);
  ~CrossRemoteSearchDialog();

signals:
  void openLocation(const QString &remotePath);

private:
  QLineEdit *mQueryEdit = nullptr;
  QCheckBox *mCaseSensitive = nullptr;
  QPushButton *mSearchButton = nullptr;
  QPushButton *mCancelButton = nullptr;
  QTableWidget *mResults = nullptr;
  QLabel *mStatus = nullptr;
  QStringList mRemotes;
  QList<QProcess *> mRunning;
  int mTotalMatches = 0;

  void startSearch();
  void cancelSearch();
  void addResult(const QString &remote, const QString &path, qint64 size,
                 const QString &modTime);
};
