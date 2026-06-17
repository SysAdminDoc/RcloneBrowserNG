#include "rclone_capabilities.h"

#include <QDebug>
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
  // Test 1: parse typical rclone backend features JSON
  {
    QByteArray json = R"({
      "Features": {
        "Copy": true,
        "Move": false,
        "DirMove": false,
        "Purge": true,
        "PublicLink": true,
        "About": true,
        "CleanUp": true,
        "ServerSideAcrossConfigs": false,
        "CanHaveEmptyDirectories": true
      }
    })";
    BackendFeatures f = BackendFeatures::fromJson(json);
    require(f.queried, "queried must be true after successful parse");
    require(f.copy, "copy should be true");
    require(!f.move, "move should be false");
    require(!f.dirMove, "dirMove should be false");
    require(f.purge, "purge should be true");
    require(f.publicLink, "publicLink should be true");
    require(f.about, "about should be true");
    require(f.cleanUp, "cleanUp should be true");
    require(!f.serverSideAcrossConfigs, "serverSideAcrossConfigs should be false");
  }

  // Test 2: parse flat JSON (no Features wrapper)
  {
    QByteArray json = R"({
      "Copy": true,
      "Move": true,
      "PublicLink": false,
      "About": false
    })";
    BackendFeatures f = BackendFeatures::fromJson(json);
    require(f.queried, "queried must be true for flat JSON");
    require(f.copy, "copy should be true");
    require(f.move, "move should be true");
    require(!f.publicLink, "publicLink should be false");
    require(!f.about, "about should be false");
  }

  // Test 3: invalid JSON returns unqueried defaults
  {
    BackendFeatures f = BackendFeatures::fromJson("not json");
    require(!f.queried, "queried must be false for invalid JSON");
    require(f.copy, "default copy should be true");
    require(f.move, "default move should be true");
    require(!f.publicLink, "default publicLink should be false");
  }

  // Test 4: empty JSON returns unqueried defaults
  {
    BackendFeatures f = BackendFeatures::fromJson("");
    require(!f.queried, "queried must be false for empty input");
  }

  // Test 5: defaultForType returns sensible defaults
  {
    BackendFeatures drive = BackendFeatures::defaultForType("drive");
    require(drive.publicLink, "drive should support publicLink");
    require(drive.about, "drive should support about");
    require(drive.trashSupported, "drive should support trash");
    require(!drive.trashFlag.isEmpty(), "drive should have a trash flag");

    BackendFeatures sftp = BackendFeatures::defaultForType("sftp");
    require(!sftp.publicLink, "sftp should not support publicLink");
    require(sftp.move, "sftp should support move");

    BackendFeatures unknown = BackendFeatures::defaultForType("someunknown");
    require(unknown.copy, "unknown default copy should be true");
    require(!unknown.publicLink, "unknown default publicLink should be false");
  }

  // Test 6: cache put/get round-trip
  {
    BackendFeatures f;
    f.queried = true;
    f.publicLink = true;
    BackendFeatureCache::instance().put("testremote", f);
    require(BackendFeatureCache::instance().has("testremote"),
            "cache should have testremote");
    BackendFeatures cached = BackendFeatureCache::instance().get("testremote");
    require(cached.queried, "cached queried should be true");
    require(cached.publicLink, "cached publicLink should be true");
  }

  qInfo() << "All backend features tests passed.";
  return 0;
}
