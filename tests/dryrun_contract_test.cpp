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
};

QTEST_MAIN(DryRunContractTest)
#include "dryrun_contract_test.moc"
