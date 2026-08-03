#include "parsing_regression_test.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace {
struct ParsedItem {
  QString name;
  bool isDir;
  quint64 size;
  QString modTime;
  QString path;
};

QVector<ParsedItem> extractItems(const QByteArray &data) {
  QVector<ParsedItem> items;
  int braceDepth = 0;
  int objStart = -1;

  for (int i = 0; i < data.size(); ++i) {
    char c = data[i];
    if (c == '{') {
      if (braceDepth == 0)
        objStart = i;
      braceDepth++;
    } else if (c == '}') {
      braceDepth--;
      if (braceDepth == 0 && objStart >= 0) {
        QByteArray objBytes = data.mid(objStart, i - objStart + 1);
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(objBytes, &err);
        if (doc.isObject()) {
          QJsonObject obj = doc.object();
          ParsedItem item;
          item.name = obj.value("Name").toString();
          item.isDir = obj.value("IsDir").toBool();
          item.size = static_cast<quint64>(obj.value("Size").toDouble());
          item.modTime = obj.value("ModTime").toString();
          item.path = obj.value("Path").toString();
          items.append(item);
        }
        objStart = -1;
      }
    }
  }
  return items;
}

struct StatsResult {
  double bytes;
  double totalBytes;
  double speed;
  int errors;
  int transfers;
  int totalTransfers;
};

StatsResult parseStatsLine(const QByteArray &line) {
  StatsResult result = {};
  QJsonDocument doc = QJsonDocument::fromJson(line);
  if (!doc.isObject())
    return result;
  QJsonObject obj = doc.object();
  if (!obj.contains("stats"))
    return result;
  QJsonObject stats = obj.value("stats").toObject();
  result.bytes = stats.value("bytes").toDouble();
  result.totalBytes = stats.value("totalBytes").toDouble();
  result.speed = stats.value("speed").toDouble();
  result.errors = stats.value("errors").toInt();
  result.transfers = stats.value("transfers").toInt();
  result.totalTransfers = stats.value("totalTransfers").toInt();
  return result;
}
} // namespace

void ParsingRegressionTest::extractsStandardListing() {
    QByteArray data = R"([
{"Path":"documents","Name":"documents","Size":-1,"MimeType":"inode/directory","ModTime":"2024-01-15T10:30:00.000Z","IsDir":true},
{"Path":"photo.jpg","Name":"photo.jpg","Size":1234567,"MimeType":"image/jpeg","ModTime":"2024-03-20T14:22:33.456Z","IsDir":false},
{"Path":"notes.txt","Name":"notes.txt","Size":42,"MimeType":"text/plain","ModTime":"2023-12-01T08:00:00.000Z","IsDir":false}
])";
    auto items = extractItems(data);
    QCOMPARE(items.size(), 3);
    QCOMPARE(items.at(0).name, QStringLiteral("documents"));
    QVERIFY(items.at(0).isDir);
    QCOMPARE(items.at(1).size, quint64(1234567));
    QCOMPARE(items.at(2).size, quint64(42));
}

void ParsingRegressionTest::extractsChunkedListing() {
    QByteArray chunk1 = R"([{"Path":"file1.txt","Name":"file1.txt","Si)";
    QByteArray chunk2 = R"(ze":100,"IsDir":false,"ModTime":"2024-01-01T00:00:00Z"},{"Pa)";
    QByteArray chunk3 = R"(th":"file2.txt","Name":"file2.txt","Size":200,"IsDir":false,"ModTime":"2024-01-02T00:00:00Z"}])";
    auto items = extractItems(chunk1 + chunk2 + chunk3);
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).size, quint64(100));
    QCOMPARE(items.at(1).size, quint64(200));
}

void ParsingRegressionTest::extractsEmptyListing() {
    auto items = extractItems("[]");
    QVERIFY(items.isEmpty());
}

void ParsingRegressionTest::extractsNestedMetadata() {
    QByteArray data = R"([
{"Path":"test.bin","Name":"test.bin","Size":0,"IsDir":false,"ModTime":"2024-06-01T00:00:00Z","Metadata":{"key":"value"}}
])";
    auto items = extractItems(data);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.at(0).name, QStringLiteral("test.bin"));
}

void ParsingRegressionTest::parsesTransferStats() {
    QByteArray line = R"({"level":"info","msg":"","source":"","time":"2024-01-15T10:30:00Z","stats":{"bytes":5242880,"totalBytes":10485760,"speed":1048576.0,"errors":0,"checks":10,"totalChecks":20,"transfers":3,"totalTransfers":5,"elapsedTime":5.0,"eta":5.0}})";
    StatsResult stats = parseStatsLine(line);
    QCOMPARE(stats.bytes, 5242880.0);
    QCOMPARE(stats.totalBytes, 10485760.0);
    QCOMPARE(stats.transfers, 3);
    QCOMPARE(stats.totalTransfers, 5);
}

void ParsingRegressionTest::parsesErrorStats() {
    QByteArray line = R"({"level":"error","msg":"file.txt: permission denied","time":"2024-01-15T10:30:00Z","stats":{"bytes":0,"totalBytes":1000,"speed":0,"errors":2,"transfers":0,"totalTransfers":1}})";
    StatsResult stats = parseStatsLine(line);
    QCOMPARE(stats.errors, 2);
}

void ParsingRegressionTest::ignoresNonStatsLine() {
    QByteArray line = R"({"level":"info","msg":"Transferred: 1 file, 5.0 MiB","time":"2024-01-15T10:30:00Z"})";
    StatsResult stats = parseStatsLine(line);
    QCOMPARE(stats.bytes, 0.0);
}

void ParsingRegressionTest::preservesLargeSizes() {
    QByteArray data = R"([{"Path":"bigfile.iso","Name":"bigfile.iso","Size":8589934592,"IsDir":false,"ModTime":"2024-01-01T00:00:00Z"}])";
    auto items = extractItems(data);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.at(0).size, quint64(8589934592ULL));
}

void ParsingRegressionTest::preservesUnicodeNames() {
    QByteArray data = R"([{"Path":"日本語ファイル.txt","Name":"日本語ファイル.txt","Size":10,"IsDir":false,"ModTime":"2024-01-01T00:00:00Z"}])";
    auto items = extractItems(data);
    QCOMPARE(items.size(), 1);
    QVERIFY(items.at(0).name.contains(QString::fromUtf8("日本語")));
}

void ParsingRegressionTest::preservesSpecialPaths() {
    QByteArray data = R"([{"Path":"path with spaces/file (1).txt","Name":"file (1).txt","Size":1,"IsDir":false,"ModTime":"2024-01-01T00:00:00Z"}])";
    auto items = extractItems(data);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.at(0).name, QStringLiteral("file (1).txt"));
    QCOMPARE(items.at(0).path,
             QStringLiteral("path with spaces/file (1).txt"));
}

QTEST_MAIN(ParsingRegressionTest)
