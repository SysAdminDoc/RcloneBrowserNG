#pragma once

#include "pch.h"

#include <functional>

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
  // Every file the sync removes from the destination, including the ones
  // --backup-dir or --suffix moves aside instead: rclone counts those against
  // --max-delete and --max-delete-size as deletes.
  int toDelete = 0;
  // How many of toDelete are moved aside rather than deleted.
  int toBackup = 0;
  // Replaced files whose old copy goes into the backup ("move", or "move to
  // a.txt.bak" with --suffix). Not a delete itself, but rclone can count one
  // or two for it (see CapWarnings). A --track-renames rename isn't one.
  int replacedIntoBackup = 0;
  qint64 transferBytes = 0;
  qint64 deleteBytes = 0;
  // Capped; `moreDeletions` counts the ones not listed.
  QStringList deletions;
  int moreDeletions = 0;
  QString error;

  bool deletesAnything() const { return toDelete > 0; }
};

// Parses the `--use-json-log` output of a `--dry-run` pass. rclone marks
// each skipped action with a "skipped" field: "copy", "delete", "move into
// backup dir" (a delete under --backup-dir or --suffix), or "update
// modification time". A replaced file's old copy going to the backup is
// "move" or "move to <name>" beside the copy that replaces it, which isn't
// a delete; a move with no copy is a --track-renames rename, or under
// `rclone move` the transfer itself.
Summary Parse(const QByteArray &jsonLog, int maxDeletionsListed = 20);

// One line for the panel heading.
QString Headline(const Summary &summary);

// The deletion summary's title: what the sync deletes, what it moves into
// the backup, or both. A run that deletes nothing gets the summary only to
// warn about a cap, and its title names the old copies of replaced files
// going into the backup.
QString DeletionTitle(const Summary &summary);

// Why the real run would stop at a delete cap, one line each, for the
// deletion summary. countCap and bytesCap are -1 when there's no cap;
// niceSize formats a byte count.
QStringList CapWarnings(const Summary &summary, qint64 countCap,
                        qint64 bytesCap,
                        const std::function<QString(qint64)> &niceSize);

} // namespace SyncPreview
