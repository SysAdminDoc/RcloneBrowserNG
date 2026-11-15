#pragma once

#include "pch.h"
#include "rc_sync_request.h"

// Deleting several selected items through one rclone job/batch call instead
// of one rclone job per item.
namespace RcDeleteBatch {

struct Item {
  // "Delete <path>" for the job card the per-item route shows.
  QString message;
  // The path as the user sees it, used to name an item that failed.
  QString label;
  // remote:path, exactly what the per-item command line ends with.
  QString target;
  bool folder = false;
  // The full rclone command line the per-item route runs for this item.
  QStringList args;
};

struct Plan {
  QList<Item> items;
  // Flags that belong to the remote's own backend, such as
  // --drive-use-trash. The batch carries them as connection string
  // parameters on each item's fs.
  QStringList backendFlags;
  // The user's default rclone options. They go in the batch's _config, and
  // anything that can't be expressed there keeps the delete on the per-item
  // route.
  QStringList extraOptions;
};

struct Request {
  bool usable = false;
  // Why the per-item route has to be used instead.
  QString reason;
  // The job/batch body, without _group and _async.
  QJsonObject payload;
  // One per input, in input order, for reporting failures.
  QStringList labels;
};

// The option index is the daemon's own options/info table, the same one the
// sync route uses, so a default option this rclone doesn't know is refused
// rather than dropped.
Request Build(const Plan &plan, const RcSyncRequest::OptionIndex &index);

// rclone reports a batch job as successful even when some of its inputs
// failed. The per-item outcome is only in output.results, one entry per
// input: {} on success, {"error": ...} on failure. This returns one
// "label: error" line per failed input, plus a line for each input that has
// no result at all.
QStringList Failures(const QJsonValue &output, const QStringList &labels);

} // namespace RcDeleteBatch
