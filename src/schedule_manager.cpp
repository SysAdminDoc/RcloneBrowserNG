#include "schedule_manager.h"
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace {
const QString kPrefix = "RcloneBrowserNG_";

#if defined(Q_OS_LINUX)
bool hasSystemd() {
  return QFile::exists("/run/systemd/system") ||
         QFile::exists("/sys/fs/cgroup/systemd");
}

QString systemdUserDir() {
  QString dir = QDir::homePath() + "/.config/systemd/user";
  QDir().mkpath(dir);
  return dir;
}
#endif

QString xmlEscape(const QString &s) {
  QString out = s;
  out.replace('&', "&amp;");
  out.replace('<', "&lt;");
  out.replace('>', "&gt;");
  out.replace('"', "&quot;");
  return out;
}
}

QString ScheduleManager::appPath() {
  return QDir::toNativeSeparators(qApp->applicationFilePath());
}

QString ScheduleManager::scheduleTaskName(const QString &taskName) {
  QString safe;
  safe.reserve(taskName.size());
  for (const QChar &ch : taskName) {
    if (ch.isLetterOrNumber() || ch == '-' || ch == '_' || ch == '.' ||
        ch == ' ')
      safe.append(ch);
    else
      safe.append('_');
  }
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

QString ScheduleManager::generateWindowsTaskXml(
    const QString &sysName, const QString &appPath, const QString &taskName,
    const QString &interval, const QString &time) {
  QString triggerXml;
  if (interval == "hourly") {
    triggerXml = "      <CalendarTrigger>\n"
                 "        <Repetition>\n"
                 "          <Interval>PT1H</Interval>\n"
                 "        </Repetition>\n"
                 "        <StartBoundary>2020-01-01T00:00:00</StartBoundary>\n"
                 "      </CalendarTrigger>\n";
  } else if (interval == "weekly") {
    QString st = time.isEmpty() ? "00:00" : time;
    triggerXml = QString(
        "      <CalendarTrigger>\n"
        "        <StartBoundary>2020-01-01T%1:00</StartBoundary>\n"
        "        <ScheduleByWeek>\n"
        "          <WeeksInterval>1</WeeksInterval>\n"
        "          <DaysOfWeek><Sunday /></DaysOfWeek>\n"
        "        </ScheduleByWeek>\n"
        "      </CalendarTrigger>\n").arg(st);
  } else if (interval.endsWith("m")) {
    int minutes = interval.left(interval.length() - 1).toInt();
    if (minutes < 1) minutes = 1;
    triggerXml = QString(
        "      <CalendarTrigger>\n"
        "        <Repetition>\n"
        "          <Interval>PT%1M</Interval>\n"
        "        </Repetition>\n"
        "        <StartBoundary>2020-01-01T00:00:00</StartBoundary>\n"
        "      </CalendarTrigger>\n").arg(minutes);
  } else if (interval.endsWith("h")) {
    int hours = interval.left(interval.length() - 1).toInt();
    if (hours < 1) hours = 1;
    triggerXml = QString(
        "      <CalendarTrigger>\n"
        "        <Repetition>\n"
        "          <Interval>PT%1H</Interval>\n"
        "        </Repetition>\n"
        "        <StartBoundary>2020-01-01T00:00:00</StartBoundary>\n"
        "      </CalendarTrigger>\n").arg(hours);
  } else {
    QString st = time.isEmpty() ? "00:00" : time;
    triggerXml = QString(
        "      <CalendarTrigger>\n"
        "        <StartBoundary>2020-01-01T%1:00</StartBoundary>\n"
        "        <ScheduleByDay>\n"
        "          <DaysInterval>1</DaysInterval>\n"
        "        </ScheduleByDay>\n"
        "      </CalendarTrigger>\n").arg(st);
  }

  return QString(
      "<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
      "<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
      "  <RegistrationInfo>\n"
      "    <Description>RcloneBrowserNG task: %1</Description>\n"
      "  </RegistrationInfo>\n"
      "  <Triggers>\n"
      "%2"
      "  </Triggers>\n"
      "  <Settings>\n"
      "    <StartWhenAvailable>true</StartWhenAvailable>\n"
      "    <ExecutionTimeLimit>PT72H</ExecutionTimeLimit>\n"
      "    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
      "    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
      "    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
      "  </Settings>\n"
      "  <Actions Context=\"Author\">\n"
      "    <Exec>\n"
      "      <Command>%3</Command>\n"
      "      <Arguments>--run-task \"%4\"</Arguments>\n"
      "    </Exec>\n"
      "  </Actions>\n"
      "</Task>\n")
          .arg(xmlEscape(sysName), triggerXml, xmlEscape(appPath),
               xmlEscape(taskName));
}

QString ScheduleManager::generateSystemdService(const QString &taskName,
                                                const QString &appPath) {
  return QString(
      "[Unit]\nDescription=RcloneBrowserNG task: %1\n\n"
      "[Service]\nType=oneshot\nExecStart=%2 --run-task \"%1\"\n")
          .arg(taskName, appPath);
}

QString ScheduleManager::generateSystemdTimer(const QString &taskName,
                                              const QString &interval,
                                              const QString &time) {
  QString onCalendar;
  if (interval == "hourly") {
    onCalendar = "*-*-* *:00:00";
  } else if (interval == "weekly") {
    onCalendar = QString("Sun *-*-* %1:00:00")
                     .arg(time.isEmpty() ? "00" : time.left(2));
  } else if (interval == "daily") {
    onCalendar = QString("*-*-* %1:00:00")
                     .arg(time.isEmpty() ? "00" : time.left(2));
  } else if (interval.endsWith("m")) {
    onCalendar = QString("*-*-* *:%1/%2:00")
                     .arg("00", interval.left(interval.length() - 1));
  } else if (interval.endsWith("h")) {
    onCalendar = QString("*-*-* 00/%1:00:00")
                     .arg(interval.left(interval.length() - 1));
  } else {
    onCalendar = "*-*-* 00:00:00";
  }

  return QString(
      "[Unit]\nDescription=RcloneBrowserNG timer: %1\n\n"
      "[Timer]\nOnCalendar=%2\nPersistent=true\n\n"
      "[Install]\nWantedBy=timers.target\n")
          .arg(taskName, onCalendar);
}

QString ScheduleManager::generateMacPlist(const QString &sysName,
                                          const QString &appPath,
                                          const QString &taskName,
                                          const QString &interval,
                                          const QString &time) {
  int hour = 0;
  if (!time.isEmpty() && time.contains(':'))
    hour = time.left(time.indexOf(':')).toInt();

  QString scheduleKey;
  if (interval == "daily") {
    scheduleKey = QString(
        "  <key>StartCalendarInterval</key>\n"
        "  <dict>\n"
        "    <key>Hour</key><integer>%1</integer>\n"
        "    <key>Minute</key><integer>0</integer>\n"
        "  </dict>\n").arg(hour);
  } else if (interval == "weekly") {
    scheduleKey = QString(
        "  <key>StartCalendarInterval</key>\n"
        "  <dict>\n"
        "    <key>Weekday</key><integer>0</integer>\n"
        "    <key>Hour</key><integer>%1</integer>\n"
        "    <key>Minute</key><integer>0</integer>\n"
        "  </dict>\n").arg(hour);
  } else {
    int intervalSec = 86400;
    if (interval == "hourly") intervalSec = 3600;
    else if (interval.endsWith("m"))
      intervalSec = interval.left(interval.length() - 1).toInt() * 60;
    else if (interval.endsWith("h"))
      intervalSec = interval.left(interval.length() - 1).toInt() * 3600;
    scheduleKey = QString(
        "  <key>StartInterval</key><integer>%1</integer>\n").arg(intervalSec);
  }

  return QString(
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
      "%4"
      "  <key>RunAtLoad</key><false/>\n"
      "</dict>\n</plist>\n")
          .arg(xmlEscape(sysName), xmlEscape(appPath),
               xmlEscape(taskName), scheduleKey);
}

bool ScheduleManager::installSchedule(const QString &taskName,
                                       const QString &interval,
                                       const QString &time, QString *error) {
  const QString sysName = scheduleTaskName(taskName);
  const QString app = appPath();

#if defined(Q_OS_WIN32)
  QString xml = generateWindowsTaskXml(sysName, app, taskName, interval, time);

  QString tempPath = QDir::tempPath() + "/" + sysName + ".xml";
  QFile xmlFile(tempPath);
  if (!xmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error) *error = "Cannot write temporary XML: " + tempPath;
    return false;
  }
  xmlFile.write(xml.toUtf8());
  xmlFile.close();

  QProcess proc;
  proc.start("schtasks.exe",
             QStringList() << "/Create" << "/F" << "/TN" << sysName
                           << "/XML" << tempPath);
  if (!proc.waitForFinished(15000)) {
    QFile::remove(tempPath);
    if (error) *error = "schtasks.exe timed out";
    return false;
  }
  QFile::remove(tempPath);
  if (proc.exitCode() != 0) {
    if (error)
      *error = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
    return false;
  }
  return true;

#elif defined(Q_OS_LINUX)
  if (hasSystemd()) {
    QString dir = systemdUserDir();
    QString servicePath = dir + "/" + sysName + ".service";
    QString timerPath = dir + "/" + sysName + ".timer";

    QString service = generateSystemdService(taskName, app);
    QString timer = generateSystemdTimer(taskName, interval, time);

    QFile sf(servicePath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
      if (error) *error = "Cannot write " + servicePath;
      return false;
    }
    sf.write(service.toUtf8());
    sf.close();

    QFile tf(timerPath);
    if (!tf.open(QIODevice::WriteOnly | QIODevice::Text)) {
      if (error) *error = "Cannot write " + timerPath;
      return false;
    }
    tf.write(timer.toUtf8());
    tf.close();

    QProcess reload;
    reload.start("systemctl", QStringList() << "--user" << "daemon-reload");
    reload.waitForFinished(5000);

    QProcess enable;
    enable.start("systemctl",
                 QStringList() << "--user" << "enable" << "--now"
                               << sysName + ".timer");
    enable.waitForFinished(5000);
    if (enable.exitCode() != 0) {
      if (error)
        *error = QString::fromUtf8(enable.readAllStandardError()).trimmed();
      return false;
    }
    return true;
  }

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

  QString safeTaskName = taskName;
  safeTaskName.replace('\'', "'\\''");
  filtered << QString("%1 '%2' --run-task '%3' %4")
                  .arg(cronExpr, app, safeTaskName, marker);

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

  QString plist = generateMacPlist(sysName, app, taskName, interval, time);

  QFile file(plistPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error) *error = "Cannot write " + plistPath;
    return false;
  }
  file.write(plist.toUtf8());
  file.close();

  QString uid = QString::number(getuid());
  QString domain = "gui/" + uid;
  QProcess load;
  load.start("launchctl",
             QStringList() << "bootstrap" << domain << plistPath);
  load.waitForFinished(5000);
  if (load.exitCode() != 0) {
    QProcess fallback;
    fallback.start("launchctl", QStringList() << "load" << plistPath);
    fallback.waitForFinished(5000);
    if (fallback.exitCode() != 0) {
      if (error) *error = QString::fromUtf8(fallback.readAllStandardError()).trimmed();
      return false;
    }
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
  if (hasSystemd()) {
    QString dir = systemdUserDir();
    QProcess disable;
    disable.start("systemctl",
                  QStringList() << "--user" << "disable" << "--now"
                                << sysName + ".timer");
    disable.waitForFinished(5000);

    QFile::remove(dir + "/" + sysName + ".timer");
    QFile::remove(dir + "/" + sysName + ".service");

    QProcess reload;
    reload.start("systemctl", QStringList() << "--user" << "daemon-reload");
    reload.waitForFinished(5000);
    return true;
  }

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
  QString uid = QString::number(getuid());
  QString serviceTarget = "gui/" + uid + "/" + sysName;
  QProcess unload;
  unload.start("launchctl",
               QStringList() << "bootout" << serviceTarget);
  unload.waitForFinished(5000);
  if (unload.exitCode() != 0) {
    QProcess fallback;
    fallback.start("launchctl", QStringList() << "unload" << plistPath);
    fallback.waitForFinished(5000);
  }
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
  auto parseCsvLine = [](const QString &line) -> QStringList {
    QStringList fields;
    QString field;
    bool inQuote = false;
    for (int i = 0; i < line.size(); ++i) {
      QChar c = line[i];
      if (inQuote) {
        if (c == '"') {
          if (i + 1 < line.size() && line[i + 1] == '"') {
            field += '"';
            ++i;
          } else {
            inQuote = false;
          }
        } else {
          field += c;
        }
      } else if (c == '"') {
        inQuote = true;
      } else if (c == ',') {
        fields << field.trimmed();
        field.clear();
      } else {
        field += c;
      }
    }
    fields << field.trimmed();
    return fields;
  };

  QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
  for (const QString &line : output.split('\n')) {
    if (!line.contains(kPrefix))
      continue;
    QStringList cols = parseCsvLine(line);
    if (cols.isEmpty())
      continue;
    QString name = cols[0];
    if (name.startsWith('\\'))
      name = name.mid(1);
    if (!name.startsWith(kPrefix))
      continue;
    ScheduleEntry entry;
    entry.taskName = name.mid(kPrefix.length());
    if (cols.size() > 2) {
      entry.time = cols[1];
      entry.interval = cols[2];
    }
    if (cols.size() > 3)
      entry.enabled = (cols[3] != "Disabled");
    result.append(entry);
  }

#elif defined(Q_OS_LINUX)
  if (hasSystemd()) {
    QDir dir(systemdUserDir());
    for (const auto &fi : dir.entryInfoList(
             QStringList() << kPrefix + "*.timer", QDir::Files)) {
      ScheduleEntry entry;
      entry.taskName = fi.baseName().mid(kPrefix.length());
      QProcess status;
      status.start("systemctl",
                   QStringList() << "--user" << "is-enabled" << fi.fileName());
      status.waitForFinished(3000);
      entry.enabled =
          QString::fromUtf8(status.readAllStandardOutput()).trimmed() ==
          "enabled";
      result.append(entry);
    }
  } else {
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
