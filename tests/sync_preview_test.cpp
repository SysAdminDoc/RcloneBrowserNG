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

    // rclone logs the move of the old copy beside the copy that replaces it
    // (measured), so a lone move is never one.
    const Summary renamed = Parse(
        R"({"level":"notice","skipped":"move to a.txt.bak","size":4,"object":"a.txt"})"
        "\n"
        R"({"level":"notice","skipped":"copy","size":12,"object":"a.txt"})");
    QCOMPARE(renamed.toDelete, 0);
    QCOMPARE(renamed.replacedIntoBackup, 1);
    QCOMPARE(summary.replacedIntoBackup, 1);
  }

  // --track-renames logs a rename as "move to <new name>" on the old name,
  // with no copy, and it deletes nothing: a real run at --max-delete 0
  // finishes (measured on v1.76.0, with and without a backup). A preview
  // once took it for a replaced file and warned about the backup.
  void aRenameIsNotABackupMove() {
    const Summary renames = Parse(
        R"JSON({"level":"notice","msg":"Skipped move to renamed.txt as --dry-run is set (size 17)","object":"old.txt","skipped":"move to renamed.txt","size":17})JSON"
        "\n"
        // moc reads raw strings as plain ones, so an escaped quote inside
        // one hides the test class from it. This line is a plain literal.
        "{\"level\":\"info\",\"msg\":\"Renamed from \\\"old.txt\\\"\","
        "\"object\":\"renamed.txt\"}");
    QCOMPARE(renames.replacedIntoBackup, 0);
    QCOMPARE(renames.toDelete, 0);
    const auto nice = [](qint64 bytes) { return QString("%1 B").arg(bytes); };
    QCOMPARE(SyncPreview::CapWarnings(renames, 0, 1, nice), QStringList());

    // A rename and a replaced file in one sync with --backup-dir.
    const Summary both = Parse(
        R"({"level":"notice","object":"b.txt","skipped":"move","size":3})"
        "\n"
        R"({"level":"notice","object":"b.txt","skipped":"copy","size":5})"
        "\n"
        R"({"level":"notice","object":"old.txt","skipped":"move to renamed.txt","size":17})");
    QCOMPARE(both.replacedIntoBackup, 1);
    QCOMPARE(both.toTransfer, 1);

    // Under `rclone move` the transfer is a move too, so when an object moves
    // twice, one of the moves is its old copy going into the backup.
    const Summary moved = Parse(
        R"({"level":"notice","object":"a.txt","skipped":"move","size":3})"
        "\n"
        R"({"level":"notice","object":"a.txt","skipped":"move","size":12})"
        "\n"
        R"({"level":"notice","object":"c.txt","skipped":"move","size":2})");
    QCOMPARE(moved.replacedIntoBackup, 1);
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

    // A move into the backup counts one to three times (see the next test),
    // so the warning cannot promise how many get through before the stop.
    // It used to say "after deleting or moving 4", true only when each move
    // counts once.
    summary.toBackup = 2;
    const QStringList mixed = SyncPreview::CapWarnings(summary, 4, -1, nice);
    QCOMPARE(mixed.size(), 1);
    QVERIFY2(mixed[0].contains("partway through deleting or moving them") &&
                 !mixed[0].contains("after"),
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
    QVERIFY2(count[0].contains("older copy") &&
                 count[0].contains("can't move files") &&
                 count[0].contains("could stop at the delete cap of 2"),
             qPrintable(count[0]));
    // Three counts a move is the most: an older copy deleted, then a copy
    // and delete when the rename fails onto another drive. On the memory
    // backend, which copies but cannot move, two extras into an empty
    // backup failed at --max-delete 3 and finished at 4 (measured).
    QCOMPARE(SyncPreview::CapWarnings(summary, 4, -1, nice).size(), 1);
    QCOMPARE(SyncPreview::CapWarnings(summary, 5, -1, nice).size(), 1);
    QCOMPARE(SyncPreview::CapWarnings(summary, 6, -1, nice), QStringList());

    // The size of an older copy isn't in the dry run, so any size cap could
    // be the one that stops it.
    const QStringList size = SyncPreview::CapWarnings(summary, -1, 1000, nice);
    QCOMPARE(size.size(), 1);
    QVERIFY2(size[0].contains("could stop at --max-delete-size (1000 B)"),
             qPrintable(size[0]));
    QVERIFY2(SyncPreview::CapWarnings(summary, 3, 1000, nice)[0].contains(
                 "the delete cap of 3 or --max-delete-size (1000 B)"),
             "both caps named");

    // The old copy of a replaced file adds up to two.
    summary.replacedIntoBackup = 1;
    QCOMPARE(SyncPreview::CapWarnings(summary, 7, -1, nice).size(), 1);
    QCOMPARE(SyncPreview::CapWarnings(summary, 8, -1, nice), QStringList());

    // No backup, no older copies to count.
    Summary plain;
    plain.toDelete = 2;
    plain.deleteBytes = 4;
    QCOMPARE(SyncPreview::CapWarnings(plain, 2, 1000, nice), QStringList());
  }

  // A sync that only replaces files deletes nothing, yet on the memory
  // backend one replaced file into an empty backup failed at --max-delete
  // 0 (measured). The summary opens for it with its own title.
  void warnsWhenOnlyReplacedFilesCanTripTheCap() {
    const auto nice = [](qint64 bytes) { return QString("%1 B").arg(bytes); };
    const Summary replaced = Parse(
        R"({"level":"notice","object":"a.txt","skipped":"move","size":3})"
        "\n"
        R"({"level":"notice","object":"a.txt","skipped":"copy","size":12})"
        "\n"
        R"({"level":"notice","object":"b.txt","skipped":"move to b.txt.bak","size":3})"
        "\n"
        R"({"level":"notice","object":"b.txt","skipped":"copy","size":9})");
    QVERIFY(!replaced.deletesAnything());
    QCOMPARE(replaced.replacedIntoBackup, 2);
    QCOMPARE(SyncPreview::DeletionTitle(replaced),
             QStringLiteral("Sync will move old copies of 2 files into the "
                            "backup"));
    const QStringList atZero = SyncPreview::CapWarnings(replaced, 0, -1, nice);
    QCOMPARE(atZero.size(), 1);
    QVERIFY2(atZero[0].contains("could stop at the delete cap of 0"),
             qPrintable(atZero[0]));
    QCOMPARE(SyncPreview::CapWarnings(replaced, 3, -1, nice).size(), 1);
    QCOMPARE(SyncPreview::CapWarnings(replaced, 4, -1, nice), QStringList());
    QCOMPARE(SyncPreview::CapWarnings(replaced, -1, 100, nice).size(), 1);
    QCOMPARE(SyncPreview::CapWarnings(replaced, -1, -1, nice), QStringList());

    // Plain copies into a destination without a backup warn about nothing.
    const Summary copies = Parse(
        R"({"level":"notice","object":"a.txt","skipped":"copy","size":12})");
    QCOMPARE(SyncPreview::CapWarnings(copies, 0, 0, nice), QStringList());
  }
};

QTEST_APPLESS_MAIN(SyncPreviewTest)
#include "sync_preview_test.moc"
