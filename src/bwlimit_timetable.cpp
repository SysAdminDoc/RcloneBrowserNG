#include "bwlimit_timetable.h"

#include <QRegularExpression>

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

// One side of a rate, as rclone's SizeSuffix reads it: "off", or a
// non-negative number with an optional b, k, M, G, T, P or E suffix (any
// case), where k to E can carry "i" or "iB". A bare number is KiB.
QString SizeProblem(const QString &size) {
  if (size.isEmpty()) {
    return QStringLiteral("A rate is missing. Use a size like 512k or 10M, "
                          "or off.");
  }
  if (size.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0) {
    return QString();
  }
  static const QString kMultipliers = QStringLiteral("bkmgtpe");
  QString number = size;
  const QChar last = size.back().toLower();
  if (IsAsciiDigit(last) || last == QLatin1Char('.')) {
    // No suffix.
  } else if (last == QLatin1Char('b') && size.size() > 2 &&
             size.at(size.size() - 2) == QLatin1Char('i')) {
    if (!kMultipliers.contains(size.at(size.size() - 3).toLower())) {
      return QStringLiteral("\"%1\" isn't a size rclone knows.").arg(size);
    }
    number.chop(3);
  } else if (last == QLatin1Char('b')) {
    number.chop(1);
  } else if (last == QLatin1Char('i') && size.size() > 1) {
    if (!kMultipliers.contains(size.at(size.size() - 2).toLower())) {
      return QStringLiteral("\"%1\" isn't a size rclone knows.").arg(size);
    }
    number.chop(2);
  } else if (kMultipliers.contains(last)) {
    number.chop(1);
  } else {
    return QStringLiteral("\"%1\" ends in a unit rclone doesn't know. Use k, "
                          "M or G, or off.")
        .arg(size);
  }
  bool ok = false;
  const double value = number.toDouble(&ok);
  if (!ok || number.trimmed() != number) {
    return QStringLiteral("\"%1\" isn't a size rclone can read. Use a size "
                          "like 512k or 10M, or off.")
        .arg(size);
  }
  if (value < 0) {
    return QStringLiteral("A rate can't be negative.");
  }
  return QString();
}

} // namespace

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
  // A single value with no space or comma is just a rate, as in rclone.
  if (!value.contains(QLatin1Char(' ')) && !value.contains(QLatin1Char(','))) {
    return RateProblem(value);
  }
  static const QRegularExpression kSpace(QStringLiteral("\\s+"));
  for (const QString &entry : value.split(kSpace, Qt::SkipEmptyParts)) {
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
