#include "schedule_manager.h"

namespace {
const QString kPrefix = "RcloneBrowserNG_";
}

QString ScheduleManager::appPath() {
  return QDir::toNativeSeparators(qApp->applicationFilePath());
}

QString ScheduleManager::scheduleTaskName(const QString &taskName) {
  QString safe = taskName;
  safe.replace('\\', '_').replace('/', '_').replace('"', '_');
  return kPrefix + safe;
}

bool ScheduleManager::isSupported() {
#if defined(Q_OS_WIN32)
  return true;
#elif defined(Q_OS_LINUX)
  return QFile::exists("/usr/bin/crontab") ||
         QFile::exists("/bin/crontab");
#elif defined(Q_OS_MACOS)
  return true;
#else
  return false;
#endif
}

bool ScheduleManager::installSchedule(const QString &taskName,
                                       const QString &interval,
                                       const QString &time, QString *error) {
  const QString sysName = scheduleTaskName(taskName);
  const QString app = appPath();

#if defined(Q_OS_WIN32)
  QString schedule;
  QString modifier;
  if (interval == "hourly") {
    schedule = "HOURLY";
  } else if (interval == "daily") {
    schedule = "DAILY";
  } else if (interval == "weekly") {
    schedule = "WEEKLY";
  } else if (interval.endsWith("m")) {
    schedule = "MINUTE";
    modifier = interval.left(interval.length() - 1);
  } else if (interval.endsWith("h")) {
    schedule = "HOURLY";
    modifier = interval.left(interval.length() - 1);
  } else {
    schedule = "DAILY";
  }

  QStringList args;
  args << "/Create" << "/F" << "/TN" << sysName << "/SC" << schedule;
  if (!modifier.isEmpty()) {
    args << "/MO" << modifier;
  }
  if (!time.isEmpty() && (schedule == "DAILY" || schedule == "WEEKLY")) {
    args << "/ST" << time;
  }
  args << "/TR"
       << QString("\"%1\" --run-task \"%2\"").arg(app, taskName);

  QProcess proc;
  proc.start("schtasks.exe", args);
  if (!proc.waitForFinished(15000)) {
    if (error) *error = "schtasks.exe timed out";
    return false;
  }
  if (proc.exitCode() != 0) {
    if (error)
      *error = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
    return false;
  }
  return true;

#elif defined(Q_OS_LINUX)
  QProcess existing;
  existing.start("crontab", QStringList() << "-l");
  existing.waitForFinished(5000);
  QString crontab = QString::fromUtf8(existing.readAllStandardOutput());

  QString marker = "# " + sysName;
  QStringList lines = crontab.split('\n');
  QStringList filtered;
  for (const auto &line : lines) {
    if (!line.contains(marker))
      filtered << line;
  }

  QString cronExpr;
  if (interval == "hourly") {
    cronExpr = QString("0 * * * *");
  } else if (interval == "weekly") {
    cronExpr = QString("%1 * * 0").arg(time.isEmpty() ? "0 0" : "0 " + time.left(2));
  } else if (interval.endsWith("m")) {
    cronExpr = QString("*/%1 * * * *").arg(interval.left(interval.length() - 1));
  } else {
    cronExpr = QString("0 %1 * * *").arg(time.isEmpty() ? "0" : time.left(2));
  }

  filtered << QString("%1 \"%2\" --run-task \"%3\" %4")
                  .arg(cronExpr, app, taskName, marker);

  QProcess install;
  install.start("crontab", QStringList() << "-");
  install.write(filtered.join('\n').toUtf8());
  install.closeWriteChannel();
  if (!install.waitForFinished(5000)) {
    if (error) *error = "crontab timed out";
    return false;
  }
  if (install.exitCode() != 0) {
    if (error) *error = QString::fromUtf8(install.readAllStandardError()).trimmed();
    return false;
  }
  return true;

#elif defined(Q_OS_MACOS)
  QString plistDir =
      QDir::homePath() + "/Library/LaunchAgents";
  QDir().mkpath(plistDir);
  QString plistPath = plistDir + "/" + sysName + ".plist";

  int intervalSec = 86400;
  if (interval == "hourly") intervalSec = 3600;
  else if (interval == "weekly") intervalSec = 604800;
  else if (interval.endsWith("m"))
    intervalSec = interval.left(interval.length() - 1).toInt() * 60;
  else if (interval.endsWith("h"))
    intervalSec = interval.left(interval.length() - 1).toInt() * 3600;

  QString plist = QString(
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
      "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
      "<plist version=\"1.0\">\n<dict>\n"
      "  <key>Label</key><string>%1</string>\n"
      "  <key>ProgramArguments</key><array>\n"
      "    <string>%2</string>\n"
      "    <string>--run-task</string>\n"
      "    <string>%3</string>\n"
      "  </array>\n"
      "  <key>StartInterval</key><integer>%4</integer>\n"
      "  <key>RunAtLoad</key><false/>\n"
      "</dict>\n</plist>\n")
          .arg(sysName, app, taskName)
          .arg(intervalSec);

  QFile file(plistPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error) *error = "Cannot write " + plistPath;
    return false;
  }
  file.write(plist.toUtf8());
  file.close();

  QProcess load;
  load.start("launchctl", QStringList() << "load" << plistPath);
  load.waitForFinished(5000);
  if (load.exitCode() != 0) {
    if (error) *error = QString::fromUtf8(load.readAllStandardError()).trimmed();
    return false;
  }
  return true;

#else
  if (error) *error = "Scheduling not supported on this platform";
  return false;
#endif
}

bool ScheduleManager::removeSchedule(const QString &taskName, QString *error) {
  const QString sysName = scheduleTaskName(taskName);

#if defined(Q_OS_WIN32)
  QProcess proc;
  proc.start("schtasks.exe",
             QStringList() << "/Delete" << "/F" << "/TN" << sysName);
  proc.waitForFinished(10000);
  if (proc.exitCode() != 0) {
    if (error)
      *error = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
    return false;
  }
  return true;

#elif defined(Q_OS_LINUX)
  QProcess existing;
  existing.start("crontab", QStringList() << "-l");
  existing.waitForFinished(5000);
  QString crontab = QString::fromUtf8(existing.readAllStandardOutput());

  QString marker = "# " + sysName;
  QStringList lines = crontab.split('\n');
  QStringList filtered;
  for (const auto &line : lines) {
    if (!line.contains(marker))
      filtered << line;
  }

  QProcess install;
  install.start("crontab", QStringList() << "-");
  install.write(filtered.join('\n').toUtf8());
  install.closeWriteChannel();
  install.waitForFinished(5000);
  return install.exitCode() == 0;

#elif defined(Q_OS_MACOS)
  QString plistPath =
      QDir::homePath() + "/Library/LaunchAgents/" + sysName + ".plist";
  QProcess unload;
  unload.start("launchctl", QStringList() << "unload" << plistPath);
  unload.waitForFinished(5000);
  QFile::remove(plistPath);
  return true;

#else
  if (error) *error = "Not supported";
  return false;
#endif
}

QList<ScheduleEntry> ScheduleManager::listSchedules(QString *error) {
  QList<ScheduleEntry> result;

#if defined(Q_OS_WIN32)
  QProcess proc;
  proc.start("schtasks.exe",
             QStringList() << "/Query" << "/FO" << "CSV" << "/NH");
  proc.waitForFinished(10000);
  if (proc.exitCode() != 0) {
    if (error)
      *error = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
    return result;
  }
  QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
  for (const QString &line : output.split('\n')) {
    if (!line.contains(kPrefix))
      continue;
    QStringList cols = line.split(',');
    if (cols.isEmpty())
      continue;
    QString name = cols[0].trimmed();
    name.remove('"');
    if (name.startsWith('\\'))
      name = name.mid(1);
    if (!name.startsWith(kPrefix))
      continue;
    ScheduleEntry entry;
    entry.taskName = name.mid(kPrefix.length());
    if (cols.size() > 2) {
      entry.time = cols[1].trimmed().remove('"');
      entry.interval = cols[2].trimmed().remove('"');
    }
    entry.enabled = !line.contains("Disabled");
    result.append(entry);
  }

#elif defined(Q_OS_LINUX)
  QProcess existing;
  existing.start("crontab", QStringList() << "-l");
  existing.waitForFinished(5000);
  QString crontab = QString::fromUtf8(existing.readAllStandardOutput());
  for (const QString &line : crontab.split('\n')) {
    int marker = line.indexOf("# " + kPrefix);
    if (marker < 0)
      continue;
    ScheduleEntry entry;
    entry.taskName =
        line.mid(marker + 2 + kPrefix.length()).trimmed();
    entry.interval = line.left(line.indexOf('"')).trimmed();
    result.append(entry);
  }

#elif defined(Q_OS_MACOS)
  QDir agentsDir(QDir::homePath() + "/Library/LaunchAgents");
  for (const auto &fi : agentsDir.entryInfoList(
           QStringList() << kPrefix + "*.plist", QDir::Files)) {
    ScheduleEntry entry;
    entry.taskName =
        fi.baseName().mid(kPrefix.length());
    result.append(entry);
  }
#endif

  Q_UNUSED(error);
  return result;
}
