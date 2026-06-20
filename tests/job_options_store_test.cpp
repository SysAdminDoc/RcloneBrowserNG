#include "job_options_store.h"

#include <QBuffer>
#include <QTest>

namespace {
JobOptions *makeTask() {
  auto jo = new JobOptions(false);
  jo->description = "Nightly upload";
  jo->operation = JobOptions::Sync;
  jo->sync = true;
  jo->syncTiming = JobOptions::After;
  jo->skipNewer = true;
  jo->transfers = "4";
  jo->checkers = "8";
  jo->source = "C:/data";
  jo->dest = "remote:backup";
  jo->isFolder = true;
  jo->DriveSharedWithMe = true;
  jo->watchFolder = true;
  jo->backupDir = "remote:backups/{date}";
  jo->backupRetainCount = 7;
  jo->conflictResolve = "newer";
  jo->uniqueId = QUuid::fromString("{11111111-2222-3333-4444-555555555555}");
  return jo;
}

void writeLegacyTask(QIODevice *device, const JobOptions &jo) {
  constexpr qint32 legacyVersionWithUniqueId = 3;
  QDataStream stream(device);
  stream.setVersion(QDataStream::Qt_5_2);
  stream << jo.myName() << legacyVersionWithUniqueId << jo.description
         << static_cast<quint32>(jo.jobType)
         << static_cast<quint32>(jo.operation) << jo.sync
         << static_cast<quint32>(jo.syncTiming) << jo.skipNewer
         << jo.skipExisting << jo.compare
         << static_cast<quint32>(jo.compareOption) << jo.verbose
         << jo.sameFilesystem << jo.dontUpdateModified << jo.transfers
         << jo.checkers << jo.bandwidth << jo.minSize << jo.minAge
         << jo.maxAge << jo.maxDepth << jo.connectTimeout << jo.idleTimeout
         << jo.retries << jo.lowLevelRetries << jo.deleteExcluded
         << jo.excluded << jo.extra << jo.DriveSharedWithMe << jo.source
         << jo.dest << jo.isFolder << jo.uniqueId;
}

void verifyTaskMatches(const JobOptions *task, bool expectWatchFolder = true,
                       bool expectCurrentFields = true) {
  QCOMPARE(task->description, QString("Nightly upload"));
  QCOMPARE(task->operation, JobOptions::Sync);
  QCOMPARE(task->syncTiming, JobOptions::After);
  QVERIFY2(task->skipNewer, "skip-newer flag changed");
  QCOMPARE(task->source, QString("C:/data"));
  QCOMPARE(task->dest, QString("remote:backup"));
  QVERIFY2(task->isFolder, "folder flag changed");
  QVERIFY2(task->DriveSharedWithMe, "Drive shared flag changed");
  QCOMPARE(task->watchFolder, expectWatchFolder);
  if (expectCurrentFields) {
    QCOMPARE(task->backupDir, QString("remote:backups/{date}"));
    QCOMPARE(task->backupRetainCount, 7);
    QCOMPARE(task->conflictResolve, QString("newer"));
  } else {
    QVERIFY2(task->backupDir.isEmpty(), "legacy backup dir should be empty");
    QCOMPARE(task->backupRetainCount, 0);
    QVERIFY2(task->conflictResolve.isEmpty(),
             "legacy conflict strategy should be empty");
  }
  QCOMPARE(task->uniqueId,
           QUuid::fromString("{11111111-2222-3333-4444-555555555555}"));
}
} // namespace

class JobOptionsStoreTest : public QObject {
  Q_OBJECT

private slots:
  void newFormatRoundTrip() {
    QList<JobOptions *> tasks;
    tasks.append(makeTask());

    QByteArray bytes;
    QBuffer out(&bytes);
    QVERIFY2(out.open(QIODevice::WriteOnly), "failed to open write buffer");
    QString error;
    QVERIFY2(WriteJobOptionsStore(&out, tasks, &error),
             qPrintable("failed to write new store: " + error));
    out.close();

    QBuffer in(&bytes);
    QVERIFY2(in.open(QIODevice::ReadOnly), "failed to open read buffer");
    JobOptionsStoreLoadResult loaded = ReadJobOptionsStore(&in);
    QVERIFY2(loaded.error.isEmpty(),
             qPrintable("failed to read new store: " + loaded.error));
    QVERIFY2(!loaded.migratedFromLegacy, "new store was marked legacy");
    QCOMPARE(loaded.tasks.size(), 1);
    verifyTaskMatches(loaded.tasks.first());
    ClearJobOptionsList(&loaded.tasks);
    ClearJobOptionsList(&tasks);
  }

  void legacyFormatMigration() {
    QList<JobOptions *> tasks;
    tasks.append(makeTask());

    QByteArray legacyBytes;
    QBuffer legacyOut(&legacyBytes);
    QVERIFY2(legacyOut.open(QIODevice::WriteOnly),
             "failed to open legacy write buffer");
    writeLegacyTask(&legacyOut, *tasks.first());
    legacyOut.close();

    QBuffer legacyIn(&legacyBytes);
    QVERIFY2(legacyIn.open(QIODevice::ReadOnly),
             "failed to open legacy read buffer");
    JobOptionsStoreLoadResult legacy = ReadJobOptionsStore(&legacyIn);
    QVERIFY2(legacy.error.isEmpty(),
             qPrintable("failed to read legacy store: " + legacy.error));
    QVERIFY2(legacy.migratedFromLegacy, "legacy store was not marked migrated");
    QCOMPARE(legacy.tasks.size(), 1);
    verifyTaskMatches(legacy.tasks.first(), false, false);
    ClearJobOptionsList(&legacy.tasks);
    ClearJobOptionsList(&tasks);
  }

  void newerSchemaRejected() {
    QByteArray newerBytes;
    QBuffer newerOut(&newerBytes);
    QVERIFY2(newerOut.open(QIODevice::WriteOnly),
             "failed to open newer write buffer");
    QDataStream newerStream(&newerOut);
    newerStream.setVersion(QDataStream::Qt_5_2);
    newerStream << QString("RcloneBrowserNG.tasks") << qint32(999) << qint32(0);
    newerOut.close();

    QBuffer newerIn(&newerBytes);
    QVERIFY2(newerIn.open(QIODevice::ReadOnly),
             "failed to open newer read buffer");
    JobOptionsStoreLoadResult newer = ReadJobOptionsStore(&newerIn);
    QVERIFY2(newer.tasks.isEmpty(), "newer schema unexpectedly returned tasks");
    QVERIFY2(newer.error.contains("stored task schema is newer"),
             "newer schema error was not actionable");
  }

  void missingTasksArrayInJson() {
    QByteArray missingTasks = R"({"version":1})";
    QBuffer missingTasksIn(&missingTasks);
    QVERIFY2(missingTasksIn.open(QIODevice::ReadOnly),
             "failed to open missing-tasks JSON buffer");
    JobOptionsStoreLoadResult missingTasksLoaded =
        ReadJobOptionsStoreJson(&missingTasksIn);
    QVERIFY2(missingTasksLoaded.tasks.isEmpty(),
             "missing tasks array unexpectedly returned tasks");
    QVERIFY2(missingTasksLoaded.error.contains("tasks array"),
             "missing tasks array error was not actionable");
  }

  void nonObjectTaskInJson() {
    QByteArray nonObjectTask = R"({"version":1,"tasks":[42]})";
    QBuffer nonObjectTaskIn(&nonObjectTask);
    QVERIFY2(nonObjectTaskIn.open(QIODevice::ReadOnly),
             "failed to open non-object JSON task buffer");
    JobOptionsStoreLoadResult nonObjectTaskLoaded =
        ReadJobOptionsStoreJson(&nonObjectTaskIn);
    QVERIFY2(nonObjectTaskLoaded.tasks.isEmpty(),
             "non-object task unexpectedly returned tasks");
    QVERIFY2(nonObjectTaskLoaded.error.contains("not an object"),
             "non-object task error was not actionable");
  }

  void hookTrustGate() {
    auto *hooked = makeTask();
    hooked->preCommand = "echo pre";
    hooked->postCommand = "echo post";
    hooked->hooksTrusted = true;
    QList<JobOptions *> hookedList;
    hookedList.append(hooked);

    QByteArray jsonBytes;
    QBuffer jsonOut(&jsonBytes);
    QString error;
    QVERIFY2(jsonOut.open(QIODevice::WriteOnly), "failed to open hook json out");
    QVERIFY2(WriteJobOptionsStoreJson(&jsonOut, hookedList, &error),
             qPrintable("failed to write hooked task: " + error));
    jsonOut.close();

    QBuffer jsonIn(&jsonBytes);
    QVERIFY2(jsonIn.open(QIODevice::ReadOnly), "failed to open hook json in");
    JobOptionsStoreLoadResult hookedLoaded = ReadJobOptionsStoreJson(&jsonIn);
    QVERIFY2(hookedLoaded.error.isEmpty(),
             qPrintable("failed to read hooked task: " + hookedLoaded.error));
    QCOMPARE(hookedLoaded.tasks.size(), 1);
    QCOMPARE(hookedLoaded.tasks.first()->preCommand, QString("echo pre"));
    QCOMPARE(hookedLoaded.tasks.first()->postCommand, QString("echo post"));
    QVERIFY2(hookedLoaded.tasks.first()->hooksTrusted == true,
             "hooksTrusted should be preserved when explicitly true");
    ClearJobOptionsList(&hookedLoaded.tasks);

    // Verify that tasks without hooksTrusted in JSON default to false
    QByteArray untrustedJson =
        R"({"version":1,"tasks":[{"description":"test","preCommand":"rm -rf /","jobType":1,"operation":1,"source":"/a","dest":"r:b","uniqueId":"{11111111-2222-3333-4444-555555555555}"}]})";
    QBuffer untrustedIn(&untrustedJson);
    QVERIFY2(untrustedIn.open(QIODevice::ReadOnly), "open untrusted json");
    JobOptionsStoreLoadResult untrusted = ReadJobOptionsStoreJson(&untrustedIn);
    QVERIFY2(untrusted.error.isEmpty(), "untrusted load error");
    QCOMPARE(untrusted.tasks.size(), 1);
    QVERIFY2(!untrusted.tasks.first()->hooksTrusted,
             "task loaded without hooksTrusted key must default to untrusted");
    QCOMPARE(untrusted.tasks.first()->preCommand, QString("rm -rf /"));
    ClearJobOptionsList(&untrusted.tasks);
    ClearJobOptionsList(&hookedList);
  }

  void sensitiveFieldProtection() {
    auto *sensitive = makeTask();
    sensitive->heartbeatUrl = "https://hc-ping.com/secret-uuid-123";
    sensitive->webhookUrl = "https://discord.com/api/webhooks/SECRET";
    sensitive->preCommand = "echo secret-pre";
    sensitive->postCommand = "echo secret-post";
    QList<JobOptions *> sensitiveList;
    sensitiveList.append(sensitive);

    QByteArray sensitiveBytes;
    QBuffer sensitiveOut(&sensitiveBytes);
    QString error;
    QVERIFY2(sensitiveOut.open(QIODevice::WriteOnly), "open sensitive out");
    QVERIFY2(WriteJobOptionsStoreJson(&sensitiveOut, sensitiveList, &error),
             qPrintable("write sensitive task: " + error));
    sensitiveOut.close();

    QString stored = QString::fromUtf8(sensitiveBytes);
    QVERIFY2(!stored.contains("secret-uuid-123"),
             "heartbeatUrl secret must not appear in plain text in JSON");
    QVERIFY2(!stored.contains("SECRET"),
             "webhookUrl secret must not appear in plain text in JSON");
    QVERIFY2(!stored.contains("secret-pre"),
             "preCommand must not appear in plain text in JSON");
    QVERIFY2(!stored.contains("secret-post"),
             "postCommand must not appear in plain text in JSON");

    QBuffer sensitiveIn(&sensitiveBytes);
    QVERIFY2(sensitiveIn.open(QIODevice::ReadOnly), "open sensitive in");
    JobOptionsStoreLoadResult sensitiveLoaded =
        ReadJobOptionsStoreJson(&sensitiveIn);
    QVERIFY2(sensitiveLoaded.error.isEmpty(),
             qPrintable("load sensitive task: " + sensitiveLoaded.error));
    QCOMPARE(sensitiveLoaded.tasks.size(), 1);
    QCOMPARE(sensitiveLoaded.tasks.first()->heartbeatUrl,
             QString("https://hc-ping.com/secret-uuid-123"));
    QCOMPARE(sensitiveLoaded.tasks.first()->webhookUrl,
             QString("https://discord.com/api/webhooks/SECRET"));
    QCOMPARE(sensitiveLoaded.tasks.first()->preCommand,
             QString("echo secret-pre"));
    QCOMPARE(sensitiveLoaded.tasks.first()->postCommand,
             QString("echo secret-post"));
    ClearJobOptionsList(&sensitiveLoaded.tasks);
    ClearJobOptionsList(&sensitiveList);
  }
};

QTEST_MAIN(JobOptionsStoreTest)
#include "job_options_store_test.moc"
