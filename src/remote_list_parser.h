#pragma once

#include "pch.h"

// The remotes list used to be read by splitting `listremotes --long` on ':'
// and demanding exactly two parts. rclone prints `name: type description`,
// so a remote carrying a description produced a garbage type, no icon and a
// misleading tooltip -- and a description containing a colon dropped the
// remote from the list entirely. `--json` avoids the whole problem and has
// been available since rclone v1.68.0.
namespace RemoteListParser {

struct Remote {
  QString name;
  QString type;
  QString description;
  // "file" or "environment"; only the JSON output carries it.
  QString source;
};

// Parses `listremotes --json`. Returns an empty list and sets `error` when
// the output is not the expected array of objects.
QVector<Remote> ParseJson(const QByteArray &output, QString *error = nullptr);

// Parses `listremotes --long`, for rclone older than 1.68. The name runs to
// the first colon (rclone forbids colons in remote names), the next
// whitespace-delimited token is the type, and whatever follows is the
// description, colons and all.
QVector<Remote> ParseLong(const QByteArray &output);

// Tooltip text for a remote: the type, plus the description when there is
// one worth showing.
QString TooltipFor(const Remote &remote);

} // namespace RemoteListParser
