#pragma once

#include "job_options.h"
#include "pch.h"

struct JobOptionsStoreLoadResult {
  QList<JobOptions *> tasks;
  QString error;
  bool migratedFromLegacy = false;
};

JobOptionsStoreLoadResult ReadJobOptionsStore(QIODevice *device);
bool WriteJobOptionsStore(QIODevice *device, const QList<JobOptions *> &tasks,
                          QString *error = nullptr);

JobOptionsStoreLoadResult ReadJobOptionsStoreJson(QIODevice *device);
bool WriteJobOptionsStoreJson(QIODevice *device,
                              const QList<JobOptions *> &tasks,
                              QString *error = nullptr);

struct SavedTasksLoadResult {
  QList<JobOptions *> tasks;
  // A file was read. False with an empty error just means no saved tasks.
  bool loaded = false;
  // The tasks came from tasks.bin, so the caller should save them as
  // tasks.json.
  bool fromLegacyFile = false;
  // Shown to the user. Set whenever a file was there but could not be read.
  QString error;
};

// Reads tasks.json, or tasks.bin when tasks.json is missing or unreadable.
// A file that cannot be read is never renamed or deleted: an unreadable
// tasks.json gets a copy kept next to it (NG saves over it later) and an
// unreadable tasks.bin is left exactly as it is.
SavedTasksLoadResult LoadSavedTasks(const QString &jsonPath,
                                    const QString &binPath);

QString JobOptionsStoreSecurityNotice();
QString ProtectJobOptionSensitiveValue(const QString &value);
QString UnprotectJobOptionSensitiveValue(const QString &value);

void ClearJobOptionsList(QList<JobOptions *> *tasks);
