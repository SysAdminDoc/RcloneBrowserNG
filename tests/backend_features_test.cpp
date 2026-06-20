#include "rclone_capabilities.h"

#include <QTest>

class BackendFeaturesTest : public QObject {
  Q_OBJECT

private slots:
  void parseTypicalJson() {
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
    QVERIFY2(f.queried, "queried must be true after successful parse");
    QVERIFY2(f.copy, "copy should be true");
    QVERIFY2(!f.move, "move should be false");
    QVERIFY2(!f.dirMove, "dirMove should be false");
    QVERIFY2(f.purge, "purge should be true");
    QVERIFY2(f.publicLink, "publicLink should be true");
    QVERIFY2(f.about, "about should be true");
    QVERIFY2(f.cleanUp, "cleanUp should be true");
    QVERIFY2(!f.serverSideAcrossConfigs,
             "serverSideAcrossConfigs should be false");
  }

  void parseFlatJson() {
    QByteArray json = R"({
      "Copy": true,
      "Move": true,
      "PublicLink": false,
      "About": false
    })";
    BackendFeatures f = BackendFeatures::fromJson(json);
    QVERIFY2(f.queried, "queried must be true for flat JSON");
    QVERIFY2(f.copy, "copy should be true");
    QVERIFY2(f.move, "move should be true");
    QVERIFY2(!f.publicLink, "publicLink should be false");
    QVERIFY2(!f.about, "about should be false");
  }

  void invalidJson() {
    BackendFeatures f = BackendFeatures::fromJson("not json");
    QVERIFY2(!f.queried, "queried must be false for invalid JSON");
    QVERIFY2(f.copy, "default copy should be true");
    QVERIFY2(f.move, "default move should be true");
    QVERIFY2(!f.publicLink, "default publicLink should be false");
  }

  void emptyInput() {
    BackendFeatures f = BackendFeatures::fromJson("");
    QVERIFY2(!f.queried, "queried must be false for empty input");
  }

  void defaultForType() {
    BackendFeatures drive = BackendFeatures::defaultForType("drive");
    QVERIFY2(drive.publicLink, "drive should support publicLink");
    QVERIFY2(drive.about, "drive should support about");
    QVERIFY2(drive.trashSupported, "drive should support trash");
    QVERIFY2(!drive.trashFlag.isEmpty(), "drive should have a trash flag");

    BackendFeatures sftp = BackendFeatures::defaultForType("sftp");
    QVERIFY2(!sftp.publicLink, "sftp should not support publicLink");
    QVERIFY2(sftp.move, "sftp should support move");

    BackendFeatures unknown = BackendFeatures::defaultForType("someunknown");
    QVERIFY2(unknown.copy, "unknown default copy should be true");
    QVERIFY2(!unknown.publicLink, "unknown default publicLink should be false");
  }

  void cachePutGetRoundTrip() {
    BackendFeatures f;
    f.queried = true;
    f.publicLink = true;
    BackendFeatureCache::instance().put("testremote", f);
    QVERIFY2(BackendFeatureCache::instance().has("testremote"),
             "cache should have testremote");
    BackendFeatures cached =
        BackendFeatureCache::instance().get("testremote");
    QVERIFY2(cached.queried, "cached queried should be true");
    QVERIFY2(cached.publicLink, "cached publicLink should be true");
  }
};

QTEST_MAIN(BackendFeaturesTest)
#include "backend_features_test.moc"
