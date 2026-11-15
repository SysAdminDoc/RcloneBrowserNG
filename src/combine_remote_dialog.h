#pragma once

#include "combine_remote.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QTableWidget;

struct CombineCandidate {
  QString name;
  QString type;
};

// Builds an rclone "combine" remote from remotes that already exist: each
// ticked remote becomes a folder at the root of the new one, so the whole set
// can be browsed or mounted as a single drive.
class CombineRemoteDialog : public QDialog {
  Q_OBJECT

public:
  CombineRemoteDialog(const QList<CombineCandidate> &candidates,
                      const QStringList &existingNames,
                      const QString &initialName,
                      const QString &preselected, QWidget *parent = nullptr);

  QString remoteName() const;
  QList<CombineUpstream> upstreams() const;
  // Empty when the dialog can be accepted, otherwise what to change.
  QString problem() const;

  enum Column { IncludeColumn = 0, FolderColumn = 1, PathColumn = 2 };
  QTableWidget *table() const { return mTable; }
  QLineEdit *nameField() const { return mName; }

private:
  void refreshPreview();
  void tryAccept();

  QStringList mExistingNames;
  QLineEdit *mName = nullptr;
  QTableWidget *mTable = nullptr;
  QLabel *mPreview = nullptr;
  QLabel *mValidation = nullptr;
};
