#pragma once

#include <QJsonObject>
#include <QString>

QString JoinRemotePath(const QString &parentPath, const QString &childPath);
QString ChildRemotePathFromLsjson(const QString &parentPath,
                                  const QJsonObject &entry);
