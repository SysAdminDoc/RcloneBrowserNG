#include "remote_path.h"

#include <QDebug>
#include <QJsonObject>
#include <cstdlib>

namespace {

void require(bool condition, const QString &message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

} // namespace

int main() {
  const QString special = "folder [x] (y) : trailing ";
  require(JoinRemotePath(".", special) == special,
          "root path join did not preserve punctuation/trailing space");
  require(JoinRemotePath("parent", special) == "parent/" + special,
          "nested path join did not preserve punctuation/trailing space");

  QJsonObject withPath;
  withPath.insert("Path", special);
  withPath.insert("Name", "different fallback");
  require(ChildRemotePathFromLsjson("parent", withPath) == "parent/" + special,
          "lsjson Path was not preferred over Name");

  QJsonObject alreadyRelativeToRoot;
  alreadyRelativeToRoot.insert("Path", "parent/" + special);
  require(ChildRemotePathFromLsjson("parent", alreadyRelativeToRoot) ==
              "parent/" + special,
          "prejoined lsjson Path was joined twice");

  QJsonObject fallbackName;
  fallbackName.insert("Name", special);
  require(ChildRemotePathFromLsjson(".", fallbackName) == special,
          "Name fallback did not preserve special characters");

  require(IsGooglePhotosRecursiveAlbumPath("album/family"),
          "Google Photos album path was not recognized");
  require(IsGooglePhotosRecursiveAlbumPath("shared-album/family"),
          "Google Photos shared album path was not recognized");
  require(!IsGooglePhotosRecursiveAlbumPath("album"),
          "Google Photos album root should stay one-level");
  require(!IsGooglePhotosRecursiveAlbumPath("media/by-month"),
          "Google Photos media paths should not use the album fallback");

  return 0;
}
