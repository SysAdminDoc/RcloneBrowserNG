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
    QTest::newRow("as printed") << "rclone v1.76.0" << false;
    QTest::newRow("v prefix below") << "v1.75.2" << true;
    // A dev or beta build of the floor release may predate its fixes, the
    // same rule SelfUpdateIsSafe applies. compareVersion alone ranks
    // "1.76.0-beta.9200" above 1.76.0, which is why the suffix is handled.
    QTest::newRow("1.76.0-DEV") << "1.76.0-DEV" << true;
    QTest::newRow("1.76.0 beta") << "v1.76.0-beta.9200.0123abcd" << true;
    QTest::newRow("1.75.2-DEV") << "1.75.2-DEV" << true;
    QTest::newRow("1.77.0-DEV") << "1.77.0-DEV" << false;
    QTest::newRow("1.77.0 beta") << "v1.77.0-beta.9300.4567ef01" << false;
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
    // compareVersion reads a non-numeric string as 0, which used to make
    // these "below the floor".
    QVERIFY(!RcloneSecurityFloor::IsBelowFloor("unknown"));
    QVERIFY(!RcloneSecurityFloor::IsBelowFloor("rclone"));
  }

  void floorIsAtLeastTheNovember2026Batch() {
    QVERIFY2(!RcloneSecurityFloor::IsBelowFloor("1.76.0"),
             "the floor must not sit above a version that is already safe");
    QVERIFY2(RcloneSecurityFloor::IsBelowFloor("1.75.2"),
             "1.75.2 predates the 2026-11-06 advisories");
  }

  void offersSelfUpdateOnlyWhereTheUpdaterIsFixed_data() {
    QTest::addColumn<QString>("version");
    QTest::addColumn<bool>("safe");
    // GHSA-w33h-3qgq-8mv8 covers >=1.55.0, <=1.75.1.
    QTest::newRow("1.55.0") << "1.55.0" << false;
    QTest::newRow("1.75.1") << "1.75.1" << false;
    QTest::newRow("1.75.2") << "1.75.2" << true;
    QTest::newRow("1.76.0") << "1.76.0" << true;
    QTest::newRow("v prefix") << "v1.75.2" << true;
    QTest::newRow("as printed") << "rclone v1.75.1" << false;
    // A dev or beta build's number is the release it leads up to. Before
    // 1.76.0 shipped, master could still have the old updater.
    QTest::newRow("1.75.1-DEV") << "1.75.1-DEV" << false;
    QTest::newRow("1.75.2-DEV") << "1.75.2-DEV" << false;
    QTest::newRow("1.76.0-DEV") << "1.76.0-DEV" << false;
    QTest::newRow("1.76.0 beta") << "v1.76.0-beta.9200.0123abcd" << false;
    QTest::newRow("1.77.0-DEV") << "1.77.0-DEV" << true;
    QTest::newRow("1.77.0 beta") << "v1.77.0-beta.9300.4567ef01" << true;
    QTest::newRow("empty") << "" << false;
    QTest::newRow("unreadable") << "unknown" << false;
  }

  void offersSelfUpdateOnlyWhereTheUpdaterIsFixed() {
    QFETCH(QString, version);
    QFETCH(bool, safe);
    QCOMPARE(RcloneSecurityFloor::SelfUpdateIsSafe(version), safe);
  }

  void selfUpdateReasonNamesTheAdvisory() {
    QVERIFY(RcloneSecurityFloor::SelfUpdateUnsafeReason().contains(
        "GHSA-w33h-3qgq-8mv8"));
    QVERIFY(RcloneSecurityFloor::SelfUpdateUnsafeReason().contains(
        "rclone.org"));
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
    // A 1.75.2 user is below the floor but has every fix except the rc one.
    // The text has to say where the older advisories stop.
    QVERIFY(summary.contains("before 1.75.2"));
  }
};

QTEST_APPLESS_MAIN(RcloneSecurityFloorTest)
#include "rclone_security_floor_test.moc"
