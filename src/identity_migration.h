#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

struct IdentityMigrationOptions {
  // Standard mode only: a portable build keeps its settings in the .ini
  // next to the executable, which the identity never touched.
  bool copySettings = true;
  // Standard mode only, for the same reason: portable tasks live next to
  // the executable.
  bool importClassicTasks = true;
};

struct IdentityMigrationResult {
  bool alreadyDone = false;
  int settingsCopied = 0;
  QStringList filesCopied;
  int classicTasksImported = 0;
  bool restoredClassicTasksFile = false;
  QString classicTasksError;

  bool changedAnything() const;
  QString summary() const;
};

// One-time move away from the settings and data folder NG used to share
// with classic RcloneBrowser. Everything is copied, nothing is moved or
// deleted, so classic RcloneBrowser (and an older NG) keep what they had:
//
// - settings are copied only when NG has none of its own yet, so existing
//   users keep their preferences and nothing set under the new identity is
//   overwritten;
// - NG's own files (tasks.json, job-history.json) are copied when the new
//   folder does not have them;
// - classic's tasks.bin is read, never written, and its tasks are added to
//   NG's tasks.json unless a task with the same id is already there;
// - a tasks.bin.corrupt that NG 2.0.2 renamed is put back as tasks.bin when
//   classic has not written a new one since.
//
// A marker in `settings` makes later starts skip all of it.
IdentityMigrationResult MigrateFromSharedIdentity(
    QSettings &legacySettings, QSettings &settings,
    const QString &legacyDataDir, const QString &dataDir,
    const IdentityMigrationOptions &options = IdentityMigrationOptions());

// The startup entry point. Call it after ApplyAppIdentity() and before
// anything reads settings or saved tasks.
IdentityMigrationResult RunSharedIdentityMigration();

// Sized in the header: Qt 6.4's QAnyStringView can't take an array whose
// length the compiler can't see, which broke the build on Ubuntu 24.04.
inline constexpr char kIdentityMigrationKey[] = "Migration/sharedIdentity";
