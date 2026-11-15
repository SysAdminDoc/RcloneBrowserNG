#pragma once

#include <QJsonObject>
#include <QString>

QString JoinRemotePath(const QString &parentPath, const QString &childPath);
QString ChildRemotePathFromLsjson(const QString &parentPath,
                                  const QJsonObject &entry);
bool IsGooglePhotosRecursiveAlbumPath(const QString &path);

// The remote name a pane uses to browse this computer's own disks: rclone's
// on-the-fly local backend, so joins come out as ":local:C:/Users" and need
// no entry in rclone.conf. A bare "/" in front of the colon is not a remote;
// rclone reads "/:C:/" as a local path that does not exist.
QString LocalFilesystemRemote();

// Splits a saved "remote:path" target such as a bookmark. An on-the-fly
// backend (":local:C:/x") has its first colon at index 0, which a plain
// indexOf(':') reads as "no remote name". A "/" remote saved by the old
// local pane comes back as the local backend so the bookmark still opens.
bool SplitRemoteTarget(const QString &target, QString *remote, QString *path);
