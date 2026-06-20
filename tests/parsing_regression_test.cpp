#include <QByteArray>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdlib>

namespace {
void require(bool condition, const QString &message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

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

int main() {
  {
    QByteArray data = R"([
{"Path":"documents","Name":"documents","Size":-1,"MimeType":"inode/directory","ModTime":"2024-01-15T10:30:00.000Z","IsDir":true},
{"Path":"photo.jpg","Name":"photo.jpg","Size":1234567,"MimeType":"image/jpeg","ModTime":"2024-03-20T14:22:33.456Z","IsDir":false},
{"Path":"notes.txt","Name":"notes.txt","Size":42,"MimeType":"text/plain","ModTime":"2023-12-01T08:00:00.000Z","IsDir":false}
])";
    auto items = extractItems(data);
    require(items.size() == 3, "lsjson standard: wrong count");
    require(items[0].name == "documents", "item 0 name");
    require(items[0].isDir, "item 0 should be dir");
    require(items[1].size == 1234567, "item 1 size");
    require(items[2].size == 42, "item 2 size");
  }

  {
    QByteArray chunk1 = R"([{"Path":"file1.txt","Name":"file1.txt","Si)";
    QByteArray chunk2 = R"(ze":100,"IsDir":false,"ModTime":"2024-01-01T00:00:00Z"},{"Pa)";
    QByteArray chunk3 = R"(th":"file2.txt","Name":"file2.txt","Size":200,"IsDir":false,"ModTime":"2024-01-02T00:00:00Z"}])";
    auto items = extractItems(chunk1 + chunk2 + chunk3);
    require(items.size() == 2, "chunked: wrong count");
    require(items[0].size == 100, "chunked item 0 size");
    require(items[1].size == 200, "chunked item 1 size");
  }

  {
    auto items = extractItems("[]");
    require(items.isEmpty(), "empty dir should have 0 items");
  }

  {
    QByteArray data = R"([
{"Path":"test.bin","Name":"test.bin","Size":0,"IsDir":false,"ModTime":"2024-06-01T00:00:00Z","Metadata":{"key":"value"}}
])";
    auto items = extractItems(data);
    require(items.size() == 1, "nested metadata: wrong count");
    require(items[0].name == "test.bin", "nested metadata item name");
  }

  {
    QByteArray line = R"({"level":"info","msg":"","source":"","time":"2024-01-15T10:30:00Z","stats":{"bytes":5242880,"totalBytes":10485760,"speed":1048576.0,"errors":0,"checks":10,"totalChecks":20,"transfers":3,"totalTransfers":5,"elapsedTime":5.0,"eta":5.0}})";
    StatsResult stats = parseStatsLine(line);
    require(stats.bytes == 5242880.0, "stats bytes");
    require(stats.totalBytes == 10485760.0, "stats totalBytes");
    require(stats.transfers == 3, "stats transfers");
    require(stats.totalTransfers == 5, "stats totalTransfers");
  }

  {
    QByteArray line = R"({"level":"error","msg":"file.txt: permission denied","time":"2024-01-15T10:30:00Z","stats":{"bytes":0,"totalBytes":1000,"speed":0,"errors":2,"transfers":0,"totalTransfers":1}})";
    StatsResult stats = parseStatsLine(line);
    require(stats.errors == 2, "error stats: errors");
  }

  {
    QByteArray line = R"({"level":"info","msg":"Transferred: 1 file, 5.0 MiB","time":"2024-01-15T10:30:00Z"})";
    StatsResult stats = parseStatsLine(line);
    require(stats.bytes == 0, "non-stats line should have 0 bytes");
  }

  {
    QByteArray data = R"([{"Path":"bigfile.iso","Name":"bigfile.iso","Size":8589934592,"IsDir":false,"ModTime":"2024-01-01T00:00:00Z"}])";
    auto items = extractItems(data);
    require(items.size() == 1, "large file: wrong count");
    require(items[0].size == 8589934592ULL, "large file: wrong size");
  }

  {
    QByteArray data = R"([{"Path":"日本語ファイル.txt","Name":"日本語ファイル.txt","Size":10,"IsDir":false,"ModTime":"2024-01-01T00:00:00Z"}])";
    auto items = extractItems(data);
    require(items.size() == 1, "unicode: wrong count");
    require(items[0].name.contains(QString::fromUtf8("日本語")), "unicode filename");
  }

  {
    QByteArray data = R"([{"Path":"path with spaces/file (1).txt","Name":"file (1).txt","Size":1,"IsDir":false,"ModTime":"2024-01-01T00:00:00Z"}])";
    auto items = extractItems(data);
    require(items.size() == 1, "special chars: wrong count");
    require(items[0].name == "file (1).txt", "special chars name");
    require(items[0].path == "path with spaces/file (1).txt", "special chars path");
  }

  return 0;
}
