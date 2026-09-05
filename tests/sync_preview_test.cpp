#include "sync_preview.h"

#include <QTest>

using SyncPreview::Parse;
using SyncPreview::Summary;

// Captured verbatim from `rclone sync --dry-run --use-json-log -v` on
// rclone v1.75.0, against a source and destination holding: keep.txt
// (identical), changed.txt (differs), new.txt (source only), and
// obsolete.txt plus alsogone.txt (destination only).
static const char *kRealDryRun = R"JSON({"time":"2026-09-05T07:41:09.529556-04:00","level":"notice","msg":"Config file not found - using defaults","source":"config/config.go:374"}
{"time":"2026-09-05T07:41:12.2636062-04:00","level":"notice","msg":"Skipped copy as --dry-run is set (size 21)","skipped":"copy","size":21,"object":"changed.txt","objectType":"*local.Object","source":"operations/operations.go:2631"}
{"time":"2026-09-05T07:41:12.2636062-04:00","level":"notice","msg":"Skipped copy as --dry-run is set (size 9)","skipped":"copy","size":9,"object":"new.txt","objectType":"*local.Object","source":"operations/operations.go:2631"}
{"time":"2026-09-05T07:41:12.2709411-04:00","level":"notice","msg":"Skipped update modification time as --dry-run is set (size 4)","skipped":"update modification time","size":4,"object":"keep.txt","objectType":"*local.Object","source":"operations/operations.go:2631"}
{"time":"2026-09-05T07:41:12.2750166-04:00","level":"notice","msg":"Skipped delete as --dry-run is set (size 9)","skipped":"delete","size":9,"object":"obsolete.txt","objectType":"*local.Object","source":"operations/operations.go:2631"}
{"time":"2026-09-05T07:41:12.2750166-04:00","level":"notice","msg":"Skipped delete as --dry-run is set (size 13)","skipped":"delete","size":13,"object":"alsogone.txt","objectType":"*local.Object","source":"operations/operations.go:2631"}
{"time":"2026-09-05T07:41:12.2750166-04:00","level":"notice","msg":"stats","stats":{"bytes":30,"deletes":2,"transfers":2},"source":"accounting/stats.go:551"}
)JSON";

class SyncPreviewTest : public QObject {
  Q_OBJECT

private slots:
  void classifiesARealDryRun() {
    const Summary summary = Parse(kRealDryRun);
    QVERIFY2(summary.error.isEmpty(), qPrintable(summary.error));

    QCOMPARE(summary.toDelete, 2);
    QCOMPARE(summary.toTransfer, 2);
    QCOMPARE(summary.toUpdate, 1);
    QCOMPARE(summary.deleteBytes, static_cast<qint64>(22));
    QCOMPARE(summary.transferBytes, static_cast<qint64>(30));

    // The stats line at the end also carries "deletes":2; counting it would
    // double the total, so only per-object "skipped" records are counted.
    QCOMPARE(summary.deletions.size(), 2);
    QVERIFY(summary.deletions.contains(QStringLiteral("obsolete.txt")));
    QVERIFY(summary.deletions.contains(QStringLiteral("alsogone.txt")));
    QCOMPARE(summary.moreDeletions, 0);
    QVERIFY(summary.deletesAnything());
  }

  void reportsNothingToDeleteForACleanSync() {
    // A sync whose destination already matches must not interrupt the user.
    const QByteArray output =
        R"({"level":"notice","msg":"stats","stats":{"deletes":0}})";
    const Summary summary = Parse(output);
    QVERIFY(!summary.deletesAnything());
    QCOMPARE(summary.toDelete, 0);
  }

  void copyOnlyTransferDoesNotLookLikeADeletion() {
    const QByteArray output =
        R"JSON({"level":"notice","msg":"Skipped copy as --dry-run is set (size 5)","skipped":"copy","size":5,"object":"a.txt"})JSON";
    const Summary summary = Parse(output);
    QCOMPARE(summary.toTransfer, 1);
    QCOMPARE(summary.toDelete, 0);
    QVERIFY(!summary.deletesAnything());
  }

  void capsTheListedDeletionsAndCountsTheRest() {
    QByteArray output;
    for (int i = 0; i < 55; ++i) {
      output += QString(R"({"level":"notice","skipped":"delete","size":1,"object":"file%1.txt"})"
                        "\n")
                    .arg(i)
                    .toUtf8();
    }
    const Summary summary = Parse(output, 20);
    QCOMPARE(summary.toDelete, 55);
    QCOMPARE(summary.deletions.size(), 20);
    QCOMPARE(summary.moreDeletions, 35);
    QCOMPARE(summary.deleteBytes, static_cast<qint64>(55));
  }

  void surfacesAnErrorLine() {
    const QByteArray output =
        R"({"level":"error","msg":"directory not found"})";
    const Summary summary = Parse(output);
    QCOMPARE(summary.error, QStringLiteral("directory not found"));
    QVERIFY(!summary.deletesAnything());
  }

  void toleratesPlainTextFromAnOlderRclone() {
    const QByteArray output =
        "NOTICE: not json at all\n"
        R"({"level":"notice","skipped":"delete","size":4,"object":"gone.txt"})"
        "\n";
    const Summary summary = Parse(output);
    QCOMPARE(summary.toDelete, 1);
    QCOMPARE(summary.deletions.first(), QStringLiteral("gone.txt"));
  }

  void headlineNamesEveryNonZeroBucket() {
    const Summary summary = Parse(kRealDryRun);
    const QString headline = SyncPreview::Headline(summary);
    QVERIFY2(headline.contains("2 to transfer"), qPrintable(headline));
    QVERIFY2(headline.contains("1 timestamp update"), qPrintable(headline));
    QVERIFY2(headline.contains("2 to delete"), qPrintable(headline));

    QCOMPARE(SyncPreview::Headline(Summary()),
             QStringLiteral("Nothing to do: the destination already matches."));
  }
};

QTEST_APPLESS_MAIN(SyncPreviewTest)
#include "sync_preview_test.moc"
