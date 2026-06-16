#pragma once

#include "pch.h"

struct ScheduleEntry {
  QString taskName;
  QString interval;
  QString time;
  bool enabled = true;
};

class ScheduleManager {
public:
  static bool installSchedule(const QString &taskName,
                               const QString &interval, const QString &time,
                               QString *error);
  static bool removeSchedule(const QString &taskName, QString *error);
  static QList<ScheduleEntry> listSchedules(QString *error);
  static bool isSupported();

private:
  static QString appPath();
  static QString scheduleTaskName(const QString &taskName);
};
