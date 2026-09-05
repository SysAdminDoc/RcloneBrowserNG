#pragma once

#include "pch.h"

// A scheduled transfer that fails at 3am leaves nothing behind: the Jobs tab
// error list and the support bundle both die with the process. This writes a
// rotating log to disk instead. Asked for three times upstream
// (kapitainsky#134, kapitainsky#233, mmozeiko#148).
namespace AppLog {

enum class Level {
  Off = 0,
  Error = 1,
  Warning = 2,
  Info = 3,
  Debug = 4,
};

// Installs the Qt message handler and mirrors Diagnostics::appendLog to the
// file. Safe to call once, early in main().
void Install();

// <AppLocalDataLocation>/logs, created on demand.
QString LogDirectory();
QString LogFilePath();

Level CurrentLevel();
void SetLevel(Level level);

// Round-trips through the settings value, so an unknown or missing name
// falls back to Info rather than silently turning logging off.
Level LevelFromName(const QString &name);
QString LevelName(Level level);
QStringList LevelNames();

// Writes one timestamped line, after redaction. Rotates first when the file
// has reached the size cap.
void Write(Level level, const QString &source, const QString &message);

void Flush();

// Size at which the log rotates, and how many older generations are kept.
int MaxBytes();
int Generations();

} // namespace AppLog
