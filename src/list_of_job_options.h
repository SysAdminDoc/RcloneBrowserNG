#pragma once
#include "job_options.h"
#include <qfile.h>

class ListOfJobOptions : public QObject {
  Q_OBJECT

protected:
  ~ListOfJobOptions() = default;
  ListOfJobOptions();

public:
  static ListOfJobOptions *getInstance();
  bool Persist(JobOptions *jo);
  bool Forget(JobOptions *jo);
  QList<JobOptions *> &getTasks() { return tasks; }
  QString lastLoadError() const { return mLastLoadError; }

signals:
  void tasksListUpdated();

private:
  static ListOfJobOptions *SavedJobOptions;
  static const QString persistenceFileName;
  static bool RestoreFromUserData(ListOfJobOptions &dataIn);
  static QString GetPersistenceFilePath();

  QList<JobOptions *> tasks;
  QString mLastLoadError;
  bool PersistToUserData();
};
