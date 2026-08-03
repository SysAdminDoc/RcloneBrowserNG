#include "staged_transfer.h"

#include "job_options_store.h"

namespace {

const QStringList kSensitiveKeys = {
    "heartbeatUrl", "preCommand", "postCommand", "webhookUrl"};

void setSensitive(QJsonObject *object, const QString &key,
                  const QString &value) {
  object->insert(key, ProtectJobOptionSensitiveValue(value));
}

QJsonObject toJson(const StagedTransfer &transfer) {
  QJsonObject object;
  object.insert("message", transfer.message);
  object.insert("source", transfer.source);
  object.insert("dest", transfer.dest);
  QJsonArray args;
  for (const QString &arg : transfer.args) {
    args.append(arg);
  }
  object.insert("args", args);
  setSensitive(&object, "heartbeatUrl", transfer.heartbeatUrl);
  setSensitive(&object, "preCommand", transfer.preCommand);
  setSensitive(&object, "postCommand", transfer.postCommand);
  setSensitive(&object, "webhookUrl", transfer.webhookUrl);
  object.insert("taskName", transfer.taskName);
  object.insert("backupDirTemplate", transfer.backupDirTemplate);
  object.insert("backupRetainCount", transfer.backupRetainCount);
  object.insert("verifyAfterTransfer", transfer.verifyAfterTransfer);
  object.insert("hooksTrusted", transfer.hooksTrusted);
  return object;
}

bool readString(const QJsonObject &object, const QString &key, QString *value,
               bool sensitive, bool legacy, QString *error) {
  const QJsonValue raw = object.value(key);
  if (raw.isUndefined() || raw.isNull()) {
    value->clear();
    return true;
  }
  if (!raw.isString()) {
    if (error) {
      *error = QString("staged field '%1' must be a string").arg(key);
    }
    return false;
  }

  const QString stored = raw.toString();
  if (!sensitive || legacy || stored.isEmpty()) {
    *value = stored;
    return true;
  }

  const bool encoded = stored.startsWith("dpapi:") || stored.startsWith("b64:");
  if (!encoded) {
    if (error) {
      *error = QString("staged sensitive field '%1' is not protected").arg(key);
    }
    return false;
  }
  const QString decoded = UnprotectJobOptionSensitiveValue(stored);
  if (stored.startsWith("dpapi:") && decoded == stored) {
    if (error) {
      *error = QString("staged sensitive field '%1' could not be decrypted")
                   .arg(key);
    }
    return false;
  }
  *value = decoded;
  return true;
}

bool readEntry(const QJsonObject &object, bool legacy, StagedTransfer *transfer,
               QString *error) {
  if (!object.value("message").isString() ||
      object.value("message").toString().isEmpty() ||
      !object.value("source").isString() ||
      !object.value("dest").isString() || !object.value("args").isArray()) {
    if (error) {
      *error = "staged entry is missing message, paths, or args";
    }
    return false;
  }

  transfer->message = object.value("message").toString();
  transfer->source = object.value("source").toString();
  transfer->dest = object.value("dest").toString();
  transfer->args.clear();
  for (const QJsonValue &value : object.value("args").toArray()) {
    if (!value.isString()) {
      if (error) {
        *error = "staged args must contain only strings";
      }
      return false;
    }
    transfer->args.append(value.toString());
  }

  for (const QString &key : kSensitiveKeys) {
    QString *destination = nullptr;
    if (key == "heartbeatUrl") {
      destination = &transfer->heartbeatUrl;
    } else if (key == "preCommand") {
      destination = &transfer->preCommand;
    } else if (key == "postCommand") {
      destination = &transfer->postCommand;
    } else {
      destination = &transfer->webhookUrl;
    }
    if (!readString(object, key, destination, true, legacy, error)) {
      return false;
    }
  }

  if (!readString(object, "taskName", &transfer->taskName, false, legacy,
                  error) ||
      !readString(object, "backupDirTemplate", &transfer->backupDirTemplate,
                  false, legacy, error)) {
    return false;
  }
  if (object.contains("backupRetainCount") &&
      !object.value("backupRetainCount").isDouble()) {
    if (error) {
      *error = "staged backupRetainCount must be numeric";
    }
    return false;
  }
  if (object.contains("verifyAfterTransfer") &&
      !object.value("verifyAfterTransfer").isBool()) {
    if (error) {
      *error = "staged verifyAfterTransfer must be boolean";
    }
    return false;
  }
  if (object.contains("hooksTrusted") &&
      !object.value("hooksTrusted").isBool()) {
    if (error) {
      *error = "staged hooksTrusted must be boolean";
    }
    return false;
  }
  transfer->backupRetainCount = object.value("backupRetainCount").toInt(0);
  transfer->verifyAfterTransfer =
      object.value("verifyAfterTransfer").toBool(false);
  transfer->hooksTrusted = object.value("hooksTrusted").toBool(false);
  return true;
}

} // namespace

namespace StagedTransferStore {

QJsonDocument Serialize(const QList<StagedTransfer> &transfers) {
  QJsonArray staged;
  for (const StagedTransfer &transfer : transfers) {
    staged.append(toJson(transfer));
  }
  QJsonObject root;
  root.insert("version", kSchemaVersion);
  root.insert("staged", staged);
  return QJsonDocument(root);
}

bool Deserialize(const QJsonDocument &document,
                 QList<StagedTransfer> *transfers, bool *migratedFromV1,
                 QString *error) {
  if (transfers) {
    transfers->clear();
  }
  if (migratedFromV1) {
    *migratedFromV1 = false;
  }
  if (error) {
    error->clear();
  }
  if (!transfers || !document.isObject()) {
    if (error) {
      *error = "staged store root must be an object";
    }
    return false;
  }

  const QJsonObject root = document.object();
  const int version = root.value("version").toInt(0);
  if (version != 1 && version != kSchemaVersion) {
    if (error) {
      *error = QString("unsupported staged store schema version %1").arg(version);
    }
    return false;
  }
  if (!root.value("staged").isArray()) {
    if (error) {
      *error = "staged store entries must be an array";
    }
    return false;
  }
  const bool legacy = version == 1;
  for (const QJsonValue &value : root.value("staged").toArray()) {
    if (!value.isObject()) {
      if (error) {
        *error = "staged store contains a non-object entry";
      }
      transfers->clear();
      return false;
    }
    StagedTransfer transfer;
    if (!readEntry(value.toObject(), legacy, &transfer, error)) {
      transfers->clear();
      return false;
    }
    transfers->append(transfer);
  }
  if (migratedFromV1) {
    *migratedFromV1 = legacy;
  }
  return true;
}

} // namespace StagedTransferStore
