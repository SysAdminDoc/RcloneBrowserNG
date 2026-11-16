#pragma once

#include <QString>
#include <QtGlobal>

// Numbers read the way rclone 1.76.0 reads them from flags and options.
// rclone is written in Go, so these follow Go's strconv and fmt rules
// (src/strconv/atoi.go, atof.go and src/fmt/scan.go) and rclone's
// fs/sizesuffix.go, and refuse exactly what those refuse.
namespace RcloneNumber {

// strconv.ParseInt(text, 0, 64): an optional sign, then decimal, 0b, 0o,
// 0x or a leading 0 for octal, with _ allowed between digits ("1_000",
// "0x_1F") and nothing else around the number.
bool ParseInt(const QString &text, qint64 *value);

// How rclone reads an integer option such as --max-delete (fmt.Sscanln):
// spaces may come before and after the number, so " 5" is 5, and a leading
// 0 means octal, so "010" is 8 and "08" is refused.
bool ScanInt(const QString &text, qint64 *value);

// strconv.ParseFloat(text, 64): decimal ("1.5", ".5", "5.", "1e3") or hex,
// which needs a p exponent ("0x1p4"), with _ between digits, plus Inf,
// Infinity and NaN in any case. False for anything else, and for a value
// too big for a double.
bool ParseFloat(const QString &text, double *value);

enum class SizeError {
  None,
  Empty,
  BadSuffix,
  BadNumber,
  NotANumber,
  Negative,
  TooBig,
};

// SizeSuffix.Set: "off" (any case) is -1, a bare number is KiB, b is bytes,
// and k, M, G, T, P and E (any case) can carry i or iB. Values of 8 EiB and
// up are refused. *bytes is set only when the result is SizeError::None.
SizeError ParseSize(const QString &text, qint64 *bytes);

} // namespace RcloneNumber
