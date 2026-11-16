#include "rclone_number.h"
#include "test_rclone.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

struct Case {
  QString text;
  std::optional<qint64> expected; // nullopt: rclone refuses it
};

const std::optional<qint64> kRefused;

// --max-delete=<text>, read back through "rclone rc --loopback options/get"
// on rclone v1.76.0 (2026-11-15). The two extremes are written exactly,
// since options/get prints them through a float.
const QList<Case> kCounts{
    {u"5"_s, 5},
    {u" 5"_s, 5},
    {u"5 "_s, 5},
    {u"\t5"_s, 5},
    {u"010"_s, 8},
    {u"08"_s, kRefused},
    {u"0x10"_s, 16},
    {u"0X1f"_s, 31},
    {u"0o17"_s, 15},
    {u"0O17"_s, 15},
    {u"0b101"_s, 5},
    {u"0B11"_s, 3},
    {u"1_000"_s, 1000},
    {u"0_10"_s, 8},
    {u"0x_1F"_s, 31},
    {u"_1"_s, kRefused},
    {u"1_"_s, kRefused},
    {u"1__0"_s, kRefused},
    {u"+5"_s, 5},
    {u"-3"_s, -3},
    {u"-0x10"_s, -16},
    {u"0x"_s, kRefused},
    {u"0b"_s, kRefused},
    {u"0o"_s, kRefused},
    {u"0"_s, 0},
    {u"00"_s, 0},
    {u"-"_s, kRefused},
    {u"+"_s, kRefused},
    {u""_s, kRefused},
    {u"5x"_s, kRefused},
    {u"5\n"_s, 5},
    {u"\n5"_s, kRefused},
    {u"5\nx"_s, 5},
    {u"9223372036854775807"_s, std::numeric_limits<qint64>::max()},
    {u"9223372036854775808"_s, kRefused},
    {u"-9223372036854775808"_s, std::numeric_limits<qint64>::min()},
    {u"-9223372036854775809"_s, kRefused},
    {u"0b2"_s, kRefused},
    {u"1e3"_s, kRefused},
    {u"1.0"_s, kRefused},
    {u"0xg"_s, kRefused},
    {u"0o8"_s, kRefused},
    {u" +7 "_s, 7},
    {u"\uff15"_s, kRefused},
    {u"\u00a05"_s, 5},
    {u"5\u3000"_s, 5},
    {u"\r\n5"_s, kRefused},
    {u"+-5"_s, kRefused},
    {u"0x1F_"_s, kRefused},
    {u"0_x1F"_s, kRefused},
    {u"07_7"_s, 63},
    {u"0b_1"_s, 1},
    {u"1 2"_s, kRefused},
};

// --max-delete-size=<text>, read back the same way. "off" reads as -1.
// options/get prints a big size through a three-decimal string such as
// "909.495Pi", so the large ones here are written as Go computes them.
const QList<Case> kSizes{
    {u"10"_s, 10240},
    {u"3k"_s, 3072},
    {u"2b"_s, 2},
    {u"10Ki"_s, 10240},
    {u"10KI"_s, 10240},
    {u"10MiB"_s, 10485760},
    {u"10MB"_s, kRefused},
    {u"1_000"_s, 1024000},
    {u"0x1p4"_s, 16384},
    {u"0x10"_s, kRefused},
    {u"1e16"_s, kRefused},
    {u"1e15"_s, Q_INT64_C(1024000000000000000)},
    {u"Infk"_s, kRefused},
    {u"NaNk"_s, kRefused},
    {u"nan"_s, kRefused},
    {u"inf"_s, kRefused},
    {u"-1k"_s, kRefused},
    {u"-0"_s, 0},
    {u"1biB"_s, kRefused},
    {u"1bi"_s, kRefused},
    {u".5k"_s, 512},
    {u"5."_s, 5120},
    {u"5.k"_s, 5120},
    {u"1.e3"_s, 1024000},
    {u"8Ei"_s, kRefused},
    {u"7.99Ei"_s, Q_INT64_C(9211842821808707584)},
    {u"7Ei"_s, Q_INT64_C(8070450532247928832)},
    {u"8191Pi"_s, Q_INT64_C(9222246136947933184)},
    {u"8192Pi"_s, kRefused},
    {u"1_.5"_s, kRefused},
    {u"1._5"_s, kRefused},
    {u"0x1.8p1"_s, 3072},
    {u"0x_1p4"_s, 16384},
    {u"+1k"_s, 1024},
    {u"1e400k"_s, kRefused},
    {u"1e-400k"_s, 0},
    {u"off"_s, -1},
    {u"OFF"_s, -1},
    {u""_s, kRefused},
    {u" 5k"_s, kRefused},
    {u"5k "_s, kRefused},
    {u"iB"_s, kRefused},
    {u"B"_s, kRefused},
    {u"i"_s, kRefused},
    {u"k"_s, kRefused},
    {u"1.5"_s, 1536},
    {u"1E3"_s, 1024000},
    {u"0x1P4"_s, 16384},
    {u"infinityb"_s, kRefused},
    {u"-infb"_s, kRefused},
    {u"0xp1"_s, kRefused},
    {u"1e"_s, Q_INT64_C(1152921504606846976)},
    {u"1e+"_s, kRefused},
    {u"1e_3"_s, kRefused},
    {u"1_e3"_s, kRefused},
    {u"01"_s, 1024},
    {u"0b101"_s, kRefused},
    {u"+Infk"_s, kRefused},
    {u"Infinityk"_s, kRefused},
    {u"infik"_s, kRefused},
    {u"1.5.5"_s, kRefused},
    {u"0x1p4k"_s, 16384},
    {u"1_0_0b"_s, 100},
    {u"0x1p-2"_s, 256},
    {u"1e-3b"_s, 0},
    {u"1MIB"_s, kRefused},
    {u"1miB"_s, 1048576},
    {u"1Mib"_s, 1048576},
    {u"\uff11k"_s, kRefused},
    // More digits than a double holds: rclone rounds the number to the
    // nearest double before it truncates to bytes.
    {u"0.9999999999999999444b"_s, 0},
    {u"241.99999999999997894b"_s, 241},
    {u"467.99999999999995590b"_s, 467},
    {u"861.99999999999994378b"_s, 862},
    {u"1023.99999999999988631b"_s, 1023},
    // A tie in the 16 hex digits kept, broken by a digit past them. Above
    // 2^34 options/get prints sizes through "%.3f<suffix>", so the exact
    // value here is ParseFloat's, measured through --tpslimit below.
    {u"0x1.00000000000008000001p52b"_s, Q_INT64_C(4503599627370497)},
};

// --retries-sleep=<text> in nanoseconds, read back through "rclone rc
// --loopback options/get" on rclone v1.76.0 (2026-11-15). The two ends of
// int64 are written exactly; options/get prints them through a float.
const QList<Case> kDurations{
    {u"0"_s, 0},
    {u"-0"_s, 0},
    {u"+0"_s, 0},
    {u"0s"_s, 0},
    {u"1s"_s, Q_INT64_C(1000000000)},
    {u"1.5s"_s, Q_INT64_C(1500000000)},
    {u"300ms"_s, Q_INT64_C(300000000)},
    {u"1.5ms"_s, Q_INT64_C(1500000)},
    {u"1m30s"_s, Q_INT64_C(90000000000)},
    {u"1h"_s, Q_INT64_C(3600000000000)},
    {u".5s"_s, Q_INT64_C(500000000)},
    {u"5.s"_s, Q_INT64_C(5000000000)},
    {u"+2s"_s, Q_INT64_C(2000000000)},
    {u"-1.5m"_s, Q_INT64_C(-90000000000)},
    {u"1us"_s, 1000},
    {u"1µs"_s, 1000},
    {u"1μs"_s, 1000},
    {u"1ns"_s, 1},
    {u"1h2m3.5s"_s, Q_INT64_C(3723500000000)},
    {u"1.5h30m"_s, Q_INT64_C(7200000000000)},
    {u"1m1m"_s, Q_INT64_C(120000000000)},
    {u"1h1h1h"_s, Q_INT64_C(10800000000000)},
    {u"0.000000001s"_s, 1},
    {u"1.0000000005s"_s, Q_INT64_C(1000000000)},
    {u"1.99999999999999999999s"_s, Q_INT64_C(2000000000)},
    {u"0.1234567891ns"_s, 0},
    {u"9223372036s"_s, Q_INT64_C(9223372036000000000)},
    {u"2562047h"_s, Q_INT64_C(9223369200000000000)},
    {u"9223372036854775807ns"_s, std::numeric_limits<qint64>::max()},
    {u"-9223372036854775808ns"_s, std::numeric_limits<qint64>::min()},
    {u"9223372036854775808ns"_s, kRefused},
    {u"-9223372036854775809ns"_s, kRefused},
    {u"100000000000000000000ns"_s, kRefused},
    {u"9223372037s"_s, kRefused},
    {u"2562048h"_s, kRefused},
    {u"300000y"_s, kRefused},
    {u"10"_s, kRefused},
    {u"1.5"_s, kRefused},
    {u"-3"_s, kRefused},
    {u"00"_s, kRefused},
    {u"0."_s, kRefused},
    {u"2d"_s, kRefused},
    {u"1.5d"_s, kRefused},
    {u"1d2h"_s, kRefused},
    {u"1w"_s, kRefused},
    {u"1M"_s, kRefused},
    {u"1y"_s, kRefused},
    {u"1S"_s, kRefused},
    {u"1D"_s, kRefused},
    {u"1x"_s, kRefused},
    {u"1e2"_s, kRefused},
    {u"1e400"_s, kRefused},
    {u"0x1p3"_s, kRefused},
    {u"1_0"_s, kRefused},
    {u"off"_s, kRefused},
    {u"inf"_s, kRefused},
    {u""_s, kRefused},
    {u"-"_s, kRefused},
    {u"+"_s, kRefused},
    {u" 1s"_s, kRefused},
    {u"1s "_s, kRefused},
    {u"s"_s, kRefused},
    {u".s"_s, kRefused},
    {u"1.2.3s"_s, kRefused},
    {u"1ms2"_s, kRefused},
    {u"5m-1s"_s, kRefused},
    {u"2026-01-02"_s, kRefused},
};

struct FloatCase {
  QString text;
  std::optional<double> expected; // nullopt: rclone refuses it
};

// --tpslimit=<text>, a plain float64 flag, read back exactly through
// options/get on rclone v1.76.0 (2026-11-15).
const QList<FloatCase> kFloats{
    {u"0.9999999999999999444"_s, std::nextafter(1.0, 0.0)},
    {u"861.99999999999994378"_s, 862.0},
    {u"1.797693134862315807937289714053e308"_s,
     std::numeric_limits<double>::max()},
    {u"1.7976931348623159e308"_s, std::nullopt},
    {u"2.4703282292062328e-324"_s, std::numeric_limits<double>::denorm_min()},
    {u"2.4703282292062327e-324"_s, 0.0},
    {u"1e-111111"_s, 0.0},
    {u"0x1p-1075"_s, 0.0},
    {u"0x1.0000000000001p-1075"_s, std::numeric_limits<double>::denorm_min()},
    {u"0x1.00000000000008p0"_s, 1.0},
    {u"0x1.00000000000008000001p0"_s,
     1.0 + std::numeric_limits<double>::epsilon()},
    {u"0x1.00000000000008000001p52"_s, 4503599627370497.0},
    {u"0x1.fffffffffffff8p1023"_s, std::nullopt},
    {u"0x1.fffffffffffff7ffffp1023"_s, std::numeric_limits<double>::max()},
};

QString Show(const QString &text) {
  QString shown;
  for (const QChar c : text) {
    shown += c.unicode() >= 0x20 && c.unicode() < 0x7f
                 ? QString(c)
                 : u"\\u%1"_s.arg(static_cast<uint>(c.unicode()), 4, 16,
                                         QLatin1Char('0'));
  }
  return u"\""_s + shown + u"\""_s;
}

} // namespace

class RcloneNumberTest : public QObject {
  Q_OBJECT

private slots:
  void scansCountsAsRcloneDoes() {
    for (const Case &c : kCounts) {
      qint64 value = 12345;
      const bool ok = RcloneNumber::ScanInt(c.text, &value);
      QVERIFY2(ok == c.expected.has_value(),
               qPrintable(Show(c.text) + (ok ? u" accepted"_s
                                             : u" refused"_s)));
      if (ok) {
        QVERIFY2(value == *c.expected,
                 qPrintable(Show(c.text) + u" gave "_s +
                            QString::number(value)));
      }
    }
  }

  void parsesSizesAsRcloneDoes() {
    for (const Case &c : kSizes) {
      qint64 bytes = 12345;
      const RcloneNumber::SizeError error =
          RcloneNumber::ParseSize(c.text, &bytes);
      const bool ok = error == RcloneNumber::SizeError::None;
      QVERIFY2(ok == c.expected.has_value(),
               qPrintable(Show(c.text) + (ok ? u" accepted"_s
                                             : u" refused"_s)));
      if (ok) {
        QVERIFY2(bytes == *c.expected,
                 qPrintable(Show(c.text) + u" gave "_s +
                            QString::number(bytes)));
      }
    }
  }

  void namesWhatIsWrongWithASize() {
    using RcloneNumber::SizeError;
    QVERIFY(RcloneNumber::ParseSize(u""_s, nullptr) ==
            SizeError::Empty);
    QVERIFY(RcloneNumber::ParseSize(u"10MB"_s, nullptr) ==
            SizeError::BadNumber);
    QVERIFY(RcloneNumber::ParseSize(u"1X"_s, nullptr) ==
            SizeError::BadSuffix);
    QVERIFY(RcloneNumber::ParseSize(u"1biB"_s, nullptr) ==
            SizeError::BadSuffix);
    QVERIFY(RcloneNumber::ParseSize(u"NaNk"_s, nullptr) ==
            SizeError::NotANumber);
    QVERIFY(RcloneNumber::ParseSize(u"-1k"_s, nullptr) ==
            SizeError::Negative);
    QVERIFY(RcloneNumber::ParseSize(u"8Ei"_s, nullptr) ==
            SizeError::TooBig);
    QVERIFY(RcloneNumber::ParseSize(u"Infk"_s, nullptr) ==
            SizeError::TooBig);
  }

  // fs/sizesuffix.go String with Go's %.3f, worked out by hand.
  // agreesWithTheInstalledRclone checks the same code against rclone.
  void formatsSizesAsRcloneDoes() {
    const auto bytesOf = [](const QString &text) {
      qint64 bytes = 0;
      return RcloneNumber::ParseSize(text, &bytes) ==
                     RcloneNumber::SizeError::None
                 ? bytes
                 : std::numeric_limits<qint64>::min();
    };
    QCOMPARE(RcloneNumber::FormatSize(-1), u"off"_s);
    QCOMPARE(RcloneNumber::FormatSize(0), u"0"_s);
    QCOMPARE(RcloneNumber::FormatSize(1023), u"1023"_s);
    QCOMPARE(RcloneNumber::FormatSize(1024), u"1Ki"_s);
    QCOMPARE(RcloneNumber::FormatSize(1100), u"1.074Ki"_s);
    QCOMPARE(RcloneNumber::FormatSize(1099), u"1.073Ki"_s);
    QCOMPARE(RcloneNumber::FormatSize(1536), u"1.500Ki"_s);
    QCOMPARE(RcloneNumber::FormatSize(3000), u"2.930Ki"_s);
    // Exact ties go to even, as Go's %.3f does.
    QCOMPARE(RcloneNumber::FormatSize(1088), u"1.062Ki"_s);
    QCOMPARE(RcloneNumber::FormatSize(bytesOf(u"2.0625M"_s)), u"2.062Mi"_s);
    QCOMPARE(RcloneNumber::FormatSize(bytesOf(u"1.0005k"_s)), u"1Ki"_s);
    QCOMPARE(RcloneNumber::FormatSize(bytesOf(u"7.999E"_s)), u"7.999Ei"_s);
    QCOMPARE(RcloneNumber::FormatSize(bytesOf(u"7.9994E"_s)), u"7.999Ei"_s);
    // These two rclone then refuses to read back.
    QCOMPARE(RcloneNumber::FormatSize(bytesOf(u"7.9995E"_s)), u"8.000Ei"_s);
    QCOMPARE(RcloneNumber::FormatSize(std::numeric_limits<qint64>::max()),
             u"8Ei"_s);
    QCOMPARE(RcloneNumber::FormatSize(bytesOf(u"1023.9999P"_s)),
             u"1024.000Pi"_s);
  }

  void parsesDurationsAsGoDoes() {
    for (const Case &c : kDurations) {
      qint64 value = 12345;
      const bool ok = RcloneNumber::ParseGoDuration(c.text, &value);
      QVERIFY2(ok == c.expected.has_value(),
               qPrintable(Show(c.text) + (ok ? u" accepted"_s
                                             : u" refused"_s)));
      if (ok) {
        QVERIFY2(value == *c.expected,
                 qPrintable(Show(c.text) + u" gave "_s +
                            QString::number(value)));
      }
    }
  }

  void parseIntTakesNoSpaces() {
    // ParseInt is the strict half: fmt's scanner is what allows spaces and
    // cuts "08" at the 8.
    qint64 value = 0;
    QVERIFY(!RcloneNumber::ParseInt(u" 5"_s, &value));
    QVERIFY(!RcloneNumber::ParseInt(u"08"_s, &value));
    QVERIFY(RcloneNumber::ParseInt(u"0_10"_s, &value));
    QCOMPARE(value, Q_INT64_C(8));
    QVERIFY(RcloneNumber::ParseInt(u"-0x8000000000000000"_s, &value));
    QCOMPARE(value, std::numeric_limits<qint64>::min());
    QVERIFY(!RcloneNumber::ParseInt(u"0x8000000000000000"_s, &value));
  }

  void parsesFloatsAsGoDoes() {
    double value = 0;
    QVERIFY(RcloneNumber::ParseFloat(u"0x1p4"_s, &value));
    QCOMPARE(value, 16.0);
    QVERIFY(RcloneNumber::ParseFloat(u"1_000.5"_s, &value));
    QCOMPARE(value, 1000.5);
    QVERIFY(RcloneNumber::ParseFloat(u"-.5e1"_s, &value));
    QCOMPARE(value, -5.0);
    QVERIFY(RcloneNumber::ParseFloat(u"+Infinity"_s, &value));
    QVERIFY(std::isinf(value) && value > 0);
    QVERIFY(RcloneNumber::ParseFloat(u"-inf"_s, &value));
    QVERIFY(std::isinf(value) && value < 0);
    QVERIFY(RcloneNumber::ParseFloat(u"NaN"_s, &value));
    QVERIFY(std::isnan(value));
    QVERIFY(RcloneNumber::ParseFloat(u"1e-400"_s, &value));
    QCOMPARE(value, 0.0);
    for (const QString &bad :
         {u""_s, u"."_s, u"+nan"_s, u"infx"_s, u"infin"_s, u"0x10"_s,
          u"1e400"_s, u"1_.5"_s, u"1e"_s, u"0x1p"_s, u" 1"_s, u"1 "_s,
          u"0x"_s, u"--1"_s}) {
      QVERIFY2(!RcloneNumber::ParseFloat(bad, &value), qPrintable(Show(bad)));
    }
  }

  void roundsFloatsAsGoDoes() {
    for (const FloatCase &c : kFloats) {
      double value = 12345;
      const bool ok = RcloneNumber::ParseFloat(c.text, &value);
      QVERIFY2(ok == c.expected.has_value(),
               qPrintable(Show(c.text) + (ok ? u" accepted"_s
                                             : u" refused"_s)));
      if (ok) {
        QVERIFY2(value == *c.expected,
                 qPrintable(Show(c.text) + u" gave "_s +
                            QString::number(value, 'g', 17)));
      }
    }
  }

  void agreesWithTheInstalledRclone() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("no rclone; set RCLONE_BROWSER_TEST_RCLONE");
    }
    // What rclone made of --<flag>=<text>: nullopt when it refused it.
    const auto read = [&rclone](const QString &flag, const QString &text,
                                const QString &key) -> std::optional<double> {
      QProcess process;
      process.start(rclone, {u"--%1=%2"_s.arg(flag, text), u"rc"_s,
                             u"--loopback"_s, u"options/get"_s});
      if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        return std::nullopt;
      }
      if (process.exitStatus() != QProcess::NormalExit ||
          process.exitCode() != 0) {
        return std::nullopt;
      }
      const QJsonObject main = QJsonDocument::fromJson(
                                   process.readAllStandardOutput())
                                   .object()
                                   .value(u"main"_s)
                                   .toObject();
      return main.value(key).toDouble();
    };
    // options/get rounds big values, so those agree to a thousandth.
    const auto same = [](double rclone, qint64 ours) {
      return std::abs(rclone - static_cast<double>(ours)) <=
             std::max(0.5, std::abs(rclone) * 1e-3);
    };
    const auto check = [&](const QList<Case> &cases, const QString &flag,
                           const QString &key) {
      for (const Case &c : cases) {
        const std::optional<double> got = read(flag, c.text, key);
        QVERIFY2(got.has_value() == c.expected.has_value(),
                 qPrintable(flag + u" "_s + Show(c.text) +
                            (got ? u" accepted by rclone"_s
                                 : u" refused by rclone"_s)));
        if (got) {
          QVERIFY2(same(*got, *c.expected),
                   qPrintable(flag + u" "_s + Show(c.text) +
                              u" is "_s + QString::number(*got, 'g', 19) +
                              u" in rclone"_s));
        }
      }
    };
    check(kCounts, u"max-delete"_s, u"MaxDelete"_s);
    check(kSizes, u"max-delete-size"_s, u"MaxDeleteSize"_s);

    // rclone writes a --bwlimit out and reads it back before it runs, and
    // options/get shows the text of what it read back, with a B added to a
    // bare count. A value whose text it can't read back is refused. Ties
    // tell Go's round-to-even from rounding up: 1088 bytes would end as
    // 1.063Ki the other way.
    for (const QString &text :
         {u"0"_s, u"off"_s, u"1023b"_s, u"1088b"_s, u"1100b"_s, u"1536b"_s,
          u"3000b"_s, u"2.5k"_s, u"1.0005k"_s, u"2.0625M"_s, u"7.999E"_s,
          u"7.9994E"_s, u"7.9995E"_s, u"7.9999E"_s, u"1023.9999P"_s,
          u"9223372036854774784b"_s}) {
      QProcess process;
      process.start(rclone, {u"--bwlimit=%1"_s.arg(text), u"rc"_s,
                             u"--loopback"_s, u"options/get"_s});
      QVERIFY(process.waitForFinished(30000));
      const bool rcloneTookIt = process.exitStatus() == QProcess::NormalExit &&
                                process.exitCode() == 0;
      qint64 bytes = 0;
      QVERIFY(RcloneNumber::ParseSize(text, &bytes) ==
              RcloneNumber::SizeError::None);
      qint64 back = 0;
      const bool readsBack =
          RcloneNumber::ParseSize(RcloneNumber::FormatSizeRoundTrip(bytes),
                                  &back) == RcloneNumber::SizeError::None;
      QVERIFY2(rcloneTookIt == readsBack,
               qPrintable(u"bwlimit "_s + text +
                          (rcloneTookIt ? u" accepted by rclone"_s
                                        : u" refused by rclone"_s)));
      if (rcloneTookIt) {
        const QString ours = RcloneNumber::FormatSizeRoundTrip(back);
        const QString shown = QJsonDocument::fromJson(
                                  process.readAllStandardOutput())
                                  .object()
                                  .value(u"main"_s)
                                  .toObject()
                                  .value(u"BwLimit"_s)
                                  .toString();
        QCOMPARE(shown, ours);
      }
    }
    check(kDurations, u"retries-sleep"_s, u"RetriesInterval"_s);
    for (const FloatCase &c : kFloats) {
      const std::optional<double> got = read(u"tpslimit"_s, c.text,
                                             u"TPSLimit"_s);
      QVERIFY2(got.has_value() == c.expected.has_value(),
               qPrintable(u"tpslimit "_s + Show(c.text) +
                          (got ? u" accepted by rclone"_s
                               : u" refused by rclone"_s)));
      if (got) {
        QVERIFY2(*got == *c.expected,
                 qPrintable(u"tpslimit "_s + Show(c.text) + u" is "_s +
                            QString::number(*got, 'g', 17) +
                            u" in rclone"_s));
      }
    }
  }
};

QTEST_GUILESS_MAIN(RcloneNumberTest)
#include "rclone_number_test.moc"
