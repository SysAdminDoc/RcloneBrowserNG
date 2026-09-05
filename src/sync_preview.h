#pragma once

#include "pch.h"

// `rclone sync` removes destination files the source no longer has, and
// `--delete-excluded` removes anything a filter skipped. Neither said how
// much it was about to delete. An upstream reporter lost already-uploaded
// data by combining a misread exclude path with delete-excluded
// (kapitainsky#252).
//
// The preview is the same command the user is about to run, with --dry-run
// added, so it reports exactly what will happen rather than approximating it
// from a separate `rclone check`.
namespace SyncPreview {

struct Summary {
  // rclone reports a create and a replace identically, as a transfer.
  int toTransfer = 0;
  // Files whose contents already match; only the timestamp changes.
  int toUpdate = 0;
  int toDelete = 0;
  qint64 transferBytes = 0;
  qint64 deleteBytes = 0;
  // Capped; `moreDeletions` counts the ones not listed.
  QStringList deletions;
  int moreDeletions = 0;
  QString error;

  bool deletesAnything() const { return toDelete > 0; }
};

// Parses the `--use-json-log` output of a `--dry-run` pass. rclone marks
// each skipped action with a "skipped" field: "copy", "delete", or
// "update modification time".
Summary Parse(const QByteArray &jsonLog, int maxDeletionsListed = 20);

// One line for the panel heading.
QString Headline(const Summary &summary);

} // namespace SyncPreview
