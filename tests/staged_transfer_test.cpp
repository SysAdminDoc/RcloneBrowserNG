#include "job_options_store.h"
#include "staged_transfer.h"

#include <QTest>

class StagedTransferTest : public QObject {
  Q_OBJECT

private slots:
  void version2RoundTripProtectsExecutionMetadata() {
    StagedTransfer original;
    original.message = "Copy remote report";
    original.source = "remote:source";
    original.dest = "C:/data";
    original.args = {"copy", "--verbose", "remote:source", "C:/data"};
    original.heartbeatUrl = "https://hc-ping.com/heartbeat-secret";
    original.preCommand = "echo pre-secret";
    original.postCommand = "echo post-secret";
    original.webhookUrl = "https://hooks.example.test/webhook-secret";
    original.taskName = "Nightly report";
    original.backupDirTemplate = "C:/backups/{date}";
    original.backupRetainCount = 7;
    original.verifyAfterTransfer = true;
    original.hooksTrusted = true;

    const QJsonDocument document =
        StagedTransferStore::Serialize({original});
    const QByteArray stored = document.toJson(QJsonDocument::Compact);
    QCOMPARE(document.object().value("version").toInt(), 2);
    QVERIFY(!stored.contains("heartbeat-secret"));
    QVERIFY(!stored.contains("pre-secret"));
    QVERIFY(!stored.contains("post-secret"));
    QVERIFY(!stored.contains("webhook-secret"));

    QList<StagedTransfer> restored;
    bool migrated = true;
    QString error;
    QVERIFY2(StagedTransferStore::Deserialize(document, &restored, &migrated,
                                              &error),
             qPrintable(error));
    QCOMPARE(restored.size(), 1);
    QCOMPARE(migrated, false);
    QCOMPARE(restored.first().message, original.message);
    QCOMPARE(restored.first().args, original.args);
    QCOMPARE(restored.first().heartbeatUrl, original.heartbeatUrl);
    QCOMPARE(restored.first().preCommand, original.preCommand);
    QCOMPARE(restored.first().postCommand, original.postCommand);
    QCOMPARE(restored.first().webhookUrl, original.webhookUrl);
    QCOMPARE(restored.first().taskName, original.taskName);
    QCOMPARE(restored.first().backupRetainCount,
             original.backupRetainCount);
    QCOMPARE(restored.first().verifyAfterTransfer,
             original.verifyAfterTransfer);
    QCOMPARE(restored.first().hooksTrusted, original.hooksTrusted);
  }

  void version1MigratesBasicEntries() {
    QJsonObject entry;
    entry.insert("message", "Legacy transfer");
    entry.insert("source", "remote:source");
    entry.insert("dest", "C:/data");
    entry.insert("args", QJsonArray({"copy", "remote:source", "C:/data"}));
    entry.insert("backupDirTemplate", "C:/backups/{date}");
    entry.insert("backupRetainCount", 3);
    QJsonObject root;
    root.insert("version", 1);
    root.insert("staged", QJsonArray({entry}));

    QList<StagedTransfer> restored;
    bool migrated = false;
    QString error;
    QVERIFY2(StagedTransferStore::Deserialize(QJsonDocument(root), &restored,
                                              &migrated, &error),
             qPrintable(error));
    QCOMPARE(restored.size(), 1);
    QCOMPARE(migrated, true);
    QCOMPARE(restored.first().backupRetainCount, 3);
    QVERIFY(restored.first().heartbeatUrl.isEmpty());
    QVERIFY(!restored.first().verifyAfterTransfer);
  }

  void rejectsUnprotectedVersion2Secrets() {
    QJsonObject entry;
    entry.insert("message", "Unsafe transfer");
    entry.insert("source", "remote:source");
    entry.insert("dest", "C:/data");
    entry.insert("args", QJsonArray());
    entry.insert("heartbeatUrl", "https://example.test/plain-secret");
    QJsonObject root;
    root.insert("version", 2);
    root.insert("staged", QJsonArray({entry}));

    QList<StagedTransfer> restored;
    bool migrated = false;
    QString error;
    QVERIFY(!StagedTransferStore::Deserialize(QJsonDocument(root), &restored,
                                               &migrated, &error));
    QVERIFY(error.contains("not protected"));
    QVERIFY(restored.isEmpty());
  }
};

QTEST_MAIN(StagedTransferTest)
#include "staged_transfer_test.moc"
