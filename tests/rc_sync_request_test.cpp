#include <QtTest>

#include "rc_sync_request.h"

using RcSyncRequest::Build;
using RcSyncRequest::ConfigGlobalsReason;
using RcSyncRequest::DryRunGlobalReason;
using RcSyncRequest::IndexOptions;
using RcSyncRequest::IsDryRun;
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
    {"Name": "compare_dest", "FieldName": "CompareDest", "Type": "stringArray", "NoPrefix": true},
    {"Name": "copy_dest", "FieldName": "CopyDest", "Type": "stringArray", "NoPrefix": true},
    {"Name": "partial_suffix", "FieldName": "PartialSuffix", "Type": "string", "NoPrefix": true},
    {"Name": "dump", "FieldName": "Dump", "Type": "DumpFlags", "NoPrefix": true},
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

  // rclone ignores a path's global.<option> for a job started over rc, so a
  // run that carries one goes to the CLI in the daemon, where it applies.
  void refusesAPathThatSetsAGlobalOption() {
    const QStringList dst{"sync", "remote:src",
                          ":local,global.backup_dir=':local:bak':dst"};
    const Request request = Build(dst, "g", realIndex());
    QVERIFY(!request.usable);
    QVERIFY2(request.reason.contains("global."), qPrintable(request.reason));

    QStringList backup = baseCopy();
    backup.insert(1, "--backup-dir=:local,global.max_delete=1:bak");
    QVERIFY(!Build(backup, "g", realIndex()).usable);

    const QStringList nested{
        "sync", "remote:src",
        ":alias,remote=':local,global.max_delete=1:dst':"};
    QVERIFY(!Build(nested, "g", realIndex()).usable);

    // override.<option> applies over rc too, so that run stays here.
    const QStringList overridden{"sync", "remote:src",
                                 ":local,override.max_delete=1:dst"};
    const Request kept = Build(overridden, "g", realIndex());
    QVERIFY2(kept.usable, qPrintable(kept.reason));
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

  // Reload refuses --compare-dest with --copy-dest and a --partial-suffix
  // over 16 bytes before the CLI runs anything (fs/config.go, v1.76.0). A
  // daemon job skips Reload: it ran the pair and ignored --copy-dest, and
  // failed every file over the suffix. A _config Dump dumped nothing. Each
  // goes to core/command, where the CLI behaves as it does on its own.
  void sendsWhatReloadRefusesToItsOwnRclone_data() {
    QTest::addColumn<QStringList>("flags");
    QTest::addColumn<QString>("refusal");
    const QString e = QString(QChar(0xE9));
    QTest::newRow("compare then copy")
        << QStringList{"--compare-dest", "remote:cmp", "--copy-dest",
                       "remote:cpy"}
        << QStringLiteral("--copy-dest");
    QTest::newRow("copy then compare, equals form")
        << QStringList{"--copy-dest=remote:cpy", "--compare-dest=remote:cmp"}
        << QStringLiteral("--copy-dest");
    QTest::newRow("two compare-dests")
        << QStringList{"--compare-dest", "remote:a", "--compare-dest",
                       "remote:b"}
        << QString();
    QTest::newRow("copy-dest alone")
        << QStringList{"--copy-dest", "remote:cpy"} << QString();
    QTest::newRow("17-byte suffix")
        << QStringList{"--partial-suffix", ".0123456789abcdef"}
        << QStringLiteral("--partial-suffix");
    QTest::newRow("16-byte suffix")
        << QStringList{"--partial-suffix=.0123456789abcde"} << QString();
    QTest::newRow("8 two-byte characters")
        << QStringList{"--partial-suffix", e.repeated(8)} << QString();
    QTest::newRow("9 characters, 17 bytes")
        << QStringList{"--partial-suffix", e.repeated(8) + "x"}
        << QStringLiteral("--partial-suffix");
    QTest::newRow("dump") << QStringList{"--dump", "headers"}
                          << QStringLiteral("--dump");
    QTest::newRow("dump, equals form")
        << QStringList{"--dump=bodies,requests"} << QStringLiteral("--dump");
  }

  void sendsWhatReloadRefusesToItsOwnRclone() {
    QFETCH(QStringList, flags);
    QFETCH(QString, refusal);
    for (const QString &command : {"copy", "sync", "bisync"}) {
      QStringList args = flags;
      args.prepend(command);
      args << "remote:src" << "remote:dst";
      const Request request = Build(args, "g", realIndex());
      if (refusal.isEmpty()) {
        QVERIFY2(request.usable, qPrintable(command + ": " + request.reason));
      } else {
        QVERIFY2(!request.usable, qPrintable(command + ": was mapped"));
        QVERIFY2(request.reason.contains(refusal),
                 qPrintable(command + ": " + request.reason));
      }
    }
  }

  void keepsEveryCompareDestTheTaskGives() {
    const QStringList args{"copy",           "--compare-dest", "remote:a",
                           "--compare-dest", "remote:b",       "remote:src",
                           "remote:dst"};
    const Request request = Build(args, "g", realIndex());
    QVERIFY2(request.usable, qPrintable(request.reason));
    const QJsonArray dests = request.payload.value("_config")
                                 .toObject()
                                 .value("CompareDest")
                                 .toArray();
    QCOMPARE(dests.size(), 2);
    QCOMPARE(dests.at(0).toString(), QStringLiteral("remote:a"));
    QCOMPARE(dests.at(1).toString(), QStringLiteral("remote:b"));
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

  void sendsAConfigThatSetsAGlobalOptionToItsOwnRclone() {
    const auto dumpOf = [](const QString &name, const QString &key,
                           const QString &value) {
      QJsonObject section;
      section.insert("type", "alias");
      section.insert(key, value);
      QJsonObject dump;
      dump.insert("plain", QJsonObject{{"type", "local"}});
      dump.insert(name, section);
      return dump;
    };

    // A key of its own, as rclone config writes it.
    const QString key = ConfigGlobalsReason(
        dumpOf("bk", "global.backup_dir", "bk:bak"), QStringList());
    QVERIFY2(key.contains("bk remote"), qPrintable(key));
    // Inside a value, where an alias's remote opens with the option.
    const QString value = ConfigGlobalsReason(
        dumpOf("al", "remote", ":local,global.max_delete=1:/d"),
        QStringList());
    QVERIFY2(value.contains("al remote"), qPrintable(value));
    // override. applies over rc, so it stays on sync/*.
    QVERIFY(ConfigGlobalsReason(dumpOf("ov", "override.max_delete", "1"),
                                QStringList())
                .isEmpty());
    QVERIFY(ConfigGlobalsReason(dumpOf("ok", "remote", "/d,global"),
                                QStringList())
                .isEmpty());
    // A folder whose name only looks like the option: no ':' follows it, so
    // it isn't a connection string's options.
    QVERIFY(ConfigGlobalsReason(dumpOf("nm", "remote", "D:/photos,global.copy"),
                                QStringList())
                .isEmpty());

    const QStringList sectionVariable{
        "PATH=C:/x", "RCLONE_CONFIG_BK_GLOBAL.BACKUP_DIR=bk:bak"};
    const QString named = ConfigGlobalsReason(QJsonObject(), sectionVariable);
    QVERIFY2(named.contains("RCLONE_CONFIG_BK_GLOBAL.BACKUP_DIR"),
             qPrintable(named));
    const QStringList lowerCase{"rclone_config_bk_global.dry_run=false"};
    QVERIFY(!ConfigGlobalsReason(QJsonObject(), lowerCase).isEmpty());
    const QStringList pathVariable{
        "RCLONE_BACKUP_DIR=:local,global.max_delete=1:/b"};
    QVERIFY(!ConfigGlobalsReason(QJsonObject(), pathVariable).isEmpty());
    const QStringList folderVariable{"RCLONE_BACKUP_DIR=D:/old,global.copy"};
    QVERIFY(ConfigGlobalsReason(QJsonObject(), folderVariable).isEmpty());

    // Variables rclone never reads, and a section's ordinary key.
    const QStringList unrelated{"RCLONE_CONFIG_BK_TYPE=alias",
                                "OTHER=:local,global.max_delete=1:/b"};
    QVERIFY(ConfigGlobalsReason(QJsonObject(), unrelated).isEmpty());
  }

  // pflag keeps the last value a bool flag gets, a short cluster gives its
  // = value to the last letter, and "--" ends the flags.
  void readsWhetherTheArgumentsAreADryRun_data() {
    QTest::addColumn<QStringList>("args");
    QTest::addColumn<bool>("dryRun");
    QTest::newRow("no flag") << QStringList{"sync", "a:", "b:"} << false;
    QTest::newRow("--dry-run")
        << QStringList{"sync", "--dry-run", "a:", "b:"} << true;
    QTest::newRow("-n") << QStringList{"sync", "-n", "a:", "b:"} << true;
    QTest::newRow("a cluster") << QStringList{"sync", "-vn", "a:"} << true;
    QTest::newRow("a cluster ending in n=false")
        << QStringList{"sync", "-vn=false", "a:"} << false;
    QTest::newRow("false for the letter after n")
        << QStringList{"sync", "-nv=false", "a:"} << true;
    QTest::newRow("--dry-run=false")
        << QStringList{"sync", "--dry-run=false", "a:"} << false;
    QTest::newRow("turned off after")
        << QStringList{"sync", "--dry-run", "--dry-run=0", "a:"} << false;
    QTest::newRow("turned on after")
        << QStringList{"sync", "--dry-run=F", "--dry-run=T", "a:"} << true;
    QTest::newRow("a value rclone refuses")
        << QStringList{"sync", "--dry-run=yes", "a:"} << true;
    QTest::newRow("a path after --")
        << QStringList{"copy", "--", "-n", "b:"} << false;
    QTest::newRow("a long flag holding n")
        << QStringList{"sync", "--no-traverse", "a:"} << false;
    QTest::newRow("a cluster without n")
        << QStringList{"sync", "-vP", "a:"} << false;
    QTest::newRow("rules ahead of the command")
        << QStringList{"--filter", "+ x", "copy", "--dry-run", "a:", "b:"}
        << true;
  }

  void readsWhetherTheArgumentsAreADryRun() {
    QFETCH(QStringList, args);
    QFETCH(bool, dryRun);
    QCOMPARE(IsDryRun(args), dryRun);
  }

  // A section's global.dry_run, read with strconv.ParseBool, is written over
  // --dry-run when the CLI opens the path (addConfigToContext, v1.76.0).
  void findsWhatTurnsADryRunOff_data() {
    QTest::addColumn<QString>("key");
    QTest::addColumn<QString>("value");
    QTest::addColumn<QStringList>("environment");
    QTest::addColumn<QString>("found");
    const QStringList none;
    QTest::newRow("nothing") << QStringLiteral("remote")
                             << QStringLiteral("C:/x") << none << QString();
    QTest::newRow("a key set false")
        << QStringLiteral("global.dry_run") << QStringLiteral("false") << none
        << QStringLiteral("the bk remote's global.dry_run in rclone.conf");
    QTest::newRow("a key set 0") << QStringLiteral("global.dry_run")
                                 << QStringLiteral("0") << none
                                 << QStringLiteral("bk");
    QTest::newRow("a key rclone can't read")
        << QStringLiteral("global.dry_run") << QStringLiteral("no") << none
        << QStringLiteral("bk");
    QTest::newRow("a key set true") << QStringLiteral("global.dry_run")
                                    << QStringLiteral("true") << none
                                    << QString();
    QTest::newRow("a key in upper case")
        << QStringLiteral("GLOBAL.dry_run") << QStringLiteral("false") << none
        << QString();
    QTest::newRow("another global") << QStringLiteral("global.transfers")
                                    << QStringLiteral("0") << none
                                    << QString();
    QTest::newRow("a connection string")
        << QStringLiteral("remote")
        << QStringLiteral(":local,global.dry_run=false:C:/x") << none
        << QStringLiteral("bk");
    QTest::newRow("a quoted value")
        << QStringLiteral("remote")
        << QStringLiteral(":local,global.dry_run='0':C:/x") << none
        << QStringLiteral("bk");
    QTest::newRow("a bare key") << QStringLiteral("remote")
                                << QStringLiteral(":local,global.dry_run:C:/x")
                                << none << QString();
    QTest::newRow("a true value")
        << QStringLiteral("remote")
        << QStringLiteral(":local,global.dry_run=1,links:C:/x") << none
        << QString();
    QTest::newRow("a folder name")
        << QStringLiteral("remote")
        << QStringLiteral("C:/x,global.dry_run=false") << none << QString();
    QTest::newRow("a longer key")
        << QStringLiteral("remote")
        << QStringLiteral(":local,global.dry_runs=false:C:/x") << none
        << QString();
    QTest::newRow("the remote's variable")
        << QStringLiteral("remote") << QStringLiteral("C:/x")
        << QStringList{"RCLONE_CONFIG_BK_GLOBAL.DRY_RUN=false"}
        << QStringLiteral("RCLONE_CONFIG_BK_GLOBAL.DRY_RUN");
    QTest::newRow("the variable in lower case")
        << QStringLiteral("remote") << QStringLiteral("C:/x")
        << QStringList{"rclone_config_bk_global.dry_run=0"}
        << QStringLiteral("rclone_config_bk_global.dry_run");
    QTest::newRow("the variable set true")
        << QStringLiteral("remote") << QStringLiteral("C:/x")
        << QStringList{"RCLONE_CONFIG_BK_GLOBAL.DRY_RUN=true"} << QString();
    QTest::newRow("a path in a variable")
        << QStringLiteral("remote") << QStringLiteral("C:/x")
        << QStringList{"RCLONE_BACKUP_DIR=:local,global.dry_run=f:C:/b"}
        << QStringLiteral("RCLONE_BACKUP_DIR");
    QTest::newRow("not an rclone variable")
        << QStringLiteral("remote") << QStringLiteral("C:/x")
        << QStringList{"OTHER=:local,global.dry_run=false:C:/b"} << QString();
    // --dry-run on the command line wins over the flag's own variable.
    QTest::newRow("RCLONE_DRY_RUN")
        << QStringLiteral("remote") << QStringLiteral("C:/x")
        << QStringList{"RCLONE_DRY_RUN=false"} << QString();
  }

  void findsWhatTurnsADryRunOff() {
    QFETCH(QString, key);
    QFETCH(QString, value);
    QFETCH(QStringList, environment);
    QFETCH(QString, found);
    QJsonObject section;
    section.insert("type", "alias");
    section.insert(key, value);
    QJsonObject dump;
    dump.insert("bk", section);
    const QString reason = DryRunGlobalReason(dump, environment);
    if (found.isEmpty()) {
      QVERIFY2(reason.isEmpty(), qPrintable(reason));
    } else {
      QVERIFY2(reason.contains(found), qPrintable(reason));
    }
  }
};

QTEST_APPLESS_MAIN(RcSyncRequestTest)
#include "rc_sync_request_test.moc"
