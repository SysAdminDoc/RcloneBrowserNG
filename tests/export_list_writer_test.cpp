#include "export_list_writer.h"

#include <QBuffer>
#include <QTest>

class ExportListWriterTest : public QObject {
  Q_OBJECT

  static const QByteArray sampleJson;

private slots:
  void csvQuotesCommaAndQuoteFields() {
    QByteArray output;
    QBuffer buffer(&output);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QString error;
    QVERIFY(WriteExportListFromLsjson(&buffer, sampleJson,
                                       ExportListFormat::Csv, &error));
    QVERIFY(output.contains(
        "\"folder/a, quoted \"\"file\"\".txt\",\"2026-01-02 03:04:05\",42\n"));
    QVERIFY(output.contains(
        "\"folder/line\nbreak.txt\",\"2026-02-03 04:05:06\",7\n"));
    QVERIFY(!output.contains("subdir"));
  }

  void textEscapesNewlines() {
    QByteArray output;
    QBuffer buffer(&output);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QString error;
    QVERIFY(WriteExportListFromLsjson(&buffer, sampleJson,
                                       ExportListFormat::Text, &error));
    QVERIFY(output.contains("folder/a, quoted \"file\".txt\n"));
    QVERIFY(output.contains("folder/line\\nbreak.txt\n"));
    QVERIFY(!output.contains("subdir"));
  }

  void malformedJsonFails() {
    QByteArray output;
    QBuffer buffer(&output);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QString error;
    QVERIFY(!WriteExportListFromLsjson(&buffer, "{", ExportListFormat::Csv,
                                        &error));
    QVERIFY(error.contains("Failed to parse rclone lsjson output"));
  }
};

const QByteArray ExportListWriterTest::sampleJson = R"([
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

QTEST_MAIN(ExportListWriterTest)
#include "export_list_writer_test.moc"
