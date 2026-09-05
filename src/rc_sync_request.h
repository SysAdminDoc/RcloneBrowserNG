#pragma once

#include "pch.h"

// Turns the CLI argument list the app already builds into a native rclone
// remote-control call (sync/copy, sync/move, sync/sync, sync/bisync).
//
// The reason this exists is that the engine used to hand the whole command to
// core/command, which runs the CLI inside the daemon. That works, but it gives
// no per-job stats group, so live bandwidth tuning, graceful stop and batched
// operations have nothing to attach to.
//
// The flag-to-option mapping is NOT hard-coded. rclone reports its own option
// metadata over options/info, and that is where the mapping comes from, for a
// specific reason found while probing rclone v1.75.0: an unknown key inside
// _config is accepted with HTTP 200 and silently ignored. A hand-written table
// with one stale name would therefore drop a flag with no error at all, and
// the flag it dropped could be --dry-run. Deriving the table from the running
// daemon means a name this rclone does not know is never sent; the request is
// refused instead and the caller falls back to core/command.
namespace RcSyncRequest {

struct OptionSpec {
  // "main" lands in _config, "filter" lands in _filter.
  QString section;
  // The JSON key. rclone reports embedded fields as "RulesOpt.ExcludeRule",
  // but the wire form is flat: "ExcludeRule". Sending the nested shape was
  // accepted with 200 and quietly did nothing, so only the last segment is
  // used.
  QString key;
  // rclone's own type name, which decides the JSON shape of the value.
  QString type;
};

// Flag ("--dry-run") to spec, built from an options/info response.
using OptionIndex = QHash<QString, OptionSpec>;

OptionIndex IndexOptions(const QJsonObject &optionsInfo);

struct Request {
  bool usable = false;
  // "sync/copy" and friends.
  QString endpoint;
  QJsonObject payload;
  // The two positional paths, so the caller can check the source is a
  // directory before committing to this route.
  QString source;
  QString dest;
  // Why the request cannot be built, for the log and the tests.
  QString reason;
};

Request Build(const QStringList &args, const QString &group,
              const OptionIndex &index);

} // namespace RcSyncRequest
