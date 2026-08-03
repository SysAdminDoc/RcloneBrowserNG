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

QString JobOptionsStoreSecurityNotice();
QString ProtectJobOptionSensitiveValue(const QString &value);
QString UnprotectJobOptionSensitiveValue(const QString &value);

void ClearJobOptionsList(QList<JobOptions *> *tasks);
