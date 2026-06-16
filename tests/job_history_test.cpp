#include "job_history.h"

#include <iostream>

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << std::endl;
    std::exit(1);
  }
}

JobHistoryEntry makeEntry() {
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

int main() {
  QVector<JobHistoryEntry> entries;
  entries.append(makeEntry());

  QByteArray data;
  QBuffer out(&data);
  out.open(QIODevice::WriteOnly);
  QString error;
  require(WriteJobHistory(&out, entries, &error),
          qPrintable("history write failed: " + error));

  QBuffer in(&data);
  in.open(QIODevice::ReadOnly);
  QVector<JobHistoryEntry> loaded = ReadJobHistory(&in, &error);
  require(error.isEmpty(), qPrintable("history read failed: " + error));
  require(loaded.size() == 1, "history entry count changed");
  require(loaded.first().name == entries.first().name, "history name changed");
  require(loaded.first().bytes == entries.first().bytes, "history bytes changed");
  require(loaded.first().files == entries.first().files, "history files changed");
  require(loaded.first().success, "history success changed");

  QBuffer bad;
  bad.setData("{");
  bad.open(QIODevice::ReadOnly);
  loaded = ReadJobHistory(&bad, &error);
  require(loaded.isEmpty(), "malformed history unexpectedly loaded entries");
  require(error.contains("Failed to parse job history"),
          "malformed history did not report parse failure");
  return 0;
}
