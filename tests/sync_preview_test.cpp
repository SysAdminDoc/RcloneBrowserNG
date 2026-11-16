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

  // With --backup-dir a sync moves each extra file aside instead of
  // deleting it, and rclone still counts that move against --max-delete
  // and --max-delete-size (measured). A replaced file's old copy is logged
  // as "move" (or "move to a.txt.bak" with --suffix), which isn't a delete.
  void countsBackupMovesAsDeletions() {
    const QByteArray output = R"JSON({"time":"2026-11-15T20:22:18.9037024-05:00","level":"notice","msg":"Skipped move into backup dir as --dry-run is set (size 8)","skipped":"move into backup dir","size":8,"object":"c.txt","objectType":"*local.Object","source":"operations/operations.go:2729"}
{"time":"2026-11-15T20:22:18.9037024-05:00","level":"notice","msg":"Skipped move into backup dir as --dry-run is set (size 7)","skipped":"move into backup dir","size":7,"object":"b.txt","objectType":"*local.Object","source":"operations/operations.go:2729"}
{"time":"2026-11-15T20:22:18.9037024-05:00","level":"notice","msg":"Skipped move as --dry-run is set (size 4)","skipped":"move","size":4,"object":"a.txt","objectType":"*local.Object","source":"operations/operations.go:2729"}
{"time":"2026-11-15T20:22:18.9037024-05:00","level":"notice","msg":"Skipped copy as --dry-run is set (size 19)","skipped":"copy","size":19,"object":"a.txt","objectType":"*local.Object","source":"operations/operations.go:2729"}
{"time":"2026-11-15T20:22:18.9037024-05:00","level":"notice","msg":"Skipped update modification time as --dry-run is set (size 5)","skipped":"update modification time","size":5,"object":"s.txt","objectType":"*local.Object","source":"operations/operations.go:2729"}
)JSON";
    const Summary summary = Parse(output);
    QCOMPARE(summary.toDelete, 2);
    QCOMPARE(summary.toBackup, 2);
    QCOMPARE(summary.deleteBytes, static_cast<qint64>(15));
    QCOMPARE(summary.toTransfer, 1);
    QCOMPARE(summary.deletions, (QStringList{"c.txt", "b.txt"}));
    QCOMPARE(SyncPreview::Headline(summary),
             QStringLiteral("1 to transfer, 1 timestamp update, 2 to move into "
                            "the backup."));

    const Summary renamed = Parse(
        R"({"level":"notice","skipped":"move to a.txt.bak","size":4,"object":"a.txt"})");
    QCOMPARE(renamed.toDelete, 0);
    QCOMPARE(renamed.replacedIntoBackup, 1);
    QCOMPARE(summary.replacedIntoBackup, 1);
  }

  void titleSaysWhatHappensToTheFiles() {
    Summary deleted;
    deleted.toDelete = 1;
    QCOMPARE(SyncPreview::DeletionTitle(deleted),
             QStringLiteral("Sync will delete 1 file"));

    Summary moved;
    moved.toDelete = 3;
    moved.toBackup = 3;
    QCOMPARE(SyncPreview::DeletionTitle(moved),
             QStringLiteral("Sync will move 3 files into the backup"));

    // The old title called all five moves.
    Summary both;
    both.toDelete = 5;
    both.toBackup = 2;
    QCOMPARE(SyncPreview::DeletionTitle(both),
             QStringLiteral("Sync will delete 3 files and move 2 into the "
                            "backup"));
  }

  void warnsWhenACapStopsTheRun() {
    const auto nice = [](qint64 bytes) { return QString("%1 B").arg(bytes); };
    Summary summary;
    summary.toDelete = 5;
    summary.deleteBytes = 50;

    QCOMPARE(SyncPreview::CapWarnings(summary, -1, -1, nice), QStringList());
    QCOMPARE(SyncPreview::CapWarnings(summary, 5, 50, nice), QStringList());

    const QStringList over = SyncPreview::CapWarnings(summary, 4, 49, nice);
    QCOMPARE(over.size(), 2);
    QVERIFY2(over[0].contains("delete cap is 4") &&
                 over[0].contains("after deleting 4"),
             qPrintable(over[0]));
    QVERIFY2(over[1].contains("--max-delete-size is 49 B") &&
                 over[1].contains("come to 50 B") &&
                 over[1].contains("before deleting"),
             qPrintable(over[1]));

    summary.toBackup = 2;
    const QStringList mixed = SyncPreview::CapWarnings(summary, 4, -1, nice);
    QCOMPARE(mixed.size(), 1);
    QVERIFY2(mixed[0].contains("after deleting or moving 4"),
             qPrintable(mixed[0]));
  }

  // Measured on rclone v1.76.0: two extra files with --backup-dir, and the
  // backup already holding an older old1.txt. The dry run lists two moves,
  // yet --max-delete 2 stops the real run (deleting the older old1.txt
  // counts too) and 3 lets it finish. A replaced file's old copy landing on
  // an older backup copy counts the same way, and so does --suffix.
  void warnsThatOlderBackupCopiesCountAsDeletes() {
    const auto nice = [](qint64 bytes) { return QString("%1 B").arg(bytes); };
    Summary summary;
    summary.toDelete = 2;
    summary.toBackup = 2;
    summary.deleteBytes = 4;

    const QStringList count = SyncPreview::CapWarnings(summary, 2, -1, nice);
    QCOMPARE(count.size(), 1);
    QVERIFY2(count[0].contains("older copies") &&
                 count[0].contains("could stop at the delete cap of 2"),
             qPrintable(count[0]));
    // Every move doubled is the most it can count.
    QCOMPARE(SyncPreview::CapWarnings(summary, 4, -1, nice), QStringList());

    // The size of an older copy isn't in the dry run, so any size cap could
    // be the one that stops it.
    const QStringList size = SyncPreview::CapWarnings(summary, -1, 1000, nice);
    QCOMPARE(size.size(), 1);
    QVERIFY2(size[0].contains("could stop at --max-delete-size (1000 B)"),
             qPrintable(size[0]));
    QVERIFY2(SyncPreview::CapWarnings(summary, 3, 1000, nice)[0].contains(
                 "the delete cap of 3 or --max-delete-size (1000 B)"),
             "both caps named");

    summary.replacedIntoBackup = 1;
    QCOMPARE(SyncPreview::CapWarnings(summary, 4, -1, nice).size(), 1);
    QCOMPARE(SyncPreview::CapWarnings(summary, 5, -1, nice), QStringList());

    // No backup, no older copies to count.
    Summary plain;
    plain.toDelete = 2;
    plain.deleteBytes = 4;
    QCOMPARE(SyncPreview::CapWarnings(plain, 2, 1000, nice), QStringList());
  }
};

QTEST_APPLESS_MAIN(SyncPreviewTest)
#include "sync_preview_test.moc"
