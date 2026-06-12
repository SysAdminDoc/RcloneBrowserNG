#include "export_list_writer.h"

#include <QBuffer>
#include <QDebug>
#include <cstdlib>

namespace {

void require(bool condition, const QString &message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

QByteArray writeListing(const QByteArray &json, ExportListFormat format) {
  QByteArray output;
  QBuffer buffer(&output);
  require(buffer.open(QIODevice::WriteOnly), "failed to open output buffer");

  QString error;
  require(WriteExportListFromLsjson(&buffer, json, format, &error),
          "writer failed: " + error);

  return output;
}

} // namespace

int main() {
  const QByteArray json = R"([
    {
      "Path": "folder/a, quoted \"file\".txt",
      "Name": "a, quoted \"file\".txt",
      "Size": 42,
      "ModTime": "2026-01-02T03:04:05.000",
      "IsDir": false
    },
    {
      "Path": "folder/line\nbreak.txt",
      "Name": "line\nbreak.txt",
      "Size": 7,
      "ModTime": "2026-02-03T04:05:06.000",
      "IsDir": false
    },
    {
      "Path": "folder/subdir",
      "Name": "subdir",
      "IsDir": true
    }
  ])";

  const QByteArray csv = writeListing(json, ExportListFormat::Csv);
  require(csv.contains("\"folder/a, quoted \"\"file\"\".txt\",\"2026-01-02 "
                       "03:04:05\",42\n"),
          "CSV output did not quote comma/quote fields correctly");
  require(csv.contains("\"folder/line\nbreak.txt\",\"2026-02-03 "
                       "04:05:06\",7\n"),
          "CSV output did not preserve newline fields");
  require(!csv.contains("subdir"), "CSV output included directories");

  const QByteArray text = writeListing(json, ExportListFormat::Text);
  require(text.contains("folder/a, quoted \"file\".txt\n"),
          "text output changed ordinary punctuation");
  require(text.contains("folder/line\\nbreak.txt\n"),
          "text output did not escape newline paths");
  require(!text.contains("subdir"), "text output included directories");

  QByteArray malformed;
  QBuffer malformedBuffer(&malformed);
  require(malformedBuffer.open(QIODevice::WriteOnly),
          "failed to open malformed output buffer");
  QString error;
  require(!WriteExportListFromLsjson(&malformedBuffer, "{", ExportListFormat::Csv,
                                     &error),
          "malformed JSON unexpectedly succeeded");
  require(error.contains("Failed to parse rclone lsjson output"),
          "malformed JSON error was not actionable");

  return 0;
}
