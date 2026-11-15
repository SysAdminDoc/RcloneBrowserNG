#pragma once

#include "pch.h"

std::unique_ptr<QSettings> GetSettings();

void ReadSettings(QSettings *settings, QObject *widget);
void WriteSettings(QSettings *settings, QObject *widget);

bool IsPortableMode();
// The ini that turns on portable mode on Windows: the app's own name with
// .ini, next to it. The password helper rclone runs answers to the app's ini.
QString WindowsPortableIniPath(const QString &applicationFilePath);
// Where portable mode keeps tasks.json and the logs folder: next to the
// executable (next to the .app bundle on macOS), or the AppImage's config
// folder on Linux.
QString PortableDataDirectory();

QString GetRclone();
void SetRclone(const QString &rclone);

QStringList GetRcloneConf();
void SetRcloneConf(const QString &rcloneConf);

// Reads the rclone path, the rclone.conf path and the password-command
// switch from the saved settings. The main window and a scheduled
// --run-task both start from these.
void LoadRcloneSettings();

void UseRclonePassword(QProcess *process);
void SetRclonePassword(const QString &rclonePassword);
bool IsRclonePasswordCommandEnabled();
void SetRclonePasswordCommandEnabled(bool enabled);
QString ReadRcloneConfigPassword(QString *error = nullptr);
void ClearRcloneConfigPassword();
bool IsRclonePasswordCommandRequest(const QStringList &arguments);

QStringList GetDriveSharedWithMe();
QStringList SplitRcloneOptions(const QString &options);
bool ParseHttpUrl(const QString &value, QUrl *url = nullptr,
                  QString *error = nullptr);
bool BuildBackupRetentionPlan(const QString &backupDirTemplate, int keepCount,
                              const QStringList &snapshotNames,
                              QString *parentPath,
                              QStringList *deleteTargets);
QStringList GetDefaultRcloneOptionsList();
QStringList GetDefaultExcludeList();
QStringList GetGlobalBandwidthLimit();
QStringList GetShowHidden();

QString GetNiceSize(quint64 size);

unsigned int compareVersion(std::string, std::string);

// Windows mount remote-control endpoint. The port is derived from the mount
// folder so the same mount can be located again at unmount time; a random
// per-mount credential authenticates the endpoint (see RcMountCredential).
quint16 GetRcMountPort(const QString &folder);
QString MakeRcPassword();
