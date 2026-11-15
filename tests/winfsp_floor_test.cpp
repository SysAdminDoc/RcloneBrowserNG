#include "winfsp_floor.h"

#include <QDate>
#include <QTest>

// The WinFsp check used to warn only at 2.1.25156 and older and recommend
// 2026 Beta1, which lacks CVE-2026-7162 and six later fixes. Pin the floor
// against each published build, the shape of the warning and where it sends
// people.
class WinFspFloorTest : public QObject {
  Q_OBJECT

private slots:
  void warnsBelowTheFloor_data() {
    QTest::addColumn<QString>("version");
    QTest::addColumn<bool>("warns");
    // GetFileVersionInfo reports four parts, so the rows use that form.
    QTest::newRow("2.0 (WinFsp 2023)") << "2.0.23075.0" << true;
    // WinFsp 2025, the latest stable. None of the 2026 fixes.
    QTest::newRow("2.1.25156 stable") << "2.1.25156.0" << true;
    // 2026 Beta1: CVE-2026-3006 only.
    QTest::newRow("2.2.26112 Beta1") << "2.2.26112.0" << true;
    // 2026 Beta2: adds CVE-2026-7162.
    QTest::newRow("2.2.26183 Beta2") << "2.2.26183.0" << true;
    QTest::newRow("2.2.26194 Beta3") << "2.2.26194.0" << true;
    // 2026 Beta4: two more fixes, four still missing.
    QTest::newRow("2.2.26215 Beta4") << "2.2.26215.0" << true;
    // 2026 Beta5: every published fix.
    QTest::newRow("2.2.26286 Beta5") << "2.2.26286.0" << false;
    QTest::newRow("three parts") << "2.2.26286" << false;
    QTest::newRow("later build") << "2.2.26300.0" << false;
    QTest::newRow("next release") << "2.3.0.0" << false;
  }

  void warnsBelowTheFloor() {
    QFETCH(QString, version);
    QFETCH(bool, warns);
    QCOMPARE(WinFspFloor::IsBelowFloor(version), warns);
  }

  void staysQuietWhenTheVersionIsUnknown() {
    // windowsFileVersion returns an empty string when the DLL has no
    // version resource.
    QVERIFY(!WinFspFloor::IsBelowFloor(QString()));
    QVERIFY(!WinFspFloor::IsBelowFloor("  "));
    QVERIFY(!WinFspFloor::IsBelowFloor("unknown"));
    // A zeroed version resource reads as all zeros. That's not a build.
    QVERIFY(!WinFspFloor::IsBelowFloor("0.0.0.0"));
    QVERIFY(!WinFspFloor::IsBelowFloor("0"));
    // A real build with zero parts still counts.
    QVERIFY(WinFspFloor::IsBelowFloor("0.0.0.1"));
  }

  void warningNamesTheFixesAndTheBuild() {
    const QString text = WinFspFloor::Warning("2.2.26112.0");
    QVERIFY(text.contains("2.2.26112.0"));
    QVERIFY(text.contains("CVE-2026-3006"));
    QVERIFY(text.contains("CVE-2026-7162"));
    QVERIFY(text.contains("six more"));
    QVERIFY(text.contains(WinFspFloor::kMinimumVersion));
    QVERIFY(text.contains("2026 Beta5"));
    // The fixed builds are only on GitHub. winfsp.dev/rel offers the stable
    // 2.1.25156, the build being warned about, and GitHub's release list
    // marks that one "Latest", so the link is Beta5's own page.
    QVERIFY(text.contains(WinFspFloor::kDownloadUrl));
    QCOMPARE(QString(WinFspFloor::kDownloadUrl),
             QString("https://github.com/winfsp/winfsp/releases/tag/v2.2B5"));
    // The link follows the recommendation: "2026 Beta5" is tag v2.2B5, so a
    // floor raised to Beta6 with the old link left in fails here.
    const QString release(WinFspFloor::kRecommendedRelease);
    const qsizetype beta = release.indexOf("Beta");
    if (beta >= 0) {
      QVERIFY2(QString(WinFspFloor::kDownloadUrl)
                   .endsWith("B" + release.mid(beta + 4)),
               WinFspFloor::kDownloadUrl);
    }
    QVERIFY(!text.contains("winfsp.dev/rel"));
    // The old text sent people to Beta1, which is still below the floor.
    QVERIFY(!text.contains("Beta1 or newer"));
  }

  void installAdviceSkipsTheStableBuild() {
    // The "WinFsp required" box used to suggest winget install
    // WinFsp.WinFsp, which installs 2.1.25156, right below the floor.
    const QString text = WinFspFloor::InstallAdvice();
    QVERIFY(text.contains("2026 Beta5"));
    QVERIFY(text.contains(WinFspFloor::kDownloadUrl));
    QVERIFY(!text.contains("winget install"));
    QVERIFY(!text.contains("winfsp.dev/rel"));
  }

  void theRecordedInstallFolderIsCheckedFirst() {
    // WinFsp installed somewhere else used to count as not installed.
    const QStringList custom =
        WinFspFloor::DllCandidates("D:\\Tools\\WinFsp\\");
    QCOMPARE(custom.first(), QString("D:/Tools/WinFsp/bin/winfsp-x64.dll"));
    QCOMPARE(custom.size(), 4);
    QVERIFY(custom.contains("C:/Program Files (x86)/WinFsp/bin/winfsp-x64.dll"));

    const QStringList defaults = WinFspFloor::DllCandidates(QString());
    QCOMPARE(defaults,
             (QStringList{"C:/Program Files (x86)/WinFsp/bin/winfsp-x64.dll",
                          "C:/Program Files/WinFsp/bin/winfsp-x64.dll",
                          "C:/Windows/System32/winfsp-x64.dll"}));
    QCOMPARE(WinFspFloor::DllCandidates("C:\\Program Files (x86)\\WinFsp\\"),
             defaults);
    QCOMPARE(WinFspFloor::DllCandidates("c:\\program files (x86)\\winfsp\\")
                 .size(),
             3);
  }

  void theInstallFolderComesFromTheRegistry() {
#if defined(Q_OS_WIN32)
    // Decide whether WinFsp is installed without the reader under test, so
    // a read that comes back empty fails instead of skipping. The installer
    // always records its folder.
    if (!QFileInfo::exists(
            "C:/Program Files (x86)/WinFsp/bin/winfsp-x64.dll")) {
      QSKIP("WinFsp isn't installed in its default folder on this computer");
    }
    const QString dir = WinFspFloor::InstallDirFromRegistry();
    QVERIFY2(!dir.isEmpty(), "WinFsp is installed but its folder wasn't read");
    QVERIFY2(QFileInfo(dir).isDir(), qPrintable(dir));
    QVERIFY2(QFileInfo::exists(WinFspFloor::DllCandidates(dir).first()),
             qPrintable(dir));
#else
    QSKIP("WinFsp is Windows only");
#endif
  }

  void reviewDateIsParseableAndNotInTheFuture() {
    // scripts/release_check.py ages this date out. A typo there would
    // either never expire or block every release.
    const QDate reviewed =
        QDate::fromString(WinFspFloor::kReviewedDate, Qt::ISODate);
    QVERIFY2(reviewed.isValid(), WinFspFloor::kReviewedDate);
    QVERIFY(reviewed <= QDate::currentDate());
    QVERIFY(WinFspFloor::kReviewIntervalDays > 0);
  }

  void raisingTheFloorWarnsAgain() {
    // Someone warned about 2.1.25156 under the old 2.1.25156 floor was told
    // to get Beta1. The key has to differ from the bare version they were
    // keyed on before, or they never hear about Beta5.
    QVERIFY(WinFspFloor::WarnedKey("2.1.25156.0") != "2.1.25156.0");
    QVERIFY(WinFspFloor::WarnedKey("2.1.25156.0")
                .contains(WinFspFloor::kMinimumVersion));
    QCOMPARE(WinFspFloor::WarnedKey(" 2.1.25156.0 "),
             WinFspFloor::WarnedKey("2.1.25156.0"));
  }
};

QTEST_APPLESS_MAIN(WinFspFloorTest)
#include "winfsp_floor_test.moc"
