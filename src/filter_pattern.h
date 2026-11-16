#pragma once

#include "pch.h"

// rclone filter patterns look like paths and are not. They match at any
// depth unless they start with "/", so an exclude meant for one folder
// quietly applies everywhere. Combined with --delete-excluded that removes
// files at the destination, which is how an upstream reporter lost data
// (kapitainsky#252).
namespace FilterPattern {

struct Description {
  // False for a blank line, and for a pattern rclone takes but the
  // explainer can't model: a {{regexp}} section, \Q...\E, \b and the other
  // assertions, (?flags), a {n,m} repeat, \p{Greek} and other script names,
  // nesting too deep to follow, a pattern over 1000 characters that rclone
  // would accept (one it refuses still gets wellFormed false), or one with
  // no path the explainer can show it excluding ("x{/}" only matches a path
  // ending in "/"). Those get no line rather than a guess.
  bool valid = false;
  // False when rclone will refuse the glob outright. Measured against
  // v1.75.0: `--exclude "a{b"` exits 1 with "mismatched '{' and '}'" and
  // nothing transfers at all, so this has to be said rather than an
  // example printed for a pattern that will never run.
  bool wellFormed = true;
  // rclone's reason: the glob error, or the first error Go's regexp parser
  // reports.
  QString problem;
  // A leading "/" pins the pattern to the transfer's root.
  bool anchored = false;
  // The pattern ends in "/". rclone adds "**" to it, so it takes in the
  // directory and everything under it.
  bool directoryOnly = false;
  // True when the same example one directory down (under "sub/", or
  // "other/" when the example already starts with "sub/") is excluded too.
  // `/**`, `/**.tmp` and `/*/` all do this, so an anchored pattern is not
  // automatically top-level-only.
  bool crossesDirectories = false;
  // One line saying where it applies.
  QString scope;
  // A concrete path this pattern excludes, checked against the pattern the
  // way rclone checks it.
  QString matchesExample;
  // That deeper path when it's excluded too. Empty otherwise.
  QString alsoMatchesExample;
  // That deeper path when it isn't, which is the part people get wrong.
  // Empty whenever no such example can be stated truthfully.
  QString missesExample;
  // The first "[!...]" or "[^...]" class as written. Since rclone 1.76.0
  // both are negated and neither matches "/", which is worth saying. Left
  // empty when Go's regexp closes that class at a different "]", or reads
  // its first member as a range ("[!-a]" is the range / to a).
  QString negatedClass;
};

Description Describe(const QString &pattern);

// The lines shown under the exclude editor, one per non-empty pattern.
QStringList DescribeAll(const QString &patternsText);

} // namespace FilterPattern
