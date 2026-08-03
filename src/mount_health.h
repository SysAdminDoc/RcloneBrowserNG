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
