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
  QLabel *mEmptyState = nullptr;
  QComboBox *mHistoryCombo = nullptr;
  QComboBox *mTypeFilter = nullptr;
  QSpinBox *mMinSize = nullptr;
  QSpinBox *mMaxSize = nullptr;
  QStringList mRemotes;
  QHash<QString, QCheckBox *> mRemoteChecks;
  QList<QProcess *> mRunning;
  QStringList mRemoteErrors;
  int mTotalMatches = 0;
  int mFailedRemotes = 0;

  void startSearch();
  void cancelSearch();
  void addResult(const QString &remote, const QString &path, qint64 size,
                 const QString &modTime);
  void loadHistory();
  void saveHistory(const QString &query);
  QStringList selectedRemotes() const;
};
