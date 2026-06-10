#include "icon_cache.h"
#include "item_model.h"
#if defined(Q_OS_MACOS)
#include "osx_helper.h"
#endif

IconCache::IconCache(QObject *parent) : QObject(parent) {
  mFileIcon = QFileIconProvider().icon(QFileIconProvider::File);

  mThread.start();
  moveToThread(&mThread);
}

IconCache::~IconCache() {
  mThread.quit();
  mThread.wait();
}

void IconCache::getIcon(Item *item, const QPersistentModelIndex &parent) {
  QString ext = QFileInfo(item->name).suffix();
  QIcon icon;
  auto it = mIcons.find(ext);
  if (it == mIcons.end()) {
#if defined(Q_OS_WIN32)
    // COM has to be initialized on the worker thread that actually calls
    // the shell API, not on the thread that constructed this object
    static thread_local bool comInitialized = false;
    if (!comInitialized) {
      CoInitializeEx(NULL, COINIT_MULTITHREADED);
      comInitialized = true;
    }

    SHFILEINFOW info;
    if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(("dummy." + ext).utf16()),
                       FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                       SHGFI_ICON | SHGFI_USEFILEATTRIBUTES) &&
        info.hIcon) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      icon = QIcon(QPixmap::fromImage(QImage::fromHICON(info.hIcon)));
#else
      icon = QtWin::fromHICON(info.hIcon);
#endif
      DestroyIcon(info.hIcon);
    }
#elif defined(Q_OS_MACOS)
    icon = osxGetIcon(ext.toUtf8().constData());
#else
    QMimeType mime = mMimeDatabase.mimeTypeForFile(
        item->name, QMimeDatabase::MatchExtension);
    if (mime.isValid()) {
      icon = QIcon::fromTheme(mime.iconName());
    }
#endif
    if (icon.isNull()) {
      icon = mFileIcon;
    }
    if (mIcons.size() >= 1024)
      mIcons.clear();
    mIcons.insert(ext, icon);
  } else {
    icon = it.value();
  }

  emit iconReady(item, parent, icon);
}
