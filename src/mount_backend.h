#pragma once

#include "pch.h"

struct MacMountBackendFacts {
  QString macFuseVersion;
  bool fuseTInstalled = false;
  bool nfsMountSupported = false;
  int macOsMajorVersion = 0;
  QStringList userMountOptions;
};

struct MountBackendPlan {
  QString command = "mount";
  QStringList argsBeforeRemote;
  QString backendName;
  QString warningKey;
  QString warningVersion;
  QString warningTitle;
  QString warningText;
};

QString DetectMacFuseVersion();
bool DetectFuseTInstalled();
bool IsMacOs26OrNewer();
bool RcloneCommandSupported(const QString &rclone, const QString &command);
bool MountOptionsContainFuseBackend(const QStringList &options);
MountBackendPlan PlanMacMountBackend(const MacMountBackendFacts &facts);
