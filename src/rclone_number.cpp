#include "rclone_number.h"

#include <QtAlgorithms>
#include <cmath>
#include <limits>

namespace RcloneNumber {

namespace {

bool IsDigit(char16_t c) { return c >= u'0' && c <= u'9'; }

// Go's lower(): only meaningful for letters, which is all it's used for.
char16_t Lower(char16_t c) { return static_cast<char16_t>(c | 0x20); }

bool IsHexLetter(char16_t c) { return Lower(c) >= u'a' && Lower(c) <= u'f'; }

// fmt's isSpace table (src/fmt/scan.go).
bool IsGoSpace(char16_t c) {
  return (c >= 0x0009 && c <= 0x000d) || c == 0x0020 || c == 0x0085 ||
         c == 0x00a0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
         c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f ||
         c == 0x3000;
}

// strconv's underscoreOK: _ only between digits, or between a base prefix
// and a digit.
bool UnderscoreOk(QStringView s) {
  char saw = '^';
  qsizetype i = 0;
  if (!s.isEmpty() && (s[0] == u'-' || s[0] == u'+')) {
    s = s.mid(1);
  }
  bool hex = false;
  if (s.size() >= 2 && s[0] == u'0' &&
      (Lower(s[1].unicode()) == u'b' || Lower(s[1].unicode()) == u'o' ||
       Lower(s[1].unicode()) == u'x')) {
    i = 2;
    saw = '0';
    hex = Lower(s[1].unicode()) == u'x';
  }
  for (; i < s.size(); i++) {
    const char16_t c = s[i].unicode();
    if (IsDigit(c) || (hex && IsHexLetter(c))) {
      saw = '0';
      continue;
    }
    if (c == u'_') {
      if (saw != '0') {
        return false;
      }
      saw = '_';
      continue;
    }
    if (saw == '_') {
      return false;
    }
    saw = '!';
  }
  return saw != '_';
}

// strconv.ParseUint(s, 0, 64).
bool ParseUint(QStringView s, quint64 *value) {
  if (s.isEmpty()) {
    return false;
  }
  const QStringView s0 = s;
  int base = 10;
  if (s[0] == u'0') {
    const char16_t prefix = s.size() >= 3 ? Lower(s[1].unicode()) : 0;
    if (prefix == u'b') {
      base = 2;
      s = s.mid(2);
    } else if (prefix == u'o') {
      base = 8;
      s = s.mid(2);
    } else if (prefix == u'x') {
      base = 16;
      s = s.mid(2);
    } else {
      base = 8;
      s = s.mid(1);
    }
  }
  bool underscores = false;
  quint64 n = 0;
  for (const QChar ch : s) {
    const char16_t c = ch.unicode();
    int digit = 0;
    if (c == u'_') {
      underscores = true;
      continue;
    } else if (IsDigit(c)) {
      digit = c - u'0';
    } else if (Lower(c) >= u'a' && Lower(c) <= u'z') {
      digit = Lower(c) - u'a' + 10;
    } else {
      return false;
    }
    if (digit >= base) {
      return false;
    }
    const quint64 limit = std::numeric_limits<quint64>::max();
    if (n > (limit - static_cast<quint64>(digit)) / static_cast<quint64>(base)) {
      return false; // out of range
    }
    n = n * static_cast<quint64>(base) + static_cast<quint64>(digit);
  }
  if (underscores && !UnderscoreOk(s0)) {
    return false;
  }
  *value = n;
  return true;
}

// strconv's special(): inf, infinity and nan, any case. Only a match of the
// whole text counts, since ParseFloat refuses anything left over.
bool Special(QStringView s, double *value) {
  double sign = 1;
  QStringView rest = s;
  if (!rest.isEmpty() && (rest[0] == u'+' || rest[0] == u'-')) {
    sign = rest[0] == u'-' ? -1 : 1;
    rest = rest.mid(1);
    if (rest.compare(QLatin1String("inf"), Qt::CaseInsensitive) == 0 ||
        rest.compare(QLatin1String("infinity"), Qt::CaseInsensitive) == 0) {
      *value = sign * std::numeric_limits<double>::infinity();
      return true;
    }
    return false;
  }
  if (rest.compare(QLatin1String("inf"), Qt::CaseInsensitive) == 0 ||
      rest.compare(QLatin1String("infinity"), Qt::CaseInsensitive) == 0) {
    *value = std::numeric_limits<double>::infinity();
    return true;
  }
  if (rest.compare(QLatin1String("nan"), Qt::CaseInsensitive) == 0) {
    *value = std::numeric_limits<double>::quiet_NaN();
    return true;
  }
  return false;
}

// mantissa * 2^exponent rounded to the nearest double, ties to even, the
// way strconv's atofHex does it. truncated says hex digits past the 16 kept
// in mantissa weren't all zero, so a value that looks like a tie rounds up.
// The rounding is done here rather than by ldexp, which rounds a second
// time when the result is subnormal.
double HexToDouble(quint64 mantissa, bool truncated, int exponent) {
  if (mantissa == 0) {
    return 0;
  }
  const int shift = qCountLeadingZeroBits(mantissa);
  mantissa <<= shift;
  exponent -= shift;
  const int topExponent = exponent + 63;
  // 53 bits for a normal double, fewer as the value sinks below 2^-1022.
  const int keep = topExponent >= -1022 ? 53 : 53 - (-1022 - topExponent);
  if (keep < 0) {
    return 0; // under half the smallest subnormal
  }
  const int drop = 64 - keep;
  quint64 kept = drop == 64 ? 0 : mantissa >> drop;
  const bool roundBit = ((mantissa >> (drop - 1)) & 1) != 0;
  const quint64 restMask = drop - 1 == 0 ? 0 : (quint64(1) << (drop - 1)) - 1;
  const bool rest = (mantissa & restMask) != 0 || truncated;
  if (roundBit && (rest || (kept & 1) != 0)) {
    kept++;
  }
  // kept fits in 54 bits and the scale lands on a whole bit, so this is
  // exact; past the top it gives infinity, which is the range error.
  return std::ldexp(static_cast<double>(kept), exponent + drop);
}

} // namespace

bool ParseInt(const QString &text, qint64 *value) {
  QStringView s(text);
  if (s.isEmpty()) {
    return false;
  }
  bool negative = false;
  if (s[0] == u'+') {
    s = s.mid(1);
  } else if (s[0] == u'-') {
    negative = true;
    s = s.mid(1);
  }
  quint64 magnitude = 0;
  if (!ParseUint(s, &magnitude)) {
    return false;
  }
  const quint64 cutoff = quint64(1) << 63;
  if ((!negative && magnitude >= cutoff) || (negative && magnitude > cutoff)) {
    return false;
  }
  if (value) {
    *value = negative ? static_cast<qint64>(0 - magnitude)
                      : static_cast<qint64>(magnitude);
  }
  return true;
}

bool ScanInt(const QString &text, qint64 *value) {
  // fmt.Sscanln(text, &int64): SkipSpace, an optional sign, a base prefix
  // (a lone leading 0 means octal), the digits that base allows plus _,
  // then strconv.ParseInt on that token, then only spaces up to a newline
  // or the end.
  const qsizetype n = text.size();
  qsizetype pos = 0;
  while (pos < n) {
    const char16_t c = text.at(pos).unicode();
    if (c == u'\r' && pos + 1 < n && text.at(pos + 1) == u'\n') {
      pos++;
      continue;
    }
    if (c == u'\n') {
      return false; // "unexpected newline"
    }
    if (!IsGoSpace(c)) {
      break;
    }
    pos++;
  }
  if (pos == n) {
    return false; // EOF
  }
  QString token;
  if (text.at(pos) == u'+' || text.at(pos) == u'-') {
    token += text.at(pos++);
  }
  QString digits = QStringLiteral("0123456789_");
  bool haveDigits = false;
  if (pos < n && text.at(pos) == u'0') {
    token += text.at(pos++);
    haveDigits = true;
    const char16_t next = pos < n ? text.at(pos).unicode() : 0;
    if (next == u'b' || next == u'B') {
      token += text.at(pos++);
      digits = QStringLiteral("01_");
    } else if (next == u'o' || next == u'O') {
      token += text.at(pos++);
      digits = QStringLiteral("01234567_");
    } else if (next == u'x' || next == u'X') {
      token += text.at(pos++);
      digits = QStringLiteral("0123456789aAbBcCdDeEfF_");
    } else {
      digits = QStringLiteral("01234567_");
    }
  }
  if (!haveDigits && (pos == n || !digits.contains(text.at(pos)))) {
    return false; // "expected integer"
  }
  while (pos < n && digits.contains(text.at(pos))) {
    token += text.at(pos++);
  }
  qint64 parsed = 0;
  if (!ParseInt(token, &parsed)) {
    return false;
  }
  while (pos < n) {
    const char16_t c = text.at(pos++).unicode();
    if (c == u'\n') {
      break;
    }
    if (!IsGoSpace(c)) {
      return false; // "expected newline"
    }
  }
  if (value) {
    *value = parsed;
  }
  return true;
}

bool ParseFloat(const QString &text, double *value) {
  const QStringView s(text);
  double special = 0;
  if (Special(s, &special)) {
    if (value) {
      *value = special;
    }
    return true;
  }
  // strconv's readFloat.
  const qsizetype n = s.size();
  qsizetype i = 0;
  bool underscores = false;
  bool negative = false;
  if (i >= n) {
    return false;
  }
  if (s[i] == u'+') {
    i++;
  } else if (s[i] == u'-') {
    i++;
    negative = true;
  }
  quint64 base = 10;
  int maxMantDigits = 19;
  char16_t expChar = u'e';
  bool hex = false;
  if (i + 2 < n && s[i] == u'0' && Lower(s[i + 1].unicode()) == u'x') {
    base = 16;
    maxMantDigits = 16;
    i += 2;
    expChar = u'p';
    hex = true;
  }
  bool sawDot = false;
  bool sawDigits = false;
  int nd = 0;
  int ndMant = 0;
  int dp = 0;
  quint64 mantissa = 0;
  bool truncated = false;
  // Every significant decimal digit, for the correctly rounded conversion.
  QByteArray significant;
  for (; i < n; i++) {
    const char16_t c = s[i].unicode();
    if (c == u'_') {
      underscores = true;
      continue;
    }
    if (c == u'.') {
      if (sawDot) {
        break;
      }
      sawDot = true;
      dp = nd;
      continue;
    }
    if (IsDigit(c)) {
      sawDigits = true;
      if (c == u'0' && nd == 0) {
        dp--;
        continue;
      }
      nd++;
      if (!hex) {
        significant.append(static_cast<char>(c));
      }
      if (ndMant < maxMantDigits) {
        mantissa = mantissa * base + static_cast<quint64>(c - u'0');
        ndMant++;
      } else if (c != u'0') {
        truncated = true;
      }
      continue;
    }
    if (base == 16 && IsHexLetter(c)) {
      sawDigits = true;
      nd++;
      if (ndMant < maxMantDigits) {
        mantissa = mantissa * 16 + static_cast<quint64>(Lower(c) - u'a' + 10);
        ndMant++;
      } else {
        truncated = true;
      }
      continue;
    }
    break;
  }
  if (!sawDigits) {
    return false;
  }
  if (!sawDot) {
    dp = nd;
  }
  if (base == 16) {
    dp *= 4;
    ndMant *= 4;
  }
  if (i < n && Lower(s[i].unicode()) == expChar) {
    i++;
    if (i >= n) {
      return false;
    }
    int esign = 1;
    if (s[i] == u'+') {
      i++;
    } else if (s[i] == u'-') {
      i++;
      esign = -1;
    }
    if (i >= n || !IsDigit(s[i].unicode())) {
      return false;
    }
    int e = 0;
    for (; i < n && (IsDigit(s[i].unicode()) || s[i] == u'_'); i++) {
      if (s[i] == u'_') {
        underscores = true;
        continue;
      }
      if (e < 10000) {
        e = e * 10 + (s[i].unicode() - u'0');
      }
    }
    dp += e * esign;
  } else if (hex) {
    return false; // a hex float needs its p exponent
  }
  if (underscores && !UnderscoreOk(s.left(i))) {
    return false;
  }
  if (i != n) {
    return false; // ParseFloat refuses anything left over
  }
  // strconv rounds correctly, so the value can't be built from the 19
  // digits kept above: 0.9999999999999999444 would come out as 1.
  double result = 0;
  if (hex) {
    result = HexToDouble(mantissa, truncated,
                         mantissa != 0 ? dp - ndMant : 0);
  } else if (mantissa != 0) {
    // The value is 0.<significant> * 10^dp, with dp holding strconv's own
    // exponent (it stops reading the exponent's digits at 10000). Qt's
    // double-conversion rounds that correctly.
    significant.append('e').append(QByteArray::number(dp - nd));
    bool ok = false;
    result = significant.toDouble(&ok);
    if (!ok) {
      // Qt gives 0 both ways; the exponent says which. strconv calls
      // overflow a range error and reads underflow as zero.
      if (dp > 0) {
        return false;
      }
      result = 0;
    }
  }
  if (!std::isfinite(result)) {
    return false; // ErrRange
  }
  if (value) {
    *value = negative ? -result : result;
  }
  return true;
}

SizeError ParseSize(const QString &text, qint64 *bytes) {
  if (text.isEmpty()) {
    return SizeError::Empty;
  }
  if (text.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0) {
    if (bytes) {
      *bytes = -1;
    }
    return SizeError::None;
  }
  auto multiplierFor = [](QChar symbol, double *multiplier) {
    const qsizetype at = QStringLiteral("kmgtpe").indexOf(
        QChar(Lower(symbol.unicode())));
    if (symbol.unicode() > 0x7f || at < 0) {
      return false;
    }
    *multiplier = std::ldexp(1.0, 10 * static_cast<int>(at + 1));
    return true;
  };
  const qsizetype size = text.size();
  const QChar last = text.back();
  double multiplier = 1024;
  qsizetype suffix = 0;
  if (IsDigit(last.unicode()) || last == u'.') {
    // A bare number is KiB.
  } else if (last == u'b' || last == u'B') {
    if (size > 2 && text.at(size - 2) == u'i') {
      if (!multiplierFor(text.at(size - 3), &multiplier)) {
        return SizeError::BadSuffix;
      }
      suffix = 3;
    } else {
      multiplier = 1;
      suffix = 1;
    }
  } else if (last == u'i' || last == u'I') {
    if (size < 2 || !multiplierFor(text.at(size - 2), &multiplier)) {
      return SizeError::BadSuffix;
    }
    suffix = 2;
  } else {
    if (!multiplierFor(last, &multiplier)) {
      return SizeError::BadSuffix;
    }
    suffix = 1;
  }
  double value = 0;
  if (!ParseFloat(text.left(size - suffix), &value)) {
    return SizeError::BadNumber;
  }
  if (std::isnan(value)) {
    return SizeError::NotANumber;
  }
  if (value < 0) {
    return SizeError::Negative;
  }
  value *= multiplier;
  // float64(math.MaxInt64) is 2^63.
  if (value >= 9223372036854775808.0) {
    return SizeError::TooBig;
  }
  if (bytes) {
    *bytes = static_cast<qint64>(value);
  }
  return SizeError::None;
}

// Go's time.ParseDuration (src/time/format.go), byte for byte: the units
// are matched as UTF-8, and the integer and fraction parts overflow
// exactly where Go's do.
bool ParseGoDuration(const QString &text, qint64 *nanoseconds) {
  constexpr quint64 kLimit = quint64(1) << 63;
  const QByteArray bytes = text.toUtf8();
  qsizetype i = 0;
  bool negative = false;
  if (i < bytes.size() && (bytes[i] == '-' || bytes[i] == '+')) {
    negative = bytes[i] == '-';
    i++;
  }
  const QByteArray rest = bytes.mid(i);
  if (rest == "0") {
    *nanoseconds = 0;
    return true;
  }
  if (rest.isEmpty()) {
    return false;
  }
  const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
  quint64 total = 0;
  while (i < bytes.size()) {
    if (!(bytes[i] == '.' || isDigit(bytes[i]))) {
      return false;
    }
    quint64 whole = 0;
    const qsizetype wholeStart = i;
    for (; i < bytes.size() && isDigit(bytes[i]); i++) {
      if (whole > kLimit / 10) {
        return false;
      }
      whole = whole * 10 + static_cast<quint64>(bytes[i] - '0');
      if (whole > kLimit) {
        return false;
      }
    }
    const bool before = i != wholeStart;
    quint64 fraction = 0;
    double scale = 1;
    bool after = false;
    if (i < bytes.size() && bytes[i] == '.') {
      i++;
      const qsizetype fractionStart = i;
      bool full = false;
      for (; i < bytes.size() && isDigit(bytes[i]); i++) {
        if (full) {
          continue;
        }
        if (fraction > (kLimit - 1) / 10) {
          full = true;
          continue;
        }
        const quint64 next = fraction * 10 + static_cast<quint64>(bytes[i] - '0');
        if (next > kLimit) {
          full = true;
          continue;
        }
        fraction = next;
        scale *= 10;
      }
      after = i != fractionStart;
    }
    if (!before && !after) {
      return false;
    }
    const qsizetype unitStart = i;
    while (i < bytes.size() && bytes[i] != '.' && !isDigit(bytes[i])) {
      i++;
    }
    const QByteArray unitName = bytes.mid(unitStart, i - unitStart);
    quint64 unit = 0;
    if (unitName == "ns") {
      unit = 1;
    } else if (unitName == "us" || unitName == "\xC2\xB5s" ||
               unitName == "\xCE\xBCs") {
      unit = 1000;
    } else if (unitName == "ms") {
      unit = 1000000;
    } else if (unitName == "s") {
      unit = 1000000000;
    } else if (unitName == "m") {
      unit = 60ULL * 1000000000;
    } else if (unitName == "h") {
      unit = 3600ULL * 1000000000;
    } else {
      return false; // missing or unknown unit
    }
    if (whole > kLimit / unit) {
      return false;
    }
    whole *= unit;
    if (fraction > 0) {
      whole += static_cast<quint64>(static_cast<double>(fraction) *
                                    (static_cast<double>(unit) / scale));
      if (whole > kLimit) {
        return false;
      }
    }
    total += whole;
    if (total > kLimit) {
      return false;
    }
  }
  if (negative) {
    *nanoseconds = total == kLimit ? std::numeric_limits<qint64>::min()
                                   : -static_cast<qint64>(total);
    return true;
  }
  if (total > kLimit - 1) {
    return false;
  }
  *nanoseconds = static_cast<qint64>(total);
  return true;
}

} // namespace RcloneNumber
