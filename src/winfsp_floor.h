#pragma once

#include "pch.h"

// The oldest WinFsp the app mounts with without warning. It used to be a
// bare "2.1.25156" literal in main_window.cpp that recommended 2026 Beta1,
// which carries only the first of the 2026 vulnerability fixes.
namespace WinFspFloor {

// The first build carrying every published fix: 2026 Beta5, released
// 2026-10-13. Checked against https://github.com/winfsp/winfsp/releases on
// 2026-11-15. Every 2026 build is a prerelease, and the latest stable
// (WinFsp 2025, 2.1.25156) has none of the fixes.
inline constexpr char kMinimumVersion[] = "2.2.26286";
inline constexpr char kRecommendedRelease[] = "WinFsp 2026 Beta5";

// The recommended build's release page. winfsp.dev/rel and winget's
// WinFsp.WinFsp offer only the stable 2.1.25156, the build the warning is
// about, and GitHub's release list marks that one "Latest", so only the
// Beta5 page itself is hard to misread.
inline constexpr char kDownloadUrl[] =
    "https://github.com/winfsp/winfsp/releases/tag/v2.2B5";

// When the floor was last checked against the WinFsp releases, and how long
// that check stays good. scripts/release_check.py refuses to publish once it
// is older, as it does for the rclone floor.
inline constexpr char kReviewedDate[] = "2026-11-15";
inline constexpr int kReviewIntervalDays = 180;

// True when the DLL's file version ("2.2.26112.0") is below the floor. An
// empty, unreadable or all-zero version is not reported, so a failed read
// never sends anyone after an update they may not need.
bool IsBelowFloor(const QString &fileVersion);

// The warning shown once per installed version and floor. Names what the
// installed build is missing and where to get the fixed one.
QString Warning(const QString &fileVersion);

// What to tell someone who has no WinFsp at all: the build to install and
// where it is.
QString InstallAdvice();

// What "already warned" is keyed on. Includes the floor, so raising it warns
// people again who were told about an older recommendation.
QString WarnedKey(const QString &fileVersion);

// Where to look for the WinFsp DLL, best first: the bin folder under
// installDir (the folder the installer records), then the default install
// folders. Every architecture's DLL is in bin, ARM64 included.
QStringList DllCandidates(const QString &installDir);

#if defined(Q_OS_WIN32)
// The InstallDir the installer writes to the 32-bit registry view, or empty
// when WinFsp isn't installed. Only reads: a missing key stays missing.
QString InstallDirFromRegistry();
#endif

} // namespace WinFspFloor
