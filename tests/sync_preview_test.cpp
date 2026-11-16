#include "sync_preview.h"
#include "test_rclone.h"

#include <QTemporaryDir>
#include <QTest>

using SyncPreview::Parse;
using SyncPreview::Summary;

// The flags a measured run used.
static SyncPreview::RunFlags Flags(bool backup, bool renames) {
  SyncPreview::RunFlags flags;
  flags.backup = backup;
  flags.renames = renames;
  return flags;
}

// One dry-run line the way rclone writes it.
static QByteArray Line(const char *object, const char *skipped, int size) {
  return QByteArray("{\"level\":\"notice\",\"object\":\"") + object +
         "\",\"skipped\":\"" + skipped + "\",\"size\":" +
         QByteArray::number(size) + "}\n";
}

static QString Nice(qint64 bytes) { return QString("%1 B").arg(bytes); }

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
    // (measured).
    const Summary renamed = Parse(
        R"({"level":"notice","skipped":"move to a.txt.bak","size":4,"object":"a.txt"})"
        "\n"
        R"({"level":"notice","skipped":"copy","size":12,"object":"a.txt"})");
    QCOMPARE(renamed.toDelete, 0);
    QCOMPARE(renamed.replacedIntoBackup, 1);
    QCOMPARE(summary.replacedIntoBackup, 1);
  }

  // --track-renames logs a rename as "move to <new name>" on the old name,
  // with no copy. On local it deletes nothing (a real run at --max-delete 0
  // finishes), but on the memory backend, which can copy and not move, the
  // rename is a copy and a delete, and a rename-only sync failed at 0 and
  // finished at 1 (measured on v1.76.0). A preview once took it for a
  // replaced file and warned about the backup.
  void aRenameIsNotABackupMove() {
    const Summary renames = Parse(
        R"JSON({"level":"notice","msg":"Skipped move to renamed.txt as --dry-run is set (size 17)","object":"old.txt","skipped":"move to renamed.txt","size":17})JSON"
        "\n"
        // moc reads raw strings as plain ones, so an escaped quote inside
        // one hides the test class from it. This line is a plain literal.
        "{\"level\":\"info\",\"msg\":\"Renamed from \\\"old.txt\\\"\","
        "\"object\":\"renamed.txt\"}",
        20, SyncPreview::Command::Sync, Flags(false, true));
    QCOMPARE(renames.replacedIntoBackup, 0);
    QCOMPARE(renames.renamed, 1);
    QCOMPARE(renames.toDelete, 0);
    QCOMPARE(SyncPreview::DeletionTitle(renames),
             QStringLiteral("Sync will rename 1 file"));
    const auto nice = [](qint64 bytes) { return QString("%1 B").arg(bytes); };
    const QStringList atZero = SyncPreview::CapWarnings(renames, 0, -1, nice);
    QCOMPARE(atZero.size(), 1);
    QVERIFY2(atZero[0].contains("A rename counts too") &&
                 !atZero[0].contains("backup") &&
                 atZero[0].contains("This sync could stop at the delete cap "
                                    "of 0"),
             qPrintable(atZero[0]));
    QCOMPARE(SyncPreview::CapWarnings(renames, 2, -1, nice), QStringList());

    // A rename and a replaced file in one sync with --backup-dir.
    const Summary both = Parse(
        R"({"level":"notice","object":"b.txt","skipped":"move","size":3})"
        "\n"
        R"({"level":"notice","object":"b.txt","skipped":"copy","size":5})"
        "\n"
        R"({"level":"notice","object":"old.txt","skipped":"move to renamed.txt","size":17})",
        20, SyncPreview::Command::Sync, Flags(true, true));
    QCOMPARE(both.replacedIntoBackup, 1);
    QCOMPARE(both.renamed, 1);
    QCOMPARE(both.toTransfer, 1);
    QCOMPARE(SyncPreview::DeletionTitle(both),
             QStringLiteral("Sync will rename 1 file and move old copies of 1 "
                            "file into the backup"));

    // Under `rclone move` the transfer is a move too, so when an object moves
    // twice, one of the moves is its old copy going into the backup. The
    // transfer is logged last.
    const Summary moved = Parse(
        R"({"level":"notice","object":"a.txt","skipped":"move","size":3})"
        "\n"
        R"({"level":"notice","object":"a.txt","skipped":"move","size":12})"
        "\n"
        R"({"level":"notice","object":"c.txt","skipped":"move","size":2})",
        20, SyncPreview::Command::Move);
    QCOMPARE(moved.replacedIntoBackup, 1);
    QCOMPARE(moved.moved, 2);
    QCOMPARE(moved.renamed, 0);
    QCOMPARE(moved.toTransfer, 2);
    QCOMPARE(moved.transferBytes, static_cast<qint64>(14));
    QCOMPARE(SyncPreview::Headline(moved), QStringLiteral("2 to transfer."));
    QCOMPARE(SyncPreview::DeletionTitle(moved),
             QStringLiteral("Move will move 2 files and move old copies of 1 "
                            "file into the backup"));
  }

  // What each dry run below logged, and the lowest --max-delete its real run
  // finished at, all measured on rclone v1.76.0. A run that fails at a cap
  // has to get the warning there, or Run goes straight into the failure.
  // Local runs were on Windows, whose file names ignore case; the backup
  // already held an older copy where the name says so.
  void warnsAtEveryCapTheRealRunFailsAt_data() {
    QTest::addColumn<QByteArray>("log");
    QTest::addColumn<int>("command");
    QTest::addColumn<int>("firstCapThatFinished");
    QTest::addColumn<int>("replaced");
    QTest::addColumn<int>("renamed");
    QTest::addColumn<int>("moved");
    // --backup-dir or --suffix, and --track-renames or --fix-case.
    QTest::addColumn<bool>("backup");
    QTest::addColumn<bool>("renames");

    const int sync = static_cast<int>(SyncPreview::Command::Sync);
    const int move = static_cast<int>(SyncPreview::Command::Move);

    // A.txt over a.txt with --backup-dir. The move and the copy name the
    // file differently, which once hid the replaced file.
    QTest::newRow("case only, older copy")
        << Line("a.txt", "move", 3) + Line("A.txt", "copy", 12) << sync << 1
        << 1 << 0 << 0 << true << false;
    // An NFC name over the same name in NFD.
    QTest::newRow("normalization, older copy")
        << Line("cafe\\u0301.txt", "move", 3) +
               Line("caf\\u00e9.txt", "copy", 12)
        << sync << 1 << 1 << 0 << 0 << true << false;
    QTest::newRow("name-transform all,uppercase, older copy")
        << Line("A.TXT", "move", 3) + Line("a.txt", "copy", 12) << sync << 1
        << 1 << 0 << 0 << true << false;
    // The backup on another drive with older copies of both files, so the
    // extra file counts three times and the replaced one twice.
    QTest::newRow("backup on another drive")
        << Line("a.txt", "move", 3) +
               Line("k.txt", "update modification time", 1) +
               Line("A.txt", "copy", 12) +
               Line("x.txt", "move into backup dir", 5)
        << sync << 5 << 1 << 0 << 0 << true << false;
    QTest::newRow("suffix-keep-extension")
        << Line("a.tar.gz", "move to a-old.tar.gz", 3) +
               Line("a.tar.gz", "copy", 12)
        << sync << 0 << 1 << 0 << 0 << true << false;
    // With --track-renames.
    QTest::newRow("rename on local")
        << Line("old.txt", "move to renamed.txt", 16) << sync << 0 << 0 << 1
        << 0 << false << true;
    QTest::newRow("rename on memory")
        << Line("old.txt", "move to renamed.txt", 16) << sync << 1 << 0 << 1
        << 0 << false << true;
    QTest::newRow("two renames on memory")
        << Line("o2.txt", "move to r2.txt", 16) +
               Line("o1.txt", "move to r1.txt", 16)
        << sync << 2 << 0 << 2 << 0 << false << true;
    // --fix-case on the memory backend, an NFC name over the same name in
    // NFD, with an older copy in the backup under the NFD name. The rename
    // goes through a temporary name, a copy and delete each time, and then
    // the renamed file's old copy moves into the backup, which the dry run
    // doesn't list. The preview once read the rename as that move.
    QTest::newRow("fix-case with --backup-dir on memory")
        << Line("cafe\\u0301.txt", "move to caf\\u00e9.txt", 3) +
               Line("caf\\u00e9.txt", "copy", 19)
        << sync << 3 << 1 << 1 << 0 << true << true;
    QTest::newRow("fix-case of two files with --backup-dir on memory")
        << Line("re\\u0301sume\\u0301.doc", "move to r\\u00e9sum\\u00e9.doc",
                3) +
               Line("cafe\\u0301.txt", "move to caf\\u00e9.txt", 3) +
               Line("r\\u00e9sum\\u00e9.doc", "copy", 19) +
               Line("caf\\u00e9.txt", "copy", 19)
        << sync << 6 << 2 << 2 << 0 << true << true;
    QTest::newRow("fix-case with --suffix on memory")
        << Line("cafe\\u0301.txt", "move to caf\\u00e9.txt", 3) +
               Line("caf\\u00e9.txt", "copy", 19)
        << sync << 3 << 1 << 1 << 0 << true << true;
    // --refresh-times with --backup-dir on rclone's own WebDAV server,
    // which can't set a file's time. Both files are uploaded again and
    // their older backup copies deleted, which the dry run doesn't list.
    QTest::newRow("refresh-times with --backup-dir on WebDAV")
        << Line("f1.txt", "update modification time", 65) +
               Line("f0.txt", "update modification time", 65)
        << sync << 2 << 0 << 0 << 0 << true << false;
    // rclone move with --delete-excluded, which deletes nothing at the
    // destination. Replacing a local file deletes the old one first.
    QTest::newRow("move over a local file")
        << Line("a.txt", "move", 12) << move << 1 << 0 << 0 << 1 << false
        << false;
    // From local to memory, each moved file is copied, then deleted.
    QTest::newRow("move between remotes")
        << Line("m.txt", "move", 7) + Line("n.txt", "move", 8) << move << 2
        << 0 << 0 << 2 << false << false;
  }

  void warnsAtEveryCapTheRealRunFailsAt() {
    QFETCH(QByteArray, log);
    QFETCH(int, command);
    QFETCH(int, firstCapThatFinished);
    QFETCH(int, replaced);
    QFETCH(int, renamed);
    QFETCH(int, moved);
    QFETCH(bool, backup);
    QFETCH(bool, renames);

    const Summary summary =
        Parse(log, 20, static_cast<SyncPreview::Command>(command),
              Flags(backup, renames));
    QCOMPARE(summary.replacedIntoBackup, replaced);
    QCOMPARE(summary.renamed, renamed);
    QCOMPARE(summary.moved, moved);

    const auto nice = [](qint64 bytes) { return QString("%1 B").arg(bytes); };
    for (int cap = 0; cap < firstCapThatFinished; cap++) {
      const QStringList warnings = SyncPreview::CapWarnings(summary, cap, -1,
                                                            nice);
      QVERIFY2(warnings.join(' ').contains(
                   QString("could stop at the delete cap of %1").arg(cap)) ||
                   warnings.join(' ').contains(
                       QString("The delete cap is %1").arg(cap)),
               qPrintable(QString("cap %1: %2").arg(cap).arg(
                   warnings.join(" | "))));
    }
    // Every one of these runs moves something, so the summary has a
    // title for it.
    QVERIFY2(!SyncPreview::DeletionTitle(summary).contains("could stop"),
             qPrintable(SyncPreview::DeletionTitle(summary)));
  }

  // On Windows, whose names ignore case, --fix-case renames a.txt to A.txt
  // and then replaces it. With no backup set, the summary still said old
  // copies would go into the backup and named --backup-dir. Under Move it
  // read the rename and the moved file as one file moving twice.
  void aFixCaseRenameIsARename() {
    const Summary sync =
        Parse(Line("a.txt", "move to A.txt", 3) + Line("A.txt", "copy", 12),
              20, SyncPreview::Command::Sync, Flags(false, true));
    QCOMPARE(sync.renamed, 1);
    QCOMPARE(sync.replacedIntoBackup, 0);
    QCOMPARE(SyncPreview::DeletionTitle(sync),
             QStringLiteral("Sync will rename 1 file"));
    QVERIFY2(!SyncPreview::NoDeleteDetail(sync).contains("backup"),
             qPrintable(SyncPreview::NoDeleteDetail(sync)));

    const QByteArray moveLog =
        Line("a.txt", "move to A.txt", 3) + Line("A.txt", "move", 12);
    const Summary move =
        Parse(moveLog, 20, SyncPreview::Command::Move, Flags(false, true));
    QCOMPARE(move.renamed, 1);
    QCOMPARE(move.moved, 1);
    QCOMPARE(move.replacedIntoBackup, 0);
    QCOMPARE(move.transferBytes, static_cast<qint64>(12));
    QCOMPARE(SyncPreview::DeletionTitle(move),
             QStringLiteral("Move will move 1 file and rename 1 file"));

    // With a backup, the renamed file's old copy goes into it.
    QCOMPARE(Parse(moveLog, 20, SyncPreview::Command::Move, Flags(true, true))
                 .replacedIntoBackup,
             1);
    // A file whose contents match is only renamed, so nothing goes into
    // the backup.
    QCOMPARE(Parse(Line("a.txt", "move to A.txt", 3), 20,
                   SyncPreview::Command::Sync, Flags(true, true))
                 .replacedIntoBackup,
             0);
  }

  // --name-transform file,prefix=x_ with --backup-dir moves the old copy
  // of a replaced x_a.txt to x_x_a.txt in the backup, logged beside the
  // copy of a.txt (measured on v1.76.0). Nothing renames without
  // --track-renames or --fix-case, and the summary once called it a rename.
  void aNameTransformMoveIsABackupMove() {
    const Summary summary = Parse(
        Line("x_a.txt", "move to x_x_a.txt", 3) + Line("a.txt", "copy", 12),
        20, SyncPreview::Command::Sync, Flags(true, false));
    QCOMPARE(summary.replacedIntoBackup, 1);
    QCOMPARE(summary.renamed, 0);
    QCOMPARE(SyncPreview::DeletionTitle(summary),
             QStringLiteral("Sync will move old copies of 1 file into the "
                            "backup"));
  }

  // The WebDAV run above also deleted 26 B of older backup copies, so a
  // size cap could stop it too. Without a backup the file is uploaded over
  // its old copy, or the old copy is removed without a count
  // (fs/operations/operations.go), so nothing counts.
  void timestampUpdatesCountWithABackup() {
    const QByteArray log = Line("f1.txt", "update modification time", 65) +
                           Line("f0.txt", "update modification time", 65);
    const Summary backedUp =
        Parse(log, 20, SyncPreview::Command::Sync, Flags(true, false));
    QCOMPARE(backedUp.toUpdate, 2);
    QCOMPARE(SyncPreview::DeletionTitle(backedUp),
             QStringLiteral("Sync will update the time on 2 files"));
    QVERIFY2(SyncPreview::NoDeleteDetail(backedUp).contains(
                 "can't change a file's time"),
             qPrintable(SyncPreview::NoDeleteDetail(backedUp)));
    const QStringList size = SyncPreview::CapWarnings(backedUp, -1, 25, Nice);
    QCOMPARE(size.size(), 1);
    QVERIFY2(size[0].contains("can't change a file's time") &&
                 size[0].contains("could stop at --max-delete-size (25 B)"),
             qPrintable(size[0]));
    QCOMPARE(SyncPreview::CapWarnings(backedUp, 4, -1, Nice), QStringList());

    const Summary plain =
        Parse(log, 20, SyncPreview::Command::Sync, Flags(false, false));
    QCOMPARE(SyncPreview::CapWarnings(plain, 0, 0, Nice), QStringList());
  }

  void readsTheFlagsTheWayRcloneDoes_data() {
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<bool>("backup");
    QTest::addColumn<bool>("renames");

    QTest::newRow("none") << QStringList{"sync", "a", "b"} << false << false;
    QTest::newRow("--backup-dir x")
        << QStringList{"sync", "a", "b", "--backup-dir", "x"} << true << false;
    QTest::newRow("--backup-dir=x")
        << QStringList{"sync", "--backup-dir=x", "a", "b"} << true << false;
    QTest::newRow("--backup-dir=") << QStringList{"sync", "--backup-dir="}
                                   << false << false;
    QTest::newRow("--suffix .bak")
        << QStringList{"sync", "--suffix", ".bak"} << true << false;
    QTest::newRow("--suffix=") << QStringList{"sync", "--suffix="} << false
                               << false;
    // It only changes where --suffix goes.
    QTest::newRow("--suffix-keep-extension alone")
        << QStringList{"sync", "--suffix-keep-extension"} << false << false;
    QTest::newRow("the last --backup-dir wins")
        << QStringList{"sync", "--backup-dir", "x", "--backup-dir", ""}
        << false << false;
    QTest::newRow("--fix-case") << QStringList{"sync", "--fix-case"} << false
                                << true;
    QTest::newRow("--fix-case=false")
        << QStringList{"sync", "--fix-case", "--fix-case=false"} << false
        << false;
    QTest::newRow("--track-renames=true")
        << QStringList{"sync", "--track-renames=true"} << false << true;
    QTest::newRow("--track-renames=F")
        << QStringList{"sync", "--track-renames=F"} << false << false;
    QTest::newRow("--track-renames-strategy")
        << QStringList{"sync", "--track-renames-strategy", "modtime"} << false
        << false;
    // A pattern can look like a flag.
    QTest::newRow("--exclude --fix-case")
        << QStringList{"sync", "--exclude", "--fix-case"} << false << false;
    QTest::newRow("--filter --backup-dir")
        << QStringList{"sync", "--filter", "--backup-dir", "x"} << false
        << false;
    // A string flag takes the next argument, whatever it looks like.
    QTest::newRow("--backup-dir --fix-case")
        << QStringList{"sync", "--backup-dir", "--fix-case"} << true << false;
  }

  void readsTheFlagsTheWayRcloneDoes() {
    QFETCH(QStringList, arguments);
    QFETCH(bool, backup);
    QFETCH(bool, renames);
    const SyncPreview::RunFlags flags = SyncPreview::FlagsIn(arguments);
    QCOMPARE(flags.backup, backup);
    QCOMPARE(flags.renames, renames);
  }

  // A bisync's dry run keeps its caps and stops where the real run would,
  // and its --max-delete is a percentage, so the summary adds no cap
  // warning of its own. It used to say Sync in its title and button.
  void aBisyncIsNamedAndLeavesItsCapsToItsDryRun() {
    const Summary summary =
        Parse(Line("a.txt", "delete", 3) + Line("b.txt", "delete", 3) +
                  Line("c.txt", "delete", 3),
              20, SyncPreview::Command::Bisync);
    QCOMPARE(SyncPreview::DeletionTitle(summary),
             QStringLiteral("Bisync will delete 3 files"));
    QCOMPARE(SyncPreview::CapWarnings(summary, 1, 1, Nice), QStringList());
  }

  // bisync p1 p2 --backup-dir bak --exclude *.log --delete-excluded with an
  // older f.txt in bak (measured on v1.76.0). The dry run passes at
  // --max-delete-size 20B to 29B, but before the real run moves f.txt's old
  // copy into bak it deletes the older one there, 20 B more, and aborts,
  // needing --resync. The summary once gave a bisync no warning at all.
  void aBisyncWithABackupWarnsAboutTheSizeCap() {
    const QStringList arguments{"bisync",     "--backup-dir", "bak",
                                "--exclude",  "*.log",        "--delete-excluded",
                                "p1",         "p2"};
    const Summary summary =
        Parse(Line("f.txt", "move", 11) + Line("f.txt", "copy", 19) +
                  Line("x.log", "move into backup dir", 8) +
                  Line("keep.txt", "move into backup dir", 4) +
                  Line("g.txt", "move into backup dir", 3),
              20, SyncPreview::Command::Bisync,
              SyncPreview::FlagsIn(arguments));
    QCOMPARE(summary.toBackup, 3);
    QCOMPARE(summary.replacedIntoBackup, 1);
    QCOMPARE(SyncPreview::DeletionTitle(summary),
             QStringLiteral("Bisync will move 3 files into the backup"));
    const qint64 passedDryRun[] = {20, 27, 28, 29};
    for (const qint64 cap : passedDryRun) {
      const QStringList warnings =
          SyncPreview::CapWarnings(summary, -1, cap, Nice);
      const QString expected =
          QString("could stop at --max-delete-size (%1 B)").arg(cap);
      QVERIFY2(warnings.size() == 1 && warnings[0].contains(expected),
               qPrintable(warnings.join(" | ")));
    }
    // Its --max-delete is a percentage, and rclone turns the count cap off
    // for the bisync's own deletes.
    QCOMPARE(SyncPreview::CapWarnings(summary, 0, -1, Nice), QStringList());
    QCOMPARE(SyncPreview::RemovalListTitle(summary),
             QStringLiteral("Files this bisync will move into the backup"));
    const QString detail = SyncPreview::RemovalDetail(summary, "15 B");
    QVERIFY2(detail.startsWith("rclone's dry run lists these files, and this "
                               "bisync moves them into the backup") &&
                 !detail.contains("not at the source"),
             qPrintable(detail));
  }

  // A sync's text says why it removes the files, and names
  // --delete-excluded when that's on, since then a file the source has can
  // go too.
  void removalTextNamesTheRun() {
    Summary deleted;
    deleted.toDelete = 2;
    QCOMPARE(SyncPreview::RemovalDetail(deleted, "9 B"),
             QStringLiteral("These files exist at the destination and not "
                            "at the source, so Sync removes them. "
                            "Recovering them depends on whether the remote "
                            "keeps a trash or versions. Freed: 9 B."));
    QCOMPARE(SyncPreview::RemovalListTitle(deleted),
             QStringLiteral("Files this sync will delete"));

    deleted.flags.deleteExcluded = true;
    QVERIFY2(SyncPreview::RemovalDetail(deleted, "9 B")
                 .contains("or a filter leaves them out and "
                           "--delete-excluded is on, so Sync removes them"),
             qPrintable(SyncPreview::RemovalDetail(deleted, "9 B")));

    Summary both;
    both.command = SyncPreview::Command::Bisync;
    both.toDelete = 3;
    both.toBackup = 1;
    QCOMPARE(SyncPreview::RemovalListTitle(both),
             QStringLiteral("Files this bisync will delete or move into the "
                            "backup"));
    QVERIFY2(SyncPreview::RemovalDetail(both, "1 B")
                 .startsWith("rclone's dry run lists these files, and this "
                             "bisync keeps some of them in the backup"),
             qPrintable(SyncPreview::RemovalDetail(both, "1 B")));
    // Bisync takes a folder for each side as well as --backup-dir.
    const QString bisyncFlags =
        "(--backup-dir, --backup-dir1, --backup-dir2 or --suffix)";
    QVERIFY2(SyncPreview::RemovalDetail(both, "1 B").contains(bisyncFlags),
             qPrintable(SyncPreview::RemovalDetail(both, "1 B")));
    Summary moved;
    moved.toDelete = 1;
    moved.toBackup = 1;
    QVERIFY2(SyncPreview::RemovalDetail(moved, "1 B")
                 .contains("into the backup (--backup-dir or --suffix) "
                           "instead"),
             qPrintable(SyncPreview::RemovalDetail(moved, "1 B")));
    moved.command = SyncPreview::Command::Bisync;
    QVERIFY2(SyncPreview::RemovalDetail(moved, "1 B").contains(bisyncFlags),
             qPrintable(SyncPreview::RemovalDetail(moved, "1 B")));
  }

  void readsBisyncBackupsAndDeleteExcluded_data() {
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<bool>("backup");
    QTest::addColumn<bool>("deleteExcluded");

    QTest::newRow("--backup-dir1 x")
        << QStringList{"bisync", "--backup-dir1", "x"} << true << false;
    QTest::newRow("--backup-dir2=x")
        << QStringList{"bisync", "--backup-dir2=x"} << true << false;
    QTest::newRow("--backup-dir1=") << QStringList{"bisync", "--backup-dir1="}
                                    << false << false;
    QTest::newRow("--delete-excluded")
        << QStringList{"sync", "--delete-excluded"} << false << true;
    QTest::newRow("--delete-excluded=false")
        << QStringList{"sync", "--delete-excluded", "--delete-excluded=false"}
        << false << false;
  }

  void readsBisyncBackupsAndDeleteExcluded() {
    QFETCH(QStringList, arguments);
    QFETCH(bool, backup);
    QFETCH(bool, deleteExcluded);
    const SyncPreview::RunFlags flags = SyncPreview::FlagsIn(arguments);
    QCOMPARE(flags.backup, backup);
    QCOMPARE(flags.deleteExcluded, deleteExcluded);
  }

  // rclone takes a flag's default from RCLONE_<FLAG>, and a global.<option>
  // in a connection string overrides the command line (measured on
  // v1.76.0). The summary only read the arguments.
  void readsOptionsFromTheEnvironmentAndPaths_data() {
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QStringList>("environment");
    QTest::addColumn<bool>("backup");
    QTest::addColumn<bool>("renames");

    const QStringList none;
    QTest::newRow("RCLONE_FIX_CASE")
        << QStringList{"sync", "src", "dst"}
        << QStringList{"RCLONE_FIX_CASE=true"} << false << true;
    QTest::newRow("the command line wins over RCLONE_FIX_CASE")
        << QStringList{"sync", "--fix-case=false", "src", "dst"}
        << QStringList{"RCLONE_FIX_CASE=true"} << false << false;
    QTest::newRow("RCLONE_FIX_CASE=false")
        << QStringList{"sync", "src", "dst"}
        << QStringList{"RCLONE_FIX_CASE=false"} << false << false;
    QTest::newRow("RCLONE_BACKUP_DIR")
        << QStringList{"sync", "src", "dst"}
        << QStringList{"RCLONE_BACKUP_DIR=bak"} << true << false;
    QTest::newRow("RCLONE_SUFFIX empty")
        << QStringList{"sync", "src", "dst"} << QStringList{"RCLONE_SUFFIX="}
        << false << false;
    QTest::newRow("RCLONE_TRACK_RENAMES")
        << QStringList{"sync", "src", "dst"}
        << QStringList{"RCLONE_TRACK_RENAMES=1"} << false << true;
    QTest::newRow("global.fix_case=true")
        << QStringList{"sync", "src", ":local,global.fix_case=true:dst"}
        << none << false << true;
    QTest::newRow("global.fix_case with no value")
        << QStringList{"sync", "remote,global.fix_case:dst", "src"} << none
        << false << true;
    QTest::newRow("global. wins over the command line")
        << QStringList{"sync", "--fix-case=false", "src",
                       ":local,global.fix_case=true:dst"}
        << none << false << true;
    QTest::newRow("global.fix_case=false")
        << QStringList{"sync", "--fix-case", "src",
                       "remote,global.fix_case=false:dst"}
        << none << false << false;
    QTest::newRow("override. doesn't reach the sync")
        << QStringList{"sync", "src", ":local,override.fix_case=true:dst"}
        << none << false << false;
    QTest::newRow("a quoted global.suffix")
        << QStringList{"sync", "src", "remote,global.suffix='.o:ld',x=1:dst"}
        << none << true << false;
    // rclone takes --track-renames before it opens the backup folder.
    QTest::newRow("in a flag's =value, too late for track_renames")
        << QStringList{"sync", "--backup-dir=remote,global.track_renames:bak",
                       "src", "dst"}
        << none << true << false;
    QTest::newRow("a backup folder's global.fix_case")
        << QStringList{"sync", "--backup-dir", "remote,global.fix_case:bak",
                       "src", "dst"}
        << none << true << true;
    QTest::newRow("a global.backup_dir's value isn't opened with its holder")
        << QStringList{"sync", "src",
                       ":local,global.backup_dir='r,global.track_renames:b':d"}
        << none << true << false;
    QTest::newRow("a local path with a comma")
        << QStringList{"sync", "src", "/data/a,global.fix_case:b"} << none
        << false << false;
    QTest::newRow("a drive letter")
        << QStringList{"sync", "src", "C:\\a,global.fix_case:b"} << none
        << false << false;
    QTest::newRow("a remote with no options")
        << QStringList{"sync", "src", "remote:a,global.fix_case:b"} << none
        << false << false;
  }

  void readsOptionsFromTheEnvironmentAndPaths() {
    QFETCH(QStringList, arguments);
    QFETCH(QStringList, environment);
    QFETCH(bool, backup);
    QFETCH(bool, renames);
    QProcessEnvironment env;
    for (const QString &variable : environment) {
      env.insert(variable.section('=', 0, 0), variable.section('=', 1));
    }
    const SyncPreview::RunFlags flags =
        SyncPreview::FlagsIn(SyncPreview::EffectiveArguments(arguments, env));
    QCOMPARE(flags.backup, backup);
    QCOMPARE(flags.renames, renames);
  }

  // The caps come the same way: the environment first, a path's global.
  // option last.
  void carriesTheCapsInTheirOrder() {
    QProcessEnvironment env;
    env.insert("RCLONE_MAX_DELETE", "5");
    env.insert("RCLONE_MAX_DELETE_SIZE", "1M");
    env.insert("RCLONE_FILES_FROM", "");
    env.insert("RCLONE_UNRELATED", "x");
    const QStringList arguments{"sync", "--max-delete", "3", "src",
                                "remote,global.max_delete_size=2M:dst"};
    const QStringList expected{"--max-delete=5",
                               "--max-delete-size=1M",
                               "sync",
                               "--max-delete",
                               "3",
                               "src",
                               "remote,global.max_delete_size=2M:dst",
                               "--max-delete-size=2M"};
    QCOMPARE(SyncPreview::EffectiveArguments(arguments, env), expected);

    const QProcessEnvironment dryRun =
        SyncPreview::DryRunEnvironment(env, SyncPreview::Command::Sync);
    QVERIFY(!dryRun.contains("RCLONE_MAX_DELETE"));
    QVERIFY(!dryRun.contains("RCLONE_MAX_DELETE_SIZE"));
    QVERIFY(dryRun.contains("RCLONE_UNRELATED"));
    // A bisync's dry run keeps its caps.
    QVERIFY(SyncPreview::DryRunEnvironment(env, SyncPreview::Command::Bisync)
                .contains("RCLONE_MAX_DELETE"));
  }

  // rclone sets a path's global. options when it opens the path: the
  // source, the destination, the backup folder, then each --compare-dest or
  // else each --copy-dest, and a remote nested in an option after the one
  // holding it. The last one wins, though by the time rclone opens the
  // backup and --compare-dest folders it reads only the caps, --fix-case and
  // those lists (measured on v1.76.0). A pattern and the value of any other
  // flag are never opened.
  void readsGlobalsInTheOrderRcloneOpensPaths_data() {
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QStringList>("environment");
    QTest::addColumn<QStringList>("globals");
    const QStringList none;
    QTest::newRow("open order, not argument order")
        << QStringList{"sync",
                       "--compare-dest",
                       "r,global.max_delete=1:cmp",
                       "--backup-dir=r,global.max_delete=2:bak",
                       "r,global.max_delete=3:src",
                       "r,global.max_delete=4:dst"}
        << none
        << QStringList{"--max-delete=3", "--max-delete=4", "--max-delete=2",
                       "--max-delete=1"};
    QTest::newRow("--copy-dest only without --compare-dest")
        << QStringList{"sync", "--copy-dest", "r,global.max_delete=9:cp",
                       "--compare-dest=r,global.max_delete=1:cmp", "s", "d"}
        << none << QStringList{"--max-delete=1"};
    QTest::newRow("--copy-dest alone")
        << QStringList{"sync", "--copy-dest=r,global.max_delete=9:cp", "s",
                       "d"}
        << none << QStringList{"--max-delete=9"};
    QTest::newRow("the last --backup-dir")
        << QStringList{"sync", "--backup-dir", "r,global.max_delete=1:a",
                       "--backup-dir", "r,global.max_delete=2:b", "s", "d"}
        << none << QStringList{"--max-delete=2"};
    QTest::newRow("a pattern or another flag's value")
        << QStringList{"sync",
                       "--exclude",
                       "r,global.max_delete=1:x",
                       "--filter=r,global.max_delete=2:y",
                       "--user-agent=r,global.max_delete=3:z",
                       "s",
                       "d"}
        << none << none;
    QTest::newRow("a nested remote after its holder")
        << QStringList{"sync", "s",
                       ":alias,remote=':local,global.max_delete=2:d',"
                       "global.max_delete=1:"}
        << none << QStringList{"--max-delete=1", "--max-delete=2"};
    // Bisync's run reads a config copy made before it opens these.
    QTest::newRow("bisync's backup folders count for nothing")
        << QStringList{"bisync", "--backup-dir2", "r,global.max_delete=2:b2",
                       "--backup-dir", "r,global.max_delete=0:b", "p1", "p2"}
        << none << none;
    QTest::newRow("a backup folder from the environment")
        << QStringList{"sync", "s", "d"}
        << QStringList{"RCLONE_BACKUP_DIR=r,global.max_delete=7:eb"}
        << QStringList{"--max-delete=7"};
    QTest::newRow("a path's global.backup_dir replaces --backup-dir")
        << QStringList{"sync", "--backup-dir", "r,global.max_delete=1:b1", "s",
                       ":local,global.backup_dir='r,global.max_delete=5:b2':d"}
        << none
        << QStringList{"--backup-dir=r,global.max_delete=5:b2",
                       "--max-delete=5"};
    QTest::newRow("a compare-dest folder, only the caps and --fix-case")
        << QStringList{"sync", "--compare-dest",
                       "r,global.backup_dir=x,global.track_renames,"
                       "global.fix_case,global.max_delete=2:c",
                       "s", "d"}
        << none << QStringList{"--fix-case=true", "--max-delete=2"};
    QTest::newRow("a backup folder, only the caps and --fix-case")
        << QStringList{"sync", "--backup-dir",
                       "r,global.track_renames,global.suffix=.x,"
                       "global.max_delete_size=1M:b",
                       "s", "d"}
        << none << QStringList{"--max-delete-size=1M"};
    QTest::newRow("a path's global.compare_dest replaces the list")
        << QStringList{"sync", "--compare-dest", "r,global.max_delete=1:c1",
                       "s",
                       ":local,global.compare_dest='\"r,global.max_delete=4:c2"
                       "\"':d"}
        << none
        << QStringList{"--compare-dest=\"r,global.max_delete=4:c2\"",
                       "--max-delete=4"};
    QTest::newRow("a backup folder's global.compare_dest replaces the list")
        << QStringList{"sync", "--compare-dest", "r,global.max_delete=1:c1",
                       "--backup-dir",
                       "r,global.compare_dest='\"r,global.max_delete=3:c2\"':b",
                       "s", "d"}
        << none << QStringList{"--max-delete=3"};
    QTest::newRow("global.compare_dest unquoted splits at its commas")
        << QStringList{"sync", "s",
                       ":local,global.compare_dest='r,global.max_delete=4:c':d"}
        << none << QStringList{"--compare-dest=r,global.max_delete=4:c"};
    QTest::newRow("RCLONE_COMPARE_DEST is a list")
        << QStringList{"sync", "s", "d"}
        << QStringList{"RCLONE_COMPARE_DEST=\"r,global.max_delete=6:c\""}
        << QStringList{"--max-delete=6"};
    QTest::newRow("RCLONE_COMPARE_DEST unquoted splits at its commas")
        << QStringList{"sync", "s", "d"}
        << QStringList{"RCLONE_COMPARE_DEST=r,global.max_delete=6:c"} << none;
    QTest::newRow("--compare-dest adds to RCLONE_COMPARE_DEST")
        << QStringList{"sync", "--compare-dest", "r,global.max_delete=1:c2", "s",
                       "d"}
        << QStringList{"RCLONE_COMPARE_DEST=\"r,global.max_delete=6:c\""}
        << QStringList{"--max-delete=6", "--max-delete=1"};
    QTest::newRow("RCLONE_COPY_DEST is a list")
        << QStringList{"sync", "s", "d"}
        << QStringList{"RCLONE_COPY_DEST=x,\"r,global.max_delete=8:c\""}
        << QStringList{"--max-delete=8"};
    QTest::newRow("every upstream of a union")
        << QStringList{"sync", "s",
                       ":union,upstreams=':local:a :local,global.max_delete=1:b':"}
        << none << QStringList{"--max-delete=1"};
    QTest::newRow("a quoted union upstream")
        << QStringList{"sync", "s",
                       ":union,upstreams='\"r,global.max_delete=2:b\" :local:a':"}
        << none << QStringList{"--max-delete=2"};
    QTest::newRow("a combine upstream after its dir=")
        << QStringList{"sync", "s",
                       ":combine,upstreams='d=:local,global.max_delete=1:b':d"}
        << none << QStringList{"--max-delete=1"};
  }

  void readsGlobalsInTheOrderRcloneOpensPaths() {
    QFETCH(QStringList, arguments);
    QFETCH(QStringList, environment);
    QFETCH(QStringList, globals);
    QProcessEnvironment env;
    for (const QString &variable : environment) {
      env.insert(variable.section('=', 0, 0), variable.section('=', 1));
    }
    const QStringList effective =
        SyncPreview::EffectiveArguments(arguments, env);
    const qsizetype head = environment.size() + arguments.size();
    QCOMPARE(effective.mid(head), globals);
  }

  // RCLONE_FIX_CASE=true with --backup-dir, an NFC name over the same name
  // in NFD, and an older copy in the backup, on the memory backend
  // (measured on v1.76.0). The real run stops at --max-delete 0, 1 and 2,
  // and the summary, reading the move as a backup move, missed 2.
  void aFixCaseFromTheEnvironmentCounts() {
    QProcessEnvironment env;
    env.insert("RCLONE_FIX_CASE", "true");
    const QStringList arguments{"sync", "--backup-dir", "mem:bak", "src",
                                "dst"};
    const Summary summary = Parse(
        Line("cafe\\u0301.txt", "move to caf\\u00e9.txt", 3) +
            Line("caf\\u00e9.txt", "copy", 19),
        20, SyncPreview::Command::Sync,
        SyncPreview::FlagsIn(SyncPreview::EffectiveArguments(arguments, env)));
    QCOMPARE(summary.renamed, 1);
    QCOMPARE(summary.replacedIntoBackup, 1);
    for (int cap = 0; cap <= 2; cap++) {
      const QStringList warnings =
          SyncPreview::CapWarnings(summary, cap, -1, Nice);
      QVERIFY2(warnings.join(' ').contains(
                   QString("could stop at the delete cap of %1").arg(cap)),
               qPrintable(QString("cap %1: %2").arg(cap).arg(
                   warnings.join(" | "))));
    }
  }

  // rclone's march pairs names by NFC and Go's strings.ToLower, which turns
  // I with a dot above into a plain i. --fix-case --ignore-case-sync
  // --backup-dir with an older copy in the backup moved i.txt's old copy
  // there (measured on v1.76.0), and the summary called it only a rename.
  void pairsNamesTheWayRcloneDoes() {
    const Summary summary =
        Parse(Line("i.txt", "move to \\u0130.txt", 3) +
                  Line("\\u0130.txt", "copy", 19),
              20, SyncPreview::Command::Sync, Flags(true, true));
    QCOMPARE(summary.renamed, 1);
    QCOMPARE(summary.replacedIntoBackup, 1);
    QCOMPARE(SyncPreview::DeletionTitle(summary),
             QStringLiteral("Sync will rename 1 file and move old copies of "
                            "1 file into the backup"));
  }

  // -M logs a folder whose metadata changes as "update directory
  // metadata", which isn't a file's time, and nothing in it can count
  // against a cap. The summary once called it a timestamp update.
  void aFolderMetadataUpdateIsNotAFileUpdate() {
    const Summary summary =
        Parse(Line("folder", "update directory metadata", 0), 20,
              SyncPreview::Command::Sync, Flags(true, false));
    QCOMPARE(summary.toUpdate, 0);
    QCOMPARE(SyncPreview::Headline(summary),
             QStringLiteral("Nothing to do: the destination already "
                            "matches."));
    QCOMPARE(SyncPreview::CapWarnings(summary, 0, 0, Nice), QStringList());
  }

  void addsAFilterOnlyToAMove() {
    const QStringList extras = SyncPreview::DryRunExtras(
        SyncPreview::Command::Move, QStringList{"move", "src", "dst"});
    QCOMPARE(extras.size(), 2);
    QCOMPARE(extras[0], QStringLiteral("--filter"));
    QVERIFY(extras[1].startsWith("- /"));
    QCOMPARE(SyncPreview::DryRunExtras(SyncPreview::Command::Sync,
                                       QStringList{"sync", "src", "dst"}),
             QStringList());
    // rclone refuses any other filter next to --files-from.
    QCOMPARE(SyncPreview::DryRunExtras(
                 SyncPreview::Command::Move,
                 QStringList{"move", "--files-from", "list.txt", "src", "dst"}),
             QStringList());
    QCOMPARE(SyncPreview::DryRunExtras(
                 SyncPreview::Command::Move,
                 QStringList{"--files-from0=list.txt", "move", "src", "dst"}),
             QStringList());
  }

  // A Move between two folders on one drive can move the whole folder at
  // once, so its dry run listed only that, and the summary said "Nothing
  // to do". With the destination folder already there, the real run moved
  // each file instead, deleted the old a.txt to replace it, and stopped at
  // --max-delete 0 (measured on v1.76.0).
  void aMoveDryRunListsEachFile() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("no rclone; set RCLONE_BROWSER_TEST_RCLONE");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir root(dir.path());
    QVERIFY(root.mkpath("src"));
    QVERIFY(root.mkpath("dst"));
    // a.txt replaces an older, shorter copy, and b.txt is new.
    const QStringList files{"rclone.conf", "src/a.txt", "src/b.txt",
                            "dst/a.txt"};
    const QStringList contents{"", "new a, longer", "new b", "old a"};
    for (qsizetype i = 0; i < files.size(); i++) {
      QFile file(root.filePath(files[i]));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(contents[i].toUtf8());
    }
    const QStringList run{"move", root.filePath("src"), root.filePath("dst")};
    const QStringList args =
        QStringList{"--config", root.filePath("rclone.conf"), "move"} +
        SyncPreview::DryRunExtras(SyncPreview::Command::Move, run) +
        QStringList{"--dry-run", "--use-json-log", root.filePath("src"),
                    root.filePath("dst")};
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(rclone, args);
    QVERIFY(proc.waitForFinished(60000));
    const QByteArray log = proc.readAll();
    QVERIFY2(proc.exitCode() == 0, log.constData());
    const Summary summary =
        Parse(log, 20, SyncPreview::Command::Move, SyncPreview::FlagsIn(run));
    QVERIFY2(summary.moved == 2, log.constData());
    QVERIFY(!SyncPreview::CapWarnings(summary, 0, -1, Nice).isEmpty());
  }

  // The text for a run that deletes nothing names each kind of move in it.
  void explainsWhatCanCountWhenNothingIsDeleted() {
    Summary replaced;
    replaced.replacedIntoBackup = 1;
    const QString backup = SyncPreview::NoDeleteDetail(replaced);
    QVERIFY2(backup.contains("this sync against the delete cap") &&
                 backup.contains("old copy moved into the backup") &&
                 !backup.contains("rename") && !backup.contains("moved file"),
             qPrintable(backup));

    Summary moves;
    moves.command = SyncPreview::Command::Move;
    moves.moved = 2;
    const QString moving = SyncPreview::NoDeleteDetail(moves);
    QVERIFY2(moving.contains("this move against the delete cap") &&
                 moving.contains("A moved file counts") &&
                 !moving.contains("backup"),
             qPrintable(moving));
    const auto nice = [](qint64 bytes) { return QString("%1 B").arg(bytes); };
    const QStringList warnings = SyncPreview::CapWarnings(moves, 3, -1, nice);
    QCOMPARE(warnings.size(), 1);
    QVERIFY2(warnings[0].contains("This move could stop at the delete cap "
                                  "of 3"),
             qPrintable(warnings[0]));

    Summary renames;
    renames.command = SyncPreview::Command::Copy;
    renames.renamed = 1;
    QVERIFY2(SyncPreview::NoDeleteDetail(renames).contains("A rename at the "
                                                           "destination"),
             qPrintable(SyncPreview::NoDeleteDetail(renames)));
    QCOMPARE(SyncPreview::CommandName(renames.command),
             QStringLiteral("Copy"));
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
