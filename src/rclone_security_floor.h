#pragma once

#include "pch.h"

// The oldest rclone the app is willing to drive without warning, kept in one
// place with the date somebody last checked it against rclone's advisories.
// It used to be a bare "1.74.3" literal in main_window.cpp, which had already
// gone stale: 1.74.3 is itself affected by GHSA-fqj9-69pf-6pjg.
namespace RcloneSecurityFloor {

// Bump this together with kAdvisorySummary and kReviewedDate whenever rclone
// publishes advisories. scripts/release_check.py fails the release when
// kReviewedDate has aged past kReviewIntervalDays.
extern const char kMinimumVersion[];

// ISO-8601. The day the floor was last checked against
// https://github.com/rclone/rclone/security/advisories
extern const char kReviewedDate[];

// How long a review stays good before the release harness complains.
extern const int kReviewIntervalDays;

// True when `version` is older than the floor and the user should be warned.
// An unparseable or empty version is not warned about, because the caller
// cannot tell a broken probe from an old binary.
bool IsBelowFloor(const QString &version);

// One sentence naming what the floor protects against, for the warning shown
// to the user.
QString AdvisorySummary();

// The first rclone whose `selfupdate` checks the signature on the hash it
// trusts. Older ones take the archive hash from an unsigned prefix of
// SHA256SUMS (GHSA-w33h-3qgq-8mv8, high), so running their updater is the
// vulnerable path.
inline constexpr char kSelfUpdateFixedVersion[] = "1.75.2";

// True when the installed rclone can upgrade itself with `rclone selfupdate`
// without running the vulnerable updater. Takes the version as `rclone
// version` prints it ("v1.75.2", "1.76.0-DEV", "1.77.0-beta.9300.abc").
// A pre-release build counts only when it leads up to a release after
// 1.76.0: its number names the release it leads up to, not the fixes it
// has, and a 1.75.2 or 1.76.0 pre-release may predate the fix. An empty or
// unreadable version is false, since the download page is the safe offer
// when the app can't tell.
bool SelfUpdateIsSafe(const QString &version);

// One line for the update prompt saying why it offers the download page
// instead of selfupdate.
QString SelfUpdateUnsafeReason();

} // namespace RcloneSecurityFloor
