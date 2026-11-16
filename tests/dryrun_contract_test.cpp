#include "job_options.h"
#include "job_options_store.h"
#include "utils.h"

#include <QBuffer>
#include <QRegularExpression>
#include <QTest>

namespace {
bool argsContain(const QStringList &args, const QString &flag) {
  return args.contains(flag);
}

QString argAfter(const QStringList &args, const QString &flag) {
  const int index = args.indexOf(flag);
  if (index < 0 || index + 1 >= args.size()) {
    return QString();
  }
  return args.at(index + 1);
}

JobOptions *makeTask(JobOptions::Operation op, bool dryRun) {
  auto jo = new JobOptions(false);
  jo->operation = op;
  jo->dryRun = dryRun;
  jo->source = "/src";
  jo->dest = "remote:dst";
  jo->isFolder = true;
  return jo;
}
} // namespace

class DryRunContractTest : public QObject {
  Q_OBJECT

private slots:
  void serverSideFlagIsOffByDefaultAndOptIn() {
    // Streaming every byte through this machine is the safe
    // default; the flag only appears when asked for.
    JobOptions options;
    options.operation = JobOptions::Copy;
    options.source = "a:src";
    options.dest = "b:dst";
    QVERIFY(!options.getOptions().contains(
        QStringLiteral("--server-side-across-configs")));

    options.serverSideAcrossConfigs = true;
    QVERIFY(options.getOptions().contains(
        QStringLiteral("--server-side-across-configs")));
  }

  // Sync is the only operation that deletes at the destination. Bisync reads
  // --max-delete as a percentage, so a count meant for Sync would mean
  // something else there.
  void deleteCapReachesSyncOnly() {
    auto sync = makeTask(JobOptions::Sync, false);
    QVERIFY(!argsContain(sync->getOptions(), "--max-delete"));
    sync->maxDelete = 100;
    QStringList args = sync->getOptions();
    QCOMPARE(args.count("--max-delete"), 1);
    QCOMPARE(argAfter(args, "--max-delete"), QString("100"));
    sync->maxDelete = 0;
    QCOMPARE(argAfter(sync->getOptions(), "--max-delete"), QString("0"));
    delete sync;

    for (JobOptions::Operation op :
         {JobOptions::Copy, JobOptions::Move, JobOptions::Bisync}) {
      auto other = makeTask(op, false);
      other->maxDelete = 100;
      QVERIFY2(!argsContain(other->getOptions(), "--max-delete"),
               qPrintable(other->getOptions().join(' ')));
      delete other;
    }
  }

  // rclone counts a dry run's deletes against --max-delete, so both preview
  // buttons drop the cap or the list stops partway. The task keeps it.
  void previewsListEveryDeleteAndLeaveTheTaskAlone() {
    auto sync = makeTask(JobOptions::Sync, false);
    sync->maxDelete = 2;
    const QStringList preview = sync->getPreviewOptions();
    QVERIFY(argsContain(preview, "--dry-run"));
    QVERIFY2(!argsContain(preview, "--max-delete"), qPrintable(preview.join(' ')));
    QCOMPARE(sync->maxDelete, 2);
    QVERIFY(!sync->dryRun);
    QCOMPARE(argAfter(sync->getOptions(), "--max-delete"), QString("2"));
    QVERIFY(!argsContain(sync->getOptions(), "--dry-run"));
    delete sync;
  }

  // A history row's Dry Run starts from the arguments the run was saved
  // with, cap and all, so it drops the cap the same way.
  void historyDryRunsDropTheCapToo() {
    const QStringList saved{"sync",   "--delete-during", "--max-delete",
                            "100",    "--verbose",       "--max-delete=5",
                            "src:a",  "dst:b"};
    QCOMPARE(JobOptions::DryRunArguments(saved),
             QStringList() << "sync" << "--dry-run" << "--delete-during"
                           << "--verbose" << "src:a" << "dst:b");

    // Already a dry run: the flag stays once, where it was.
    const QStringList dry{"copy", "--verbose", "--dry-run", "src:a", "dst:b"};
    QCOMPARE(JobOptions::DryRunArguments(dry), dry);

    // A cap with its value missing at the end goes without taking the
    // command or a path with it.
    QCOMPARE(JobOptions::DryRunArguments(QStringList{"sync", "src:a", "--max-delete"}),
             QStringList() << "sync" << "--dry-run" << "src:a");
    QCOMPARE(JobOptions::DryRunArguments(QStringList()),
             QStringList() << "--dry-run");

    // A saved task's own arguments come out the same as its preview's,
    // apart from the dated backup folder, which this task doesn't use.
    auto sync = makeTask(JobOptions::Sync, false);
    sync->maxDelete = 3;
    QCOMPARE(JobOptions::DryRunArguments(sync->getOptions()),
             sync->getPreviewOptions());
    delete sync;
  }

  // The task's extra options are free text and can hold a cap of their own,
  // or a --dry-run=false that would turn the preview into a real run.
  void previewsDropCapsFromExtraOptionsToo() {
    auto sync = makeTask(JobOptions::Sync, false);
    sync->maxDelete = 3;
    sync->extra = "--max-delete=5 --max-delete-size 1G --dry-run=false "
                  "--max-delete-size=2G -n=false --fast-list";
    const QStringList preview = sync->getPreviewOptions();
    QCOMPARE(preview.count("--dry-run"), 1);
    QVERIFY2(preview.contains("--fast-list"), qPrintable(preview.join(' ')));
    for (const QString &arg : preview) {
      QVERIFY2(!arg.startsWith("--max-delete") && !arg.contains('=') &&
                   arg != "1G",
               qPrintable(preview.join(' ')));
    }
    // The run itself keeps everything the user typed.
    const QStringList run = sync->getOptions();
    QVERIFY(run.contains("--max-delete-size"));
    QVERIFY(run.contains("--dry-run=false"));
    delete sync;

    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"copy", "-n", "--max-delete-size", "5G", "a:", "b:"}),
             QStringList() << "copy" << "-n" << "a:" << "b:");
  }

  // Bisync's --max-delete is a percentage that aborts before anything
  // changes, and its dry run stops at --max-delete-size on the same file
  // the real run does (both measured on v1.76.0). Dropping either made the
  // preview pass a run that aborts, or abort one that passes.
  void bisyncPreviewsKeepTheirCaps() {
    auto bisync = makeTask(JobOptions::Bisync, false);
    bisync->extra = "--max-delete 90 --max-delete-size=3k --dry-run=false";
    const QStringList preview = bisync->getPreviewOptions();
    QCOMPARE(preview.count("--dry-run"), 1);
    const qsizetype cap = preview.indexOf("--max-delete");
    QVERIFY2(cap > 0 && preview.value(cap + 1) == "90",
             qPrintable(preview.join(' ')));
    QVERIFY2(preview.contains("--max-delete-size=3k"),
             qPrintable(preview.join(' ')));
    QVERIFY2(!preview.contains("--dry-run=false"),
             qPrintable(preview.join(' ')));
    delete bisync;

    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"bisync", "--max-delete", "90", "a:", "b:"}),
             (QStringList{"bisync", "--dry-run", "--max-delete", "90", "a:",
                          "b:"}));
  }

  // The deletion summary warns from the caps the run will use, wherever
  // they came from, and rclone takes the last of a repeated flag.
  void readsTheCapsARunWillUse() {
    auto sync = makeTask(JobOptions::Sync, false);
    sync->maxDelete = 100;
    sync->extra = "--max-delete=5 --max-delete-size 2M";
    JobOptions::DeleteCaps caps = JobOptions::DeleteCapsIn(sync->getOptions());
    QCOMPARE(caps.count, 5);
    QCOMPARE(caps.bytes, 2 * 1024 * 1024);
    delete sync;

    caps = JobOptions::DeleteCapsIn({"sync", "--max-delete", "3", "a:", "b:"});
    QCOMPARE(caps.count, 3);
    QCOMPARE(caps.bytes, -1);
    caps = JobOptions::DeleteCapsIn(
        {"sync", "--max-delete", "3", "--max-delete=-1", "a:", "b:"});
    QCOMPARE(caps.count, -1);
    caps = JobOptions::DeleteCapsIn({"sync", "--max-delete-size=off", "a:"});
    QCOMPARE(caps.bytes, -1);
    caps = JobOptions::DeleteCapsIn({"sync", "--exclude=*.tmp", "a:", "b:"});
    QCOMPARE(caps.count, -1);
    QCOMPARE(caps.bytes, -1);
  }

  // rclone's SizeSuffix, values checked on v1.76.0 through options/get.
  void readsSizesTheWayRcloneDoes() {
    QCOMPARE(JobOptions::SizeSuffixBytes("3k"), 3072);
    QCOMPARE(JobOptions::SizeSuffixBytes("10"), 10240);
    QCOMPARE(JobOptions::SizeSuffixBytes("10M"), 10485760);
    QCOMPARE(JobOptions::SizeSuffixBytes("10Mi"), 10485760);
    QCOMPARE(JobOptions::SizeSuffixBytes("10MiB"), 10485760);
    QCOMPARE(JobOptions::SizeSuffixBytes("10Ki"), 10240);
    QCOMPARE(JobOptions::SizeSuffixBytes("10KI"), 10240);
    QCOMPARE(JobOptions::SizeSuffixBytes("1.5G"), Q_INT64_C(1610612736));
    QCOMPARE(JobOptions::SizeSuffixBytes("2b"), 2);
    QCOMPARE(JobOptions::SizeSuffixBytes("2B"), 2);
    QCOMPARE(JobOptions::SizeSuffixBytes("off"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes("10MB"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes(" 5k"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes("-5k"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes(""), -1);
  }

  void dryRunDefaultsFalse() {
    JobOptions jo;
    QVERIFY2(!jo.dryRun, "dryRun must default to false");
  }

  void dryRunTrueProducesDryRunFlag() {
    const JobOptions::Operation ops[] = {JobOptions::Copy, JobOptions::Move,
                                         JobOptions::Sync, JobOptions::Bisync};
    const char *names[] = {"Copy", "Move", "Sync", "Bisync"};
    for (int i = 0; i < 4; ++i) {
      auto jo = makeTask(ops[i], true);
      QStringList args = jo->getOptions();
      QVERIFY2(argsContain(args, "--dry-run"),
               qPrintable(QString("dryRun=true must produce --dry-run for %1").arg(names[i])));
      delete jo;
    }
  }

  void dryRunFalseNeverProducesDryRunFlag() {
    const JobOptions::Operation ops[] = {JobOptions::Copy, JobOptions::Move,
                                         JobOptions::Sync, JobOptions::Bisync};
    const char *names[] = {"Copy", "Move", "Sync", "Bisync"};
    for (int i = 0; i < 4; ++i) {
      auto jo = makeTask(ops[i], false);
      QStringList args = jo->getOptions();
      QVERIFY2(!argsContain(args, "--dry-run"),
               qPrintable(QString("dryRun=false must NOT produce --dry-run for %1")
                              .arg(names[i])));
      delete jo;
    }
  }

  void dryRunNotPersistedThroughRoundTrip() {
    auto jo = makeTask(JobOptions::Sync, true);
    jo->description = "test-persist";
    jo->uniqueId =
        QUuid::fromString("{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}");

    QList<JobOptions *> tasks;
    tasks.append(jo);

    QByteArray bytes;
    QBuffer out(&bytes);
    QVERIFY2(out.open(QIODevice::WriteOnly), "failed to open write buffer");
    QString error;
    QVERIFY2(WriteJobOptionsStore(&out, tasks, &error),
             qPrintable("failed to write store: " + error));
    out.close();

    QBuffer in(&bytes);
    QVERIFY2(in.open(QIODevice::ReadOnly), "failed to open read buffer");
    JobOptionsStoreLoadResult loaded = ReadJobOptionsStore(&in);
    QVERIFY2(loaded.error.isEmpty(),
             qPrintable("failed to read store: " + loaded.error));
    QCOMPARE(loaded.tasks.size(), 1);
    QVERIFY2(!loaded.tasks.first()->dryRun,
             "dryRun must NOT survive save/load round-trip");

    QStringList args = loaded.tasks.first()->getOptions();
    QVERIFY2(!argsContain(args, "--dry-run"),
             "loaded task must NOT produce --dry-run");

    ClearJobOptionsList(&loaded.tasks);
    ClearJobOptionsList(&tasks);
  }

  void dryRunAppearsExactlyOnce() {
    auto jo = makeTask(JobOptions::Sync, true);
    jo->sync = true;
    jo->syncTiming = JobOptions::After;
    jo->skipNewer = true;
    jo->verbose = true;
    jo->compare = true;
    jo->compareOption = JobOptions::Checksum;

    QStringList args = jo->getOptions();
    int count = 0;
    for (const auto &a : args) {
      if (a == "--dry-run")
        ++count;
    }
    QVERIFY2(count == 1,
             qPrintable(QString("--dry-run must appear exactly once, got %1")
                            .arg(count)));
    delete jo;
  }

  void togglingDryRunProducesCleanArgs() {
    auto jo = makeTask(JobOptions::Copy, true);
    QStringList withDry = jo->getOptions();
    QVERIFY2(argsContain(withDry, "--dry-run"), "initial dry-run missing");

    jo->dryRun = false;
    QStringList withoutDry = jo->getOptions();
    QVERIFY2(!argsContain(withoutDry, "--dry-run"),
             "after clearing dryRun, --dry-run must not appear");
    delete jo;
  }

  void backupDirDateExpansionsUnique() {
    auto jo = makeTask(JobOptions::Sync, false);
    jo->backupDir = "remote:backups/{date}";

    const QString first = argAfter(jo->getOptions(), "--backup-dir");
    const QString second = argAfter(jo->getOptions(), "--backup-dir");
    QVERIFY2(!first.isEmpty(), "first backup-dir arg missing");
    QVERIFY2(!second.isEmpty(), "second backup-dir arg missing");
    QVERIFY2(first != second,
             "backup-dir {date} expansions must be unique per command");
    QVERIFY2(QRegularExpression(
                 "^remote:backups/\\d{4}-\\d{2}-\\d{2}_\\d{6}_\\d{3}_[0-9a-z]+_[0-9a-z]{4}$")
                 .match(first)
                 .hasMatch(),
             "backup-dir {date} expansion format changed unexpectedly");
    delete jo;
  }

  void freeFormExtraOptionsPreserveQuotes() {
    auto jo = makeTask(JobOptions::Copy, false);
    jo->extra = "--metadata \"display name=Quarterly Report\" --suffix \"old copy\"";

    QStringList args = jo->getOptions();
    QVERIFY2(args.contains("--metadata"), "quoted extra flag missing");
    QVERIFY2(args.contains("display name=Quarterly Report"),
             "quoted extra value with spaces was split");
    QVERIFY2(args.contains("--suffix"), "second quoted extra flag missing");
    QVERIFY2(args.contains("old copy"), "second quoted extra value was split");
    QVERIFY2(!args.contains("\"display"),
             "quote characters leaked into parsed extra args");
    delete jo;
  }

  void backupRetentionPrunesOldSnapshots() {
    QString parent;
    QStringList targets;
    const bool planned = BuildBackupRetentionPlan(
        "remote:backups/{date}", 2,
        QStringList()
            << "2026-06-17_010203_004_abcd_0001/"
            << "not-a-backup/"
            << "2026-06-18_010203_004_abcd_0002/"
            << "2026-06-16_010203_004_abcd_0000/",
        &parent, &targets);
    QVERIFY2(planned, "backup retention plan was not created");
    QCOMPARE(parent, QString("remote:backups"));
    QCOMPARE(targets,
             QStringList({"remote:backups/2026-06-16_010203_004_abcd_0000"}));
  }

  void nestedBackupTemplatesPruneDatedSegment() {
    QString parent;
    QStringList targets;
    const bool planned = BuildBackupRetentionPlan(
        "remote:backups/snap-{date}/deleted", 1,
        QStringList()
            << "snap-2026-06-17_010203"
            << "snap-2026-06-18_010203"
            << "manual-2026-06-10_010203",
        &parent, &targets);
    QVERIFY2(planned, "nested backup retention plan was not created");
    QCOMPARE(parent, QString("remote:backups"));
    QCOMPARE(targets,
             QStringList({"remote:backups/snap-2026-06-17_010203"}));
  }

  void fixedBackupDirsAndKeepAllNeverPrune() {
    QString parent;
    QStringList targets;
    QVERIFY2(!BuildBackupRetentionPlan("remote:backups/current", 2,
                                       QStringList() << "current", &parent,
                                       &targets),
             "fixed backup dir should not create a retention plan");
    QVERIFY2(targets.isEmpty(), "fixed backup dir produced delete targets");
    QVERIFY2(!BuildBackupRetentionPlan("remote:backups/{date}", 0,
                                       QStringList() << "2026-06-18_010203",
                                       &parent, &targets),
             "keep-all setting should not create a retention plan");
    QVERIFY2(targets.isEmpty(), "keep-all setting produced delete targets");
  }

  void externalCallbackUrlsMustBeValidHttp() {
    QUrl parsed;
    QString error;
    QVERIFY2(ParseHttpUrl("https://example.com/hook?token=abc", &parsed, &error),
             "valid HTTPS URL was rejected");
    QVERIFY2(parsed.scheme() == "https" && parsed.host() == "example.com",
             "valid HTTPS URL parsed incorrectly");
    QVERIFY2(!ParseHttpUrl("file:///C:/secret.txt", &parsed, &error),
             "file URL was accepted");
    QVERIFY2(error.contains("http://"), "file URL error was not actionable");
    QVERIFY2(!ParseHttpUrl("https://", &parsed, &error),
             "hostless HTTPS URL was accepted");
  }

  // A task saved without transfers or checkers (hand-written, or from an
  // older tasks.json) used to pass "--transfers ''", which stops rclone.
  void aTaskWithoutTransferCountsUsesRcloneDefaults() {
    QByteArray bytes = R"({"version":1,"tasks":[{"description":"bare",
        "jobType":2,"operation":1,"source":"/src","dest":"remote:dst",
        "isFolder":true,
        "uniqueId":"{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"}]})";
    QBuffer in(&bytes);
    QVERIFY(in.open(QIODevice::ReadOnly));
    JobOptionsStoreLoadResult loaded = ReadJobOptionsStoreJson(&in);
    QVERIFY2(loaded.error.isEmpty(), qPrintable(loaded.error));
    QCOMPARE(loaded.tasks.size(), 1);

    const QStringList args = loaded.tasks.first()->getOptions();
    for (const QString &flag : {"--transfers", "--checkers", "--contimeout",
                                "--timeout", "--retries", "--low-level-retries"}) {
      QVERIFY2(!args.contains(flag), qPrintable(flag + " in " + args.join(' ')));
    }
    QVERIFY2(!args.contains(QString()) && !args.contains("s"),
             qPrintable(args.join(' ')));

    JobOptions *jo = loaded.tasks.first();
    jo->transfers = "4";
    jo->checkers = "8";
    jo->connectTimeout = "60";
    jo->idleTimeout = "300";
    jo->retries = "3";
    jo->lowLevelRetries = "10";
    const QStringList set = jo->getOptions();
    QCOMPARE(argAfter(set, "--transfers"), QString("4"));
    QCOMPARE(argAfter(set, "--checkers"), QString("8"));
    QCOMPARE(argAfter(set, "--contimeout"), QString("60s"));
    QCOMPARE(argAfter(set, "--timeout"), QString("300s"));
    QCOMPARE(argAfter(set, "--retries"), QString("3"));
    QCOMPARE(argAfter(set, "--low-level-retries"), QString("10"));
    ClearJobOptionsList(&loaded.tasks);
  }
};

QTEST_MAIN(DryRunContractTest)
#include "dryrun_contract_test.moc"
