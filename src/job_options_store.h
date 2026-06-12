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
void ClearJobOptionsList(QList<JobOptions *> *tasks);
