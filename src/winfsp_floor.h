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

// True when the DLL's file version ("2.2.26112.0") is below the floor. An
// empty or unreadable version is not reported, so a failed read never sends
// anyone after an update they may not need.
bool IsBelowFloor(const QString &fileVersion);

// The warning shown once per installed version and floor. Names what the
// installed build is missing and where to get the fixed one.
QString Warning(const QString &fileVersion);

// What "already warned" is keyed on. Includes the floor, so raising it warns
// people again who were told about an older recommendation.
QString WarnedKey(const QString &fileVersion);

} // namespace WinFspFloor
