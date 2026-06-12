#include "list_of_job_options.h"
#include "job_options_store.h"
#include <QDataStream>
#include <qdir.h>
#include <qlogging.h>
#include <qstandardpaths.h>
#include <utils.h>

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

  JobOptionsStoreLoadResult loaded = ReadJobOptionsStore(&file);
  if (!loaded.error.isEmpty()) {
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
            .arg(loaded.error, corruptPath);
    return false;
  }

  ClearJobOptionsList(&dataIn.tasks);
  dataIn.tasks = loaded.tasks;
  if (loaded.migratedFromLegacy) {
    dataIn.PersistToUserData();
  }
  return true;
}

bool ListOfJobOptions::PersistToUserData() {
  // QSaveFile writes to a temporary file and renames atomically on commit,
  // so a crash mid-write can no longer wipe every saved task
  QSaveFile file(GetPersistenceFilePath());
  if (!file.open(QIODevice::WriteOnly))
    return false;
  QString error;
  if (!WriteJobOptionsStore(&file, tasks, &error))
    return false;

  if (!file.commit())
    return false;

  emit tasksListUpdated();

  return true;
}
