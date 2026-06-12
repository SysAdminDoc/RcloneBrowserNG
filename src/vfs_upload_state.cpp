#include "vfs_upload_state.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace {

VfsUploadState invalidState(const QString &error) {
  VfsUploadState state;
  state.error = error;
  return state;
}

bool hasNumericValue(const QJsonObject &object, const QString &key) {
  return object.contains(key) && object.value(key).isDouble();
}

int numericCount(const QJsonObject &object, const QString &key) {
  const double value = object.value(key).toDouble();
  return value > 0 ? static_cast<int>(value) : 0;
}

quint64 numericBytes(const QJsonObject &object, const QString &key) {
  const double value = object.value(key).toDouble();
  return value > 0 ? static_cast<quint64>(value) : 0;
}

QJsonObject parseObject(const QByteArray &json, VfsUploadState *state) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    *state = invalidState(QString("failed to parse rc JSON at offset %1: %2")
                              .arg(parseError.offset)
                              .arg(parseError.errorString()));
    return QJsonObject();
  }
  if (!doc.isObject()) {
    *state = invalidState("rc response was not a JSON object");
    return QJsonObject();
  }
  return doc.object();
}

} // namespace

VfsUploadState ParseVfsQueueState(const QByteArray &json) {
  VfsUploadState state;
  const QJsonObject root = parseObject(json, &state);
  if (!state.error.isEmpty()) {
    return state;
  }

  if (!root.contains("queue")) {
    state.valid = true;
    return state;
  }
  if (!root.value("queue").isArray()) {
    return invalidState("vfs/queue response did not contain a queue array");
  }

  state.valid = true;
  const QJsonArray queue = root.value("queue").toArray();
  state.pendingFiles = queue.size();
  for (const QJsonValue &value : queue) {
    if (!value.isObject()) {
      return invalidState("vfs/queue contained a non-object queue item");
    }

    const QJsonObject item = value.toObject();
    if (hasNumericValue(item, "size")) {
      state.pendingBytes += numericBytes(item, "size");
    } else {
      state.bytesKnown = false;
    }
  }
  return state;
}

VfsUploadState ParseVfsStatsUploadState(const QByteArray &json) {
  VfsUploadState state;
  const QJsonObject root = parseObject(json, &state);
  if (!state.error.isEmpty()) {
    return state;
  }

  const QJsonValue diskCacheValue = root.value("diskCache");
  if (diskCacheValue.isUndefined() || diskCacheValue.isNull()) {
    state.valid = true;
    return state;
  }
  if (!diskCacheValue.isObject()) {
    return invalidState("vfs/stats diskCache was not a JSON object");
  }

  state.valid = true;
  state.bytesKnown = false;
  const QJsonObject diskCache = diskCacheValue.toObject();
  state.pendingFiles += numericCount(diskCache, "uploadsQueued");
  state.pendingFiles += numericCount(diskCache, "uploadsInProgress");

  const QStringList byteKeys = {"dirtyBytes", "bytesToUpload",
                                "uploadsQueuedBytes",
                                "uploadsInProgressBytes"};
  for (const QString &key : byteKeys) {
    if (hasNumericValue(diskCache, key)) {
      if (!state.bytesKnown) {
        state.pendingBytes = 0;
        state.bytesKnown = true;
      }
      state.pendingBytes += numericBytes(diskCache, key);
    }
  }

  return state;
}

QString FormatUploadByteSize(quint64 size) {
  static const char prefix[] = "KMGTPE";
  for (int i = sizeof(prefix) - 2; i >= 0; i--) {
    const quint64 base = quint64(1) << ((i + 1) * 10);
    if (size >= base) {
      const double value = double(size) / double(base);
      return QString("%1 %2")
          .arg(value, 0, 'f', value >= 100 ? 0 : 1)
          .arg(QChar(prefix[i]));
    }
  }
  return QString("%1 B").arg(size);
}
