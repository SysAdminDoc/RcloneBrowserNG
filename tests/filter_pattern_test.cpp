#include "filter_pattern.h"

#include <QTest>

using FilterPattern::Describe;
using FilterPattern::DescribeAll;

// Every claim below was checked against rclone v1.75.0 on 2026-09-05, with a
// tree holding notes.tmp, sub/notes.tmp, build/a.o, sub/build/b.o and
// logs/x.log:
//   --exclude "*.tmp"     removed notes.tmp AND sub/notes.tmp
//   --exclude "/notes.tmp" removed notes.tmp only
//   --exclude "build/**"  removed build/a.o AND sub/build/b.o
//   --exclude "/build/**" removed build/a.o only
//   --exclude "logs/"     removed logs/x.log
class FilterPatternTest : public QObject {
  Q_OBJECT

private slots:
  void aLeadingSlashPinsThePatternToTheRoot() {
    const auto anchored = Describe("/notes.tmp");
    QVERIFY(anchored.valid);
    QVERIFY(anchored.anchored);
    QCOMPARE(anchored.matchesExample, QStringLiteral("notes.tmp"));
    // The part people get wrong: without the slash this one matches too.
    QCOMPARE(anchored.missesExample, QStringLiteral("sub/notes.tmp"));
    QVERIFY(anchored.scope.contains("top level"));
  }

  void withoutASlashItMatchesAtAnyDepth() {
    const auto unanchored = Describe("*.tmp");
    QVERIFY(unanchored.valid);
    QVERIFY(!unanchored.anchored);
    QVERIFY(unanchored.scope.contains("any depth"));
    // The glob is instantiated so the example reads like a real file.
    QCOMPARE(unanchored.matchesExample, QStringLiteral("name.tmp"));
    QVERIFY(unanchored.missesExample.isEmpty());
  }

  void doubleStarCrossesDirectories() {
    const auto pattern = Describe("build/**");
    QVERIFY(pattern.valid);
    QVERIFY(!pattern.anchored);
    QCOMPARE(pattern.matchesExample, QStringLiteral("build/sub/file"));

    const auto rooted = Describe("/build/**");
    QVERIFY(rooted.anchored);
    QCOMPARE(rooted.missesExample, QStringLiteral("sub/build/sub/file"));
  }

  void trailingSlashIsADirectory() {
    const auto directory = Describe("logs/");
    QVERIFY(directory.valid);
    QVERIFY(directory.directoryOnly);
    // Shown with something inside it, since that is what gets removed.
    QCOMPARE(directory.matchesExample, QStringLiteral("logs/file"));
  }

  void instantiatesAlternationAndCharacterClasses() {
    QCOMPARE(Describe("*.{tmp,bak}").matchesExample,
             QStringLiteral("name.tmp"));
    QCOMPARE(Describe("file[0-9].txt").matchesExample,
             QStringLiteral("file0.txt"));
    QCOMPARE(Describe("log?.txt").matchesExample,
             QStringLiteral("logx.txt"));
  }

  void blankAndWhitespaceOnlyPatternsDescribeNothing() {
    QVERIFY(!Describe("").valid);
    QVERIFY(!Describe("   ").valid);
    QVERIFY(!Describe("/").valid);
  }

  void describesOneLinePerPattern() {
    const QStringList lines = DescribeAll("*.tmp\n\n/build/**\nlogs/\n");
    QCOMPARE(lines.size(), 3);
    QVERIFY2(lines[0].contains("any depth"), qPrintable(lines[0]));
    QVERIFY2(lines[1].contains("top level"), qPrintable(lines[1]));
    QVERIFY2(lines[1].contains("but not sub/build/sub/file"),
             qPrintable(lines[1]));
    QVERIFY2(lines[2].contains("whole directory"), qPrintable(lines[2]));
  }
};

QTEST_APPLESS_MAIN(FilterPatternTest)
#include "filter_pattern_test.moc"
