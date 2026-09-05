#pragma once

#include "pch.h"

struct JobHistoryEntry {
  QDateTime startedAt;
  QDateTime finishedAt;
  QString name;
  QString source;
  QString dest;
  bool success = false;
  qint64 bytes = 0;
  int files = 0;
  int errors = 0;
  int exitCode = 0;
  // What the job card called this run. Stored rather than recomputed so
  // the history cannot describe a cancelled job as an rclone exit code.
  QString statusLabel;
  QStringList transferDetail;
  QStringList args;
};

QString RedactedJobDetail(const JobHistoryEntry &entry);

QVector<JobHistoryEntry> ReadJobHistory(QIODevice *device, QString *error);
bool WriteJobHistory(QIODevice *device, const QVector<JobHistoryEntry> &entries,
                     QString *error);

class JobHistoryStore {
public:
  static QString GetPersistenceFilePath();
  static QVector<JobHistoryEntry> Load(QString *error = nullptr);
  static bool Append(const JobHistoryEntry &entry, QString *error = nullptr,
                     int maxEntries = 200);
};
