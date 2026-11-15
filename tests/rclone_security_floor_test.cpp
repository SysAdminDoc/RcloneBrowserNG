#include "rclone_security_floor.h"

#include <QDate>
#include <QTest>

// The floor used to be a bare "1.74.3" literal in main_window.cpp, three
// rclone releases behind and wrong on its own terms: 1.74.3 is affected by
// GHSA-fqj9-69pf-6pjg, which is patched in 1.74.4. Pin the current answer and
// the shape of the warning.
class RcloneSecurityFloorTest : public QObject {
  Q_OBJECT

private slots:
  void warnsBelowTheFloor_data() {
    QTest::addColumn<QString>("version");
    QTest::addColumn<bool>("warns");
    QTest::newRow("1.65.0") << "1.65.0" << true;
    QTest::newRow("1.73.5") << "1.73.5" << true;
    // The old floor. Affected by GHSA-fqj9-69pf-6pjg, patched in 1.74.4.
    QTest::newRow("1.74.3") << "1.74.3" << true;
    QTest::newRow("1.74.4") << "1.74.4" << true;
    // Patches GHSA-mfvx-7rcj-9m5g but not the 2026-09-04 batch.
    QTest::newRow("1.75.0") << "1.75.0" << true;
    // The floor from 2026-09-04 to 2026-11-15. Affected by 17 of the 18
    // advisories fixed in 1.75.2 and 1.76.0.
    QTest::newRow("1.75.1") << "1.75.1" << true;
    // Fixes the 2026-10-09 batch but not GHSA-f8p4-8g4j-q3m2 (rc requests
    // read before the login is checked), which reaches the app's own rcd.
    QTest::newRow("1.75.2") << "1.75.2" << true;
    QTest::newRow("1.76.0") << "1.76.0" << false;
    QTest::newRow("1.77.0") << "1.77.0" << false;
    QTest::newRow("2.0.0") << "2.0.0" << false;
    QTest::newRow("padded") << "1.76" << false;
    QTest::newRow("whitespace") << "  1.76.0  " << false;
  }

  void warnsBelowTheFloor() {
    QFETCH(QString, version);
    QFETCH(bool, warns);
    QCOMPARE(RcloneSecurityFloor::IsBelowFloor(version), warns);
  }

  void staysQuietWhenTheVersionIsUnknown() {
    // A failed `rclone version` probe must not be reported as an old rclone;
    // the user would go chasing an update they already have.
    QVERIFY(!RcloneSecurityFloor::IsBelowFloor(QString()));
    QVERIFY(!RcloneSecurityFloor::IsBelowFloor("   "));
  }

  void floorIsAtLeastTheNovember2026Batch() {
    QVERIFY2(!RcloneSecurityFloor::IsBelowFloor("1.76.0"),
             "the floor must not sit above a version that is already safe");
    QVERIFY2(RcloneSecurityFloor::IsBelowFloor("1.75.2"),
             "1.75.2 predates the 2026-11-06 advisories");
  }

  void reviewDateIsParseableAndNotInTheFuture() {
    const QDate reviewed =
        QDate::fromString(RcloneSecurityFloor::kReviewedDate, Qt::ISODate);
    QVERIFY2(reviewed.isValid(), RcloneSecurityFloor::kReviewedDate);
    QVERIFY(reviewed <= QDate::currentDate());
    QVERIFY(RcloneSecurityFloor::kReviewIntervalDays > 0);
  }

  void warningNamesTheAdvisoriesAndTheFloor() {
    // "security fixes are available" tells nobody what they are risking.
    const QString summary = RcloneSecurityFloor::AdvisorySummary();
    QVERIFY(summary.contains(RcloneSecurityFloor::kMinimumVersion));
    // serve webdav, serve s3, selfupdate and the rc daemon: the advisories
    // that reach paths the app starts itself.
    QVERIFY(summary.contains("GHSA-44pm-2q5p-fm63"));
    QVERIFY(summary.contains("GHSA-gj73-fh6v-92fj"));
    QVERIFY(summary.contains("GHSA-w3h7-jxxq-vfrp"));
    QVERIFY(summary.contains("GHSA-qjw8-8hmp-c3m6"));
    QVERIFY(summary.contains("GHSA-w33h-3qgq-8mv8"));
    QVERIFY(summary.contains("GHSA-f8p4-8g4j-q3m2"));
  }
};

QTEST_APPLESS_MAIN(RcloneSecurityFloorTest)
#include "rclone_security_floor_test.moc"
