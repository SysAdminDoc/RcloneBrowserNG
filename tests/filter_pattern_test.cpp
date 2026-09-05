#include "filter_pattern.h"

#include <QTest>

using FilterPattern::Describe;
using FilterPattern::DescribeAll;

// Every claim below was measured against rclone v1.75.0 on 2026-09-05, by
// running `rclone copy src dst --exclude <pattern>` over a tree holding
// file.tmp, sub/file.tmp, sub/sub/file.tmp, build/a.o, sub/build/b.o and
// keep.txt, then listing what failed to arrive:
//   "*.tmp"      excluded file.tmp, sub/file.tmp, sub/sub/file.tmp
//   "/file.tmp"  excluded file.tmp only
//   "build/**"   excluded build/a.o and sub/build/b.o
//   "/build/**"  excluded build/a.o only
//   "/**"        excluded every file in the tree
//   "/**.tmp"    excluded file.tmp, sub/file.tmp AND sub/sub/file.tmp
//   "a{b" / "[" / "a[b"  exit 1, CRITICAL "mismatched", nothing copied at all
class FilterPatternTest : public QObject {
  Q_OBJECT

private slots:
  void aLeadingSlashPinsThePatternToTheRoot() {
    const auto anchored = Describe("/file.tmp");
    QVERIFY(anchored.valid);
    QVERIFY(anchored.wellFormed);
    QVERIFY(anchored.anchored);
    QCOMPARE(anchored.matchesExample, QStringLiteral("file.tmp"));
    // The part people get wrong: without the slash this one matches too.
    QCOMPARE(anchored.missesExample, QStringLiteral("sub/file.tmp"));
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
    QVERIFY(!rooted.crossesDirectories);
    QCOMPARE(rooted.missesExample, QStringLiteral("sub/build/sub/file"));
  }

  // The panel used to answer "/**" with "only at the top level ... but not
  // sub/sub/file", printed in a warning box next to --delete-excluded. rclone
  // removes the entire tree for that pattern, so the reassurance was the exact
  // opposite of the truth, in the one place a wrong answer costs data.
  void anAnchoredDoubleStarIsNotConfinedToTheTopLevel() {
    const auto everything = Describe("/**");
    QVERIFY(everything.valid);
    QVERIFY(everything.anchored);
    QVERIFY(everything.crossesDirectories);
    QVERIFY2(everything.missesExample.isEmpty(),
             qPrintable("claimed it misses " + everything.missesExample));
    QVERIFY2(!everything.scope.contains("top level"),
             qPrintable(everything.scope));

    const auto suffixed = Describe("/**.tmp");
    QVERIFY(suffixed.crossesDirectories);
    QVERIFY(suffixed.missesExample.isEmpty());
  }

  // Guards the whole class of claim rather than the two cases above: whenever
  // a "but not" example is offered, the pattern must genuinely be unable to
  // reach it, which for these globs means it is anchored and cannot cross a
  // separator from the start.
  void aMissesExampleIsOnlyOfferedWhenItIsTrue() {
    const QStringList patterns{"/file.tmp", "/build/**", "/**",  "/**.tmp",
                               "*.tmp",     "build/**",  "logs/", "/*/x",
                               "/a*/**",    "/?.txt"};
    for (const QString &pattern : patterns) {
      const auto description = Describe(pattern);
      if (description.missesExample.isEmpty()) {
        continue;
      }
      QVERIFY2(description.anchored, qPrintable(pattern));
      QVERIFY2(!description.crossesDirectories, qPrintable(pattern));
      QVERIFY2(description.missesExample.startsWith("sub/"),
               qPrintable(pattern));
    }
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

  // A regex that stops at the first closing brace turns this into "a}", which
  // is not a path and not something the pattern matches.
  void nestedAlternationTakesTheFirstTopLevelBranch() {
    QCOMPARE(Describe("{a,{b,c}}").matchesExample, QStringLiteral("a"));
    QCOMPARE(Describe("{{a,b},c}.txt").matchesExample,
             QStringLiteral("a.txt"));
  }

  // rclone treats an unbalanced glob as fatal: exit 1, nothing copied. Saying
  // "matches a{b" would describe a transfer that never starts.
  void anUnbalancedGlobIsReportedAsFatalRatherThanExplained() {
    for (const QString &pattern : {"a{b", "[", "a[b", "}x", "a]b"}) {
      const auto description = Describe(pattern);
      QVERIFY2(description.valid, qPrintable(pattern));
      QVERIFY2(!description.wellFormed, qPrintable(pattern));
      QVERIFY2(!description.problem.isEmpty(), qPrintable(pattern));
      QVERIFY2(description.matchesExample.isEmpty(), qPrintable(pattern));
    }

    const QStringList lines = DescribeAll("a{b\n");
    QCOMPARE(lines.size(), 1);
    QVERIFY2(lines[0].contains("will not run"), qPrintable(lines[0]));
    QVERIFY2(lines[0].contains("mismatched"), qPrintable(lines[0]));
  }

  void anEscapedBraceIsNotAnOpeningBrace() {
    const auto escaped = Describe("a\\{b");
    QVERIFY(escaped.valid);
    QVERIFY2(escaped.wellFormed, qPrintable(escaped.problem));
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
    QVERIFY2(lines[0].contains("and sub/name.tmp"), qPrintable(lines[0]));
    QVERIFY2(lines[1].contains("top level"), qPrintable(lines[1]));
    QVERIFY2(lines[1].contains("but not sub/build/sub/file"),
             qPrintable(lines[1]));
    QVERIFY2(lines[2].contains("whole directory"), qPrintable(lines[2]));
  }

  // The editor is a QPlainTextEdit, but a pasted list can still carry CRLF.
  void carriageReturnsDoNotLeakIntoTheDescription() {
    const QStringList lines = DescribeAll("*.tmp\r\n/file.tmp\r\n");
    QCOMPARE(lines.size(), 2);
    for (const QString &line : lines) {
      QVERIFY2(!line.contains(QChar('\r')), qPrintable(line));
    }
    QVERIFY2(lines[0].startsWith("*.tmp:"), qPrintable(lines[0]));
  }
};

QTEST_APPLESS_MAIN(FilterPatternTest)
#include "filter_pattern_test.moc"
