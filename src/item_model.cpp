#include "item_model.h"
#include "icon_cache.h"
#include "remote_path.h"
#include "utils.h"
#include <algorithm>

namespace {
static void advanceSpinner(QString &text) {
  int spinnerPos = (int)((size_t)text.length() - 2);
  QChar current = text[spinnerPos];
  static const QChar spinner[] = {'-', '\\', '|', '/'};
  size_t spinnerCount = sizeof(spinner) / sizeof(*spinner);
  const QChar *found = std::find(spinner, spinner + spinnerCount, current);
  size_t idx = found - spinner;
  size_t next = idx == spinnerCount - 1 ? 0 : idx + 1;
  text[spinnerPos] = spinner[next];
}

} // namespace

class ItemSorter {
public:
  inline ItemSorter(int column, Qt::SortOrder order)
      : mColumn(column), mOrder(order) {
    mCompare.setNumericMode(true);
  }

  bool operator()(const Item *a, const Item *b) const {
    switch (mColumn) {
    case 0:
      if (a->isFolder != b->isFolder) {
        return a->isFolder;
      }
      return mOrder == Qt::AscendingOrder
                 ? mCompare.compare(a->name, b->name) < 0
                 : mCompare.compare(b->name, a->name) < 0;

    case 1:
      if (a->isFolder != b->isFolder) {
        return a->isFolder;
      }
      if (a->size == b->size) {
        return mOrder == Qt::AscendingOrder
                   ? mCompare.compare(a->name, b->name) < 0
                   : mCompare.compare(b->name, a->name) < 0;
      }
      return mOrder == Qt::AscendingOrder ? a->size < b->size
                                          : b->size < a->size;

    case 2:
      if (a->isFolder != b->isFolder) {
        return a->isFolder;
      }
      if (a->modified == b->modified) {
        return mOrder == Qt::AscendingOrder
                   ? mCompare.compare(a->name, b->name) < 0
                   : mCompare.compare(b->name, a->name) < 0;
      }
      return mOrder == Qt::AscendingOrder ? a->modified < b->modified
                                          : b->modified < a->modified;
    }
    Q_ASSERT(false);
    return false;
  }

private:
  QCollator mCompare;
  int mColumn;
  Qt::SortOrder mOrder;
};

ItemModel::ItemModel(IconCache *icons, const QString &remote, bool googlePhotos,
                     QObject *parent)
    : QAbstractItemModel(parent), mRemote(remote), mGooglePhotos(googlePhotos),
      mFixedFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)) {
  QStyle *style = qApp->style();
  mDriveIcon = style->standardIcon(QStyle::SP_DriveNetIcon);
  mFolderIcon = style->standardIcon(QStyle::SP_DirIcon);
  mFileIcon = style->standardIcon(QStyle::SP_FileIcon);

  auto settings = GetSettings();
  mFolderIcons = settings->value("Settings/showFolderIcons", true).toBool();
  mFileIcons = settings->value("Settings/showFileIcons", true).toBool();

  mRoot = new Item();
  mRoot->isFolder = true;
  mRoot->state = Item::Ready;

  QObject::connect(this, &ItemModel::getIcon, icons, &IconCache::getIcon);
  QObject::connect(
      icons, &IconCache::iconReady, this,
      [=](Item *item, const QPersistentModelIndex &parent, const QIcon &icon) {
        item->state = Item::Ready;
        QString ext = QFileInfo(item->name).suffix();
        if (!mLoadedIcons.contains(ext)) {
          if (mLoadedIcons.size() >= 1024)
            mLoadedIcons.clear();
          mLoadedIcons.insert(ext, icon);
        }

        if (item->isDeleted) {
          delete item;
          return;
        }

        QModelIndex idx = index(item->num(), 0, parent);
        emit dataChanged(idx, idx, QVector<int>{Qt::DecorationRole});
      });
}

ItemModel::~ItemModel() { delete mRoot; }

const QDir &ItemModel::path(const QModelIndex &index) const {
  return get(index)->path;
}

bool ItemModel::isLoading(const QModelIndex &index) const {
  return get(index)->parent->isLoading();
}

void ItemModel::refresh(const QModelIndex &index) {
  Item *item = get(index);
  Item *folderItem = item->isFolder ? item : item->parent;
  if (folderItem->isLoading()) {
    return;
  }
  load(item->isFolder ? index : index.parent(), folderItem);
}

void ItemModel::rename(const QModelIndex &index, const QString &name) {
  Item *item = get(index);
  item->name = name;
  item->path.setPath(JoinRemotePath(item->parent->path.path(), item->name));
  emit dataChanged(index, index, QVector<int>{Qt::DisplayRole});
}

bool ItemModel::isTopLevel(const QModelIndex &index) const {
  return get(index)->parent == mRoot;
}

bool ItemModel::isFolder(const QModelIndex &index) const {
  return get(index)->isFolder;
}

bool ItemModel::hasDuplicateSiblingName(const QModelIndex &index) const {
  if (!index.isValid()) {
    return false;
  }

  Item *item = get(index);
  if (!item || !item->parent) {
    return false;
  }

  int matches = 0;
  for (Item *sibling : item->parent->childs) {
    if (sibling->name == item->name) {
      matches++;
      if (matches > 1) {
        return true;
      }
    }
  }
  return false;
}

QModelIndex ItemModel::addRoot(const QString &name, const QString &path) {
  emit layoutAboutToBeChanged();

  Item *item = new Item();
  item->isFolder = true;
  item->name = name;
  item->path.setPath(path);
  item->parent = mRoot;
  mRoot->childs.append(item);

  emit layoutChanged();

  return createIndex(item->num(), 0, item);
}

QModelIndex ItemModel::index(int row, int column,
                             const QModelIndex &parent) const {
  if (!hasIndex(row, column, parent)) {
    return QModelIndex();
  }

  Item *item = get(parent);
  return createIndex(row, column, item->childs[row]);
}

QModelIndex ItemModel::parent(const QModelIndex &index) const {
  if (!index.isValid()) {
    return QModelIndex();
  }

  Item *child = get(index);
  if (child->parent == mRoot) {
    return QModelIndex();
  }

  return createIndex(child->parent->num(), 0, child->parent);
}

bool ItemModel::hasChildren(const QModelIndex &parent) const {
  Item *item = get(parent);
  if (item->isFolder) {
    if (item->state == Item::Ready) {
      return !item->childs.isEmpty();
    }
    return true;
  }
  return false;
}

int ItemModel::rowCount(const QModelIndex &parent) const {
  Item *item = get(parent);
  if (item->isFolder) {
    if (item->state == Item::Unknown) {
      const_cast<ItemModel *>(this)->load(parent, item);
    }
  }
  return item->childs.count();
}

int ItemModel::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent);
  return 3;
}

void ItemModel::sort(int column, Qt::SortOrder order) {
  mSortColumn = column;
  mSortOrder = order;
  sort(QModelIndex(), mRoot);
}

QVariant ItemModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }

  const Item *item = get(index);

  if (role == Qt::DecorationRole && index.column() == 0) {
    if (item->state == Item::Special) {
      return QIcon();
    }

    if (item->isFolder) {
      if (mFolderIcons) {
        return item->parent == mRoot ? mDriveIcon : mFolderIcon;
      }
      return QIcon();
    }

    if (mFileIcons) {
      QString ext = QFileInfo(item->name).suffix();
      auto it = mLoadedIcons.find(ext);
      if (it == mLoadedIcons.end()) {
        return mFileIcon;
      }

      return it.value();
    }

    return QIcon();
  }

  if (role == Qt::TextAlignmentRole) {
    if (index.column() == 1) {
      return int(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant();
  }

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0:
      return item->name;
    case 1:
      if (item->isFolder || item->state == Item::Special) {
        return QString();
      } else {
        return GetNiceSize(item->size);
      }
    case 2:
      return item->modified;
    }
    Q_ASSERT(false);
  }
  return QVariant();
}

QVariant ItemModel::headerData(int section, Qt::Orientation orientation,
                               int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
    switch (section) {
    case 0:
      return "Name";
    case 1:
      return "Size";
    case 2:
      return "Modified";
    }
  }

  return QVariant();
}

bool ItemModel::removeRows(int row, int count, const QModelIndex &parent) {
  if (!hasIndex(row, 0, parent)) {
    return false;
  }

  Item *item = get(parent);
  if (row + count > item->childs.count()) {
    return false;
  }

  emit beginRemoveRows(parent, row, row + count - 1);

  for (int i = row; i < row + count; i++) {
    Item *node = item->childs.at(i);
    if (node->isLoading() || node->state == Item::LoadingIcon) {
      node->isDeleted = true;
    } else {
      delete node;
    }
  }
  item->childs.remove(row, count);

  emit endRemoveRows();

  return true;
}

Qt::ItemFlags ItemModel::flags(const QModelIndex &index) const {
  Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(index);

  if (!index.isValid()) {
    return defaultFlags;
  }

  return Qt::ItemIsDropEnabled | defaultFlags;
}

bool ItemModel::canDropMimeData(const QMimeData *data, Qt::DropAction action,
                                int row, int column,
                                const QModelIndex &parent) const {
  Q_UNUSED(row);
  Q_UNUSED(column);
  Q_UNUSED(parent);

  if (action != Qt::CopyAction && action != Qt::MoveAction) {
    return false;
  }

  if (!data->hasUrls()) {
    return false;
  }

  auto urls = data->urls();
  if (urls.isEmpty())
    return false;
  for (const auto &url : urls) {
    if (!url.isLocalFile())
      return false;
  }
  return true;
}

bool ItemModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                             int row, int column, const QModelIndex &parent) {
  if (!canDropMimeData(data, action, row, column, parent)) {
    return false;
  }

  Item *item = get(parent);
  QModelIndex dropTarget = item->isFolder ? parent : parent.parent();
  for (const auto &url : data->urls()) {
    emit drop(QDir(url.toLocalFile()), dropTarget);
  }

  return false;
}

Item *ItemModel::get(const QModelIndex &index) const {
  return index.isValid() ? static_cast<Item *>(index.internalPointer()) : mRoot;
}

void ItemModel::load(const QPersistentModelIndex &parentIndex, Item *parent) {
  auto proc = new QProcess(this);

  struct StreamParser {
    QByteArray buf;
    int braceDepth = 0;
    int objStart = -1;
    bool inString = false;
    bool hadData = false;
    QVector<Item *> items;

    void feed(const QByteArray &data, Item *p) {
      const int prevSize = buf.size();
      buf.append(data);
      hadData = true;

      for (int i = prevSize; i < buf.size(); i++) {
        const char c = buf.at(i);

        if (inString) {
          if (c == '\\') {
            i++;
          } else if (c == '"') {
            inString = false;
          }
          continue;
        }

        if (c == '"') {
          inString = true;
          continue;
        }
        if (c == '{') {
          if (braceDepth == 0) {
            objStart = i;
          }
          braceDepth++;
        } else if (c == '}') {
          braceDepth--;
          if (braceDepth == 0 && objStart >= 0) {
            QByteArray objBytes = buf.mid(objStart, i - objStart + 1);
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(objBytes, &err);
            if (doc.isObject()) {
              parseItem(doc.object(), p);
            }
            objStart = -1;
          }
        }
      }

      if (objStart > 0) {
        buf = buf.mid(objStart);
        objStart = 0;
      } else if (objStart < 0) {
        buf.clear();
      }
    }

    void parseItem(const QJsonObject &obj, Item *p) {
      Item *child = new Item();
      child->parent = p;
      child->isFolder = obj.value("IsDir").toBool();
      child->name = obj.value("Name").toString();
      child->path.setPath(ChildRemotePathFromLsjson(p->path.path(), obj));
      if (!child->isFolder)
        child->size = static_cast<quint64>(obj.value("Size").toDouble());

      QString modTime = obj.value("ModTime").toString();
      QDateTime dt = QDateTime::fromString(modTime, Qt::ISODateWithMs);
      if (dt.isValid())
        child->modified = dt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
      else if (modTime.length() >= 19)
        child->modified = modTime.left(19).replace('T', ' ');

      items.append(child);
    }
  };

  auto *parser = new StreamParser();

  Item *loading = new Item();
  loading->state = Item::Special;
  loading->name = "... loading [-]";
  loading->parent = parent;

  QTimer *timer = new QTimer(this);

  QObject::connect(timer, &QTimer::timeout, this, [=]() {
    advanceSpinner(loading->name);
    auto loadingIndex = createIndex(loading->num(), 0, loading);
    emit dataChanged(loadingIndex, loadingIndex, QVector<int>{Qt::DisplayRole});
  });

  QObject::connect(proc, &QProcess::readyReadStandardOutput, this, [=]() {
    parser->feed(proc->readAllStandardOutput(), parent);
  });

  QObject::connect(
      proc,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [=](int code, QProcess::ExitStatus) {
        proc->deleteLater();

        QStringList loadErrors;
        if (code != 0) {
          QString error =
              QString::fromUtf8(proc->readAllStandardError()).trimmed();
          loadErrors.append(error.isEmpty()
                                ? QString("rclone exited with status %1").arg(code)
                                : error);
        }

        if (parser->items.isEmpty() && code == 0 && parser->hadData) {
          loadErrors.append("Failed to parse listing");
        }

        QVector<Item *> &cache = parser->items;

        parent->state = Item::Ready;

        timer->stop();
        timer->deleteLater();

        if (!loadErrors.isEmpty()) {
          emit loadFailed(parent->path.path(), loadErrors.join('\n'));
        }

        if (parent->isDeleted) {
          qDeleteAll(cache);
          delete parser;
          delete parent;
          return;
        }

        QHash<QString, int> existing;
        for (int i = 0; i < parent->childs.count(); i++) {
          if (parent->childs[i] != loading) {
            existing.insert(parent->childs[i]->name, i);
          }
        }

        QVector<Item *> todo;
        bool modified = false;
        for (auto &item : cache) {
          auto it = existing.find(item->name);
          if (it == existing.end()) {
            if (!item->isFolder && mFileIcons) {
              QString ext = QFileInfo(item->name).suffix();
              if (!mLoadedIcons.contains(ext)) {
                item->state = Item::LoadingIcon;
                emit getIcon(item, parentIndex);
              }
            }
            todo.append(item);
            item = nullptr;
          } else {
            Item *old = parent->childs[it.value()];
            if (old->isFolder != item->isFolder ||
                old->modified != item->modified || old->size != item->size) {
              old->state = Item::Unknown;
              old->isFolder = item->isFolder;
              old->modified = item->modified;
              old->size = item->size;
              modified = true;
              emit dataChanged(createIndex(it.value(), 0, parent),
                               createIndex(it.value(), 2, parent),
                               QVector<int>{Qt::DisplayRole});
            }
            existing.erase(it);
          }
        }

        qDeleteAll(cache);
        delete parser;

        for (int i = 0; i < parent->childs.count(); i++) {
          if (parent->childs[i] == loading ||
              existing.contains(parent->childs[i]->name)) {
            emit beginRemoveRows(parentIndex, i, i);
            delete parent->childs.takeAt(i);
            emit endRemoveRows();
            i--;
          }
        }

        if (!todo.isEmpty()) {
          modified = true;
          emit beginInsertRows(parentIndex, parent->childs.count(),
                               parent->childs.count() + todo.count() - 1);
          parent->childs += todo;
          emit endInsertRows();
        }

        if (modified) {
          sort(parentIndex, parent);
        }
      });

  parent->state = Item::Loading1;

  emit beginInsertRows(parentIndex, 0, 0);
  parent->childs.prepend(loading);
  emit endInsertRows();

  timer->start(100);
  UseRclonePassword(proc);

  QStringList args;
  args << "lsjson" << GetRcloneConf();
  if (mDriveShared)
    args << "--drive-shared-with-me";
  args << GetShowHidden() << "--no-mimetype";
  if (mGooglePhotos &&
      IsGooglePhotosRecursiveAlbumPath(parent->path.path())) {
    args << "--recursive" << "--files-only";
  } else {
    args << "--max-depth" << "1";
  }
  {
    auto settings = GetSettings();
    QString ver = settings->value("Settings/rcloneVersion").toString();
    if (!ver.isEmpty() && compareVersion(ver.toStdString(), "1.74") != 2) {
      args << "--list-cutoff" << "100000";
    }
  }
  args << GetDefaultRcloneOptionsList()
       << mRemote + ":" + parent->path.path();
  proc->start(GetRclone(), args, QIODevice::ReadOnly);
}

void ItemModel::sortRecursive(Item *item, const ItemSorter &sorter) {
  std::sort(item->childs.begin(), item->childs.end(), sorter);

  for (auto child : item->childs) {
    sortRecursive(child, sorter);
  }
}

void ItemModel::sort(const QModelIndex &parent, Item *item) {
  if (item->childs.isEmpty()) {
    return;
  }

  QList<QPersistentModelIndex> parents;
  parents << parent;
  emit layoutAboutToBeChanged(parents, QAbstractItemModel::VerticalSortHint);

  QModelIndexList oldList = persistentIndexList();
  QVector<QPair<Item *, int>> oldNodes;
  oldNodes.reserve(oldList.count());
  for (const auto &index : oldList) {
    oldNodes.append(qMakePair(get(index), index.column()));
  }

  ItemSorter sorter(mSortColumn, mSortOrder);
  sortRecursive(item, sorter);

  QModelIndexList newList;
  newList.reserve(oldNodes.size());
  for (const auto &node : oldNodes) {
    Item *child = node.first;
    int column = node.second;
    int row = child->num();
    newList.append(createIndex(row, column, child));
  }

  changePersistentIndexList(oldList, newList);

  emit layoutChanged(parents, QAbstractItemModel::VerticalSortHint);
}
