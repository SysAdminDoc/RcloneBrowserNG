#pragma once

#include "pch.h"

struct StagedTransfer {
  QString message;
  QString source;
  QString dest;
  QStringList args;
  QString heartbeatUrl;
  QString preCommand;
  QString postCommand;
  QString webhookUrl;
  QString taskName;
  QString backupDirTemplate;
  int backupRetainCount = 0;
  bool verifyAfterTransfer = false;
  bool hooksTrusted = false;
};

namespace StagedTransferStore {
constexpr int kSchemaVersion = 2;

QJsonDocument Serialize(const QList<StagedTransfer> &transfers);
bool Deserialize(const QJsonDocument &document,
                 QList<StagedTransfer> *transfers, bool *migratedFromV1,
                 QString *error = nullptr);
} // namespace StagedTransferStore
