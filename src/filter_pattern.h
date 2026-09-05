#pragma once

#include "pch.h"

// rclone filter patterns look like paths and are not. They match at any
// depth unless they start with "/", so an exclude meant for one folder
// quietly applies everywhere. Combined with --delete-excluded that removes
// files at the destination, which is how an upstream reporter lost data
// (kapitainsky#252).
namespace FilterPattern {

struct Description {
  bool valid = false;
  // False when rclone will refuse the glob outright. Measured against
  // v1.75.0: `--exclude "a{b"` exits 1 with "mismatched '{' and '}'" and
  // nothing transfers at all, so this has to be said rather than an
  // example printed for a pattern that will never run.
  bool wellFormed = true;
  QString problem;
  // A leading "/" pins the pattern to the transfer's root.
  bool anchored = false;
  // A trailing "/" matches the directory and everything under it.
  bool directoryOnly = false;
  // True when the pattern reaches into subdirectories even though it is
  // anchored, which "**" does. `/**` and `/**.tmp` both matched every depth
  // in v1.75.0, so an anchored pattern is not automatically top-level-only.
  bool crossesDirectories = false;
  // One line saying where it applies.
  QString scope;
  // A concrete path this pattern matches, derived from the pattern.
  QString matchesExample;
  // A concrete path it does not, which is the part people get wrong. Empty
  // whenever no such example can be stated truthfully.
  QString missesExample;
};

Description Describe(const QString &pattern);

// The lines shown under the exclude editor, one per non-empty pattern.
QStringList DescribeAll(const QString &patternsText);

} // namespace FilterPattern
