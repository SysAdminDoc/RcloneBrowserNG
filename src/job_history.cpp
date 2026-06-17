#include "job_history.h"
#include "rclone_capabilities.h"
#include "utils.h"

namespace {
const QString kHistoryFileName = "job-history.json";

QString toIso(const QDateTime &value) {
  return value.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime fromIso(const QJsonValue &value) {
  return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}

QJsonObject toObject(const JobHistoryEntry &entry) {
  QJsonObject obj;
  obj.insert("startedAt", toIso(entry.startedAt));
  obj.insert("finishedAt", toIso(entry.finishedAt));
  obj.insert("name", entry.name);
  obj.insert("source", entry.source);
  obj.insert("dest", entry.dest);
  obj.insert("success", entry.success);
  obj.insert("bytes", QString::number(entry.bytes));
  obj.insert("files", entry.files);
  obj.insert("errors", entry.errors);
  obj.insert("exitCode", entry.exitCode);
  if (!entry.transferDetail.isEmpty()) {
    QJsonArray detail;
    for (const QString &line : entry.transferDetail) {
      detail.append(line);
    }
    obj.insert("transferDetail", detail);
  }
  return obj;
}

JobHistoryEntry fromObject(const QJsonObject &obj) {
  JobHistoryEntry entry;
  entry.startedAt = fromIso(obj.value("startedAt"));
  entry.finishedAt = fromIso(obj.value("finishedAt"));
  entry.name = obj.value("name").toString();
  entry.source = obj.value("source").toString();
  entry.dest = obj.value("dest").toString();
  entry.success = obj.value("success").toBool();
  entry.bytes = obj.value("bytes").toVariant().toLongLong();
  entry.files = obj.value("files").toInt();
  entry.errors = obj.value("errors").toInt();
  entry.exitCode = obj.value("exitCode").toInt();
  QJsonArray detail = obj.value("transferDetail").toArray();
  for (const QJsonValue &val : detail) {
    entry.transferDetail.append(val.toString());
  }
  return entry;
}

QDir historyDir() {
  if (IsPortableMode()) {
#ifdef Q_OS_MACOS
    return QDir(qApp->applicationDirPath() + "/../../..");
#elif defined(Q_OS_WIN)
    return QDir(qApp->applicationDirPath());
#else
    const QString xdgConfigHome = qgetenv("XDG_CONFIG_HOME");
    return QDir(xdgConfigHome.isEmpty() ? qApp->applicationDirPath()
                                        : xdgConfigHome + "/rclone-browser");
#endif
  }

  return QDir(
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
}
} // namespace

QVector<JobHistoryEntry> ReadJobHistory(QIODevice *device, QString *error) {
  if (error) {
    error->clear();
  }

  QVector<JobHistoryEntry> entries;
  const QByteArray data = device->readAll();
  if (data.trimmed().isEmpty()) {
    return entries;
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    if (error) {
      *error = QString("Failed to parse job history at offset %1: %2")
                   .arg(parseError.offset)
                   .arg(parseError.errorString());
    }
    return {};
  }
  if (!doc.isArray()) {
    if (error) {
      *error = "Job history root was not a JSON array.";
    }
    return {};
  }

  for (const QJsonValue &value : doc.array()) {
    if (!value.isObject()) {
      if (error) {
        *error = "Job history contained a non-object entry.";
      }
      return {};
    }
    entries.append(fromObject(value.toObject()));
  }
  return entries;
}

bool WriteJobHistory(QIODevice *device, const QVector<JobHistoryEntry> &entries,
                     QString *error) {
  if (error) {
    error->clear();
  }

  QJsonArray array;
  for (const JobHistoryEntry &entry : entries) {
    array.append(toObject(entry));
  }

  const QByteArray data = QJsonDocument(array).toJson(QJsonDocument::Indented);
  if (device->write(data) != data.size()) {
    if (error) {
      *error = "Failed to write complete job history.";
    }
    return false;
  }
  return true;
}

QString JobHistoryStore::GetPersistenceFilePath() {
  QDir dir = historyDir();
  if (!dir.exists()) {
    dir.mkpath(".");
  }
  return dir.absoluteFilePath(kHistoryFileName);
}

QVector<JobHistoryEntry> JobHistoryStore::Load(QString *error) {
  QFile file(GetPersistenceFilePath());
  if (!file.exists()) {
    if (error) {
      error->clear();
    }
    return {};
  }
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Could not open job history.";
    }
    return {};
  }
  return ReadJobHistory(&file, error);
}

bool JobHistoryStore::Append(const JobHistoryEntry &entry, QString *error,
                             int maxEntries) {
  QVector<JobHistoryEntry> entries = Load(error);
  if (error && !error->isEmpty()) {
    return false;
  }

  entries.append(entry);
  while (entries.size() > maxEntries) {
    entries.removeFirst();
  }

  QSaveFile file(GetPersistenceFilePath());
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = "Could not open job history for writing.";
    }
    return false;
  }
  if (!WriteJobHistory(&file, entries, error)) {
    return false;
  }
  if (!file.commit()) {
    if (error) {
      *error = "Could not commit job history.";
    }
    return false;
  }
  return true;
}

QString RedactedJobDetail(const JobHistoryEntry &entry) {
  QStringList lines;
  lines << QString("Job: %1").arg(entry.name);
  lines << QString("Source: %1").arg(
      Diagnostics::redactSecrets(entry.source));
  lines << QString("Dest: %1").arg(
      Diagnostics::redactSecrets(entry.dest));
  lines << QString("Started: %1").arg(
      entry.startedAt.toLocalTime().toString(Qt::ISODate));
  lines << QString("Finished: %1").arg(
      entry.finishedAt.toLocalTime().toString(Qt::ISODate));
  lines << QString("Result: %1 (exit %2)")
               .arg(entry.success ? "success" : "failed")
               .arg(entry.exitCode);
  lines << QString("Files: %1  Bytes: %2  Errors: %3")
               .arg(entry.files)
               .arg(GetNiceSize(static_cast<quint64>(entry.bytes)))
               .arg(entry.errors);
  lines << "";
  if (entry.transferDetail.isEmpty()) {
    lines << "(No per-file detail recorded for this job.)";
  } else {
    lines << QString("Transfer detail (%1 entries):")
                 .arg(entry.transferDetail.size());
    for (const QString &line : entry.transferDetail) {
      lines << "  " + Diagnostics::redactSecrets(line);
    }
  }
  return lines.join('\n');
}
