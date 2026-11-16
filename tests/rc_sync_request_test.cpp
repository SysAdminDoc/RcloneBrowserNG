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
    {"Name": "low_level_retries", "FieldName": "LowLevelRetries", "Type": "int", "NoPrefix": true},
    {"Name": "max_delete", "FieldName": "MaxDelete", "Type": "int64", "NoPrefix": true},
    {"Name": "bwlimit", "FieldName": "BwLimit", "Type": "BwTimetable", "NoPrefix": true},
    {"Name": "bwlimit_file", "FieldName": "BwLimitFile", "Type": "BwTimetable", "NoPrefix": true},
    {"Name": "tpslimit", "FieldName": "TPSLimit", "Type": "float64", "NoPrefix": true},
    {"Name": "tpslimit_burst", "FieldName": "TPSLimitBurst", "Type": "int", "NoPrefix": true},
    {"Name": "retries", "FieldName": "Retries", "Type": "int", "NoPrefix": true},
    {"Name": "retries_sleep", "FieldName": "RetriesInterval", "Type": "Duration", "NoPrefix": true},
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

  // The delete cap reaches the daemon as the same number the CLI gets, so
  // a sync run through the daemon stops at the same count.
  void sendsTheDeleteCapAsANumber() {
    const QStringList args{"sync", "--max-delete", "100", "remote:src",
                           "remote:dst"};
    const Request request = Build(args, "g", realIndex());
    QVERIFY2(request.usable, qPrintable(request.reason));
    QCOMPARE(request.endpoint, QStringLiteral("sync/sync"));

    const QJsonValue cap =
        request.payload.value("_config").toObject().value("MaxDelete");
    QVERIFY2(cap.isDouble(), "MaxDelete must be a JSON number");
    QCOMPARE(cap.toInt(), 100);
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

  // Every Sync the app builds names a delete timing. options/info has no
  // entry for one, so they come from options/get's main.DeleteMode.
  void mapsEachDeleteTimingTheDaemonReports() {
    OptionIndex index = realIndex();
    RcSyncRequest::AddDeleteModes(
        &index, QJsonObject{{"main", QJsonObject{{"DeleteMode", 3}}}});
    const QList<QPair<QString, int>> timings{
        {"--delete-before", 1}, {"--delete-during", 2}, {"--delete-after", 3}};
    for (const auto &timing : timings) {
      const QStringList args{"sync", timing.first, "remote:src", "remote:dst"};
      const Request request = Build(args, "g", index);
      QVERIFY2(request.usable, qPrintable(request.reason));
      QCOMPARE(request.endpoint, QStringLiteral("sync/sync"));
      const QJsonValue mode =
          request.payload.value("_config").toObject().value("DeleteMode");
      QVERIFY2(mode.isDouble(), "DeleteMode must be a JSON number");
      QCOMPARE(mode.toInt(), timing.second);
    }
  }

  // A daemon that doesn't report DeleteMode as a number keeps the timing on
  // core/command, since _config drops a key it doesn't know without a word.
  void keepsTimingsOnCoreCommandWithoutTheDaemonsDeleteMode() {
    const QStringList args{"sync", "--delete-during", "remote:src",
                           "remote:dst"};
    for (const QJsonObject &values :
         {QJsonObject(), QJsonObject{{"main", QJsonObject()}},
          QJsonObject{{"main", QJsonObject{{"DeleteMode", "after"}}}}}) {
      OptionIndex index = realIndex();
      RcSyncRequest::AddDeleteModes(&index, values);
      const Request request = Build(args, "g", index);
      QVERIFY(!request.usable);
      QVERIFY2(request.reason.contains("--delete-during"),
               qPrintable(request.reason));
    }
  }

  // Each flag keeps its last value, as pflag does, and two timings set at
  // once stop the CLI, so they go to core/command to say so.
  void readsDeleteTimingsTheWayTheCliDoes() {
    OptionIndex index = realIndex();
    RcSyncRequest::AddDeleteModes(
        &index, QJsonObject{{"main", QJsonObject{{"DeleteMode", 3}}}});
    const auto mode = [&index](const QStringList &flags) {
      const Request request =
          Build(QStringList{"sync"} + flags + QStringList{"a:", "b:"}, "g",
                index);
      if (!request.usable) {
        return -1;
      }
      return request.payload.value("_config").toObject().value("DeleteMode")
          .toInt(0);
    };
    QCOMPARE(mode({"--delete-before", "--delete-before=false",
                   "--delete-during"}),
             2);
    QCOMPARE(mode({"--delete-after=0"}), 0);
    QCOMPARE(mode({"--delete-before=T"}), 1);
    QCOMPARE(mode({"--delete-before", "--delete-after"}), -1);
    QCOMPARE(mode({"--delete-before=yes"}), -1);
  }

  // rclone reads a bool's value with Go's strconv.ParseBool.
  void readsBoolValuesTheWayRcloneDoes() {
    const auto dryRun = [](const QString &flag) {
      const Request request =
          Build(QStringList{"copy", flag, "a:", "b:"}, "g", realIndex());
      if (!request.usable) {
        return -1;
      }
      return int(request.payload.value("_config").toObject().value("DryRun")
                     .toBool());
    };
    QCOMPARE(dryRun("--dry-run"), 1);
    QCOMPARE(dryRun("--dry-run=0"), 0);
    QCOMPARE(dryRun("--dry-run=F"), 0);
    QCOMPARE(dryRun("--dry-run=False"), 0);
    QCOMPARE(dryRun("--dry-run=1"), 1);
    QCOMPARE(dryRun("--dry-run=TRUE"), 1);
    QCOMPARE(dryRun("--dry-run=FaLsE"), -1);
    QCOMPARE(dryRun("--dry-run=yes"), -1);
    QCOMPARE(dryRun("--dry-run="), -1);
  }

  // rclone reads an int option with fmt.Sscanln: these are what options/get
  // reported for --transfers=<text> on v1.76.0. The JSON number has to be
  // the same, since rclone takes it as is.
  void readsIntegersTheWayTheCliDoes() {
    const QList<QPair<QString, int>> accepted{
        {"5", 5}, {"010", 8}, {"0x10", 16}, {"0o10", 8},
        {"0b11", 3}, {"1_0", 10}, {" 5", 5}, {"5 ", 5}, {"+5", 5}};
    for (const auto &c : accepted) {
      const QStringList args{"copy", "--transfers=" + c.first, "remote:src",
                             "remote:dst"};
      const Request request = Build(args, "g", realIndex());
      QVERIFY2(request.usable, qPrintable(c.first + ": " + request.reason));
      const QJsonValue transfers =
          request.payload.value("_config").toObject().value("Transfers");
      QVERIFY2(transfers.isDouble() && transfers.toInt() == c.second,
               qPrintable(c.first + " gave " +
                          QString::number(transfers.toInt())));
    }
    for (const QString &refused : {"08", "1e2", "5.0", ""}) {
      const QStringList args{"copy", "--transfers=" + refused, "remote:src",
                             "remote:dst"};
      const Request request = Build(args, "g", realIndex());
      QVERIFY2(!request.usable, qPrintable(refused + " was accepted"));
    }
  }

  // The daemon has one token bucket, and a _config BwLimit is ignored
  // (measured on v1.76.0), so a job with its own limit has to run as its
  // own rclone to keep it.
  void sendsAJobWithItsOwnLimitToItsOwnRclone() {
    const QList<QStringList> limited{
        {"--bwlimit", "1M"},  {"--bwlimit=1M"},          {"--tpslimit", "10"},
        {"--tpslimit-burst", "2"}, {"--bwlimit", "off"}};
    for (const QStringList &flag : limited) {
      QStringList args = baseCopy();
      for (int i = flag.size() - 1; i >= 0; --i) {
        args.insert(1, flag.at(i));
      }
      const Request request = Build(args, "g", realIndex());
      QVERIFY2(!request.usable, qPrintable(flag.join(' ') + " was mapped"));
      const QString name = flag.first().section(QChar('='), 0, 0);
      QVERIFY2(request.reason.contains(name) &&
                   request.reason.contains("shared"),
               qPrintable(request.reason));
    }
  }

  // --bwlimit-file is per transfer, and each transfer makes its bucket from
  // the job config, so a _config BwLimitFile holds on the daemon (v1.76.0,
  // 3 MiB at 1M took 3.0 s). It stays on sync/copy, with live progress.
  void keepsAPerFileLimitOnTheDaemon() {
    for (const QStringList &flag :
         QList<QStringList>{{"--bwlimit-file", "1M"},
                            {"--bwlimit-file=08:00,512k 18:00,off"}}) {
      QStringList args = baseCopy();
      for (int i = flag.size() - 1; i >= 0; --i) {
        args.insert(1, flag.at(i));
      }
      const Request request = Build(args, "g", realIndex());
      QVERIFY2(request.usable, qPrintable(request.reason));
      const QString value =
          flag.size() == 2 ? flag.at(1) : flag.first().section(QChar('='), 1);
      QCOMPARE(request.payload.value("_config")
                   .toObject()
                   .value("BwLimitFile")
                   .toString(),
               value);
    }
  }

  // sync/* calls don't retry, so the two retry flags come back to the
  // caller instead of going into _config, where rclone ignores them.
  void handsTheRetryFlagsToTheCaller() {
    const Request plain = Build(baseCopy(), "g", realIndex());
    QVERIFY2(plain.usable, qPrintable(plain.reason));
    QCOMPARE(plain.retries, 3);
    QCOMPARE(plain.retriesSleepMs, Q_INT64_C(0));

    const QStringList args{"sync",          "--retries", "5",
                           "--retries-sleep", "1.5s",   "remote:src",
                           "remote:dst"};
    const Request request = Build(args, "g", realIndex());
    QVERIFY2(request.usable, qPrintable(request.reason));
    QCOMPARE(request.retries, 5);
    QCOMPARE(request.retriesSleepMs, Q_INT64_C(1500));
    QVERIFY(!request.payload.contains("_config"));

    // rclone bisync never runs again as a whole, so --retries gets it one
    // attempt. Its --resilient copy retry reads both from _config, so they
    // go there too, and only when the task gave them.
    const Request bisync =
        Build({"bisync", "--retries", "5", "--retries-sleep=1.5s",
               "remote:one", "remote:two"},
              "g", realIndex());
    QVERIFY2(bisync.usable, qPrintable(bisync.reason));
    QCOMPARE(bisync.endpoint, QStringLiteral("sync/bisync"));
    QCOMPARE(bisync.retries, 1);
    const QJsonObject bisyncConfig =
        bisync.payload.value("_config").toObject();
    QCOMPARE(bisyncConfig.value("Retries").toInt(), 5);
    QCOMPARE(bisyncConfig.value("RetriesInterval").toString(),
             QStringLiteral("1.5s"));
    const Request plainBisync =
        Build({"bisync", "remote:one", "remote:two"}, "g", realIndex());
    QVERIFY(!plainBisync.payload.contains("_config"));
    const auto bisyncRetries = [](const QStringList &args) {
      return Build(args, "g", realIndex())
          .payload.value("_config")
          .toObject()
          .value("Retries")
          .toInt();
    };
    // --retries 0 is one try on the CLI, so the bisync gets 1 too.
    const QStringList zero{"bisync", "--retries=0", "remote:one",
                           "remote:two"};
    QCOMPARE(bisyncRetries(zero), 1);
    // The last one given wins, as on the CLI.
    const QStringList twice{"bisync",     "--retries",  "5", "--retries",
                            "2",          "remote:one", "remote:two"};
    QCOMPARE(bisyncRetries(twice), 2);
    // Copy, move and sync keep them out of _config.
    for (const char *command : {"copy", "move"}) {
      const Request other = Build({command, "--retries", "5", "remote:src",
                                   "remote:dst"},
                                  "g", realIndex());
      QVERIFY2(other.usable, qPrintable(other.reason));
      QVERIFY2(!other.payload.contains("_config"), command);
    }

    // options/get on v1.76.0: --retries 0 and -1 both come out as 1, and
    // 010 is 8. A negative sleep is no sleep.
    const QList<QPair<QString, int>> counts{
        {"0", 1}, {"-1", 1}, {"010", 8}, {" 2", 2}};
    for (const auto &c : counts) {
      const Request r = Build({"copy", "--retries=" + c.first, "remote:src",
                               "remote:dst"},
                              "g", realIndex());
      QVERIFY2(r.usable, qPrintable(r.reason));
      QVERIFY2(r.retries == c.second,
               qPrintable(c.first + " gave " + QString::number(r.retries)));
    }
    const QList<QPair<QString, qint64>> sleeps{
        {"300ms", 300}, {"1m30s", 90000}, {"-1s", 0}, {"0", 0}};
    for (const auto &c : sleeps) {
      const Request r = Build({"copy", "--retries-sleep=" + c.first,
                               "remote:src", "remote:dst"},
                              "g", realIndex());
      QVERIFY2(r.usable, qPrintable(r.reason));
      QVERIFY2(r.retriesSleepMs == c.second,
               qPrintable(c.first + " gave " +
                          QString::number(r.retriesSleepMs)));
    }
    // rclone refuses these, and so does the CLI route the job falls to.
    for (const QString &bad : {"2d", "10", "off"}) {
      const Request r = Build({"copy", "--retries-sleep", bad, "remote:src",
                               "remote:dst"},
                              "g", realIndex());
      QVERIFY2(!r.usable, qPrintable(bad + " was accepted"));
      QVERIFY2(r.reason.contains("--retries-sleep"), qPrintable(r.reason));
    }
    const Request noValue =
        Build({"copy", "remote:src", "remote:dst", "--retries"}, "g",
              realIndex());
    QVERIFY(!noValue.usable);
  }

  // fs.ConfigInfo's Reload raises these to 1 on the CLI and an rc _config
  // skips it. Sent as they are, LowLevelRetries 0 panicked a v1.76.0
  // daemon, Checkers 0 hung the run and Transfers 0 copied nothing.
  void raisesCountsBelowOneTheWayTheCliDoes_data() {
    QTest::addColumn<QString>("flag");
    QTest::addColumn<QString>("key");
    QTest::addColumn<QString>("value");
    QTest::addColumn<int>("sent");
    const QStringList flags{"--low-level-retries", "--transfers",
                            "--checkers"};
    const QStringList keys{"LowLevelRetries", "Transfers", "Checkers"};
    for (qsizetype i = 0; i < flags.size(); ++i) {
      const QByteArray name = flags.at(i).toLatin1();
      const QList<QPair<QString, int>> cases{
          {"0", 1}, {"-1", 1}, {"-0x10", 1}, {"1", 1}, {"7", 7}};
      for (const auto &c : cases) {
        QTest::addRow("%s %s", name.constData(),
                      qPrintable(c.first))
            << flags.at(i) << keys.at(i) << c.first << c.second;
      }
    }
  }

  void raisesCountsBelowOneTheWayTheCliDoes() {
    QFETCH(QString, flag);
    QFETCH(QString, key);
    QFETCH(QString, value);
    QFETCH(int, sent);
    for (const QString &command : {"copy", "sync", "bisync"}) {
      const QStringList args{command, flag + "=" + value, "remote:src",
                             "remote:dst"};
      const Request request = Build(args, "g", realIndex());
      QVERIFY2(request.usable, qPrintable(command + ": " + request.reason));
      const QJsonValue got =
          request.payload.value("_config").toObject().value(key);
      QVERIFY2(got.isDouble(), qPrintable(command + ": no number"));
      QCOMPARE(got.toInt(), sent);
    }
  }

  // An option the task didn't give stays off the request, so the daemon's
  // own value applies.
  void leavesAnUnsetCountToTheDaemon() {
    const Request request = Build(baseCopy(), "g", realIndex());
    QVERIFY2(request.usable, qPrintable(request.reason));
    const QJsonObject config = request.payload.value("_config").toObject();
    QVERIFY(!config.contains("LowLevelRetries"));
    QVERIFY(!config.contains("Transfers"));
    QVERIFY(!config.contains("Checkers"));
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
