#pragma once

#include "pch.h"

struct MountHealthProbeResult {
  bool healthy = false;
  QString detail;
};

// This performs filesystem metadata reads only. Callers that own the UI must
// run it away from the GUI thread because a disconnected network mount can
// make filesystem metadata calls slow.
MountHealthProbeResult ProbeMountPoint(const QString &folder);

struct LocalDiskInfo {
  QString path;
  QString fileSystem;
  qint64 used = 0;
  qint64 total = 0;
};

// Mounted volumes with a known size, without the pseudo filesystems Linux and
// macOS also list (proc, tmpfs, snap images, the macOS system volumes). Same
// rule as ProbeMountPoint: a disconnected network drive can stall it, so run
// it away from the GUI thread.
QList<LocalDiskInfo> ListLocalDisks();
