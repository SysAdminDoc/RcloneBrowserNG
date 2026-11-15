#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// One remote placed inside a combine remote. It shows up as `folder` at the
// root of the combine remote and holds `remote:path`.
struct CombineUpstream {
  QString folder;
  // A configured remote name, without the colon.
  QString remote;
  // Inside the remote. Empty for its root.
  QString path;
};

// rclone's `upstreams` value for the combine backend: space separated
// `folder=remote:path` entries. rclone reads the list as CSV with a space for
// the comma, so an entry with a space or a quote in it is quoted as a whole
// and its quotes are doubled.
QString CombineUpstreamsValue(const QList<CombineUpstream> &upstreams);

// Empty when rclone will accept the list, otherwise a sentence saying what to
// change. rclone saves an unusable value without complaint and only fails
// when the remote is opened, so this has to catch it first.
QString CombineUpstreamsProblem(const QList<CombineUpstream> &upstreams);

// `config create <name> combine upstreams <value> --non-interactive`, without
// the --config flag.
QStringList CombineCreateArguments(const QString &name,
                                   const QList<CombineUpstream> &upstreams);
