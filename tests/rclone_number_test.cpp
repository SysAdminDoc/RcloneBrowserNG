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
  }
};

QTEST_GUILESS_MAIN(RcloneNumberTest)
#include "rclone_number_test.moc"
