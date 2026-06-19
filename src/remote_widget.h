#pragma once

#include "pch.h"
#include "ui_remote_widget.h"

class IconCache;
class ItemModel;
#include "rclone_capabilities.h"

class RemoteWidget : public QWidget {
  Q_OBJECT

public:
  RemoteWidget(IconCache *icons, const QString &remote, bool isLocal,
               bool isGoogle, bool isGooglePhotos, QWidget *parent = nullptr);
  ~RemoteWidget();

  QStringList getDriveSharedArgs() const;
  void refreshCurrentDir();
  void focusPathBar();
  void applyBackendFeatures(const BackendFeatures &features);

signals:
  void addTransfer(const QString &message, const QString &source,
                   const QString &remote, const QStringList &args,
                   const QString &backupDirTemplate = QString(),
                   int backupRetainCount = 0);
  void enqueueTransfer(const QString &message, const QString &source,
                       const QString &dest, const QStringList &args,
                       const QString &backupDirTemplate = QString(),
                       int backupRetainCount = 0);
  void addMount(const QString &remote, const QString &folder);
  void addStream(const QString &remote, const QString &stream);
  void requestReconnect(const QString &remote);

private:
  Ui::RemoteWidget ui;
  ItemModel *mModel = nullptr;
  QWidget *mBreadcrumbBar = nullptr;
  QLineEdit *mFileFilter = nullptr;
  QHBoxLayout *mBreadcrumbLayout = nullptr;
  bool mIsLocal = false;
  BackendFeatures mFeatures;
  QPushButton *mTrashButton = nullptr;

  QList<QPersistentModelIndex> mNavHistory;
  int mNavPos = -1;
  bool mNavInProgress = false;
  QToolButton *mBackButton = nullptr;
  QToolButton *mForwardButton = nullptr;

  bool eventFilter(QObject *obj, QEvent *event) override;
  void showBreadcrumbForIndex(const QModelIndex &index);
  void showPathMessage(const QString &message);
  void showPathEditor(const QString &text = QString());
  void hidePathEditor();
  void selectIndex(const QModelIndex &index);
  void navigateTo(const QModelIndex &index);
  void goBack();
  void goForward();
  void pushNavHistory(const QModelIndex &index);
  void updateNavButtons();
  QModelIndex findLoadedPath(const QString &path) const;
  QString displayPath(const QString &path) const;
  void showFileProperties(const QString &path, const QString &target);
};
