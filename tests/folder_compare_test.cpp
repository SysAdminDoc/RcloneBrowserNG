#include "folder_compare.h"

#include <iostream>

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << std::endl;
    std::exit(1);
  }
}

int main() {
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
  require(entries.size() == 5, "combined output entry count changed");
  require(entries.at(0).status == FolderCompareStatus::Match,
          "match marker changed");
  require(entries.at(1).status == FolderCompareStatus::MissingOnDestination,
          "missing-on-destination marker changed");
  require(entries.at(2).status == FolderCompareStatus::MissingOnSource,
          "missing-on-source marker changed");
  require(entries.at(3).status == FolderCompareStatus::Different,
          "different marker changed");
  require(entries.at(4).status == FolderCompareStatus::Error,
          "error marker changed");
  require(entries.at(3).path == "changed.bin", "combined path changed");

  require(JoinFolderComparePath("remote:", "dir/file.txt") ==
              "remote:dir/file.txt",
          "remote root join changed");
  require(JoinFolderComparePath("remote:root", "dir/file.txt") ==
              "remote:root/dir/file.txt",
          "remote subdir join changed");
  require(JoinFolderComparePath("C:/data", "dir/file.txt") ==
              "C:/data/dir/file.txt",
          "local join changed");

  return 0;
}
