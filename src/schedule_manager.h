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

  static QString generateWindowsTaskXml(const QString &sysName,
                                        const QString &appPath,
                                        const QString &taskName,
                                        const QString &interval,
                                        const QString &time);
  static QString generateSystemdService(const QString &taskName,
                                        const QString &appPath);
  static QString generateSystemdTimer(const QString &taskName,
                                      const QString &interval,
                                      const QString &time);
  static QString generateMacPlist(const QString &sysName,
                                  const QString &appPath,
                                  const QString &taskName,
                                  const QString &interval,
                                  const QString &time);

  static QString scheduleTaskName(const QString &taskName);

private:
  static QString appPath();
};
