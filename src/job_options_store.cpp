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
         << jo.preCommand << jo.postCommand << jo.webhookUrl;

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
