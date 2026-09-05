#include "parsing_regression_test.h"

#include "job_stats.h"
#include "lsjson_parser.h"

#include <QByteArray>
#include <QTest>

// This suite used to carry its own copy of both parsers, so mutating the code
// the app actually runs left it green. It links src/lsjson_parser.cpp and
// src/job_stats.cpp now: these are the shipped decoders.
namespace {

QVector<LsjsonParser::Entry> parseListing(const QList<QByteArray> &chunks,
                                          const QString &parentPath = "remote:") {
  LsjsonParser::StreamSplitter splitter;
  QVector<LsjsonParser::Entry> entries;
  for (const QByteArray &chunk : chunks) {
    for (const QJsonObject &object : splitter.feed(chunk)) {
      entries.append(LsjsonParser::DecodeEntry(object, parentPath));
    }
  }
  return entries;
}

} // namespace

void ParsingRegressionTest::extractsStandardListing() {
  const QByteArray listing = R"([
{"Path":"docs","Name":"docs","Size":-1,"IsDir":true,"ModTime":"2026-03-01T12:00:00.000000000Z"},
{"Path":"notes.txt","Name":"notes.txt","Size":1234,"IsDir":false,"ModTime":"2026-03-02T08:30:15.500000000Z"}
])";
  const auto entries = parseListing({listing});
  QCOMPARE(entries.size(), 2);

  QCOMPARE(entries[0].name, QStringLiteral("docs"));
  QVERIFY(entries[0].isFolder);
  // Size is only read for files: rclone reports -1 for a directory, and
  // casting that to quint64 would render as 18 exabytes.
  QCOMPARE(entries[0].size, static_cast<quint64>(0));
  QCOMPARE(entries[0].path, QStringLiteral("remote:/docs"));

  QCOMPARE(entries[1].name, QStringLiteral("notes.txt"));
  QVERIFY(!entries[1].isFolder);
  QCOMPARE(entries[1].size, static_cast<quint64>(1234));
  QCOMPARE(entries[1].path, QStringLiteral("remote:/notes.txt"));
  QVERIFY(!entries[1].modified.isEmpty());
}

void ParsingRegressionTest::extractsChunkedListing() {
  // rclone writes to a pipe, so a record can be split anywhere, including
  // inside a string or between the two halves of an escape.
  const QByteArray whole = R"([{"Path":"a.txt","Name":"a.txt","Size":10,"IsDir":false,"ModTime":"2026-03-01T12:00:00Z"},{"Path":"b.txt","Name":"b.txt","Size":20,"IsDir":false,"ModTime":"2026-03-01T12:00:01Z"}])";
  const auto reference = parseListing({whole});
  QCOMPARE(reference.size(), 2);

  for (int split = 1; split < whole.size(); ++split) {
    const auto entries =
        parseListing({whole.left(split), whole.mid(split)});
    QVERIFY2(entries.size() == 2,
             qPrintable(QString("split at %1 produced %2 entries")
                            .arg(split)
                            .arg(entries.size())));
    QCOMPARE(entries[0].name, reference[0].name);
    QCOMPARE(entries[1].size, reference[1].size);
  }
}

void ParsingRegressionTest::extractsEmptyListing() {
  LsjsonParser::StreamSplitter splitter;
  QVERIFY(!splitter.hadData());
  const auto objects = splitter.feed("[]");
  QVERIFY(objects.isEmpty());
  // hadData separates "rclone printed nothing" from "rclone printed something
  // unparseable"; the model reports a load failure only for the second.
  QVERIFY(splitter.hadData());
}

void ParsingRegressionTest::extractsNestedMetadata() {
  // Nested objects must not end the record early, and braces inside a quoted
  // string must not be counted at all.
  const QByteArray listing =
      R"([{"Path":"m.bin","Name":"m.bin","Size":5,"IsDir":false,)"
      R"("Metadata":{"mtime":"2026-01-01T00:00:00Z","mode":"0644"},)"
      R"("Hashes":{"md5":"d41d8cd98f00b204e9800998ecf8427e"},)"
      R"("ModTime":"2026-03-01T12:00:00Z"},)"
      R"({"Path":"brace{name}.txt","Name":"brace{name}.txt","Size":7,"IsDir":false,"ModTime":"2026-03-01T12:00:00Z"}])";
  const auto entries = parseListing({listing});
  QCOMPARE(entries.size(), 2);
  QCOMPARE(entries[0].name, QStringLiteral("m.bin"));
  QCOMPARE(entries[0].size, static_cast<quint64>(5));
  QCOMPARE(entries[1].name, QStringLiteral("brace{name}.txt"));
}

void ParsingRegressionTest::preservesLargeSizes() {
  // Sizes go through a double, so anything past 2^53 would round. 8 TiB has
  // to survive exactly.
  const QByteArray listing =
      R"([{"Path":"big.iso","Name":"big.iso","Size":8796093022208,"IsDir":false,"ModTime":"2026-03-01T12:00:00Z"}])";
  const auto entries = parseListing({listing});
  QCOMPARE(entries.size(), 1);
  QCOMPARE(entries[0].size, static_cast<quint64>(8796093022208LL));
}

void ParsingRegressionTest::preservesUnicodeNames() {
  const QByteArray listing =
      R"([{"Path":"日本語.txt","Name":"日本語.txt","Size":3,"IsDir":false,"ModTime":"2026-03-01T12:00:00Z"},)"
      R"({"Path":"emoji 🚀.txt","Name":"emoji 🚀.txt","Size":4,"IsDir":false,"ModTime":"2026-03-01T12:00:00Z"}])";
  const auto entries = parseListing({listing});
  QCOMPARE(entries.size(), 2);
  QCOMPARE(entries[0].name, QString::fromUtf8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.txt"));
  QVERIFY(entries[1].name.contains(QString::fromUtf8("\xf0\x9f\x9a\x80")));
}

void ParsingRegressionTest::preservesSpecialPaths() {
  // Escaped quotes and backslashes inside a name must not confuse the string
  // tracking that keeps braces from being miscounted.
  const QByteArray listing =
      R"([{"Path":"sub/quote\"name.txt","Name":"quote\"name.txt","Size":1,"IsDir":false,"ModTime":"2026-03-01T12:00:00Z"},)"
      R"({"Path":"sub/back\\slash.txt","Name":"back\\slash.txt","Size":2,"IsDir":false,"ModTime":"2026-03-01T12:00:00Z"},)"
      R"({"Path":"trailing space ","Name":"trailing space ","Size":3,"IsDir":false,"ModTime":"2026-03-01T12:00:00Z"}])";
  const auto entries = parseListing({listing});
  QCOMPARE(entries.size(), 3);
  QCOMPARE(entries[0].name, QStringLiteral("quote\"name.txt"));
  QCOMPARE(entries[0].path, QStringLiteral("remote:/sub/quote\"name.txt"));
  QCOMPARE(entries[1].name, QStringLiteral("back\\slash.txt"));
  // Upstream kapitainsky#107: a name ending in a space must keep it.
  QCOMPARE(entries[2].name, QStringLiteral("trailing space "));
  QCOMPARE(entries[2].path, QStringLiteral("remote:/trailing space "));
}

void ParsingRegressionTest::parsesModTimeVariants() {
  // Millisecond and second precision both appear depending on the backend,
  // and some backends send a shape Qt will not parse at all.
  QVERIFY(!LsjsonParser::FormatModTime("2026-03-01T12:00:00.123456789Z").isEmpty());
  QVERIFY(!LsjsonParser::FormatModTime("2026-03-01T12:00:00Z").isEmpty());
  QVERIFY(!LsjsonParser::FormatModTime("2017-05-31T16:15:57+01:00").isEmpty());

  // The fallback slices the leading date and time out of an unparseable value.
  QCOMPARE(LsjsonParser::FormatModTime("2026-03-01T12:00:00 not-a-zone"),
           QStringLiteral("2026-03-01 12:00:00"));

  // A bare date is still valid ISO-8601, so Qt reads it as midnight.
  QCOMPARE(LsjsonParser::FormatModTime("2026-03-01"),
           QStringLiteral("2026-03-01 00:00:00"));

  // Neither parseable nor long enough to slice, so there is nothing honest
  // to show.
  QCOMPARE(LsjsonParser::FormatModTime("not a time"), QString());
  QCOMPARE(LsjsonParser::FormatModTime(QString()), QString());
}

void ParsingRegressionTest::parsesTransferStats() {
  const QByteArray line =
      R"({"level":"info","msg":"stats","stats":{"bytes":524288,"totalBytes":1048576,)"
      R"("speed":131072,"eta":4,"elapsedTime":3661.5,"errors":0,"checks":2,"totalChecks":5,)"
      R"("transfers":1,"totalTransfers":4,)"
      R"("transferring":[{"name":"big.bin","percentage":50,"speed":131072,"eta":4},)"
      R"({"name":"","percentage":10}]}})";
  const JobStats::LogLine parsed = JobStats::ParseLogLine(line);
  QVERIFY(parsed.isJson);
  QVERIFY(parsed.stats.present);
  QCOMPARE(parsed.stats.bytes, 524288.0);
  QCOMPARE(parsed.stats.totalBytes, 1048576.0);
  QCOMPARE(parsed.stats.speed, 131072.0);
  QCOMPARE(parsed.stats.checks, 2);
  QCOMPARE(parsed.stats.totalChecks, 5);
  QCOMPARE(parsed.stats.transfers, 1);
  QCOMPARE(parsed.stats.totalTransfers, 4);

  // A nameless entry cannot be matched to a progress row, so it is dropped.
  QCOMPARE(parsed.stats.transferring.size(), 1);
  QCOMPARE(parsed.stats.transferring[0].name, QStringLiteral("big.bin"));
  QCOMPARE(parsed.stats.transferring[0].percentage, 50);
  QCOMPARE(parsed.stats.transferring[0].eta, 4.0);

  QCOMPARE(JobStats::PercentComplete(parsed.stats.bytes, parsed.stats.totalBytes), 50);
  QCOMPARE(JobStats::FormatDuration(parsed.stats.eta), QStringLiteral("4s"));
  QCOMPARE(JobStats::FormatDuration(parsed.stats.elapsedTime),
           QStringLiteral("1h1m1s"));
  QCOMPARE(JobStats::FormatCount(parsed.stats.transfers, parsed.stats.totalTransfers),
           QStringLiteral("1 / 4"));
}

void ParsingRegressionTest::parsesErrorStats() {
  const QByteArray line =
      R"({"level":"error","msg":"failed to copy","object":"broken.bin","time":"2026-03-01T12:00:00Z",)"
      R"("stats":{"bytes":0,"totalBytes":0,"errors":3,"lastError":"permission denied"}})";
  const JobStats::LogLine parsed = JobStats::ParseLogLine(line);
  QVERIFY(parsed.isJson);
  QCOMPARE(parsed.level, QStringLiteral("error"));
  QCOMPARE(parsed.object, QStringLiteral("broken.bin"));
  QCOMPARE(parsed.time, QStringLiteral("2026-03-01T12:00:00Z"));
  QCOMPARE(parsed.stats.errors, 3);
  // No totalBytes yet, so there is no percentage to show rather than a
  // division by zero.
  QCOMPARE(JobStats::PercentComplete(0, 0), 0);
  QCOMPARE(JobStats::FormatCount(0, 0), QString());
}

void ParsingRegressionTest::ignoresNonStatsLine() {
  const JobStats::LogLine plain = JobStats::ParseLogLine("Transferred: 1 / 2");
  QVERIFY(!plain.isJson);
  QVERIFY(!plain.stats.present);

  const JobStats::LogLine noStats =
      JobStats::ParseLogLine(R"({"level":"info","msg":"Checks: 4"})");
  QVERIFY(noStats.isJson);
  QVERIFY(!noStats.stats.present);
  QCOMPARE(noStats.message, QStringLiteral("Checks: 4"));
}

void ParsingRegressionTest::clampsProgressAndFormatsCounts() {
  // rclone can report more bytes than totalBytes while a transfer retries;
  // a progress readout over 100% reads as a bug in the app.
  QCOMPARE(JobStats::PercentComplete(150, 100), 100);
  QCOMPARE(JobStats::PercentComplete(-5, 100), 0);
  QCOMPARE(JobStats::PercentComplete(50, 0), 0);

  QCOMPARE(JobStats::FormatDuration(0), QString());
  QCOMPARE(JobStats::FormatDuration(-1), QString());
  QCOMPARE(JobStats::FormatDuration(59), QStringLiteral("59s"));
  QCOMPARE(JobStats::FormatDuration(60), QStringLiteral("1m0s"));
  QCOMPARE(JobStats::FormatDuration(3600), QStringLiteral("1h0m0s"));

  QCOMPARE(JobStats::FormatCount(3, 0), QStringLiteral("3"));
  QCOMPARE(JobStats::FormatCount(0, 7), QStringLiteral("0 / 7"));

  QCOMPARE(JobStats::ElideTransferName("short.txt"), QStringLiteral("short.txt"));
  const QString longName(80, QChar('x'));
  const QString elided = JobStats::ElideTransferName(longName);
  QCOMPARE(elided.length(), 47);
  QVERIFY(elided.contains("..."));
}

QTEST_APPLESS_MAIN(ParsingRegressionTest)
#include "parsing_regression_test.moc"
