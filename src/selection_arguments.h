#pragma once

#include "pch.h"

// Turning a multi-selection into rclone arguments used to mean one
// `--include <name>` per selected row. rclone filter patterns are unanchored
// globs, so that silently did the wrong thing three ways: `a.txt` also matched
// `sub/a.txt`, `b[1].txt` matched `b1.txt` instead of the selected file, and a
// selected directory matched nothing inside it. Build anchored, escaped filter
// rules instead.
namespace SelectionArguments {

struct SelectionEntry {
  QString name;
  bool isDirectory = false;
};

struct FilterRules {
  bool valid = false;
  QString error;
  // rclone arguments, ready to prepend to the transfer command.
  QStringList arguments;
};

// Escapes the rclone filter metacharacters in one path segment so the pattern
// matches the name literally.
QString EscapeFilterPattern(const QString &name);

// `entries` name items directly inside the transfer's source directory.
// Produces `--filter "+ /<name>"` per file, `--filter "+ /<name>/**"` per
// directory, and a closing `--filter "- *"` so nothing else is transferred.
FilterRules BuildSelectionFilter(const QVector<SelectionEntry> &entries);

} // namespace SelectionArguments
