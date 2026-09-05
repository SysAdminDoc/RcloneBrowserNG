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

} // namespace RcloneSecurityFloor
