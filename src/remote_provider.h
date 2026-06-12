#pragma once

#include "pch.h"

struct RemoteProvider {
  QString name;
  QString prefix;
  QString description;
};

QVector<RemoteProvider> ParseRemoteProviders(const QByteArray &json,
                                             QString *error);
QString RemoteProviderDisplayName(const RemoteProvider &provider);
