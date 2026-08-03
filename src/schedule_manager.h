#pragma once

#include "pch.h"
#include <functional>

struct ScheduleEntry {
  QString taskName;
  QString interval;
  QString time;
  bool enabled = true;
};

class ScheduleManager {
public:
  // These synchronous primitives are worker-thread implementations. GUI code
  // must use the asynchronous wrappers below so native helpers never block
  // the window thread.
  static bool installSchedule(const QString &taskName,
                               const QString &interval, const QString &time,
                               QString *error);
  static bool removeSchedule(const QString &taskName, QString *error);
  static QList<ScheduleEntry> listSchedules(QString *error);
  static bool isSupported();

  using ScheduleOperationCallback =
      std::function<void(bool ok, const QString &error)>;
  using ScheduleListCallback = std::function<void(
      const QList<ScheduleEntry> &entries, const QString &error)>;
  static void installScheduleAsync(const QString &taskName,
                                   const QString &interval,
                                   const QString &time, QObject *context,
                                   ScheduleOperationCallback callback);
  static void removeScheduleAsync(const QString &taskName, QObject *context,
                                  ScheduleOperationCallback callback);
  static void listSchedulesAsync(QObject *context,
                                 ScheduleListCallback callback);

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
  static QList<QDateTime> nextCronRuns(const QString &cronExpr, int count,
                                       QDateTime from = QDateTime());
  static bool isValidCronExpr(const QString &cronExpr);

private:
  static QString appPath();
};
