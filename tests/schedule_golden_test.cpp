#include "schedule_manager.h"

#include <QDebug>
#include <cstdlib>

namespace {
void require(bool condition, const QString &message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

void requireContains(const QString &haystack, const QString &needle,
                     const QString &context) {
  if (!haystack.contains(needle)) {
    qCritical().noquote()
        << context << ": expected to find" << needle << "in output";
    qCritical().noquote() << "Actual:\n" << haystack;
    std::exit(1);
  }
}

void requireNotContains(const QString &haystack, const QString &needle,
                        const QString &context) {
  if (haystack.contains(needle)) {
    qCritical().noquote()
        << context << ": did NOT expect to find" << needle << "in output";
    std::exit(1);
  }
}
} // namespace

int main() {
  const QString app = "/usr/bin/rclone-browser";
  const QString task = "Nightly Backup";

  // Task name sanitization
  {
    QString safe = ScheduleManager::scheduleTaskName("My Task <>&\"");
    require(safe.startsWith("RcloneBrowserNG_"), "prefix missing");
    requireNotContains(safe, "<", "task name sanitization");
    requireNotContains(safe, ">", "task name sanitization");
    requireNotContains(safe, "&", "task name sanitization");
  }

  // Windows XML: daily with StartWhenAvailable
  {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", app, task, "daily", "02:00");
    requireContains(xml, "<StartWhenAvailable>true</StartWhenAvailable>",
                    "Windows daily XML");
    requireContains(xml, "02:00", "Windows daily XML start time");
    requireContains(xml, "<DaysInterval>1</DaysInterval>",
                    "Windows daily XML schedule");
    requireContains(xml, "--run-task", "Windows daily XML command");
    requireContains(xml, "Nightly Backup", "Windows daily XML task name");
  }

  // Windows XML: hourly
  {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", app, task, "hourly", "");
    requireContains(xml, "<StartWhenAvailable>true</StartWhenAvailable>",
                    "Windows hourly XML");
    requireContains(xml, "PT1H", "Windows hourly XML interval");
  }

  // Windows XML: weekly
  {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", app, task, "weekly", "03:00");
    requireContains(xml, "<StartWhenAvailable>true</StartWhenAvailable>",
                    "Windows weekly XML");
    requireContains(xml, "<WeeksInterval>1</WeeksInterval>",
                    "Windows weekly XML schedule");
    requireContains(xml, "03:00", "Windows weekly XML start time");
  }

  // Windows XML: minute interval
  {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", app, task, "15m", "");
    requireContains(xml, "PT15M", "Windows 15m XML interval");
    requireContains(xml, "<StartWhenAvailable>true</StartWhenAvailable>",
                    "Windows 15m XML");
  }

  // Windows XML: hour interval
  {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", app, task, "2h", "");
    requireContains(xml, "PT2H", "Windows 2h XML interval");
  }

  // Windows XML: XML escaping
  {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", "C:\\Program Files\\app.exe",
        "Task <with> &special \"chars\"", "daily", "");
    requireContains(xml, "&amp;special", "Windows XML escaping &");
    requireContains(xml, "&lt;with&gt;", "Windows XML escaping <>");
    requireContains(xml, "&quot;chars&quot;", "Windows XML escaping quotes");
  }

  // Systemd service
  {
    QString svc = ScheduleManager::generateSystemdService(task, app);
    requireContains(svc, "Type=oneshot", "systemd service type");
    requireContains(svc, "--run-task", "systemd service command");
    requireContains(svc, "Nightly Backup", "systemd service task name");
  }

  // Systemd timer: daily with Persistent
  {
    QString tmr = ScheduleManager::generateSystemdTimer(task, "daily", "02");
    requireContains(tmr, "Persistent=true", "systemd daily timer persistent");
    requireContains(tmr, "*-*-* 02:00:00", "systemd daily timer calendar");
    requireContains(tmr, "WantedBy=timers.target",
                    "systemd daily timer install");
  }

  // Systemd timer: hourly
  {
    QString tmr = ScheduleManager::generateSystemdTimer(task, "hourly", "");
    requireContains(tmr, "*-*-* *:00:00", "systemd hourly timer calendar");
    requireContains(tmr, "Persistent=true", "systemd hourly timer persistent");
  }

  // Systemd timer: weekly
  {
    QString tmr = ScheduleManager::generateSystemdTimer(task, "weekly", "03");
    requireContains(tmr, "Sun *-*-* 03:00:00",
                    "systemd weekly timer calendar");
  }

  // Systemd timer: minute interval
  {
    QString tmr = ScheduleManager::generateSystemdTimer(task, "15m", "");
    requireContains(tmr, "*-*-* *:00/15:00", "systemd 15m timer calendar");
  }

  // macOS plist: daily
  {
    QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", app, task, "daily", "02:00");
    requireContains(plist, "<key>Hour</key><integer>2</integer>",
                    "macOS daily plist hour");
    requireContains(plist, "StartCalendarInterval",
                    "macOS daily plist calendar");
    requireContains(plist, "--run-task", "macOS daily plist command");
    requireNotContains(plist, "StartInterval", "macOS daily should not use interval");
  }

  // macOS plist: weekly
  {
    QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", app, task, "weekly", "04:00");
    requireContains(plist, "<key>Weekday</key><integer>0</integer>",
                    "macOS weekly plist weekday");
    requireContains(plist, "<key>Hour</key><integer>4</integer>",
                    "macOS weekly plist hour");
  }

  // macOS plist: hourly uses StartInterval
  {
    QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", app, task, "hourly", "");
    requireContains(plist, "<key>StartInterval</key><integer>3600</integer>",
                    "macOS hourly plist interval");
    requireNotContains(plist, "StartCalendarInterval",
                       "macOS hourly should use interval, not calendar");
  }

  // macOS plist: minute interval
  {
    QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", app, task, "15m", "");
    requireContains(plist, "<key>StartInterval</key><integer>900</integer>",
                    "macOS 15m plist interval");
  }

  // macOS plist: XML escaping
  {
    QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", app, "Task <with> &chars", "daily", "");
    requireContains(plist, "&amp;chars", "macOS plist XML escaping &");
    requireContains(plist, "&lt;with&gt;", "macOS plist XML escaping <>");
  }

  qInfo() << "All schedule golden tests passed.";
  return 0;
}
