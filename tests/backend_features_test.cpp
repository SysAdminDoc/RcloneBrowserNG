#include "rclone_capabilities.h"
#include "test_rclone.h"

#include <QProcess>
#include <QTemporaryDir>
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

  void trashFieldsSurviveTheQuery() {
    const BackendFeatures queried = BackendFeatures::fromJson(
        R"({"Features": {"CleanUp": true, "PublicLink": false}})");
    const BackendFeatures defaults =
        BackendFeatures::defaultForType("onedrive");
    const BackendFeatures merged =
        BackendFeatures::withTypeDefaults(queried, defaults);
    QVERIFY(merged.queried);
    QVERIFY(merged.cleanUp);
    QVERIFY(!merged.publicLink);
    QVERIFY(merged.trashSupported);
    QCOMPARE(merged.trashFlag, defaults.trashFlag);
  }

  // Every trash flag goes on the command line of a delete or purge, and
  // rclone refuses the whole command over a flag it doesn't know. OneDrive
  // shipped with one (--onedrive-no-trash), so every OneDrive delete failed.
  void trashFlagsAreFlagsRcloneKnows() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("no rclone; set RCLONE_BROWSER_TEST_RCLONE");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString config = dir.filePath("rclone.conf");
    {
      QFile file(config);
      QVERIFY(file.open(QIODevice::WriteOnly));
    }
    {
      QFile file(dir.filePath("target/keep.txt"));
      QVERIFY(QDir(dir.path()).mkpath("target"));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("x");
    }

    auto deleteWith = [&](const QString &flag, QString *output) {
      QProcess process;
      process.setProcessChannelMode(QProcess::MergedChannels);
      process.start(rclone, {"--config", config, "delete", "--dry-run", flag,
                             dir.filePath("target")});
      if (!process.waitForFinished(60000)) {
        process.kill();
        *output = "timed out";
        return -1;
      }
      *output = QString::fromUtf8(process.readAll()).trimmed();
      return process.exitCode();
    };

    // A pass below only means something if rclone checks the flag at all.
    QString output;
    QVERIFY2(deleteWith("--no-such-trash-flag", &output) != 0,
             qPrintable(output));

    int checked = 0;
    for (const QString &type :
         {"drive", "onedrive", "dropbox", "s3", "b2", "sftp", "ftp", "local"}) {
      const QString flag = BackendFeatures::defaultForType(type).trashFlag;
      if (flag.isEmpty()) {
        continue;
      }
      QVERIFY2(deleteWith(flag, &output) == 0,
               qPrintable(type + " " + flag + ": " + output));
      checked++;
    }
    QVERIFY(checked >= 2);
    QVERIFY(QFile::exists(dir.filePath("target/keep.txt")));
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
