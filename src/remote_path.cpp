#include "remote_path.h"

#include <QJsonValue>

QString JoinRemotePath(const QString &parentPath, const QString &childPath) {
  if (childPath.isEmpty()) {
    return parentPath.isEmpty() ? "." : parentPath;
  }

  if (parentPath.isEmpty() || parentPath == ".") {
    return childPath;
  }

  if (childPath == parentPath || childPath.startsWith(parentPath + "/")) {
    return childPath;
  }

  return parentPath + "/" + childPath;
}

QString ChildRemotePathFromLsjson(const QString &parentPath,
                                  const QJsonObject &entry) {
  QString childPath = entry.value("Path").toString();
  if (childPath.isEmpty()) {
    childPath = entry.value("Name").toString();
  }
  return JoinRemotePath(parentPath, childPath);
}

bool IsGooglePhotosRecursiveAlbumPath(const QString &path) {
  return path.startsWith("album/") || path.startsWith("shared-album/");
}

QString LocalFilesystemRemote() { return QStringLiteral(":local"); }

bool SplitRemoteTarget(const QString &target, QString *remote, QString *path) {
  const bool onTheFly = target.startsWith(':');
  const int colon = target.indexOf(':', onTheFly ? 1 : 0);
  if (colon <= (onTheFly ? 1 : 0)) {
    return false;
  }
  QString name = target.left(colon);
  if (name == QLatin1String("/")) {
    name = LocalFilesystemRemote();
  }
  if (remote) {
    *remote = name;
  }
  if (path) {
    *path = target.mid(colon + 1);
  }
  return true;
}
