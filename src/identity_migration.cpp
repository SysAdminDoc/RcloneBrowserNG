#include "identity_migration.h"

#include "app_identity.h"
#include "job_options_store.h"
#include "utils.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace {
const char kClassicTasksFile[] = "tasks.bin";
const char kTasksFile[] = "tasks.json";

// The files NG itself writes to its data folder. Classic RcloneBrowser only
// ever wrote tasks.bin there, which is handled separately and never copied.
const char *const kOwnFiles[] = {"tasks.json", "job-history.json"};

QString taskKey(const JobOptions *task) {
  // Classic tasks saved before uniqueId existed get a fresh id on every
  // read, so match those on what the task does as well.
  return task->description + QChar(0x1f) + task->source + QChar(0x1f) +
         task->dest;
}

void importClassicTasks(const QDir &legacyDir, const QDir &dataDir,
                        IdentityMigrationResult *result) {
  const QString classicPath = legacyDir.filePath(kClassicTasksFile);
  const QString renamedPath = classicPath + ".corrupt";
  // NG 2.0.2 renamed a tasks.bin it could not read, which took classic's
  // saved tasks away from classic too. Put it back, unless classic has
  // already started a new one.
  if (!QFileInfo::exists(classicPath) && QFileInfo::exists(renamedPath) &&
      QFile::rename(renamedPath, classicPath)) {
    result->restoredClassicTasksFile = true;
  }
  if (!QFileInfo::exists(classicPath)) {
    return;
  }

  QFile classicFile(classicPath);
  if (!classicFile.open(QIODevice::ReadOnly)) {
    result->classicTasksError = classicFile.errorString();
    return;
  }
  JobOptionsStoreLoadResult classic = ReadJobOptionsStore(&classicFile);
  classicFile.close();
  if (!classic.error.isEmpty()) {
    // Left exactly where it is. Classic may still read it.
    result->classicTasksError = classic.error;
    ClearJobOptionsList(&classic.tasks);
    return;
  }
  if (classic.tasks.isEmpty()) {
    return;
  }

  const QString tasksPath = dataDir.filePath(kTasksFile);
  QList<JobOptions *> tasks;
  if (QFileInfo::exists(tasksPath)) {
    QFile current(tasksPath);
    if (!current.open(QIODevice::ReadOnly)) {
      result->classicTasksError = current.errorString();
      ClearJobOptionsList(&classic.tasks);
      return;
    }
    JobOptionsStoreLoadResult loaded = ReadJobOptionsStoreJson(&current);
    current.close();
    if (!loaded.error.isEmpty()) {
      // Never overwrite a task file that cannot be read.
      result->classicTasksError =
          QString("%1 could not be read (%2)").arg(kTasksFile, loaded.error);
      ClearJobOptionsList(&loaded.tasks);
      ClearJobOptionsList(&classic.tasks);
      return;
    }
    tasks = loaded.tasks;
  }

  QSet<QUuid> ids;
  QSet<QString> keys;
  for (const JobOptions *task : tasks) {
    ids.insert(task->uniqueId);
    keys.insert(taskKey(task));
  }
  int imported = 0;
  for (JobOptions *task : classic.tasks) {
    if (ids.contains(task->uniqueId) || keys.contains(taskKey(task))) {
      delete task;
      continue;
    }
    ids.insert(task->uniqueId);
    keys.insert(taskKey(task));
    tasks.append(task);
    ++imported;
  }
  classic.tasks.clear();

  if (imported > 0) {
    QSaveFile out(tasksPath);
    QString error;
    if (out.open(QIODevice::WriteOnly) &&
        WriteJobOptionsStoreJson(&out, tasks, &error) && out.commit()) {
      result->classicTasksImported = imported;
    } else {
      out.cancelWriting();
      result->classicTasksError =
          error.isEmpty() ? out.errorString() : error;
    }
  }
  ClearJobOptionsList(&tasks);
}
} // namespace

bool IdentityMigrationResult::changedAnything() const {
  return settingsCopied > 0 || !filesCopied.isEmpty() ||
         classicTasksImported > 0 || restoredClassicTasksFile;
}

QString IdentityMigrationResult::summary() const {
  QStringList parts;
  if (settingsCopied > 0) {
    parts << QString("copied %1 settings").arg(settingsCopied);
  }
  if (!filesCopied.isEmpty()) {
    parts << QString("copied %1").arg(filesCopied.join(", "));
  }
  if (restoredClassicTasksFile) {
    parts << "renamed tasks.bin.corrupt back to tasks.bin";
  }
  if (classicTasksImported > 0) {
    parts << QString("imported %1 classic RcloneBrowser task(s)")
                 .arg(classicTasksImported);
  }
  if (!classicTasksError.isEmpty()) {
    parts << QString("left classic tasks.bin untouched: %1")
                 .arg(classicTasksError);
  }
  if (parts.isEmpty()) {
    return QString();
  }
  return "Moved off the settings shared with classic RcloneBrowser: " +
         parts.join("; ");
}

IdentityMigrationResult MigrateFromSharedIdentity(
    QSettings &legacySettings, QSettings &settings,
    const QString &legacyDataDir, const QString &dataDir,
    const IdentityMigrationOptions &options) {
  IdentityMigrationResult result;
  if (settings.contains(kIdentityMigrationKey)) {
    result.alreadyDone = true;
    return result;
  }

  if (options.copySettings && settings.allKeys().isEmpty()) {
    const QStringList keys = legacySettings.allKeys();
    for (const QString &key : keys) {
      settings.setValue(key, legacySettings.value(key));
      ++result.settingsCopied;
    }
  }

  const QDir legacyDir(legacyDataDir);
  const QDir targetDir(dataDir);
  const bool distinct =
      !legacyDataDir.isEmpty() && !dataDir.isEmpty() &&
      QDir::cleanPath(legacyDir.absolutePath())
              .compare(QDir::cleanPath(targetDir.absolutePath()),
                       Qt::CaseInsensitive) != 0;
  if (distinct && legacyDir.exists() &&
      (options.copyDataFiles || options.importClassicTasks)) {
    QDir().mkpath(targetDir.absolutePath());
    for (const char *name : kOwnFiles) {
      const QString from = legacyDir.filePath(name);
      const QString to = targetDir.filePath(name);
      if (options.copyDataFiles && QFileInfo::exists(from) &&
          !QFileInfo::exists(to) && QFile::copy(from, to)) {
        result.filesCopied << name;
      }
    }
    if (options.importClassicTasks) {
      importClassicTasks(legacyDir, targetDir, &result);
    }
  }

  settings.setValue(kIdentityMigrationKey,
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  settings.sync();
  return result;
}

IdentityMigrationResult RunSharedIdentityMigration() {
  const bool portable = IsPortableMode();

  // QStandardPaths builds the folder from the application identity, so
  // switch to the old pair just long enough to ask where it was.
  QCoreApplication::setOrganizationName(kLegacyOrganization);
  QCoreApplication::setApplicationName(kLegacyAppName);
  const QString legacyDataDir =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  ApplyAppIdentity();
  const QString dataDir =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

  QSettings legacySettings(QSettings::defaultFormat(), QSettings::UserScope,
                           kLegacyOrganization, kLegacyAppName);
  auto settings = GetSettings();

  IdentityMigrationOptions options;
  options.copySettings = !portable;
  options.importClassicTasks = !portable;
  options.copyDataFiles = !portable;
  return MigrateFromSharedIdentity(legacySettings, *settings, legacyDataDir,
                                   dataDir, options);
}
