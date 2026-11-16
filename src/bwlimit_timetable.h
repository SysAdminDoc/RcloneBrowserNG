#pragma once

#include <QString>
#include <QStringList>

// Checks a --bwlimit value the way rclone v1.76.0 parses it, so a timetable
// rclone would refuse is caught in the editor instead of stopping the job
// at start. Each function returns an empty string when rclone accepts the
// text, else one sentence saying what to fix.
namespace BwlimitTimetable {

// The time half of an entry: HH:MM, optionally after a weekday and a dash
// (Mon-08:00, monday-08:00).
QString TimeProblem(const QString &time);

// The rate half of an entry, or a whole value with no times: off, 512k,
// 10M, 1.5Mi, 10MiB, a bare number in KiB, or upload:download (10M:1M).
QString RateProblem(const QString &rate);

// A whole --bwlimit value: one rate, or entries "time,rate" separated by
// spaces. Empty text means no limit and is fine.
QString Problem(const QString &value);

} // namespace BwlimitTimetable
