#pragma once

#include <QCoreApplication>

// Settings and per-user data live under this organisation and application
// pair: the registry key HKCU\Software\rclone-browser-ng\rclone-browser-ng,
// %LOCALAPPDATA%\rclone-browser-ng\rclone-browser-ng and their Linux and
// macOS equivalents.
//
// Up to 2.0.2 NG used classic RcloneBrowser's own pair, so the two builds
// read and wrote one registry key and one data folder. NG renamed classic's
// tasks.bin when it could not read it, and the RcloneBrowser Portable
// launcher rewrote NG's rclone and rclone.conf paths on every start (#17).
inline constexpr char kAppOrganization[] = "rclone-browser-ng";
inline constexpr char kAppName[] = "rclone-browser-ng";

// The pair NG shared with classic RcloneBrowser. Read once, by the
// migration in identity_migration.h, and never written.
inline constexpr char kLegacyOrganization[] = "rclone-browser";
inline constexpr char kLegacyAppName[] = "rclone-browser";

inline void ApplyAppIdentity() {
  QCoreApplication::setOrganizationName(kAppOrganization);
  QCoreApplication::setApplicationName(kAppName);
}
