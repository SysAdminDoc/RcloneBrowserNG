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

// options/info has no entry for --delete-before, --delete-during or
// --delete-after, which set ConfigInfo.DeleteMode. They're added to the
// index only when the daemon's options/get reports main.DeleteMode as a
// number, so a rclone that renamed it keeps those runs on core/command.
void AddDeleteModes(OptionIndex *index, const QJsonObject &optionsGet);

// CLI flags turned into the _config and _filter objects. Anything that isn't
// a flag comes back in positional. ok is false, with reason set, as soon as
// one flag can't be mapped.
struct Options {
  bool ok = false;
  QString reason;
  QJsonObject config;
  QJsonObject filter;
  QStringList positional;
  // --retries and --retries-sleep, which never reach _config: rclone's sync
  // calls don't retry, so the caller has to. 3 attempts and no pause are
  // rclone's defaults.
  int retries = 3;
  qint64 retriesSleepMs = 0;
  // The same two as _config would carry them, only those given. Build puts
  // them into a bisync's _config for --resilient's copy retry, which reads
  // them there. options/info lists no bisync flag, so Build refuses
  // --resilient today and they change nothing until it can map it.
  QJsonObject retryConfig;
};

Options MapOptions(const QStringList &tokens, const OptionIndex &index);

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
  // How many times to run the call before giving up, and the pause between
  // runs, from --retries and --retries-sleep.
  int retries = 3;
  qint64 retriesSleepMs = 0;
};

Request Build(const QStringList &args, const QString &group,
              const OptionIndex &index);

// Why a request Build allows still has to run as a separate rclone, given
// the daemon's config/dump answer and the NAME=value environment the daemon
// runs with, or empty when it can go to sync/*. A remote's section (a key or
// a value), an RCLONE_CONFIG_<REMOTE>_GLOBAL.<OPTION> variable or a path in
// an RCLONE_ variable can set a global. option, which rclone applies on the
// command line and skips for a job started over rc.
QString ConfigGlobalsReason(const QJsonObject &dump,
                            const QStringList &environment);

} // namespace RcSyncRequest
