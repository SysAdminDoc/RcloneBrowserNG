#include "job_history.h"
#include "rclone_capabilities.h"

#include <QBuffer>
#include <QTest>

class JobHistoryTest : public QObject {
  Q_OBJECT

  static JobHistoryEntry makeEntry() {
    JobHistoryEntry entry;
    entry.startedAt = QDateTime::fromString("2026-06-16T10:11:12.123Z",
                                            Qt::ISODateWithMs);
    entry.finishedAt = QDateTime::fromString("2026-06-16T10:12:13.456Z",
                                             Qt::ISODateWithMs);
    entry.name = "Copy localdisk:/source";
    entry.source = "localdisk:/source";
    entry.dest = "localdisk:/dest";
    entry.success = true;
    entry.bytes = 123456789;
    entry.files = 42;
    entry.errors = 0;
    entry.exitCode = 0;
    return entry;
  }

private slots:
  void roundTrip() {
    QVector<JobHistoryEntry> entries;
    entries.append(makeEntry());

    QByteArray data;
    QBuffer out(&data);
    out.open(QIODevice::WriteOnly);
    QString error;
    QVERIFY(WriteJobHistory(&out, entries, &error));

    QBuffer in(&data);
    in.open(QIODevice::ReadOnly);
    QVector<JobHistoryEntry> loaded = ReadJobHistory(&in, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().name, entries.first().name);
    QCOMPARE(loaded.first().bytes, entries.first().bytes);
    QCOMPARE(loaded.first().files, entries.first().files);
    QVERIFY(loaded.first().success);
  }

  void malformedJsonFails() {
    QBuffer bad;
    bad.setData("{");
    bad.open(QIODevice::ReadOnly);
    QString error;
    QVector<JobHistoryEntry> loaded = ReadJobHistory(&bad, &error);
    QVERIFY(loaded.isEmpty());
    QVERIFY(error.contains("Failed to parse job history"));
  }

  void redactsSensitiveOutput() {
    const QString input =
        "--rc-pass rc-secret token=token-secret client_secret=client-secret "
        "Authorization: Bearer bearer-secret";
    const QString redacted = Diagnostics::redactSecrets(input);
    QVERIFY(!redacted.contains("rc-secret"));
    QVERIFY(!redacted.contains("token-secret"));
    QVERIFY(!redacted.contains("client-secret"));
    QVERIFY(!redacted.contains("bearer-secret"));
    QVERIFY(redacted.count("<redacted>") >= 4);
  }
};

QTEST_MAIN(JobHistoryTest)
#include "job_history_test.moc"
