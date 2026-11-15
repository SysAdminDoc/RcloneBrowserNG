#include "schedule_manager.h"

#include <QTest>

class ScheduleGoldenTest : public QObject {
  Q_OBJECT
private slots:
  void taskNameSanitization() {
    QString safe = ScheduleManager::scheduleTaskName("My Task <>&\"");
    QVERIFY(safe.startsWith("RcloneBrowserNG_"));
    QVERIFY(!safe.contains("<"));
    QVERIFY(!safe.contains(">"));
    QVERIFY(!safe.contains("&"));
  }

  void windowsDailyXml() {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", "/usr/bin/rclone-browser", "Nightly Backup",
        "daily", "02:00");
    QVERIFY(xml.contains("<StartWhenAvailable>true</StartWhenAvailable>"));
    QVERIFY(xml.contains("02:00"));
    QVERIFY(xml.contains("<DaysInterval>1</DaysInterval>"));
    QVERIFY(xml.contains("--run-task"));
    QVERIFY(xml.contains("Nightly Backup"));
  }

  void windowsHourlyXml() {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", "/usr/bin/rclone-browser", "Nightly Backup",
        "hourly", "");
    QVERIFY(xml.contains("<StartWhenAvailable>true</StartWhenAvailable>"));
    QVERIFY(xml.contains("PT1H"));
  }

  void windowsWeeklyXml() {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", "/usr/bin/rclone-browser", "Nightly Backup",
        "weekly", "03:00");
    QVERIFY(xml.contains("<StartWhenAvailable>true</StartWhenAvailable>"));
    QVERIFY(xml.contains("<WeeksInterval>1</WeeksInterval>"));
    QVERIFY(xml.contains("03:00"));
  }

  void windows15mXml() {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", "/usr/bin/rclone-browser", "Nightly Backup",
        "15m", "");
    QVERIFY(xml.contains("PT15M"));
    QVERIFY(xml.contains("<StartWhenAvailable>true</StartWhenAvailable>"));
  }

  void windows2hXml() {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", "/usr/bin/rclone-browser", "Nightly Backup",
        "2h", "");
    QVERIFY(xml.contains("PT2H"));
  }

  void windowsXmlEscaping() {
    QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", "C:\\Program Files\\app.exe",
        "Task <with> &special \"chars\"", "daily", "");
    QVERIFY(xml.contains("&amp;special"));
    QVERIFY(xml.contains("&lt;with&gt;"));
    // The task name is also quoted for the command line, so its own
    // quotes carry a backslash.
    QVERIFY(xml.contains("\\&quot;chars\\&quot;"));
  }

  void systemdService() {
    QString svc = ScheduleManager::generateSystemdService("Nightly Backup",
                                                           "/usr/bin/rclone-browser");
    QVERIFY(svc.contains("Type=oneshot"));
    QVERIFY(svc.contains("--run-task"));
    QVERIFY(svc.contains("Nightly Backup"));
  }

  void systemdDailyTimer() {
    QString tmr = ScheduleManager::generateSystemdTimer("Nightly Backup",
                                                         "daily", "02");
    QVERIFY(tmr.contains("Persistent=true"));
    QVERIFY(tmr.contains("*-*-* 02:00:00"));
    QVERIFY(tmr.contains("WantedBy=timers.target"));
  }

  void systemdHourlyTimer() {
    QString tmr = ScheduleManager::generateSystemdTimer("Nightly Backup",
                                                         "hourly", "");
    QVERIFY(tmr.contains("*-*-* *:00:00"));
    QVERIFY(tmr.contains("Persistent=true"));
  }

  void systemdWeeklyTimer() {
    QString tmr = ScheduleManager::generateSystemdTimer("Nightly Backup",
                                                         "weekly", "03");
    QVERIFY(tmr.contains("Sun *-*-* 03:00:00"));
  }

  void systemd15mTimer() {
    QString tmr = ScheduleManager::generateSystemdTimer("Nightly Backup",
                                                         "15m", "");
    QVERIFY(tmr.contains("*-*-* *:00/15:00"));
  }

  void macOsDailyPlist() {
    QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", "/usr/bin/rclone-browser", "Nightly Backup",
        "daily", "02:00");
    QVERIFY(plist.contains("<key>Hour</key><integer>2</integer>"));
    QVERIFY(plist.contains("StartCalendarInterval"));
    QVERIFY(plist.contains("--run-task"));
    QVERIFY(!plist.contains("StartInterval"));
  }

  void macOsWeeklyPlist() {
    QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", "/usr/bin/rclone-browser", "Nightly Backup",
        "weekly", "04:00");
    QVERIFY(plist.contains("<key>Weekday</key><integer>0</integer>"));
    QVERIFY(plist.contains("<key>Hour</key><integer>4</integer>"));
  }

  void macOsHourlyPlist() {
    QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", "/usr/bin/rclone-browser", "Nightly Backup",
        "hourly", "");
    QVERIFY(plist.contains("<key>StartInterval</key><integer>3600</integer>"));
    QVERIFY(!plist.contains("StartCalendarInterval"));
  }

  void macOs15mPlist() {
    QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", "/usr/bin/rclone-browser", "Nightly Backup",
        "15m", "");
    QVERIFY(plist.contains("<key>StartInterval</key><integer>900</integer>"));
  }

  void macOsXmlEscaping() {
    QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", "/usr/bin/rclone-browser",
        "Task <with> &chars", "daily", "");
    QVERIFY(plist.contains("&amp;chars"));
    QVERIFY(plist.contains("&lt;with&gt;"));
  }

  // Run conditions travel on the command line, after the task name, in each
  // scheduler's own format.
  void windowsXmlCarriesRunConditions() {
    const QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", "C:\\Apps\\RcloneBrowser.exe", "Nightly Backup",
        "daily", "02:00", {"--require-ac", "--require-unmetered"});
    QVERIFY(xml.contains("<Arguments>--run-task &quot;Nightly Backup&quot; "
                         "--require-ac --require-unmetered</Arguments>"));
    // The app logs a skipped run itself, so Task Scheduler must still start
    // it on battery.
    QVERIFY(xml.contains(
        "<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"));
  }

  void windowsXmlWithoutRunConditions() {
    const QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", "C:\\Apps\\RcloneBrowser.exe", "Nightly Backup",
        "daily", "02:00");
    QVERIFY(xml.contains(
        "<Arguments>--run-task &quot;Nightly Backup&quot;</Arguments>"));
  }

  // CommandLineToArgvW reads a backslash before a quote as an escape, so a
  // name ending in one used to swallow the closing quote.
  void windowsArgumentQuoting_data() {
    QTest::addColumn<QString>("taskName");
    QTest::addColumn<QString>("arguments");
    QTest::newRow("plain") << "Nightly" << "--run-task &quot;Nightly&quot;";
    QTest::newRow("quote") << "The \"big\" one"
                           << "--run-task &quot;The \\&quot;big\\&quot; "
                              "one&quot;";
    QTest::newRow("trailing backslash")
        << "D:\\Backups\\" << "--run-task &quot;D:\\Backups\\\\&quot;";
    QTest::newRow("inner backslash")
        << "a\\b" << "--run-task &quot;a\\b&quot;";
  }

  void windowsArgumentQuoting() {
    QFETCH(QString, taskName);
    QFETCH(QString, arguments);
    const QString xml = ScheduleManager::generateWindowsTaskXml(
        "RcloneBrowserNG_test", "C:\\Apps\\RcloneBrowser.exe", taskName,
        "daily", "");
    QVERIFY2(xml.contains("<Arguments>" + arguments + "</Arguments>"),
             qPrintable(xml));
  }

  void systemdServiceCarriesRunConditions() {
    const QString svc = ScheduleManager::generateSystemdService(
        "Nightly Backup", "/usr/bin/rclone-browser", {"--require-ac"});
    QVERIFY(svc.contains("ExecStart=\"/usr/bin/rclone-browser\" --run-task "
                         "\"Nightly Backup\" --require-ac\n"));
  }

  // systemd expands %h and friends inside ExecStart, and a quote would end
  // the argument early.
  void systemdServiceEscapesSpecifiersAndQuotes() {
    const QString svc = ScheduleManager::generateSystemdService(
        "50% \"sync\"", "/home/me/My Apps/rclone-browser");
    QVERIFY(svc.contains("ExecStart=\"/home/me/My Apps/rclone-browser\" "
                         "--run-task \"50%% \\\"sync\\\"\"\n"));
    QVERIFY(svc.contains("Description=RcloneBrowserNG task: 50%% \"sync\""));
  }

  void macOsPlistCarriesRunConditions() {
    const QString plist = ScheduleManager::generateMacPlist(
        "RcloneBrowserNG_test", "/Applications/Rclone Browser NG.app",
        "Nightly Backup", "daily", "02:00", {"--require-unmetered"});
    QVERIFY(plist.contains("    <string>--run-task</string>\n"
                           "    <string>Nightly Backup</string>\n"
                           "    <string>--require-unmetered</string>\n"
                           "  </array>\n"));
  }

  void cronLineCarriesRunConditions() {
    const QString line = ScheduleManager::generateCronLine(
        "RcloneBrowserNG_Nightly Backup", "/usr/bin/rclone-browser",
        "Nightly Backup", "daily", "02:00", {"--require-ac"});
    QCOMPARE(line, QString("0 02 * * * '/usr/bin/rclone-browser' --run-task "
                           "'Nightly Backup' --require-ac "
                           "# RcloneBrowserNG_Nightly Backup"));
  }

  // cron turns a bare % into a line break, and a quote in the name has to
  // leave and re-enter the shell's single quotes.
  void cronLineEscapesPercentAndQuotes() {
    const QString line = ScheduleManager::generateCronLine(
        "RcloneBrowserNG_50_ Bob_s", "/opt/it's/rclone-browser", "50% Bob's",
        "hourly", "");
    QCOMPARE(line, QString("0 * * * * '/opt/it'\\''s/rclone-browser' "
                           "--run-task '50\\% Bob'\\''s' "
                           "# RcloneBrowserNG_50_ Bob_s"));
  }

  // The listing used to cut the line at a double quote the line never has,
  // so the whole crontab line showed up as the interval.
  void cronLineReadsBack() {
    const QString line = ScheduleManager::generateCronLine(
        "RcloneBrowserNG_Nightly Backup", "/usr/bin/rclone-browser",
        "Nightly Backup", "0 2 * * 1-5", "", {"--require-ac"});
    const ScheduleEntry entry = ScheduleManager::parseCronLine(line);
    QCOMPARE(entry.taskName, QString("Nightly Backup"));
    QCOMPARE(entry.interval, QString("0 2 * * 1-5"));
    QVERIFY(ScheduleManager::parseCronLine("0 * * * * /usr/bin/backup")
                .taskName.isEmpty());
  }

  void cronValidExpressions() {
    QVERIFY(ScheduleManager::isValidCronExpr("0 2 * * *"));
    QVERIFY(ScheduleManager::isValidCronExpr("*/15 * * * *"));
    QVERIFY(ScheduleManager::isValidCronExpr("0 0 * * 0"));
    QVERIFY(ScheduleManager::isValidCronExpr("30 14 1-5 * 1-5"));
  }

  void cronInvalidExpressions() {
    QVERIFY(!ScheduleManager::isValidCronExpr("60 * * * *"));
    QVERIFY(!ScheduleManager::isValidCronExpr("* 25 * * *"));
    QVERIFY(!ScheduleManager::isValidCronExpr("* * 32 * *"));
    QVERIFY(!ScheduleManager::isValidCronExpr("* * * 13 *"));
    QVERIFY(!ScheduleManager::isValidCronExpr("* * * * 8"));
    QVERIFY(!ScheduleManager::isValidCronExpr("not a cron"));
    QVERIFY(!ScheduleManager::isValidCronExpr("* * *"));
  }

  void cronNextRuns() {
    auto runs = ScheduleManager::nextCronRuns("0 * * * *", 3);
    QCOMPARE(runs.size(), 3);
  }

  void cronDstNoDuplicateRuns() {
    const QTimeZone zone("America/New_York");
    QVERIFY2(zone.isValid(), "America/New_York timezone data is required");
    const QDateTime from(QDate(2026, 11, 1), QTime(0, 58), zone);
    auto runs = ScheduleManager::nextCronRuns("30 1 * * *", 2, from);
    QCOMPARE(runs.size(), 2);
    QCOMPARE(runs.at(0).date(), QDate(2026, 11, 1));
    QCOMPARE(runs.at(0).time().hour(), 1);
    QCOMPARE(runs.at(0).time().minute(), 30);
    QCOMPARE(runs.at(1).date(), QDate(2026, 11, 2));
    QCOMPARE(runs.at(1).time().hour(), 1);
    QCOMPARE(runs.at(1).time().minute(), 30);
  }

  void cronInvalidReturnsEmpty() {
    auto runs = ScheduleManager::nextCronRuns("60 * * * *", 5);
    QVERIFY(runs.isEmpty());
  }

  void scheduleListingCallbackIsAsync() {
    bool called = false;
    QThread *callbackThread = nullptr;
    QEventLoop loop;
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);

    ScheduleManager::listSchedulesAsync(
        this, [&](const QList<ScheduleEntry> &, const QString &) {
          called = true;
          callbackThread = QThread::currentThread();
          loop.quit();
        });
    loop.exec();

    QVERIFY(called);
    QCOMPARE(callbackThread, QThread::currentThread());
  }
};

QTEST_MAIN(ScheduleGoldenTest)
#include "schedule_golden_test.moc"
