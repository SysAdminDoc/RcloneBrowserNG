#include "selection_arguments.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using SelectionArguments::BuildSelectionFilter;
using SelectionArguments::EscapeFilterPattern;
using SelectionArguments::SelectionEntry;

// Verified against rclone v1.75.0 before this test was written:
//   --include "a.txt"     also matched sub/a.txt
//   --include "b[1].txt"  matched b1.txt and not the selected file
//   --include "folder"    matched nothing inside the folder
// The anchored, escaped --filter rules below matched exactly the named
// entries in the same fixture.
class SelectionArgumentsTest : public QObject {
  Q_OBJECT

private slots:
  void escapesEveryGlobMetacharacter_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("escaped");
    QTest::newRow("plain") << "report.txt" << "report.txt";
    QTest::newRow("brackets") << "b[1].txt" << "b\\[1\\].txt";
    QTest::newRow("star") << "note*.md" << "note\\*.md";
    QTest::newRow("question") << "who?.md" << "who\\?.md";
    QTest::newRow("braces") << "{draft}.md" << "\\{draft\\}.md";
    QTest::newRow("backslash") << "a\\b.txt" << "a\\\\b.txt";
    QTest::newRow("all") << "[a]{b}c*d?e" << "\\[a\\]\\{b\\}c\\*d\\?e";
    // Spaces and hashes are ordinary characters in a filter rule; a leading
    // hash only starts a comment inside a --filter-from file.
    QTest::newRow("hash") << "#notes.txt" << "#notes.txt";
    QTest::newRow("trailing space") << "folder name " << "folder name ";
  }

  void escapesEveryGlobMetacharacter() {
    QFETCH(QString, name);
    QFETCH(QString, escaped);
    QCOMPARE(EscapeFilterPattern(name), escaped);
  }

  void buildsAnchoredRulesForFilesAndDirectories() {
    const QVector<SelectionEntry> entries = {
        {"a.txt", false},
        {"b[1].txt", false},
        {"folder", true},
    };
    const auto rules = BuildSelectionFilter(entries);
    QVERIFY2(rules.valid, qPrintable(rules.error));
    const QStringList expected = {
        "--filter", "+ /a.txt",
        "--filter", "+ /b\\[1\\].txt",
        "--filter", "+ /folder/**",
        "--filter", "- *",
    };
    QCOMPARE(rules.arguments, expected);
  }

  void everyRuleIsAnchoredToTheSourceRoot() {
    // The unanchored `--include a.txt` also matched sub/a.txt. A leading
    // slash confines the rule to the transfer's source directory.
    const auto rules = BuildSelectionFilter({{"a.txt", false}});
    QVERIFY(rules.valid);
    for (int i = 0; i + 1 < rules.arguments.size(); i += 2) {
      QCOMPARE(rules.arguments.at(i), QStringLiteral("--filter"));
      const QString rule = rules.arguments.at(i + 1);
      if (rule == QLatin1String("- *")) {
        continue;
      }
      QVERIFY2(rule.startsWith(QLatin1String("+ /")), qPrintable(rule));
    }
  }

  void closingRuleExcludesEverythingElse() {
    const auto rules = BuildSelectionFilter({{"a.txt", false}});
    QVERIFY(rules.valid);
    QCOMPARE(rules.arguments.last(), QStringLiteral("- *"));
  }

  void emptyNamesAreSkippedAndAnEmptySelectionFails() {
    const auto mixed = BuildSelectionFilter({{"", false}, {"a.txt", false}});
    QVERIFY(mixed.valid);
    QCOMPARE(mixed.arguments.size(), 4);

    const auto empty = BuildSelectionFilter({});
    QVERIFY(!empty.valid);
    QVERIFY(!empty.error.isEmpty());

    const auto blank = BuildSelectionFilter({{"", false}});
    QVERIFY(!blank.valid);
  }

  void refusesNamesHoldingALineBreak() {
    // --filter rules are line oriented, so a name with a newline would split
    // into two rules and transfer something nobody selected.
    const auto rules = BuildSelectionFilter({{"two\nlines.txt", false}});
    QVERIFY(!rules.valid);
    QVERIFY(rules.error.contains("line break"));
    QVERIFY(BuildSelectionFilter({{"carriage\rreturn.txt", false}}).valid ==
            false);
  }

  // The unit cases above only prove what string we generate. This one hands
  // the generated arguments to a real rclone and checks what actually moves,
  // which is the part that would drift if rclone changed its filter matching
  // or stopped accepting flags ahead of the subcommand.
  void rcloneCopiesExactlyTheNamedEntries() {
    const QString rclone = QStandardPaths::findExecutable("rclone");
    if (rclone.isEmpty()) {
      QSKIP("rclone is not on PATH");
    }

    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    const QDir root(fixture.path());
    QVERIFY(root.mkpath("src/sub"));
    QVERIFY(root.mkpath("src/folder/deep"));
    const QStringList files = {
        "src/a.txt",           // selected
        "src/sub/a.txt",       // same name, deeper: must stay behind
        "src/b1.txt",          // must stay behind
        "src/b[1].txt",        // selected, and a glob would grab b1.txt instead
        "src/keep.txt",        // must stay behind
        "src/folder/f1.txt",   // inside a selected directory
        "src/folder/deep/f2.txt",
    };
    for (const QString &relative : files) {
      QFile file(root.filePath(relative));
      QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(relative));
      file.write("x");
      file.close();
    }

    const auto rules = BuildSelectionFilter({
        {"a.txt", false},
        {"b[1].txt", false},
        {"folder", true},
    });
    QVERIFY2(rules.valid, qPrintable(rules.error));

    // Filters go ahead of the subcommand, which is where RemoteWidget
    // prepends them.
    QStringList args = rules.arguments;
    args << "copy" << root.filePath("src") << root.filePath("dst");
    QProcess copy;
    copy.start(rclone, args);
    QVERIFY(copy.waitForFinished(60000));
    QCOMPARE(copy.exitCode(), 0);

    QProcess list;
    list.start(rclone, QStringList() << "lsf" << "-R" << root.filePath("dst"));
    QVERIFY(list.waitForFinished(60000));
    QStringList copied = QString::fromUtf8(list.readAllStandardOutput())
                             .split('\n', Qt::SkipEmptyParts);
    for (QString &entry : copied) {
      entry = entry.trimmed();
    }
    copied.removeAll(QString());
    copied.sort();

    QStringList expected = {"a.txt", "b[1].txt", "folder/", "folder/deep/",
                            "folder/deep/f2.txt", "folder/f1.txt"};
    expected.sort();
    QCOMPARE(copied, expected);
  }

  void refusesSelectionsTooLargeForOneCommandLine() {
    QVector<SelectionEntry> entries;
    const QString name(240, QChar('n'));
    for (int i = 0; i < 200; ++i) {
      entries.append({name + QString::number(i), false});
    }
    const auto rules = BuildSelectionFilter(entries);
    QVERIFY(!rules.valid);
    QVERIFY(rules.error.contains("command line"));

    // A selection that fits still works.
    QVERIFY(BuildSelectionFilter(entries.mid(0, 20)).valid);
  }
};

QTEST_APPLESS_MAIN(SelectionArgumentsTest)
#include "selection_arguments_test.moc"
