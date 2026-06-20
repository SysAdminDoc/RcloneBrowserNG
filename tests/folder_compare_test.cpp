#include "folder_compare.h"

#include <QTest>

class FolderCompareTest : public QObject {
  Q_OBJECT

private slots:
  void parseCombinedOutput() {
    const QString output =
        "2026/06/16 10:00:00 NOTICE: source: 2 differences found\n"
        "= same.txt\r\n"
        "+ only-source.txt\n"
        "- only-destination.txt\n"
        "* changed.bin\n"
        "! unreadable.dat\n"
        "ignored line\n";

    const QVector<FolderCompareEntry> entries =
        ParseRcloneCheckCombinedOutput(output);
    QVERIFY2(entries.size() == 5, "combined output entry count changed");
    QVERIFY2(entries.at(0).status == FolderCompareStatus::Match,
             "match marker changed");
    QVERIFY2(entries.at(1).status == FolderCompareStatus::MissingOnDestination,
             "missing-on-destination marker changed");
    QVERIFY2(entries.at(2).status == FolderCompareStatus::MissingOnSource,
             "missing-on-source marker changed");
    QVERIFY2(entries.at(3).status == FolderCompareStatus::Different,
             "different marker changed");
    QVERIFY2(entries.at(4).status == FolderCompareStatus::Error,
             "error marker changed");
    QVERIFY2(entries.at(3).path == "changed.bin", "combined path changed");
  }

  void joinPaths() {
    QVERIFY2(JoinFolderComparePath("remote:", "dir/file.txt") ==
                 "remote:dir/file.txt",
             "remote root join changed");
    QVERIFY2(JoinFolderComparePath("remote:root", "dir/file.txt") ==
                 "remote:root/dir/file.txt",
             "remote subdir join changed");
    QVERIFY2(JoinFolderComparePath("C:/data", "dir/file.txt") ==
                 "C:/data/dir/file.txt",
             "local join changed");
  }
};

QTEST_MAIN(FolderCompareTest)
#include "folder_compare_test.moc"
