#pragma once

#include "pch.h"

// rclone reports transfer progress as newline-delimited JSON on stderr under
// `--use-json-log`. Decoding and formatting live here, apart from the widget
// that displays them, so the schema this app depends on can be pinned by a
// test instead of only being exercised by a running transfer.
namespace JobStats {

struct TransferringFile {
  QString name;
  int percentage = 0;
  double speed = 0;
  double eta = 0;
};

struct Stats {
  bool present = false; // the line carried a "stats" object
  double bytes = 0;
  double totalBytes = 0;
  double speed = 0;
  double eta = 0;
  double elapsedTime = 0;
  int errors = 0;
  int checks = 0;
  int totalChecks = 0;
  int transfers = 0;
  int totalTransfers = 0;
  // Bytes the provider moved between its own remotes, which never
  // crossed this machine. Only non-zero with
  // --server-side-across-configs or a same-remote copy.
  int serverSideCopies = 0;
  qint64 serverSideCopyBytes = 0;
  QVector<TransferringFile> transferring;
};

struct LogLine {
  bool isJson = false;
  QString message;
  QString level;
  QString object;
  QString time;
  Stats stats;
};

LogLine ParseLogLine(const QByteArray &raw);

// "1h2m3s", "2m3s" or "3s". Empty for a non-positive duration.
QString FormatDuration(double seconds);

// Percentage complete, clamped: rclone can briefly report more bytes than
// totalBytes while a transfer is retried, and a progress bar above 100 reads
// as a bug.
int PercentComplete(double bytes, double totalBytes);

// "3 / 10" when a total is known, "3" when only a running count is, and empty
// when there is nothing to say.
QString FormatCount(int done, int total);

// Long transfer names are shortened from the middle so both the leading
// directories and the file name stay readable.
QString ElideTransferName(const QString &name);

} // namespace JobStats
