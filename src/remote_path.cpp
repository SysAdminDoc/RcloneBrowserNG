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
