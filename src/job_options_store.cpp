#include "job_options_store.h"

namespace {
const QString kStoreMagic = "RcloneBrowserNG.tasks";
constexpr qint32 kStoreSchemaVersion = 1;
constexpr qint32 kMaxTaskCount = 100000;

QDataStream &operator>>(QDataStream &in, JobOptions::Operation &e) {
  quint32 v;
  in >> v;
  e = static_cast<JobOptions::Operation>(v);
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::SyncTiming &e) {
  quint32 v;
  in >> v;
  e = static_cast<JobOptions::SyncTiming>(v);
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::CompareOption &e) {
  quint32 v;
  in >> v;
  e = static_cast<JobOptions::CompareOption>(v);
  return in;
}

QDataStream &operator>>(QDataStream &in, JobOptions::JobType &e) {
  quint32 v;
  in >> v;
  e = static_cast<JobOptions::JobType>(v);
  return in;
}

QDataStream &operator<<(QDataStream &stream, const JobOptions &jo) {
  stream << jo.myName() << JobOptions::classVersion << jo.description
         << jo.jobType << jo.operation << jo.sync << jo.syncTiming
         << jo.skipNewer << jo.skipExisting << jo.compare << jo.compareOption
         << jo.verbose << jo.sameFilesystem << jo.dontUpdateModified
         << jo.transfers << jo.checkers << jo.bandwidth << jo.minSize
         << jo.minAge << jo.maxAge << jo.maxDepth << jo.connectTimeout
         << jo.idleTimeout << jo.retries << jo.lowLevelRetries
         << jo.deleteExcluded << jo.excluded << jo.extra
         << jo.DriveSharedWithMe << jo.source << jo.dest << jo.isFolder
         << jo.uniqueId << jo.heartbeatUrl << jo.nameTransform
         << jo.preCommand << jo.postCommand << jo.webhookUrl
         << jo.watchFolder << jo.backupDir << jo.backupRetainCount
         << jo.conflictResolve;

  return stream;
}

void readJobOptions(QDataStream &stream, JobOptions &jo,
                    const QString &prefetchedName = QString()) {
  QString actualName = prefetchedName;
  if (actualName.isEmpty()) {
    stream >> actualName;
  }
  if (QString::compare(actualName, jo.myName()) != 0) {
    throw SerializationException("incorrect class");
  }

  qint32 actualVersion;
  stream >> actualVersion;
  if (actualVersion > JobOptions::classVersion) {
    throw SerializationException("stored version is newer");
  }

  stream >> jo.description >> jo.jobType >> jo.operation >> jo.sync >>
      jo.syncTiming >> jo.skipNewer >> jo.skipExisting >> jo.compare >>
      jo.compareOption >> jo.verbose >> jo.sameFilesystem >>
      jo.dontUpdateModified >> jo.transfers >> jo.checkers >> jo.bandwidth >>
      jo.minSize >> jo.minAge >> jo.maxAge >> jo.maxDepth >>
      jo.connectTimeout >> jo.idleTimeout >> jo.retries >>
      jo.lowLevelRetries >> jo.deleteExcluded >> jo.excluded >> jo.extra >>
      jo.DriveSharedWithMe >> jo.source >> jo.dest;

  if (actualVersion >= 2) {
    stream >> jo.isFolder;
    if (actualVersion >= 3) {
      stream >> jo.uniqueId;
      if (actualVersion >= 4) {
        stream >> jo.heartbeatUrl;
        if (actualVersion >= 5) {
          stream >> jo.nameTransform;
          if (actualVersion >= 6) {
            stream >> jo.preCommand >> jo.postCommand;
            if (actualVersion >= 7) {
              stream >> jo.webhookUrl;
              if (actualVersion >= 8) {
                stream >> jo.watchFolder;
                if (actualVersion >= 9) {
                  stream >> jo.backupDir >> jo.backupRetainCount >>
                      jo.conflictResolve;
                }
              }
            }
          }
        }
      }
    }
  }
  if (jo.uniqueId.isNull()) {
    jo.uniqueId = QUuid::createUuid();
  }

  if (stream.status() != QDataStream::Ok) {
    throw SerializationException("truncated task data");
  }
}

bool readOneTask(QDataStream &stream, QList<JobOptions *> *tasks,
                 QString *error, const QString &prefetchedName = QString()) {
  auto jo = new JobOptions();
  try {
    readJobOptions(stream, *jo, prefetchedName);
  } catch (SerializationException &e) {
    delete jo;
    if (error) {
      *error = e.Message;
    }
    return false;
  }
  tasks->append(jo);
  return true;
}
} // namespace

JobOptionsStoreLoadResult ReadJobOptionsStore(QIODevice *device) {
  JobOptionsStoreLoadResult result;
  QDataStream stream(device);
  stream.setVersion(QDataStream::Qt_5_2);

  if (stream.atEnd()) {
    return result;
  }

  QString first;
  stream >> first;
  if (stream.status() != QDataStream::Ok) {
    result.error = "failed to read task store header";
    return result;
  }

  if (first == kStoreMagic) {
    qint32 schemaVersion = 0;
    qint32 taskCount = 0;
    stream >> schemaVersion >> taskCount;
    if (stream.status() != QDataStream::Ok) {
      result.error = "failed to read task store schema";
      return result;
    }
    if (schemaVersion > kStoreSchemaVersion) {
      result.error = "stored task schema is newer";
      return result;
    }
    if (taskCount < 0 || taskCount > kMaxTaskCount) {
      result.error = "stored task count is invalid";
      return result;
    }

    for (qint32 i = 0; i < taskCount; i++) {
      if (!readOneTask(stream, &result.tasks, &result.error)) {
        ClearJobOptionsList(&result.tasks);
        return result;
      }
    }
    return result;
  }

  result.migratedFromLegacy = true;
  if (!readOneTask(stream, &result.tasks, &result.error, first)) {
    ClearJobOptionsList(&result.tasks);
    return result;
  }

  while (!stream.atEnd()) {
    if (!readOneTask(stream, &result.tasks, &result.error)) {
      ClearJobOptionsList(&result.tasks);
      return result;
    }
  }

  return result;
}

bool WriteJobOptionsStore(QIODevice *device, const QList<JobOptions *> &tasks,
                          QString *error) {
  if (error) {
    error->clear();
  }
  QDataStream stream(device);
  stream.setVersion(QDataStream::Qt_5_2);

  stream << kStoreMagic << kStoreSchemaVersion
         << static_cast<qint32>(tasks.size());
  for (const JobOptions *it : tasks) {
    stream << *it;
  }

  if (stream.status() != QDataStream::Ok) {
    if (error) {
      *error = "failed to write task store";
    }
    return false;
  }
  return true;
}

void ClearJobOptionsList(QList<JobOptions *> *tasks) {
  qDeleteAll(*tasks);
  tasks->clear();
}

namespace {
QJsonObject jobOptionsToJson(const JobOptions &jo) {
  QJsonObject obj;
  obj["description"] = jo.description;
  obj["jobType"] = static_cast<int>(jo.jobType);
  obj["operation"] = static_cast<int>(jo.operation);
  obj["sync"] = jo.sync;
  obj["syncTiming"] = static_cast<int>(jo.syncTiming);
  obj["skipNewer"] = jo.skipNewer;
  obj["skipExisting"] = jo.skipExisting;
  obj["compare"] = jo.compare;
  obj["compareOption"] = static_cast<int>(jo.compareOption);
  obj["verbose"] = jo.verbose;
  obj["sameFilesystem"] = jo.sameFilesystem;
  obj["dontUpdateModified"] = jo.dontUpdateModified;
  obj["transfers"] = jo.transfers;
  obj["checkers"] = jo.checkers;
  obj["bandwidth"] = jo.bandwidth;
  obj["minSize"] = jo.minSize;
  obj["minAge"] = jo.minAge;
  obj["maxAge"] = jo.maxAge;
  obj["maxDepth"] = jo.maxDepth;
  obj["connectTimeout"] = jo.connectTimeout;
  obj["idleTimeout"] = jo.idleTimeout;
  obj["retries"] = jo.retries;
  obj["lowLevelRetries"] = jo.lowLevelRetries;
  obj["deleteExcluded"] = jo.deleteExcluded;
  obj["excluded"] = jo.excluded;
  obj["extra"] = jo.extra;
  obj["source"] = jo.source;
  obj["dest"] = jo.dest;
  obj["isFolder"] = jo.isFolder;
  obj["uniqueId"] = jo.uniqueId.toString();
  obj["DriveSharedWithMe"] = jo.DriveSharedWithMe;
  obj["heartbeatUrl"] = jo.heartbeatUrl;
  obj["nameTransform"] = jo.nameTransform;
  obj["preCommand"] = jo.preCommand;
  obj["postCommand"] = jo.postCommand;
  obj["webhookUrl"] = jo.webhookUrl;
  obj["watchFolder"] = jo.watchFolder;
  obj["backupDir"] = jo.backupDir;
  obj["backupRetainCount"] = jo.backupRetainCount;
  obj["conflictResolve"] = jo.conflictResolve;
  return obj;
}

template <typename E>
static E clampEnum(int v, int maxVal) {
  if (v < 0 || v > maxVal)
    return static_cast<E>(0);
  return static_cast<E>(v);
}

JobOptions *jobOptionsFromJson(const QJsonObject &obj) {
  auto *jo = new JobOptions();
  jo->description = obj["description"].toString();
  jo->jobType = clampEnum<JobOptions::JobType>(obj["jobType"].toInt(), JobOptions::Download);
  jo->operation = clampEnum<JobOptions::Operation>(obj["operation"].toInt(), JobOptions::Bisync);
  jo->sync = obj["sync"].toBool();
  jo->syncTiming =
      clampEnum<JobOptions::SyncTiming>(obj["syncTiming"].toInt(), JobOptions::UnknownTiming);
  jo->skipNewer = obj["skipNewer"].toBool();
  jo->skipExisting = obj["skipExisting"].toBool();
  jo->compare = obj["compare"].toBool();
  jo->compareOption =
      clampEnum<JobOptions::CompareOption>(obj["compareOption"].toInt(), JobOptions::ChecksumIgnoreSize);
  jo->verbose = obj["verbose"].toBool();
  jo->sameFilesystem = obj["sameFilesystem"].toBool();
  jo->dontUpdateModified = obj["dontUpdateModified"].toBool();
  jo->transfers = obj["transfers"].toString();
  jo->checkers = obj["checkers"].toString();
  jo->bandwidth = obj["bandwidth"].toString();
  jo->minSize = obj["minSize"].toString();
  jo->minAge = obj["minAge"].toString();
  jo->maxAge = obj["maxAge"].toString();
  jo->maxDepth = obj["maxDepth"].toInt();
  jo->connectTimeout = obj["connectTimeout"].toString();
  jo->idleTimeout = obj["idleTimeout"].toString();
  jo->retries = obj["retries"].toString();
  jo->lowLevelRetries = obj["lowLevelRetries"].toString();
  jo->deleteExcluded = obj["deleteExcluded"].toBool();
  jo->excluded = obj["excluded"].toString();
  jo->extra = obj["extra"].toString();
  jo->source = obj["source"].toString();
  jo->dest = obj["dest"].toString();
  jo->isFolder = obj["isFolder"].toBool();
  jo->uniqueId = QUuid::fromString(obj["uniqueId"].toString());
  if (jo->uniqueId.isNull())
    jo->uniqueId = QUuid::createUuid();
  jo->DriveSharedWithMe = obj["DriveSharedWithMe"].toBool();
  jo->heartbeatUrl = obj["heartbeatUrl"].toString();
  jo->nameTransform = obj["nameTransform"].toString();
  jo->preCommand = obj["preCommand"].toString();
  jo->postCommand = obj["postCommand"].toString();
  jo->webhookUrl = obj["webhookUrl"].toString();
  jo->watchFolder = obj["watchFolder"].toBool();
  jo->backupDir = obj["backupDir"].toString();
  jo->backupRetainCount = obj["backupRetainCount"].toInt();
  jo->conflictResolve = obj["conflictResolve"].toString();
  return jo;
}
} // namespace

JobOptionsStoreLoadResult ReadJobOptionsStoreJson(QIODevice *device) {
  JobOptionsStoreLoadResult result;
  QByteArray data = device->readAll();
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    result.error = QString("invalid JSON task store at offset %1: %2")
                       .arg(parseError.offset)
                       .arg(parseError.errorString());
    return result;
  }
  if (!doc.isObject()) {
    result.error = "invalid JSON task store";
    return result;
  }
  QJsonObject root = doc.object();
  int version = root["version"].toInt(0);
  if (version > 1) {
    result.error = QString("JSON task store schema version %1 is newer than "
                           "supported (1); upgrade Rclone Browser NG")
                       .arg(version);
    return result;
  }
  const QJsonValue tasksValue = root.value("tasks");
  if (!tasksValue.isArray()) {
    result.error = "JSON task store is missing a tasks array";
    return result;
  }
  QJsonArray arr = tasksValue.toArray();
  for (int i = 0; i < arr.size(); ++i) {
    const QJsonValue val = arr.at(i);
    if (!val.isObject()) {
      ClearJobOptionsList(&result.tasks);
      result.error = QString("JSON task store task %1 is not an object").arg(i);
      return result;
    }
    result.tasks.append(jobOptionsFromJson(val.toObject()));
  }
  return result;
}

bool WriteJobOptionsStoreJson(QIODevice *device,
                              const QList<JobOptions *> &tasks,
                              QString *error) {
  QJsonArray arr;
  for (const JobOptions *jo : tasks) {
    arr.append(jobOptionsToJson(*jo));
  }
  QJsonObject root;
  root["version"] = 1;
  root["tasks"] = arr;
  QJsonDocument doc(root);
  QByteArray data = doc.toJson(QJsonDocument::Indented);
  if (device->write(data) != data.size()) {
    if (error)
      *error = "failed to write JSON task store";
    return false;
  }
  return true;
}
