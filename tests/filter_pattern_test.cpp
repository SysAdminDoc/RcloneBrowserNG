#include "filter_pattern.h"
#include "test_rclone.h"

#include <QDir>
#include <QElapsedTimer>
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
      // Go 1.27 looks Unicode names up in any case, and takes the long
      // category names. Measured on v1.76.0 on 2026-11-16.
      {"\\pl.txt", "x.txt"},
      {"[\\p{l}]", "x"},
      {"*.\\pn", "name.0"},
      {"[!\\p{^s}]", "$"},
      {"[\\p{Lu}]", "X"},
      {"[\\p{Letter}]", "x"},
      // The * and + repeat the class [[a], and ]x follows it. rclone
      // excluded ]x and a]x for the first and a]x for the second.
      {"[[a]*]x", "]x"},
      {"[[a]+]x", "a]x"},
      // a/ can't end a file's path, and this isn't a directory rule since
      // the glob doesn't end in "/". rclone excluded b and nothing else.
      {"{a/,b}", "b"},
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
      {"[\\p{Bogus}]", "invalid character class range \\p{Bogus}"},
      // Go stops at the first error, so these name the unknown \p{foo}
      // and not what follows it.
      {"[\\p{foo}\\Q]", "invalid character class range \\p{foo}"},
      {"[\\p{foo}][]", "invalid character class range \\p{foo}"},
      {"[\\p{foo}z-a]", "invalid character class range \\p{foo}"},
      // \pl is fine, so the error is the one after it.
      {"\\pl[]", "missing closing ]"},
      {"\\pl\\x", "invalid escape sequence \\x$"},
      {"[\\p{l}][a-\\S]", "invalid escape sequence \\S"},
      // A | or ( or ) that Go reads outside the class [[].
      {"[[]|*]", "missing argument to repetition operator *"},
      {"[[a](]", "missing closing )"},
      {"[[a])]", "unexpected )"},
      {"[[a]**]", "invalid nested repetition operator **"},
      {"[[a]*?*]", "invalid nested repetition operator *?*"},
  };
}

// rclone takes these, and the explainer leaves them alone rather than
// guess: a {{regexp}} section, \Q...\E, an assertion, a trailing backslash
// that turns rclone's closing $ into a literal, a match on a control
// character (\12 is a newline), Unicode marks, a Unicode script name (Go
// carries script tables the explainer doesn't) and a {n,m} repeat. The
// rest only match a path no listing holds. Measured on v1.76.0 on
// 2026-11-16, x{/}, x[/], [/], // and \/ excluded nothing from a tree of
// file, x/file and sub/file, since a file's path never ends in "/" or has
// an empty part, and none of these globs ends in "/".
QStringList UndescribedPatterns() {
  return {"{{.*}}.txt", "a{\\{{b}}c", "\\Qa*\\E", "\\b",   "a\\",
          "\\12.txt",   "\\pM",       "\\pC",     "[\\p{Greek}]",
          "x{/}",       "x[/]",       "[/]",      "//",    "\\/",
          "[[a]{2}]x"};
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
                       "[!a[b]c]",
                       "/",
                       "/*/",
                       "/sub/**",
                       "[!-a]"};
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
      QVERIFY2(listing.ran && listing.exitCode == 0,
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
      const QString also = description.alsoMatchesExample;
      if (!Creatable(matches) || (!misses.isEmpty() && !Creatable(misses)) ||
          (!also.isEmpty() && !Creatable(also))) {
        continue;
      }
      const QString root = scratch.filePath(QString::number(++tree));
      QVERIFY(QDir().mkpath(root));
      QVERIFY2(Create(root, matches), qPrintable(matches));
      QVERIFY(Create(root, "keep/kept"));
      if (!misses.isEmpty()) {
        QVERIFY2(Create(root, misses), qPrintable(misses));
      }
      if (!also.isEmpty()) {
        QVERIFY2(Create(root, also), qPrintable(also));
      }
      const Listing listing = ListWithExclude(rclone, config, pattern, root);
      QVERIFY2(listing.ran && listing.exitCode == 0, qPrintable(pattern));
      // Proves the listing ran over the tree. "/**" and "/" exclude all of
      // it, and "/*/" everything in a subdirectory.
      const QStringList excludesAll{"/**", "/", "/*/"};
      QVERIFY2(listing.files.contains("keep/kept") ==
                   !excludesAll.contains(pattern),
               qPrintable(pattern));
      QVERIFY2(!listing.files.contains(matches),
               qPrintable(pattern + " kept " + matches));
      if (!also.isEmpty()) {
        QVERIFY2(!listing.files.contains(also),
                 qPrintable(pattern + " kept " + also));
      }
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
  }

  // Measured on v1.76.0 on 2026-11-16 with `rclone lsf -R --exclude
  // <pattern>` over file, x/file, sub/file, sub/name/file, other/sub/file
  // and sub/sub/file. rclone adds "**" to a pattern ending in "/":
  //   "/"        excluded every file
  //   "/*/"      excluded every file in a subdirectory, sub/name/file too
  //   "/sub/**"  excluded sub/file, sub/name/file and sub/sub/file only
  void aPatternEndingInASlashCoversEverythingInside() {
    const auto everything = Describe("/");
    QVERIFY(everything.valid);
    QVERIFY(everything.wellFormed);
    QVERIFY(everything.directoryOnly);
    QVERIFY(everything.crossesDirectories);
    QCOMPARE(everything.matchesExample, QStringLiteral("file"));
    QCOMPARE(everything.alsoMatchesExample, QStringLiteral("sub/file"));
    QCOMPARE(everything.scope,
             QStringLiteral("everywhere below the transfer root"));
    QVERIFY(everything.missesExample.isEmpty());

    const auto inside = Describe("/*/");
    QVERIFY(inside.crossesDirectories);
    QCOMPARE(inside.matchesExample, QStringLiteral("name/file"));
    QCOMPARE(inside.alsoMatchesExample, QStringLiteral("sub/name/file"));
    QVERIFY2(inside.missesExample.isEmpty(), qPrintable(inside.missesExample));
    const QStringList lines = DescribeAll("/*/");
    QCOMPARE(lines.size(), 1);
    QVERIFY2(lines[0].contains("and sub/name/file"), qPrintable(lines[0]));

    const auto rooted = Describe("/sub/**");
    QVERIFY(!rooted.crossesDirectories);
    QCOMPARE(rooted.matchesExample, QStringLiteral("sub/sub/file"));
    QCOMPARE(rooted.missesExample, QStringLiteral("other/sub/sub/file"));
  }

  // Go reads [^/-a] as the range / to a, so "[!-a] matches one character
  // that isn't listed" would be wrong. rclone excluded -, b, x and ~ and
  // kept 0, A and _.
  void aNegatedClassGoReadsAsARangeIsNotNamed() {
    const auto range = Describe("[!-a]");
    QVERIFY(range.valid);
    QVERIFY(range.wellFormed);
    QCOMPARE(range.matchesExample, QStringLiteral("x"));
    QVERIFY2(range.negatedClass.isEmpty(), qPrintable(range.negatedClass));

    // A later range in the class used to clear that, so these got the
    // sentence. rclone v1.76.0 excluded - and b for [!-a0-9] and kept A and
    // 5, the class Go reads as "not / to a and not 0 to 9".
    const QStringList laterRanges{"[!-a0-9]", "*.[!-a0-9]",
                                  QString(QChar(0xE9)) + "[^-z0-9Z]",
                                  "[^-aa-z]a"};
    for (const QString &pattern : laterRanges) {
      const auto later = Describe(pattern);
      QVERIFY2(later.valid && later.wellFormed, qPrintable(pattern));
      QVERIFY2(later.negatedClass.isEmpty(),
               qPrintable(pattern + ": " + later.negatedClass));
    }
    // Without the leading "-" the "/" stands alone, so the sentence stays.
    const QStringList plain{"[!a0-9]", "[!0-9]"};
    for (const QString &pattern : plain) {
      QVERIFY2(!Describe(pattern).negatedClass.isEmpty(), qPrintable(pattern));
    }
  }

  // "/*/x" matched name/x, which isn't at the top level. The scope said
  // "only at the top level" anyway, and a reader takes that to mean nothing
  // deeper goes. rclone v1.76.0 excluded name/x for /*/x and x/b for
  // /{a,*/b}.
  void anAnchoredPatternThatReachesDownStartsFromTheTop() {
    const QStringList reaching{"/*/x", "/{a,*/b}", "/build/**", "/logs/"};
    for (const QString &pattern : reaching) {
      const auto description = Describe(pattern);
      QVERIFY2(description.valid && description.wellFormed,
               qPrintable(pattern));
      QCOMPARE(description.scope,
               QStringLiteral("only from the top level of the transfer"));
    }
    QCOMPARE(Describe("/file.tmp").scope,
             QStringLiteral("only at the top level of the transfer"));
    QCOMPARE(Describe("/*.tmp").scope,
             QStringLiteral("only at the top level of the transfer"));
  }

  // The word used to come from a "/" in the text, which picked the wrong
  // one for about 1 anchored line in 5. Measured on rclone v1.76.0 with
  // trees holding x, a, b, xx, zz and namex at the top and x/x, a/b, b/x,
  // x/a, sub/x, z/z and zz/x below: the first list excluded files at the
  // top and nothing below, and the second excluded a/a, a/f, b/a, a/x,
  // a/- or the like.
  void theScopeWordComesFromWhatThePatternCanReach_data() {
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<bool>("below");
    const QStringList at{"/[^/]",  "/x{/,}",  "/{/,}a",  "/{a,/b}",
                         "/z[!/x]", "/[!/-a]", "/*[!/]",  "/[!\\/]",
                         "/a[!/]b", "/a[^/]b", "/{a,b/}", "/[!/]*"};
    for (const QString &pattern : at) {
      QTest::newRow(qPrintable(pattern)) << pattern << false;
    }
    const QStringList from{"/a{a,**}",  "/{a,b}**",       "/b{**,a}",
                           "/b[.-0]a",  "/?\\PLx",        "/\\S[\\D]a",
                           "/a[\\S]\\W", "/a**",          "/build**",
                           "/a\\x2fb",  "/a[+-0]b",       "/a\\Wb",
                           "/a[[:punct:]]b", "/a\\pPb",   "/a\\Sb",
                           QString("/") + QChar(0xE9) + "[+-0]x"};
    for (const QString &pattern : from) {
      QTest::newRow(qPrintable(pattern)) << pattern << true;
    }
  }

  void theScopeWordComesFromWhatThePatternCanReach() {
    QFETCH(QString, pattern);
    QFETCH(bool, below);
    const auto description = Describe(pattern);
    QVERIFY2(description.valid && description.wellFormed,
             qPrintable(description.problem));
    QVERIFY2(!description.crossesDirectories,
             qPrintable(description.alsoMatchesExample));
    QCOMPARE(description.scope,
             below ? QStringLiteral("only from the top level of the transfer")
                   : QStringLiteral("only at the top level of the transfer"));
  }

  // A match below the top level has to be a path rclone could list: a
  // "." or ".." folder, or an empty name, can't hold a file.
  void aPathNoFolderCanHoldDoesNotReachBelow() {
    const QStringList at{"/{x,.[/]x}",    "/{x,..[/]x}", "/{x,a[/][/]x}",
                         "/{x,a[/]..}",   "/{x,a[/].}",  "/{x,a[/]}"};
    for (const QString &pattern : at) {
      const auto description = Describe(pattern);
      QVERIFY2(description.valid && description.wellFormed,
               qPrintable(pattern));
      QCOMPARE(description.matchesExample, QStringLiteral("x"));
      QVERIFY2(description.scope ==
                   QStringLiteral("only at the top level of the transfer"),
               qPrintable(pattern + ": " + description.scope));
    }
    QCOMPARE(Describe("/{x,...[/]x}").scope,
             QStringLiteral("only from the top level of the transfer"));
  }

  // A pattern over 1000 characters gets no line when rclone takes it, but
  // one rclone refuses for a reason Go gives still says so. Measured on
  // rclone v1.76.0: each of these stopped with the reason named.
  void aLongPatternRcloneRefusesStillSaysSo_data() {
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("reason");
    const QString z1000 = QString("z").repeated(1000);
    const QString a995 = QString("a").repeated(995);
    QTest::newRow("bad escape first")
        << "\\x" + z1000 << "invalid escape sequence";
    QTest::newRow("bad escape last")
        << QString("a").repeated(1000) + "\\x" << "invalid escape sequence";
    QTest::newRow("bad range, 1001")
        << "[!z-a]" + a995 << "invalid character class range";
    QTest::newRow("bad range, 1006")
        << "[!z-a]" + QString("a").repeated(1000)
        << "invalid character class range";
    QTest::newRow("bad name")
        << "[\\p{Bogus}]" + QString("a").repeated(1000)
        << "invalid character class range";
    QTest::newRow("in braces")
        << "{a,[!z-a]" + QString("b").repeated(1000) + "}"
        << "invalid character class range";
    QTest::newRow("directory, 1000 typed")
        << "[!z-a]" + QString("a").repeated(993) + "/"
        << "invalid character class range";
  }

  void aLongPatternRcloneRefusesStillSaysSo() {
    QFETCH(QString, pattern);
    QFETCH(QString, reason);
    const auto description = Describe(pattern);
    QVERIFY(description.valid);
    QVERIFY(!description.wellFormed);
    QVERIFY2(description.problem.startsWith(reason),
             qPrintable(description.problem.left(80)));
    const QStringList lines = DescribeAll(pattern);
    QCOMPARE(lines.size(), 1);
    QVERIFY2(lines.first().contains("rclone will refuse this filter"),
             qPrintable(lines.first().right(120)));
  }

  // Nested 600 deep, Go stops with "expression nests too deeply" before it
  // reaches the \x, so naming the \x would be a wrong reason. These get no
  // line, and so does a refused pattern too long to be sure of.
  void aLongRefusedPatternItCantBeSureOfGetsNoLine() {
    const QString nested = QString("{a").repeated(600) +
                           QString("}").repeated(600) + "\\x";
    QVERIFY(!Describe(nested).valid);
    QVERIFY(!Describe(QString("a").repeated(20001) + "\\x").valid);
    QVERIFY(Describe(QString("a").repeated(19998) + "\\x").valid);
  }

  // The 1000 counts what was typed, not the "**" rclone adds to a
  // directory pattern, which made this 1000-character one 1002 long.
  // rclone v1.76.0 takes it.
  void theLengthCutCountsWhatWasTyped() {
    const QString typed = "{a," + QString("b").repeated(995) + "}/";
    QCOMPARE(typed.size(), 1000);
    const auto description = Describe(typed);
    QVERIFY(description.valid);
    QVERIFY(description.wellFormed);
    QVERIFY(description.directoryOnly);
  }

  // DescribeAll runs on every change in the exclude editor, on the UI
  // thread. A pasted pattern like these used to overflow the stack ({a
  // nested 2921 deep) or take seconds (\pM 1666 times). They get no line,
  // and quickly. rclone refuses {a nested 500 deep ("expression nests too
  // deeply"), so no line is the honest answer for that one too.
  void longPatternsGetNoLineAndComeBackQuickly() {
    const QStringList patterns{
        QString("{a").repeated(2921) + QString("}").repeated(2921),
        QString("{a").repeated(500) + QString("}").repeated(500),
        QString("\\pM").repeated(1666),
        QString("[\\pM]").repeated(6000),
        QString("[\\pM]").repeated(199),
        QString("{a").repeated(333) + QString("}").repeated(333),
        QString("\\pM").repeated(333),
    };
    QElapsedTimer timer;
    timer.start();
    for (const QString &pattern : patterns) {
      QVERIFY2(!Describe(pattern).valid, qPrintable(pattern.left(24)));
    }
    QVERIFY2(timer.elapsed() < 1500,
             qPrintable(QString::number(timer.elapsed()) + " ms"));
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
