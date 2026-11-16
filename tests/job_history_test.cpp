#include "job_history.h"
#include "rclone_capabilities.h"

#include <QBuffer>
#include <QFile>
#include <QStandardPaths>
#include <QTest>
#include <QThread>

#include <atomic>

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

  // Older daemon rows hold the card's rc call, which Restart would hand to
  // rclone as if it were a sync. Those rows can't be rerun; real ones can.
  void rerunsOnlyRealCommands() {
    JobHistoryEntry entry;
    QVERIFY(!CanRerun(entry));
    entry.args = {"sync", "--delete-during", "src:a", "dst:b"};
    QVERIFY(CanRerun(entry));
    entry.args = {"C:\\Program Files\\Rclone Browser NG\\rclone.exe", "rc",
                  "--url", "http://127.0.0.1:5572/", "--user", "u", "--pass",
                  "<redacted>", "--json", "{}", "core/command"};
    QVERIFY(!CanRerun(entry));
    entry.args = {"copy", "rc", "remote:rc"};
    QVERIFY(CanRerun(entry));
  }

  // The app and a scheduled --run-task can finish together. Each Append
  // loads, adds and saves the whole file, so without a lock rows went
  // missing. Threads stand in for the two processes.
  void appendsAtTheSameTimeKeepEveryRow() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("rclone-browser-job-history-test");
    QCoreApplication::setApplicationName("rclone-browser-job-history-test");
    const QString path = JobHistoryStore::GetPersistenceFilePath();
    QFile::remove(path);

    constexpr int kWriters = 4;
    constexpr int kRowsEach = 15;
    std::atomic<int> failures{0};
    QList<QThread *> writers;
    for (int w = 0; w < kWriters; ++w) {
      writers << QThread::create([w, &failures]() {
        for (int i = 0; i < kRowsEach; ++i) {
          JobHistoryEntry entry = makeEntry();
          entry.name = QString("writer %1 row %2").arg(w).arg(i);
          if (!JobHistoryStore::Append(entry)) {
            failures++;
          }
        }
      });
    }
    for (QThread *writer : writers) {
      writer->start();
    }
    for (QThread *writer : writers) {
      writer->wait();
      delete writer;
    }

    QCOMPARE(failures.load(), 0);
    QString error;
    const QVector<JobHistoryEntry> entries = JobHistoryStore::Load(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(entries.size(), kWriters * kRowsEach);
    QFile::remove(path);
  }
};

QTEST_MAIN(JobHistoryTest)
#include "job_history_test.moc"
