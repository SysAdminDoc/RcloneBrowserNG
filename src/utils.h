#pragma once

#include "pch.h"

std::unique_ptr<QSettings> GetSettings();

void ReadSettings(QSettings *settings, QObject *widget);
void WriteSettings(QSettings *settings, QObject *widget);

bool IsPortableMode();

QString GetRclone();
void SetRclone(const QString &rclone);

QStringList GetRcloneConf();
void SetRcloneConf(const QString &rcloneConf);

void UseRclonePassword(QProcess *process);
void SetRclonePassword(const QString &rclonePassword);
bool IsRclonePasswordCommandEnabled();
void SetRclonePasswordCommandEnabled(bool enabled);
QString ReadRcloneConfigPassword(QString *error = nullptr);
void ClearRcloneConfigPassword();
bool IsRclonePasswordCommandRequest(const QStringList &arguments);

QStringList GetDriveSharedWithMe();
QStringList GetDefaultRcloneOptionsList();
QStringList GetShowHidden();

unsigned int compareVersion(std::string, std::string);

// Windows mount remote-control endpoint. The port is derived from the mount
// folder so the same mount can be located again at unmount time; a random
// per-mount credential authenticates the endpoint (see RcMountCredential).
quint16 GetRcMountPort(const QString &folder);
QString MakeRcPassword();
