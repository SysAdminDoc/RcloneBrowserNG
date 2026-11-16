#include "bwlimit_timetable.h"

#include "rclone_number.h"

namespace BwlimitTimetable {

namespace {

// rclone's parseWeekday: a three-letter or full English day name, any case.
bool IsWeekday(const QString &text) {
  static const QStringList kDays{"mon",    "monday",   "tue",     "tuesday",
                                 "wed",    "wednesday", "thu",    "thursday",
                                 "fri",    "friday",   "sat",     "saturday",
                                 "sun",    "sunday"};
  return kDays.contains(text.toLower());
}

bool IsAsciiDigit(QChar c) {
  return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

// One side of a rate, read by rclone's SizeSuffix.
QString SizeProblem(const QString &size) {
  using RcloneNumber::SizeError;
  switch (RcloneNumber::ParseSize(size, nullptr)) {
  case SizeError::None:
    return QString();
  case SizeError::Empty:
    return QStringLiteral("A rate is missing. Use a size like 512k or 10M, "
                          "or off.");
  case SizeError::BadSuffix:
    return QStringLiteral("\"%1\" ends in a unit rclone doesn't know. Use k, "
                          "M or G, or off.")
        .arg(size);
  case SizeError::BadNumber:
  case SizeError::NotANumber:
    return QStringLiteral("\"%1\" isn't a size rclone can read. Use a size "
                          "like 512k or 10M, or off.")
        .arg(size);
  case SizeError::Negative:
    return QStringLiteral("A rate can't be negative.");
  case SizeError::TooBig:
    return QStringLiteral("\"%1\" is more than rclone can hold. Keep it under "
                          "8E.")
        .arg(size);
  }
  return QString();
}

} // namespace

QStringList Entries(const QString &value) {
  QStringList entries;
  QString entry;
  for (const QChar c : value) {
    if (c == QLatin1Char(' ') || c == QLatin1Char(';')) {
      if (!entry.isEmpty()) {
        entries << entry;
        entry.clear();
      }
    } else {
      entry += c;
    }
  }
  if (!entry.isEmpty()) {
    entries << entry;
  }
  return entries;
}

bool IsSingleRate(const QString &value) {
  return !value.contains(QLatin1Char(' ')) && !value.contains(QLatin1Char(','));
}

QString TimeProblem(const QString &time) {
  static const QString kClockFormat =
      QStringLiteral("Write the time as HH:MM, like 08:00.");
  QString clock = time;
  const qsizetype dash = time.indexOf(QLatin1Char('-'));
  if (dash >= 0) {
    const QString day = time.left(dash);
    clock = time.mid(dash + 1);
    if (clock.contains(QLatin1Char('-'))) {
      return kClockFormat;
    }
    if (!IsWeekday(day)) {
      return QStringLiteral("\"%1\" isn't a day. Use Mon to Sun, or leave the "
                            "day and the dash off.")
          .arg(day);
    }
  }
  // rclone 1.76.0 refuses 0800, 8:00 and +8:00, which it used to take.
  if (clock.size() != 5 || clock.at(2) != QLatin1Char(':') ||
      !IsAsciiDigit(clock.at(0)) || !IsAsciiDigit(clock.at(1)) ||
      !IsAsciiDigit(clock.at(3)) || !IsAsciiDigit(clock.at(4))) {
    return kClockFormat;
  }
  if (clock.left(2).toInt() > 23) {
    return QStringLiteral("The hour must be 00 to 23.");
  }
  if (clock.mid(3).toInt() > 59) {
    return QStringLiteral("The minutes must be 00 to 59.");
  }
  return QString();
}

QString RateProblem(const QString &rate) {
  // upload:download. rclone splits on the colon and reads each side.
  const QStringList sides = rate.split(QLatin1Char(':'));
  if (sides.size() > 2) {
    return QStringLiteral("Use one rate, or upload:download like 10M:1M.");
  }
  for (const QString &side : sides) {
    const QString problem = SizeProblem(side);
    if (!problem.isEmpty()) {
      return problem;
    }
  }
  return QString();
}

QString Problem(const QString &value) {
  if (value.isEmpty()) {
    return QString();
  }
  if (IsSingleRate(value)) {
    return RateProblem(value);
  }
  for (const QString &entry : Entries(value)) {
    const QStringList halves = entry.split(QLatin1Char(','));
    if (halves.size() != 2) {
      return QStringLiteral("\"%1\" should be a time and a rate, like "
                            "08:00,512k.")
          .arg(entry);
    }
    QString problem = TimeProblem(halves.at(0));
    if (problem.isEmpty()) {
      problem = RateProblem(halves.at(1));
    }
    if (!problem.isEmpty()) {
      return QStringLiteral("%1: %2").arg(entry, problem);
    }
  }
  return QString();
}

} // namespace BwlimitTimetable
