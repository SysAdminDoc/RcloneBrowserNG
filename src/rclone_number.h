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

// time.ParseDuration, which rclone uses for plain duration flags such as
// --retries-sleep: one or more <number><unit> ("1h30m", ".5s", "-1.5m")
// with units ns, us, µs, μs, ms, s, m and h, or a bare "0". No spaces, no
// bare numbers other than 0, and nothing past about 292 years.
bool ParseGoDuration(const QString &text, qint64 *nanoseconds);

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

// SizeSuffix.String: how rclone writes a size back out. -1 is "off", 0 is
// "0", under 1 KiB is the bare count, and anything bigger is scaled to the
// largest unit it reaches (Ki to Ei) with three decimals, or none when the
// result is whole, rounding a tie to even like Go ("1.500Ki", "2.062Mi",
// "1Ki").
QString FormatSize(qint64 bytes);

// SizeSuffix.stringRoundTrip: FormatSize with a B after a bare count, so it
// reads back as bytes. rclone writes its options out this way and reads
// them again before it runs, so a value whose text it then refuses, such
// as 7.9995E written as "8.000Ei", stops it even though ParseSize took it.
QString FormatSizeRoundTrip(qint64 bytes);

} // namespace RcloneNumber
