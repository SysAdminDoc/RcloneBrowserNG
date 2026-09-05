#pragma once

#include <QStandardPaths>
#include <QString>

// Where the tests that need a real rclone find one.
//
// Several tests drive the app against a fixture rclone.conf and skip when
// there is no rclone to run. A skip is indistinguishable from a pass in the
// ctest summary, so those tests were quietly proving nothing on any machine
// where rclone is installed but not on PATH, which is the normal state after
// a winget install whose shim directory was never added. Setting
// RCLONE_BROWSER_TEST_RCLONE to the binary makes them run.
inline QString FindRcloneForTests() {
  const QByteArray configured = qgetenv("RCLONE_BROWSER_TEST_RCLONE");
  if (!configured.isEmpty()) {
    return QString::fromLocal8Bit(configured);
  }
  return QStandardPaths::findExecutable("rclone");
}
