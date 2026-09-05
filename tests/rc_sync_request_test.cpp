#include <QtTest>

#include "rc_sync_request.h"

using RcSyncRequest::Build;
using RcSyncRequest::IndexOptions;
using RcSyncRequest::OptionIndex;
using RcSyncRequest::Request;

namespace {

// Trimmed from the real `rclone rc --loopback options/info` output of rclone
// v1.75.0. Field names, types and the RulesOpt embedding are reproduced
// exactly; only options this project never emits were removed.
const char kOptionsInfo[] = R"JSON({
  "main": [
    {"Name": "dry_run", "FieldName": "DryRun", "Type": "bool", "NoPrefix": true},
    {"Name": "transfers", "FieldName": "Transfers", "Type": "int", "NoPrefix": true},
    {"Name": "checkers", "FieldName": "Checkers", "Type": "int", "NoPrefix": true},
    {"Name": "bwlimit", "FieldName": "BwLimit", "Type": "BwTimetable", "NoPrefix": true},
    {"Name": "contimeout", "FieldName": "ConnectTimeout", "Type": "Duration", "NoPrefix": true},
    {"Name": "backup_dir", "FieldName": "BackupDir", "Type": "string", "NoPrefix": true},
    {"Name": "name_transform", "FieldName": "NameTransform", "Type": "stringArray", "NoPrefix": true},
    {"Name": "update", "FieldName": "UpdateOlder", "Type": "bool", "NoPrefix": true},
    {"Name": "stats_file_name_length", "FieldName": "StatsFileNameLength", "Type": "int", "NoPrefix": true},
    {"Name": "use_json_log", "FieldName": "UseJSONLog", "Type": "bool", "NoPrefix": true}
  ],
  "filter": [
    {"Name": "exclude", "FieldName": "RulesOpt.ExcludeRule", "Type": "stringArray", "NoPrefix": true},
    {"Name": "min_size", "FieldName": "MinSize", "Type": "SizeSuffix", "NoPrefix": true},
    {"Name": "max_age", "FieldName": "MaxAge", "Type": "Duration", "NoPrefix": true},
    {"Name": "delete_excluded", "FieldName": "DeleteExcluded", "Type": "bool", "NoPrefix": true}
  ],
  "drive": [
    {"Name": "shared_with_me", "FieldName": "SharedWithMe", "Type": "bool", "NoPrefix": false}
  ]
})JSON";

OptionIndex realIndex() {
  const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(kOptionsInfo));
  return IndexOptions(doc.object());
}

QStringList baseCopy() {
  return QStringList() << "copy" << "remote:src" << "remote:dst";
}

} // namespace

class RcSyncRequestTest : public QObject {
  Q_OBJECT

private slots:
  void indexesMainAndFilterButNotPrefixedBackendOptions() {
    const OptionIndex index = realIndex();

    QVERIFY(index.contains("--dry-run"));
    QCOMPARE(index.value("--dry-run").section, QStringLiteral("main"));
    QCOMPARE(index.value("--dry-run").key, QStringLiteral("DryRun"));

    // rclone reports this as RulesOpt.ExcludeRule, but the wire form is flat.
    // Sending the nested shape was accepted with 200 and filtered nothing.
    QVERIFY(index.contains("--exclude"));
    QCOMPARE(index.value("--exclude").section, QStringLiteral("filter"));
    QCOMPARE(index.value("--exclude").key, QStringLiteral("ExcludeRule"));

    // A backend option needs its prefix and cannot go through _config.
    QVERIFY(!index.contains("--shared-with-me"));
    QVERIFY(!index.contains("--drive-shared-with-me"));
  }

  void buildsACopyRequest() {
    const Request request = Build(baseCopy(), "rcb-7", realIndex());

    QVERIFY2(request.usable, qPrintable(request.reason));
    QCOMPARE(request.endpoint, QStringLiteral("sync/copy"));
    QCOMPARE(request.payload.value("srcFs").toString(),
             QStringLiteral("remote:src"));
    QCOMPARE(request.payload.value("dstFs").toString(),
             QStringLiteral("remote:dst"));
    QCOMPARE(request.payload.value("_async").toBool(), true);
    QCOMPARE(request.payload.value("_group").toString(),
             QStringLiteral("rcb-7"));
    QCOMPARE(request.source, QStringLiteral("remote:src"));
    QCOMPARE(request.dest, QStringLiteral("remote:dst"));
  }

  void sendsIntegerOptionsAsNumbers() {
    QStringList args = baseCopy();
    args.insert(1, "4");
    args.insert(1, "--transfers");

    const Request request = Build(args, "g", realIndex());
    QVERIFY2(request.usable, qPrintable(request.reason));

    const QJsonValue transfers =
        request.payload.value("_config").toObject().value("Transfers");
    // A string here is rejected by rclone with a 400, so the type matters.
    QVERIFY2(transfers.isDouble(), "Transfers must be a JSON number");
    QCOMPARE(transfers.toInt(), 4);
  }

  void keepsSizeAndDurationAsTheStringTheCliWasGiven() {
    QStringList args = baseCopy();
    args.insert(1, "50");
    args.insert(1, "--min-size");
    args.insert(1, "30s");
    args.insert(1, "--contimeout");

    const Request request = Build(args, "g", realIndex());
    QVERIFY2(request.usable, qPrintable(request.reason));

    // rclone parses the JSON string with the same parser the command line
    // uses, so "50" means 50 KiB in both. Converting it to the number 50
    // would silently change it to 50 bytes.
    const QJsonValue minSize =
        request.payload.value("_filter").toObject().value("MinSize");
    QVERIFY2(minSize.isString(), "MinSize must stay a string");
    QCOMPARE(minSize.toString(), QStringLiteral("50"));

    QCOMPARE(request.payload.value("_config")
                 .toObject()
                 .value("ConnectTimeout")
                 .toString(),
             QStringLiteral("30s"));
  }

  void collectsRepeatedExcludesIntoOneArray() {
    QStringList args = baseCopy();
    args.insert(1, "*.tmp");
    args.insert(1, "--exclude");
    args.insert(1, "*.bak");
    args.insert(1, "--exclude");

    const Request request = Build(args, "g", realIndex());
    QVERIFY2(request.usable, qPrintable(request.reason));

    const QJsonArray rules =
        request.payload.value("_filter").toObject().value("ExcludeRule").toArray();
    QCOMPARE(rules.size(), 2);
    QCOMPARE(rules.at(0).toString(), QStringLiteral("*.bak"));
    QCOMPARE(rules.at(1).toString(), QStringLiteral("*.tmp"));
  }

  void refusesAFlagThisRcloneDoesNotExpose() {
    QStringList args = baseCopy();
    args.insert(1, "--one-file-system");

    const Request request = Build(args, "g", realIndex());

    // An unknown key inside _config is accepted with 200 and ignored, so a
    // flag that cannot be mapped has to stop the whole request rather than
    // be dropped.
    QVERIFY(!request.usable);
    QVERIFY2(request.reason.contains("--one-file-system"),
             qPrintable(request.reason));
  }

  void refusesABackendFlagRatherThanDroppingIt() {
    QStringList args = baseCopy();
    args.insert(1, "--drive-shared-with-me");

    const Request request = Build(args, "g", realIndex());
    QVERIFY(!request.usable);
  }

  void dropsTheTwoFlagsThatOnlyShapeCliOutput() {
    QStringList args = baseCopy();
    args.insert(1, "1s");
    args.insert(1, "--stats");
    args.insert(1, "--verbose");

    const Request request = Build(args, "g", realIndex());

    // Every job carries these, so refusing them would mean this route never
    // ran. They control the CLI's console output, which does not exist here.
    QVERIFY2(request.usable, qPrintable(request.reason));
    QCOMPARE(request.payload.value("srcFs").toString(),
             QStringLiteral("remote:src"));
    const QJsonObject config = request.payload.value("_config").toObject();
    QVERIFY(!config.contains("Verbose"));
    QVERIFY(!config.contains("StatsInterval"));
  }

  void doesNotSwallowTheValueOfAMappedStatsFlag() {
    QStringList args = baseCopy();
    args.insert(1, "0");
    args.insert(1, "--stats-file-name-length");

    const Request request = Build(args, "g", realIndex());
    QVERIFY2(request.usable, qPrintable(request.reason));
    QCOMPARE(request.payload.value("_config")
                 .toObject()
                 .value("StatsFileNameLength")
                 .toInt(-1),
             0);
  }

  void refusesANonSyncCommand() {
    const QStringList args{"delete", "--drive-use-trash", "remote:path"};
    const Request request = Build(args, "g", realIndex());
    QVERIFY(!request.usable);
    QVERIFY2(request.reason.contains("delete"), qPrintable(request.reason));
  }

  void refusesWhenThePositionalsAreNotAPair() {
    const QStringList onlySource{"copy", "remote:src"};
    QVERIFY(!Build(onlySource, "g", realIndex()).usable);

    const QStringList three{"copy", "a:", "b:", "c:"};
    QVERIFY(!Build(three, "g", realIndex()).usable);
  }

  void refusesAFlagLeftWithoutItsValue() {
    const QStringList args{"copy", "remote:src", "remote:dst", "--transfers"};
    const Request request = Build(args, "g", realIndex());
    QVERIFY(!request.usable);
    QVERIFY2(request.reason.contains("--transfers"),
             qPrintable(request.reason));
  }

  void refusesANumericFlagGivenSomethingThatIsNotANumber() {
    QStringList args = baseCopy();
    args.insert(1, "lots");
    args.insert(1, "--transfers");

    const Request request = Build(args, "g", realIndex());
    QVERIFY(!request.usable);
    QVERIFY2(request.reason.contains("number"), qPrintable(request.reason));
  }

  void acceptsTheEqualsForm() {
    QStringList args = baseCopy();
    args.insert(1, "--transfers=6");
    args.insert(1, "--dry-run=false");

    const Request request = Build(args, "g", realIndex());
    QVERIFY2(request.usable, qPrintable(request.reason));

    const QJsonObject config = request.payload.value("_config").toObject();
    QCOMPARE(config.value("Transfers").toInt(), 6);
    QCOMPARE(config.value("DryRun").toBool(true), false);
  }

  void usesPath1AndPath2ForBisync() {
    const QStringList args{"bisync", "remote:one", "remote:two"};
    const Request request = Build(args, "g", realIndex());

    QVERIFY2(request.usable, qPrintable(request.reason));
    QCOMPARE(request.endpoint, QStringLiteral("sync/bisync"));
    QCOMPARE(request.payload.value("path1").toString(),
             QStringLiteral("remote:one"));
    QCOMPARE(request.payload.value("path2").toString(),
             QStringLiteral("remote:two"));
    QVERIFY(!request.payload.contains("srcFs"));
  }

  void refusesEverythingWhenTheDaemonGaveNoMetadata() {
    // If options/info could not be read, nothing can be mapped safely.
    const Request request = Build(baseCopy(), "g", OptionIndex());
    QVERIFY(!request.usable);
  }

  void omitsEmptySections() {
    const Request request = Build(baseCopy(), "g", realIndex());
    QVERIFY2(request.usable, qPrintable(request.reason));
    QVERIFY(!request.payload.contains("_config"));
    QVERIFY(!request.payload.contains("_filter"));
  }
};

QTEST_APPLESS_MAIN(RcSyncRequestTest)
#include "rc_sync_request_test.moc"
