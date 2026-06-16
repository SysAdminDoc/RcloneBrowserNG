#pragma once

#include "pch.h"

#include <functional>

enum class FolderCompareStatus {
  Match,
  MissingOnSource,
  MissingOnDestination,
  Different,
  Error
};

struct FolderCompareEntry {
  FolderCompareStatus status = FolderCompareStatus::Match;
  QString path;
};

QVector<FolderCompareEntry> ParseRcloneCheckCombinedOutput(
    const QString &output);
QString FolderCompareStatusLabel(FolderCompareStatus status);
QString JoinFolderComparePath(const QString &root, const QString &relativePath);

class FolderCompareDialog : public QDialog {
public:
  using EnqueueTransferCallback = std::function<void(
      const QString &message, const QString &source, const QString &dest,
      const QStringList &args)>;

  FolderCompareDialog(const QString &sourcePath, const QString &destinationPath,
                      const QStringList &driveSharedArgs,
                      EnqueueTransferCallback enqueueTransfer,
                      QWidget *parent = nullptr);
  ~FolderCompareDialog();

  QString sourcePath() const;
  QString destinationPath() const;

  enum class RepairAction {
    CopyToDestination,
    CopyToSource,
    DeleteFromDestination,
    DeleteFromSource
  };

private:
  QLineEdit *mSourceEdit = nullptr;
  QLineEdit *mDestinationEdit = nullptr;
  QPushButton *mCompareButton = nullptr;
  QComboBox *mStatusFilter = nullptr;
  QLineEdit *mTextFilter = nullptr;
  QLabel *mSummary = nullptr;
  QTableWidget *mTable = nullptr;
  QPushButton *mCopyToDestinationButton = nullptr;
  QPushButton *mCopyToSourceButton = nullptr;
  QPushButton *mDeleteFromDestinationButton = nullptr;
  QPushButton *mDeleteFromSourceButton = nullptr;
  QProcess *mProcess = nullptr;
  QByteArray mOutput;
  QVector<FolderCompareEntry> mEntries;
  QVector<int> mVisibleEntryIndexes;
  QStringList mDriveSharedArgs;
  EnqueueTransferCallback mEnqueueTransfer;

  void runCompare();
  void refreshTable();
  void updateRepairButtons();
  void enqueueRepair(RepairAction action);
  bool entryMatchesFilter(const FolderCompareEntry &entry) const;
  bool entrySupportsAction(const FolderCompareEntry &entry,
                           RepairAction action) const;
  QVector<int> selectedEntryIndexes() const;
  QStringList copyArgs(const QString &source, const QString &dest) const;
  QStringList deleteArgs(const QString &target) const;
};
