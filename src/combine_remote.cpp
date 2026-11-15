#include "combine_remote.h"

#include <QSet>

namespace {
QString entryFor(const CombineUpstream &upstream) {
  return upstream.folder.trimmed() + '=' + upstream.remote.trimmed() + ':' +
         upstream.path.trimmed();
}

QString csvField(const QString &entry) {
  if (!entry.contains(' ') && !entry.contains('"')) {
    return entry;
  }
  QString quoted = entry;
  quoted.replace('"', QStringLiteral("\"\""));
  return '"' + quoted + '"';
}
} // namespace

QString CombineUpstreamsValue(const QList<CombineUpstream> &upstreams) {
  QStringList fields;
  fields.reserve(upstreams.size());
  for (const CombineUpstream &upstream : upstreams) {
    fields << csvField(entryFor(upstream));
  }
  return fields.join(' ');
}

QString CombineUpstreamsProblem(const QList<CombineUpstream> &upstreams) {
  if (upstreams.isEmpty()) {
    return "Tick at least one remote to put in the combined remote.";
  }
  QSet<QString> seen;
  for (const CombineUpstream &upstream : upstreams) {
    const QString folder = upstream.folder.trimmed();
    const QString remote = upstream.remote.trimmed();
    if (remote.isEmpty()) {
      return "Every row needs a remote.";
    }
    if (folder.isEmpty()) {
      return QString("Give %1 a folder name.").arg(remote);
    }
    if (folder == "." || folder == "..") {
      return QString("\"%1\" can't be used as a folder name.").arg(folder);
    }
    // rclone refuses '/' itself and splits the folder from the remote at the
    // first '='. The rest are characters a mounted Windows drive can't show,
    // and mounting the result is the usual reason to combine remotes.
    for (const QChar c : QStringLiteral("/\\=:*?\"<>|")) {
      if (folder.contains(c)) {
        return QString("Folder name \"%1\" can't contain %2.")
            .arg(folder, QString(c));
      }
    }
    // rclone itself only rejects exact repeats, but "Photos" and "photos"
    // are the same folder on a Windows drive.
    const QString key = folder.toCaseFolded();
    if (seen.contains(key)) {
      return QString("Two remotes use the folder name \"%1\". Each one needs "
                     "its own.")
          .arg(folder);
    }
    seen.insert(key);
  }
  return QString();
}

QStringList CombineCreateArguments(const QString &name,
                                   const QList<CombineUpstream> &upstreams) {
  return {"config",
          "create",
          name.trimmed(),
          "combine",
          "upstreams",
          CombineUpstreamsValue(upstreams),
          "--non-interactive"};
}
