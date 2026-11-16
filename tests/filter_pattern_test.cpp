#include "filter_pattern.h"
#include "test_rclone.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using FilterPattern::Describe;
using FilterPattern::DescribeAll;

namespace {

using Example = QPair<QString, QString>;

QString Eacute() { return QString(QChar(0xE9)); }

// Measured against rclone v1.76.0 on 2026-11-15 with `rclone lsf --exclude
// <pattern>` over an empty directory, then checked file by file in
// classesAndEscapesAreReadTheWayGoReadsThem. rclone passes a class and any
// backslash to Go's regexp package as written, so these follow Go's regexp
// syntax rather than a shell glob's.
QList<Example> GoSyntaxExamples() {
  return {
      {"[!!-9;-Z^-z~].txt", Eacute() + ".txt"},
      {"[!*-z].txt", "!.txt"},
      {"[*].txt", "*.txt"},
      {"[?].txt", "?.txt"},
      {"[![:alpha:]].txt", "0.txt"},
      {"[!!-Z^-}].txt", "~.txt"},
      // Go reads [a[b] as the class and c] as two more characters.
      {"[a[b]c]", "ac]"},
      // Not a POSIX class outside another class, just the letters.
      {"[:alpha:]", "a"},
      {"\\d.txt", "0.txt"},
      {"\\x41.txt", "A.txt"},
      {"\\pL.txt", "x.txt"},
      {"\\PL.txt", "0.txt"},
      {"a\\_b", "a_b"},
      {"[\\d].txt", "0.txt"},
      {"[[:^alpha:]].txt", "0.txt"},
      {"[\\x{41}]", "A"},
      {"[\\p{L}]", "x"},
      {"[\\p{^L}]", "0"},
      // A ] right after [ is a member, so this class runs on through the
      // [^/] the ? became.
      {"[]?", "^"},
  };
}

// rclone exits 1 with "bad glob pattern" or the glob error for each of
// these, and nothing transfers.
QList<Example> RefusedPatterns() {
  return {
      {"{{a,b},c}.txt", "mismatched '{{' and '}}'"},
      {"[]", "missing closing ]"},
      {"[]a]", "mismatched ']'"},
      {"[[:bogus:]]", "invalid character class range [:bogus:]"},
      {"[[:alpha:]", "mismatched '[' and ']'"},
      {"[a\\]", "missing closing ]"},
      {"\\x.txt", "invalid escape sequence \\x\\."},
      {"\\1.txt", "invalid escape sequence \\1"},
      {"\\C", "invalid escape sequence \\C"},
      {"\\" + Eacute() + ".txt", "invalid escape sequence \\" + Eacute()},
      {"\\p{Bogus}.txt", "invalid character class range \\p("},
      {"\\pX", "invalid character class range \\pX"},
      {"\\p^", "invalid character class range \\p\\"},
      {"\\p\\{L}", "mismatched '{' and '}'"},
      {"***", "too many stars"},
      {"a***b", "too many stars"},
      {"[!\\]].txt", "mismatched ']'"},
      {"[!z-a].txt", "invalid character class range z-a"},
      {"[!\\x].txt", "invalid escape sequence \\x]\\"},
      {"[a-\\]]", "mismatched ']'"},
      {"[\\x{110000}]", "invalid escape sequence \\x{110000"},
      {"[\\x{}]", "invalid escape sequence \\x{}"},
      {"[\\p^L]", "invalid character class range \\p^"},
      {"[\\p{}]", "invalid character class range \\p{}"},
  };
}

// rclone takes these, and the explainer leaves them alone rather than
// guess: a {{regexp}} section, \Q...\E, an assertion, a trailing backslash
// that turns rclone's closing $ into a literal, a match on a control
// character (\12 is a newline), Unicode marks, and a long Unicode name. Go
// carries a table of script names the explainer doesn't, so it can't tell
// [\p{Greek}] from [\p{Bogus}], which rclone refuses.
QStringList UndescribedPatterns() {
  return {"{{.*}}.txt",   "a{\\{{b}}c", "\\Qa*\\E", "\\b",
          "a\\",          "\\12.txt",   "\\pM",     "\\pC",
          "[\\p{Greek}]", "[\\p{Bogus}]"};
}

// Every pattern the tests in this file describe, for the run against rclone.
QStringList DescribedPatterns() {
  QStringList patterns{"/file.tmp",
                       "*.tmp",
                       "build/**",
                       "/build/**",
                       "/**",
                       "/**.tmp",
                       "logs/",
                       "/*/x",
                       "/a*/**",
                       "/?.txt",
                       "*.{tmp,bak}",
                       "file[0-9].txt",
                       "log?.txt",
                       "[abc].txt",
                       "[!a].txt",
                       "[^a].txt",
                       "[!xa-c].txt",
                       "[!a-z0-9_X].txt",
                       "[!!-Z_a-z][ab].txt",
                       "[!!-Z^-z]?.txt",
                       "sub[^x]a.txt",
                       "*.[!t]mp",
                       "{a,{b,c}}",
                       "a\\{b",
                       "[!a[b]c]"};
  for (const Example &example : GoSyntaxExamples()) {
    patterns << example.first;
  }
  return patterns;
}

// Windows can't hold every example the explainer gives, [?].txt's for one.
bool Creatable(const QString &path) {
  for (const QChar c : path) {
    if (c.unicode() < 0x20 || QStringLiteral("<>:\"|?*\\").contains(c)) {
      return false;
    }
  }
  for (const QString &part : path.split(QChar('/'))) {
    if (part.isEmpty() || part.endsWith(QChar('.')) ||
        part.endsWith(QChar(' '))) {
      return false;
    }
  }
  return true;
}

bool Create(const QString &root, const QString &path) {
  const qsizetype slash = path.lastIndexOf(QChar('/'));
  if (slash > 0 && !QDir(root).mkpath(path.left(slash))) {
    return false;
  }
  QFile file(root + QChar('/') + path);
  return file.open(QIODevice::WriteOnly);
}

struct Listing {
  bool ran = false;
  int exitCode = -1;
  QStringList files;
};

Listing ListWithExclude(const QString &rclone, const QString &config,
                        const QString &pattern, const QString &directory) {
  QProcess process;
  process.start(rclone, {"lsf", "-R", "--files-only", "--config", config,
                         "--exclude", pattern, directory});
  Listing listing;
  if (!process.waitForFinished(60000)) {
    process.kill();
    process.waitForFinished();
    return listing;
  }
  listing.ran = process.exitStatus() == QProcess::NormalExit;
  listing.exitCode = process.exitCode();
  for (const QString &line : QString::fromUtf8(process.readAllStandardOutput())
                                 .split(QChar('\n'), Qt::SkipEmptyParts)) {
    listing.files << line.trimmed();
  }
  return listing;
}

} // namespace

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

  // Measured against rclone v1.76.0 on 2026-11-15 with `rclone lsf -R
  // --exclude <pattern>` over a.txt, b.txt, x.txt, !.txt, ^.txt, sub/a.txt:
  //   "[abc].txt"     excluded a.txt, b.txt and sub/a.txt
  //   "[!a].txt"      excluded b.txt, x.txt, !.txt and ^.txt
  //   "[^a].txt"      the same as [!a]
  //   "sub[!x]a.txt"  excluded nothing, since the class never matches /
  //   "[!].txt" / "[^].txt"  CRITICAL "empty character class", nothing ran
  void negatedClassesGiveAnExampleOutsideTheSet() {
    QCOMPARE(Describe("[abc].txt").matchesExample, QStringLiteral("a.txt"));
    QCOMPARE(Describe("[!a].txt").matchesExample, QStringLiteral("x.txt"));
    QCOMPARE(Describe("[^a].txt").matchesExample, QStringLiteral("x.txt"));
    // x is in this set, and so is the a-c range.
    QCOMPARE(Describe("[!xa-c].txt").matchesExample, QStringLiteral("z.txt"));
    QCOMPARE(Describe("[!a-z0-9_X].txt").matchesExample,
             QStringLiteral("!.txt"));
    // The first character outside this set is "[", which the next class
    // then swallowed, giving "[.txt". Glob syntax and characters Windows
    // refuses are passed over.
    QCOMPARE(Describe("[!!-Z_a-z][ab].txt").matchesExample,
             QStringLiteral("^a.txt"));
    QCOMPARE(Describe("[!!-Z^-z]?.txt").matchesExample,
             QStringLiteral("~x.txt"));
    QVERIFY(Describe("[abc].txt").negatedClass.isEmpty());
    QCOMPARE(Describe("sub[^x]a.txt").negatedClass, QStringLiteral("[^x]"));
    QCOMPARE(Describe("[ab][!c].txt").negatedClass, QStringLiteral("[!c]"));
  }

  void theExplainerSaysANegatedClassNeverMatchesASlash() {
    const QStringList lines = DescribeAll("*.[!t]mp\n[abc].txt\n");
    QCOMPARE(lines.size(), 2);
    QVERIFY2(lines[0].contains("matches name.xmp"), qPrintable(lines[0]));
    QVERIFY2(lines[0].contains("[!t] matches one character"),
             qPrintable(lines[0]));
    QVERIFY2(lines[0].endsWith("never a /"), qPrintable(lines[0]));
    QVERIFY2(!lines[1].contains("never a /"), qPrintable(lines[1]));
  }

  void anEmptyNegatedClassIsFatal() {
    for (const QString &pattern : {"[!].txt", "[^].txt"}) {
      const auto description = Describe(pattern);
      QVERIFY2(!description.wellFormed, qPrintable(pattern));
      QCOMPARE(description.problem, QStringLiteral("empty character class"));
    }
    QVERIFY(Describe("[!a].txt").wellFormed);
  }

  // A regex that stops at the first closing brace turns this into "a}", which
  // is not a path and not something the pattern matches.
  void nestedAlternationTakesTheFirstTopLevelBranch() {
    QCOMPARE(Describe("{a,{b,c}}").matchesExample, QStringLiteral("a"));
  }

  // "{{" opens a raw regexp section in rclone, so "{{a,b},c}.txt" is a
  // section that never closes. rclone v1.76.0 refuses it.
  void aDoubleBraceStartsARegexpSection() {
    const auto unclosed = Describe("{{a,b},c}.txt");
    QVERIFY(!unclosed.wellFormed);
    QCOMPARE(unclosed.problem, QStringLiteral("mismatched '{{' and '}}'"));
    QVERIFY(!Describe("{{.*}}.txt").valid);
  }

  void classesAndEscapesAreReadTheWayGoReadsThem() {
    for (const Example &example : GoSyntaxExamples()) {
      const auto description = Describe(example.first);
      QVERIFY2(description.valid && description.wellFormed,
               qPrintable(example.first + " " + description.problem));
      QVERIFY2(description.matchesExample == example.second,
               qPrintable(example.first + " gave " +
                          description.matchesExample));
    }
  }

  void patternsRcloneRefusesAreReportedWithItsReason() {
    for (const Example &example : RefusedPatterns()) {
      const auto description = Describe(example.first);
      QVERIFY2(description.valid, qPrintable(example.first));
      QVERIFY2(!description.wellFormed, qPrintable(example.first));
      QVERIFY2(description.problem == example.second,
               qPrintable(example.first + " gave " + description.problem));
      QVERIFY2(description.matchesExample.isEmpty(), qPrintable(example.first));
    }
  }

  void patternsTheExplainerCantModelGetNoLine() {
    for (const QString &pattern : UndescribedPatterns()) {
      const auto description = Describe(pattern);
      QVERIFY2(!description.valid, qPrintable(pattern));
      QVERIFY2(description.wellFormed, qPrintable(pattern));
      QVERIFY2(DescribeAll(pattern).isEmpty(), qPrintable(pattern));
    }
  }

  // A negated class is only named when Go closes it at the same "]" as the
  // glob does. Go ends "[!a[b]c]" at the first "]", leaving "c]" as plain
  // characters.
  void aNegatedClassIsNamedOnlyWhereGoClosesIt() {
    QCOMPARE(Describe("[![:alpha:]].txt").negatedClass,
             QStringLiteral("[![:alpha:]]"));
    const auto nested = Describe("[!a[b]c]");
    QVERIFY(nested.valid);
    QVERIFY2(nested.negatedClass.isEmpty(), qPrintable(nested.negatedClass));
    QCOMPARE(nested.matchesExample, QStringLiteral("xc]"));
  }

  // The claims above, checked against the rclone that runs the transfer:
  // what the explainer calls fatal is refused, what it explains is taken,
  // the example it gives is excluded and the "but not" example is kept.
  void agreesWithTheInstalledRclone() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("no rclone; set RCLONE_BROWSER_TEST_RCLONE");
    }
    QTemporaryDir scratch;
    QVERIFY(scratch.isValid());
    const QString config = scratch.filePath("rclone.conf");
    QVERIFY(QFile(config).open(QIODevice::WriteOnly));
    const QString empty = scratch.filePath("empty");
    QVERIFY(QDir().mkpath(empty));

    for (const Example &example : RefusedPatterns()) {
      const Listing listing =
          ListWithExclude(rclone, config, example.first, empty);
      QVERIFY2(listing.ran && listing.exitCode != 0,
               qPrintable(example.first + " was taken by rclone"));
    }
    for (const QString &pattern : UndescribedPatterns()) {
      const Listing listing = ListWithExclude(rclone, config, pattern, empty);
      QVERIFY2(listing.ran, qPrintable(pattern));
      // Only [\p{Bogus}] is refused, and the explainer makes no claim
      // either way about these.
      QVERIFY2((listing.exitCode == 0) == (pattern != "[\\p{Bogus}]"),
               qPrintable(pattern + " exit " +
                          QString::number(listing.exitCode)));
    }

    int tree = 0;
    for (const QString &pattern : DescribedPatterns()) {
      const auto description = Describe(pattern);
      QVERIFY2(description.valid && description.wellFormed,
               qPrintable(pattern));
      const Listing taken = ListWithExclude(rclone, config, pattern, empty);
      QVERIFY2(taken.ran && taken.exitCode == 0,
               qPrintable(pattern + " was refused by rclone"));

      const QString matches = description.matchesExample;
      const QString misses = description.missesExample;
      if (!Creatable(matches) || (!misses.isEmpty() && !Creatable(misses))) {
        continue;
      }
      const QString root = scratch.filePath(QString::number(++tree));
      QVERIFY(QDir().mkpath(root));
      QVERIFY2(Create(root, matches), qPrintable(matches));
      QVERIFY(Create(root, "keep/kept"));
      if (!misses.isEmpty()) {
        QVERIFY2(Create(root, misses), qPrintable(misses));
      }
      const Listing listing = ListWithExclude(rclone, config, pattern, root);
      QVERIFY2(listing.ran && listing.exitCode == 0, qPrintable(pattern));
      // Proves the listing ran over the tree. "/**" excludes all of it.
      QVERIFY2(listing.files.contains("keep/kept") == (pattern != "/**"),
               qPrintable(pattern));
      QVERIFY2(!listing.files.contains(matches),
               qPrintable(pattern + " kept " + matches));
      if (!misses.isEmpty()) {
        QVERIFY2(listing.files.contains(misses),
                 qPrintable(pattern + " excluded " + misses));
      }
    }
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
