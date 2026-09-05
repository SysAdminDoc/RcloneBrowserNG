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
  // A leading "/" pins the pattern to the transfer's root.
  bool anchored = false;
  // A trailing "/" matches the directory and everything under it.
  bool directoryOnly = false;
  // One line saying where it applies.
  QString scope;
  // A concrete path this pattern matches, derived from the pattern.
  QString matchesExample;
  // A concrete path it does not, which is the part people get wrong.
  QString missesExample;
};

Description Describe(const QString &pattern);

// The lines shown under the exclude editor, one per non-empty pattern.
QStringList DescribeAll(const QString &patternsText);

} // namespace FilterPattern
