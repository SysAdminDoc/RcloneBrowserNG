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

// The rclone command the dry run ran. Under move the transfer is a move
// too, which changes what the dry run's moves are.
enum class Command { Sync, Copy, Move, Bisync };

// "Sync", "Copy", "Move" or "Bisync", for titles and buttons.
QString CommandName(Command command);

// The run's flags that change what its dry run leaves out.
struct RunFlags {
  // --backup-dir or --suffix. A replaced file's old copy goes into the
  // backup, and so does the old copy of a file rclone uploads again
  // because the remote can't set its timestamp.
  bool backup = false;
  // --track-renames or --fix-case, so a "move to" another name can be a
  // rename. Without either, every move is an old copy going into the
  // backup, whatever --name-transform does to its name.
  bool renames = false;
  // --delete-excluded, so a sync also removes what the filters leave out.
  bool deleteExcluded = false;
};

// Reads RunFlags from the run's arguments, the way rclone's flag parser
// does: the last value wins, --fix-case=false turns it off, and the value
// after --exclude, --include or --filter is a pattern, not a flag. Bisync's
// --backup-dir1 and --backup-dir2 are a backup too.
RunFlags FlagsIn(const QStringList &arguments);

// The run's arguments with the options rclone reads from elsewhere written
// in as flags, for FlagsIn and JobOptions::DeleteCapsIn (rclone v1.76.0).
// RCLONE_<FLAG> in rclone's environment is that flag's default, so it goes
// first and the command line wins over it. A global.<option> in a
// connection string (":local,global.fix_case:dir") is set when rclone opens
// that remote, after the command line, so it goes last. rclone sets up the
// run before it opens a backup or --compare-dest folder, so that folder's
// options count only for the caps and --fix-case (and a backup folder's
// for the --compare-dest and --copy-dest lists), and a bisync's backup
// folders count for nothing. A remote's config section can carry global.
// options as well, which this doesn't read.
QStringList EffectiveArguments(const QStringList &arguments,
                               const QProcessEnvironment &environment);

// What the deletion check adds to its dry run, after the command. With no
// filter active, a Move the remote can do server side moves the whole
// folder in one step, and its dry run lists only a "server-side directory
// move". When the destination folder is already there, the real run falls
// back to moving each file, and those moves count against the caps
// (measured on v1.76.0). A filter rule that matches nothing makes the dry
// run list each move. --files-from leaves no such step to take and refuses
// any other filter, so it gets nothing.
QStringList DryRunExtras(Command command,
                         const QStringList &effectiveArguments);

// The dry run's environment. Its arguments leave the caps out, except a
// bisync's (JobOptions::DryRunArguments), and so does this, or an
// RCLONE_MAX_DELETE would stop the check partway.
QProcessEnvironment DryRunEnvironment(QProcessEnvironment environment,
                                      Command command);

struct Summary {
  Command command = Command::Sync;
  RunFlags flags;
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
  // or two for it (see CapWarnings).
  int replacedIntoBackup = 0;
  // Files --track-renames or --fix-case renames at the destination ("move
  // to <new name>" with no copy beside it, or under --fix-case to the same
  // name in another case or Unicode form).
  int renamed = 0;
  // Under `rclone move`, the files it moves. Also in toTransfer.
  int moved = 0;
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
// a delete; a move with no copy is a --track-renames or --fix-case rename,
// or under `rclone move` the transfer itself.
Summary Parse(const QByteArray &jsonLog, int maxDeletionsListed = 20,
              Command command = Command::Sync, RunFlags flags = RunFlags());

// One line for the panel heading.
QString Headline(const Summary &summary);

// The deletion summary's title: what the run deletes, what it moves into
// the backup, or both. A run that deletes nothing gets the summary only to
// warn about a cap, and its title names the moves that could trip it.
QString DeletionTitle(const Summary &summary);

// The summary's explanation for a run that deletes nothing: what in it
// rclone can count against a delete cap.
QString NoDeleteDetail(const Summary &summary);

// The summary's explanation for a run that removes files, which ends with
// their size.
QString RemovalDetail(const Summary &summary, const QString &size);

// The heading over the list of files the run removes.
QString RemovalListTitle(const Summary &summary);

// Why the real run would stop at a delete cap, one line each, for the
// deletion summary. countCap and bytesCap are -1 when there's no cap;
// niceSize formats a byte count. A bisync's dry run keeps its caps and
// stops at --max-delete-size where the real run would, and its
// --max-delete is a percentage that rclone checks before it changes
// anything, so a bisync only gets the warning about the deletes a move into
// the backup can add.
QStringList CapWarnings(const Summary &summary, qint64 countCap,
                        qint64 bytesCap,
                        const std::function<QString(qint64)> &niceSize);

} // namespace SyncPreview
