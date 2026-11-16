#include "bwlimit_timetable.h"
#include "test_rclone.h"

#include <QProcess>
#include <QTest>

namespace {

// --bwlimit values rclone v1.76.0 accepts and refuses, each one run through
// "rclone --bwlimit <value> version" on 2026-11-15.
const QStringList kAccepted{
    "08:00,1M",        "23:59,1M",           "Mon-08:00,1M",
    "Monday-08:00,1M", "mon-08:00,1M",       "MON-08:00,1M",
    "08:00,10M:1M",    "08:00,off",          "08:00,1M 12:00,off",
    "10M",             "off",                "08:00",
    "08:00,1M  12:00,2M", "08:00,1M Mon-09:00,2M",
    "08:00,512k",      "08:00,512K",         "08:00,1.5M",
    "08:00,10Mi",      "08:00,10MiB",        "08:00,10m",
    "08:00,1G",        "08:00,1T",           "08:00,1P",
    "08:00,100",       "08:00,100b",         "08:00,100B",
    "08:00,0",         "08:00,OFF",          "08:00,Off",
    "08:00,off:1M",    "08:00,10M:off",      "08:00,1e3",
    "08:00,.5M",       "08:00,5.M",          "08:00,1M ",
    "08:00,1KiB",      "08:00,1Ei",
    // Semicolons separate entries too, and Go's ParseFloat takes _ and hex.
    "08:00,1M;12:00,off", "08:00,1M; 12:00,off", "08:00,1M;;12:00,2M",
    "08:00,1_000k",    "08:00,0x1p4k",       "08:00,7E",
    "08:00,8191P",     "08:00,7.99Ei",       "08:00,7.9994E",
    // Go lowercases the Turkish capital dotted I to a plain i.
    QString::fromUtf8("FR\xC4\xB0-08:00,1M"),
    QString::fromUtf8("fR\xC4\xB0" "day-08:00,1M"),
};

const QStringList kRefused{
    "0800,1M",         "+8:00,1M",           "8:00,1M",
    "08:0,1M",         "-1:00,1M",           "08:+5,1M",
    "24:00,1M",        "08:60,1M",           "Mo-08:00,1M",
    "Mon-0800,1M",     "08:00,1M 0900,2M",   "08:00,1M,2M",
    "08:00,bogus",     "Mon,1M",             "08:00 ,1M",
    QString::fromUtf8("\xEF\xBC\x90\xEF\xBC\x98:00,1M"),
    "08:00,10MB",      "08:00,-1",           "08:00, 1M",
    "08:00,10 M",      "08:00,1Mb",          "08:00,1k:",
    "08:00,:1k",       "08:00,1X",           "Mon-08-00,1M",
    // A tab doesn't separate entries, and a value with no space or comma
    // is one rate even with a semicolon in it.
    "08:00,1M\t12:00,off", "1M\t",           "1M;",
    "08:00;1M",
    // b isn't a multiplier for i or iB, and 8 EiB is past what rclone holds.
    "08:00,10bi",      "08:00,1bib",         "08:00,8E",
    "08:00,8192P",     "08:00,100000000000000000000",
    "08:00,nank",      "08:00,infk",         "08:00,1e400k",
    // rclone writes the limit out and reads it back, and from about
    // 7.9995E the text rounds to 8.000Ei, which it refuses.
    "08:00,7.9995E",   "7.9999E",            "1M:7.9999E",
    "08:00,8191.5P",   "9223372036854774784b",
};

} // namespace

class BwlimitTimetableTest : public QObject {
  Q_OBJECT

private slots:
  void acceptsWhatRcloneAccepts() {
    for (const QString &value : kAccepted) {
      const QString problem = BwlimitTimetable::Problem(value);
      QVERIFY2(problem.isEmpty(),
               qPrintable(QString("[%1] refused: %2").arg(value, problem)));
    }
    QVERIFY(BwlimitTimetable::Problem(QString()).isEmpty());
  }

  void refusesWhatRcloneRefuses() {
    for (const QString &value : kRefused) {
      QVERIFY2(!BwlimitTimetable::Problem(value).isEmpty(),
               qPrintable(QString("[%1] accepted").arg(value)));
    }
  }

  // The editor checks each cell on its own and names what to fix.
  void namesTheProblemInACell() {
    using BwlimitTimetable::RateProblem;
    using BwlimitTimetable::TimeProblem;
    QVERIFY(TimeProblem("08:00").isEmpty());
    QVERIFY(TimeProblem("Sun-23:59").isEmpty());
    QCOMPARE(TimeProblem("0800"), QString("Write the time as HH:MM, like 08:00."));
    QCOMPARE(TimeProblem("+08:00"), QString("Write the time as HH:MM, like 08:00."));
    QCOMPARE(TimeProblem("+8:00"), QString("Write the time as HH:MM, like 08:00."));
    QCOMPARE(TimeProblem("-08:00"),
             QString("\"\" isn't a day. Use Mon to Sun, or leave the day and "
                     "the dash off."));
    QCOMPARE(TimeProblem("24:00"), QString("The hour must be 00 to 23."));
    QCOMPARE(TimeProblem("08:60"), QString("The minutes must be 00 to 59."));
    QCOMPARE(TimeProblem("Mo-08:00"),
             QString("\"Mo\" isn't a day. Use Mon to Sun, or leave the day and "
                     "the dash off."));
    QCOMPARE(TimeProblem("Mon-08-00"),
             QString("Write the time as HH:MM, like 08:00."));
    QVERIFY(TimeProblem(QString::fromUtf8("FR\xC4\xB0-08:00")).isEmpty());

    // A single rate has no entry around it, so each message names the
    // value it's about.
    QVERIFY(RateProblem("off").isEmpty());
    QVERIFY(RateProblem("10M:1M").isEmpty());
    QCOMPARE(RateProblem(""),
             QString("A rate is missing. Use a size like 512k or 10M, or off."));
    QCOMPARE(RateProblem("-1"),
             QString("\"-1\" is negative. Use a size like 512k or 10M, or "
                     "off."));
    QVERIFY(!RateProblem("10MB").isEmpty());
    QCOMPARE(RateProblem("1:2:3"),
             QString("\"1:2:3\" has more than one colon. Use one rate, or "
                     "upload:download like 10M:1M."));
    QCOMPARE(RateProblem("1k:"),
             QString("\"1k:\" is missing a rate beside the colon. Use "
                     "upload:download like 10M:1M."));
    QCOMPARE(RateProblem("8E"),
             QString("\"8E\" is more than rclone can hold. Keep it at 7.999E "
                     "or less."));
    QCOMPARE(RateProblem("7.9999E"),
             QString("\"7.9999E\" is more than rclone can hold. Keep it at "
                     "7.999E or less."));
    QCOMPARE(RateProblem("10bi"),
             QString("\"10bi\" ends in a unit rclone doesn't know. Use k, M or "
                     "G, or off."));
    QCOMPARE(RateProblem("NaNk"),
             QString("\"NaNk\" isn't a size rclone can read. Use a size like "
                     "512k or 10M, or off."));
  }

  // The editor builds its rows from the same split rclone makes.
  void splitsEntriesTheWayRcloneDoes() {
    QCOMPARE(BwlimitTimetable::Entries("08:00,1M;12:00,off  Mon-09:00,2M;"),
             (QStringList{"08:00,1M", "12:00,off", "Mon-09:00,2M"}));
    QCOMPARE(BwlimitTimetable::Entries("08:00,1M\t12:00,off"),
             QStringList{"08:00,1M\t12:00,off"});
    QVERIFY(BwlimitTimetable::Entries(" ; ").isEmpty());
    QVERIFY(BwlimitTimetable::IsSingleRate("1M;"));
    QVERIFY(BwlimitTimetable::IsSingleRate(QString()));
    QVERIFY(!BwlimitTimetable::IsSingleRate("1M "));
    QVERIFY(!BwlimitTimetable::IsSingleRate("08:00,1M"));
  }

  // The tables above, run against the rclone on this machine, so a change
  // in a later rclone shows up here rather than as a job that won't start.
  void agreesWithTheInstalledRclone() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("no rclone; set RCLONE_BROWSER_TEST_RCLONE");
    }
    const auto accepted = [&rclone](const QString &value) {
      QProcess process;
      process.start(rclone, {"--bwlimit", value, "version"});
      if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        return false;
      }
      return process.exitStatus() == QProcess::NormalExit &&
             process.exitCode() == 0;
    };
    for (const QString &value : kAccepted) {
      QVERIFY2(accepted(value), qPrintable("rclone refused " + value));
    }
    for (const QString &value : kRefused) {
      QVERIFY2(!accepted(value), qPrintable("rclone accepted " + value));
    }
  }
};

QTEST_GUILESS_MAIN(BwlimitTimetableTest)
#include "bwlimit_timetable_test.moc"
