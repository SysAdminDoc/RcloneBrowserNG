#include "list_of_job_options.h"
#include <QDataStream>
#include <qdir.h>
#include <qlogging.h>
#include <qstandardpaths.h>
#include <utils.h>

static QDataStream &operator>>(QDataStream &dataStream, JobOptions &jo);
static QDataStream &operator<<(QDataStream &dataStream, JobOptions &jo);
static QDataStream &operator>>(QDataStream &in, JobOptions::Operation &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::SyncTiming &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::CompareOption &e);
static QDataStream &operator>>(QDataStream &in, JobOptions::JobType &e);

ListOfJobOptions *ListOfJobOptions::SavedJobOptions = nullptr;
const QString ListOfJobOptions::persistenceFileName = "tasks.bin";

ListOfJobOptions::ListOfJobOptions() {}

ListOfJobOptions *ListOfJobOptions::getInstance() {
  if (SavedJobOptions == nullptr) {
    SavedJobOptions = new ListOfJobOptions();
    RestoreFromUserData(*SavedJobOptions);
  }
  return SavedJobOptions;
}

bool ListOfJobOptions::Persist(JobOptions *jo) {
  bool isNew = !this->tasks.contains(jo);
  if (isNew)
    this->tasks.append(jo);
  else {
    //    int ix = tasks.indexOf(jo);
    //    JobOptions *old = tasks[ix];
    //    qDebug() << QString("old [%1] New [%2]")
    //                    .arg(old->description)
    //                    .arg(jo->description);
  }
  PersistToUserData();
  return isNew;
}

bool ListOfJobOptions::Forget(JobOptions *jo) {
  bool isKnown = this->tasks.contains(jo);
  if (!isKnown)
    return false;
  int ix = tasks.indexOf(jo);
  tasks.removeAt(ix);
  //  qDebug() << QString("removed [%1]").arg(jo->description);
  PersistToUserData();
  return isKnown;
}

QString ListOfJobOptions::GetPersistenceFilePath() {

  QDir outputDir;

  if (IsPortableMode()) {
    // in portable mode tasks' file will be saved in the same folder as
    // excecutable
#ifdef Q_OS_MACOS
    // on macOS excecutable file is located in
    // ./rclone-browser.app/Contents/MasOS/
    // to get actual bundle folder we have
    // to traverse three levels up
    outputDir = QDir(qApp->applicationDirPath() + "/../../..");
#else
#ifdef Q_OS_WIN
    // not macOS
    outputDir = QDir(qApp->applicationDirPath());
#else
    QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
    outputDir = QDir(xdg_config_home + "/rclone-browser");
#endif
#endif

  } else {

    // get data location folder from Qt  - OS dependend
    outputDir = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation));
  }

  if (!outputDir.exists()) {
    outputDir.mkpath(".");
  }
  return outputDir.absoluteFilePath(persistenceFileName);
}

bool ListOfJobOptions::RestoreFromUserData(ListOfJobOptions &dataIn) {
  QString filePath = GetPersistenceFilePath();
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  QDataStream instream(&file);
  instream.setVersion(QDataStream::Qt_5_2);

  while (!instream.atEnd()) {
    JobOptions *jo = new JobOptions();
    try {
      instream >> *jo;
    } catch (SerializationException &e) {
      delete jo;
      file.close();

      // rename the bad file aside so the user doesn't lose it entirely
      QString corruptPath = filePath + ".corrupt";
      int n = 1;
      while (QFile::exists(corruptPath +
                           (n > 1 ? QString::number(n) : QString()))) {
        ++n;
      }
      corruptPath += (n > 1 ? QString::number(n) : QString());
      QFile::rename(filePath, corruptPath);

      dataIn.mLastLoadError =
          QString("Saved tasks file could not be loaded (%1).\n\n"
                  "The file has been renamed to:\n%2\n\n"
                  "Your saved tasks will need to be recreated.")
              .arg(e.Message, corruptPath);
      return false;
    }
    dataIn.tasks.append(jo);
  }

  return true;
}

bool ListOfJobOptions::PersistToUserData() {
  // QSaveFile writes to a temporary file and renames atomically on commit,
  // so a crash mid-write can no longer wipe every saved task
  QSaveFile file(GetPersistenceFilePath());
  if (!file.open(QIODevice::WriteOnly))
    return false;
  QDataStream outstream(&file);
  outstream.setVersion(QDataStream::Qt_5_2);

  for (JobOptions *it : tasks) {
    outstream << *it;
  }

  if (!file.commit())
    return false;

  emit tasksListUpdated();

  return true;
}

QDataStream &operator<<(QDataStream &stream, JobOptions &jo) {
  stream << jo.myName() << JobOptions::classVersion << jo.description
         << jo.jobType << jo.operation << /* jo.dryRun <<*/ jo.sync
         << jo.syncTiming << jo.skipNewer << jo.skipExisting << jo.compare
         << jo.compareOption << jo.verbose << jo.sameFilesystem
         << jo.dontUpdateModified << jo.transfers << jo.checkers << jo.bandwidth
         << jo.minSize << jo.minAge << jo.maxAge << jo.maxDepth
         << jo.connectTimeout << jo.idleTimeout << jo.retries
         << jo.lowLevelRetries << jo.deleteExcluded << jo.excluded << jo.extra
         << jo.DriveSharedWithMe << jo.source << jo.dest << jo.isFolder
         << jo.uniqueId;

  return stream;
}

QDataStream &operator>>(QDataStream &stream, JobOptions &jo) {
  QString actualName;
  qint32 actualVersion;

  stream >> actualName;
  if (QString::compare(actualName, jo.myName()) != 0)
    throw SerializationException("incorrect class");

  stream >> actualVersion;
  if (actualVersion > JobOptions::classVersion)
    throw SerializationException("stored version is newer");

  stream >> jo.description >> jo.jobType >> jo.operation >>
      /* jo.dryRun >> */ jo.sync >> jo.syncTiming >> jo.skipNewer >>
      jo.skipExisting >> jo.compare >> jo.compareOption >> jo.verbose >>
      jo.sameFilesystem >> jo.dontUpdateModified >> jo.transfers >>
      jo.checkers >> jo.bandwidth >> jo.minSize >> jo.minAge >> jo.maxAge >>
      jo.maxDepth >> jo.connectTimeout >> jo.idleTimeout >> jo.retries >>
      jo.lowLevelRetries >> jo.deleteExcluded >> jo.excluded >> jo.extra >>
      jo.DriveSharedWithMe >> jo.source >> jo.dest;

  // as fields are added in later revisions, check actualVersion here and
  // conditionally extract any new fields iff they are expected based on the
  // stream value
  if (actualVersion >= 2) {
    stream >> jo.isFolder;
    if (actualVersion >= 3) {
      stream >> jo.uniqueId;
    }
  }

  return stream;
}

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
