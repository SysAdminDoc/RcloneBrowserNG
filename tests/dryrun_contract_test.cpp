#include "job_options.h"
#include "job_options_store.h"
#include "test_rclone.h"
#include "utils.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <new>

namespace {
bool argsContain(const QStringList &args, const QString &flag) {
  return args.contains(flag);
}

QString argAfter(const QStringList &args, const QString &flag) {
  const int index = args.indexOf(flag);
  if (index < 0 || index + 1 >= args.size()) {
    return QString();
  }
  return args.at(index + 1);
}

JobOptions *makeTask(JobOptions::Operation op, bool dryRun) {
  auto jo = new JobOptions(false);
  jo->operation = op;
  jo->dryRun = dryRun;
  jo->source = "/src";
  jo->dest = "remote:dst";
  jo->isFolder = true;
  return jo;
}
} // namespace

class DryRunContractTest : public QObject {
  Q_OBJECT

private slots:
  void serverSideFlagIsOffByDefaultAndOptIn() {
    // Streaming every byte through this machine is the safe
    // default; the flag only appears when asked for.
    JobOptions options;
    options.operation = JobOptions::Copy;
    options.source = "a:src";
    options.dest = "b:dst";
    QVERIFY(!options.getOptions().contains(
        QStringLiteral("--server-side-across-configs")));

    options.serverSideAcrossConfigs = true;
    QVERIFY(options.getOptions().contains(
        QStringLiteral("--server-side-across-configs")));
  }

  // DriveSharedWithMe had no initializer, so a default-built task could add
  // --drive-shared-with-me from whatever was in memory. Filling the storage
  // first makes that show on every run instead of now and then.
  void sharedWithMeIsOffByDefault() {
    alignas(JobOptions) unsigned char storage[sizeof(JobOptions)];
    std::memset(storage, 0xFF, sizeof(storage));
    auto *options = new (storage) JobOptions();
    options->operation = JobOptions::Copy;
    options->source = "a:src";
    options->dest = "b:dst";
    const bool added = options->getOptions().contains(
        QStringLiteral("--drive-shared-with-me"));
    options->~JobOptions();
    QVERIFY(!added);
  }

  // Sync is the only operation that deletes at the destination. Bisync reads
  // --max-delete as a percentage, so a count meant for Sync would mean
  // something else there.
  void deleteCapReachesSyncOnly() {
    auto sync = makeTask(JobOptions::Sync, false);
    QVERIFY(!argsContain(sync->getOptions(), "--max-delete"));
    sync->maxDelete = 100;
    QStringList args = sync->getOptions();
    QCOMPARE(args.count("--max-delete"), 1);
    QCOMPARE(argAfter(args, "--max-delete"), QString("100"));
    sync->maxDelete = 0;
    QCOMPARE(argAfter(sync->getOptions(), "--max-delete"), QString("0"));
    delete sync;

    for (JobOptions::Operation op :
         {JobOptions::Copy, JobOptions::Move, JobOptions::Bisync}) {
      auto other = makeTask(op, false);
      other->maxDelete = 100;
      QVERIFY2(!argsContain(other->getOptions(), "--max-delete"),
               qPrintable(other->getOptions().join(' ')));
      delete other;
    }
  }

  // rclone counts a dry run's deletes against --max-delete, so both preview
  // buttons drop the cap or the list stops partway. The task keeps it.
  void previewsListEveryDeleteAndLeaveTheTaskAlone() {
    auto sync = makeTask(JobOptions::Sync, false);
    sync->maxDelete = 2;
    const QStringList preview = sync->getPreviewOptions();
    QVERIFY(argsContain(preview, "--dry-run"));
    QVERIFY2(!argsContain(preview, "--max-delete"), qPrintable(preview.join(' ')));
    QCOMPARE(sync->maxDelete, 2);
    QVERIFY(!sync->dryRun);
    QCOMPARE(argAfter(sync->getOptions(), "--max-delete"), QString("2"));
    QVERIFY(!argsContain(sync->getOptions(), "--dry-run"));
    delete sync;
  }

  // A history row's Dry Run starts from the arguments the run was saved
  // with, cap and all, so it drops the cap the same way.
  void historyDryRunsDropTheCapToo() {
    const QStringList saved{"sync",   "--delete-during", "--max-delete",
                            "100",    "--verbose",       "--max-delete=5",
                            "src:a",  "dst:b"};
    QCOMPARE(JobOptions::DryRunArguments(saved),
             QStringList() << "sync" << "--dry-run" << "--delete-during"
                           << "--verbose" << "src:a" << "dst:b");

    // Already a dry run: the flag right after the command stays once.
    const QStringList dry{"copy", "--dry-run", "--verbose", "src:a", "dst:b"};
    QCOMPARE(JobOptions::DryRunArguments(dry), dry);
    // Further along it could be the value of the option before it, so the
    // preview adds its own; the second one is harmless.
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"copy", "--verbose", "--dry-run", "src:a", "dst:b"}),
             (QStringList{"copy", "--dry-run", "--verbose", "--dry-run",
                          "src:a", "dst:b"}));

    // A cap with its value missing at the end goes without taking the
    // command or a path with it.
    QCOMPARE(JobOptions::DryRunArguments(QStringList{"sync", "src:a", "--max-delete"}),
             QStringList() << "sync" << "--dry-run" << "src:a");
    QCOMPARE(JobOptions::DryRunArguments(QStringList()),
             QStringList() << "--dry-run");

    // A saved task's own arguments come out the same as its preview's,
    // apart from the dated backup folder, which this task doesn't use.
    auto sync = makeTask(JobOptions::Sync, false);
    sync->maxDelete = 3;
    QCOMPARE(JobOptions::DryRunArguments(sync->getOptions()),
             sync->getPreviewOptions());
    delete sync;
  }

  // The task's extra options are free text and can hold a cap of their own,
  // or a --dry-run=false that would turn the preview into a real run.
  void previewsDropCapsFromExtraOptionsToo() {
    auto sync = makeTask(JobOptions::Sync, false);
    sync->maxDelete = 3;
    sync->extra = "--max-delete=5 --max-delete-size 1G --dry-run=false "
                  "--max-delete-size=2G -n=false --fast-list";
    const QStringList preview = sync->getPreviewOptions();
    QCOMPARE(preview.value(1), QString("--dry-run"));
    QVERIFY2(preview.contains("--fast-list"), qPrintable(preview.join(' ')));
    for (const QString &arg : preview) {
      QVERIFY2(!arg.startsWith("--max-delete") && !arg.contains('=') &&
                   arg != "1G",
               qPrintable(preview.join(' ')));
    }
    // The run itself keeps everything the user typed.
    const QStringList run = sync->getOptions();
    QVERIFY(run.contains("--max-delete-size"));
    QVERIFY(run.contains("--dry-run=false"));
    delete sync;

    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"copy", "-n", "--max-delete-size", "5G", "a:", "b:"}),
             QStringList() << "copy" << "-n" << "a:" << "b:");
  }

  // pflag hands a short cluster's = value to its last letter, so -vn=false
  // is -v plus -n=false and turns the dry run off (measured on v1.76.0:
  // `rclone sync --dry-run -vn=false` deleted the extra file). The cluster
  // keeps its letters and loses the false value.
  void previewsDropADryRunValueInsideAShortCluster() {
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"sync", "-vn=false", "a:", "b:"}),
             (QStringList{"sync", "--dry-run", "-vn", "a:", "b:"}));
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"sync", "-Pvn=0", "a:", "b:"}),
             (QStringList{"sync", "--dry-run", "-Pvn", "a:", "b:"}));
    // Right after the command, the bare -n is surely a flag and does the job.
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"sync", "-n=false", "a:", "b:"}),
             (QStringList{"sync", "-n", "a:", "b:"}));
    // A true value keeps the dry run, and one rclone can't read stops the
    // real run as well, so neither changes.
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"sync", "-n=true", "--dry-run=x", "a:", "b:"}),
             (QStringList{"sync", "--dry-run", "-n=true", "--dry-run=x", "a:",
                          "b:"}));
    // A long flag whose name ends in n isn't a short cluster.
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"sync", "--max-duration=1h", "a:", "b:"}),
             (QStringList{"sync", "--dry-run", "--max-duration=1h", "a:", "b:"}));

    auto sync = makeTask(JobOptions::Sync, false);
    sync->extra = "-vn=false --fast-list";
    const QStringList preview = sync->getPreviewOptions();
    QCOMPARE(preview.count("--dry-run"), 1);
    QVERIFY2(preview.contains("-vn") && !preview.contains("-vn=false"),
             qPrintable(preview.join(' ')));
    delete sync;
  }

  // Any argument can be the value of the option before it, so the walk
  // can't skip a pattern flag's "value" and trust what follows: with
  // `--user-agent --exclude --dry-run=false`, --exclude is the user agent
  // and the dry run went off (measured on v1.76.0 against aa0a236; real
  // rclone deleted the extra file). A false value now loses its value
  // wherever it stands, and every other argument stays put.
  void previewsTurnOffNoMatterWhatComesBefore() {
    const QList<QPair<QStringList, QStringList>> cases{
        {{"--user-agent", "--exclude", "--dry-run=false"},
         {"--user-agent", "--exclude", "--dry-run"}},
        {{"--user-agent", "--exclude", "-n=false"},
         {"--user-agent", "--exclude", "-n"}},
        {{"--suffix", "--include", "-vn=false"},
         {"--suffix", "--include", "-vn"}},
        {{"--log-file", "--filter", "-n=0"}, {"--log-file", "--filter", "-n"}},
        {{"--exclude", "--dry-run=False"}, {"--exclude", "--dry-run"}},
    };
    for (const auto &c : cases) {
      QCOMPARE(JobOptions::DryRunArguments(QStringList{"sync"} << c.first
                                                               << "a:" << "b:"),
               QStringList{"sync"} << "--dry-run" << c.second << "a:" << "b:");
    }
    // A cap right after a pattern flag is that flag's pattern, so it stays.
    QCOMPARE(JobOptions::DryRunArguments(QStringList{
                 "sync", "--exclude", "--max-delete=5", "a:", "b:"}),
             (QStringList{"sync", "--dry-run", "--exclude", "--max-delete=5",
                          "a:", "b:"}));
  }

  // The same inputs through the installed rclone: the destination's extra
  // file is still there afterwards, under its own name.
  void previewsDeleteNothingInRealRclone() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("rclone is not on PATH and RCLONE_BROWSER_TEST_RCLONE is unset");
    }
    const QList<QStringList> extras{
        {"--user-agent", "--exclude", "--dry-run=false"},
        {"--user-agent", "--exclude", "-n=false"},
        {"--suffix", "--include", "-vn=false"},
        {"--log-file", "--filter", "-n=0"},
        {"--exclude", "-n"},
        {"--dry-run=false"},
    };
    for (const QStringList &extra : extras) {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      const QString src = dir.filePath("src");
      const QString dst = dir.filePath("dst");
      QVERIFY(QDir().mkpath(src) && QDir().mkpath(dst));
      for (const QString &path : {src + "/keep.txt", dst + "/extra.txt",
                                  dir.filePath("rclone.conf")}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(path.endsWith(".conf") ? "" : "x");
      }
      const QStringList args = JobOptions::DryRunArguments(
          QStringList{"sync"} << extra << src << dst);
      QProcess process;
      process.setWorkingDirectory(dir.path());
      process.start(rclone, QStringList{"--config", dir.filePath("rclone.conf")}
                                << args);
      QVERIFY(process.waitForFinished(60000));
      // A run rclone refused would leave the file too, and prove nothing.
      QVERIFY2(process.exitStatus() == QProcess::NormalExit &&
                   process.exitCode() == 0,
               qPrintable(args.join(' ') + "\n" +
                          QString::fromUtf8(process.readAllStandardError())));
      QVERIFY2(QFile::exists(dst + "/extra.txt"), qPrintable(args.join(' ')));
      QCOMPARE(QDir(dst).entryList(QDir::Files), QStringList{"extra.txt"});
    }
  }

  // rclone applies a global.dry_run option to the whole run when it opens a
  // path that carries one, after it read the flags (addConfigToContext in
  // fs/newfs.go). Measured on v1.76.0, each of these deleted for real in a
  // dry run, a remote nested in another's option too. A key ends at = , or
  // : and matches exactly, so the others stay as typed.
  void previewsIgnoreGlobalDryRunInPaths() {
    const QStringList saved{
        "sync",
        "src",
        ":local,global.dry_run=false:dst",
        "--backup-dir=:local,global.dry_run=false:bak",
        "--compare-dest",
        "remote,global.dry_run,x=1:cmp",
        ":alias,remote=':local,global.dry_run=False:d',global.dry_run=0:",
        ":local,global.dry_run_kept=false:k",
        ":local,global.DRY_RUN=false:u",
        ":local,xglobal.dry_run=false:v"};
    const QStringList expected{
        "sync",
        "--dry-run",
        "src",
        ":local,ignored.global.dry_run=false:dst",
        "--backup-dir=:local,ignored.global.dry_run=false:bak",
        "--compare-dest",
        "remote,ignored.global.dry_run,x=1:cmp",
        ":alias,remote=':local,ignored.global.dry_run=False:d',"
        "ignored.global.dry_run=0:",
        ":local,global.dry_run_kept=false:k",
        ":local,global.DRY_RUN=false:u",
        ":local,xglobal.dry_run=false:v"};
    QCOMPARE(JobOptions::DryRunArguments(saved), expected);
    // A second pass changes nothing more.
    QCOMPARE(JobOptions::DryRunArguments(expected), expected);
    // Bisync keeps its caps, not the option.
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"bisync", "a,global.dry_run=f:", "b:"}),
             (QStringList{"bisync", "--dry-run", "a,ignored.global.dry_run=f:",
                          "b:"}));
  }

  // The transfer dialog's Dry Run builds getOptions() with dryRun set, and
  // the extra options come after the app's --dry-run, so a --dry-run=false
  // typed there turned it into a real run. The task's own run keeps
  // everything as typed.
  void dialogDryRunsStayDryRuns() {
    auto sync = makeTask(JobOptions::Sync, true);
    sync->extra = "--dry-run=false -vn=false";
    sync->dest = ":local,global.dry_run=false:dst";
    const QStringList dry = sync->getOptions();
    QCOMPARE(dry.value(1), QString("--dry-run"));
    QVERIFY2(!dry.contains("--dry-run=false") && !dry.contains("-vn=false") &&
                 dry.contains("-vn"),
             qPrintable(dry.join(' ')));
    QCOMPARE(dry.last(), QString(":local,ignored.global.dry_run=false:dst"));

    sync->dryRun = false;
    const QStringList run = sync->getOptions();
    QVERIFY2(run.contains("--dry-run=false") && run.contains("-vn=false"),
             qPrintable(run.join(' ')));
    QCOMPARE(run.last(), QString(":local,global.dry_run=false:dst"));
    delete sync;
  }

  // The same paths through the installed rclone, and the dialog's own
  // arguments last.
  void pathOptionsDeleteNothingInRealRclone() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("rclone is not on PATH and RCLONE_BROWSER_TEST_RCLONE is unset");
    }
    // @S, @D and @B stand for the source, destination and backup folders.
    const QList<QStringList> cases{
        {"sync", "@S", ":local,global.dry_run=false:@D"},
        {"sync", ":local,global.dry_run=False:@S", "@D"},
        {"sync", "--backup-dir", ":local,global.dry_run=false:@B", "@S",
         ":local:@D"},
        {"sync", "--backup-dir=:local,global.dry_run=false:@B", "@S",
         ":local:@D"},
        {"sync", "--compare-dest", ":local,global.dry_run=false:@B", "@S", "@D"},
        {"sync", ":local,global.dry_run:@S", ":local,global.dry_run=false:@D"},
        {"sync", "@S", ":alias,remote=':local,global.dry_run=false:@D':"},
        {},
    };
    for (const QStringList &c : cases) {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      const QString src = dir.filePath("src");
      const QString dst = dir.filePath("dst");
      const QString bak = dir.filePath("bak");
      QVERIFY(QDir().mkpath(src) && QDir().mkpath(dst) && QDir().mkpath(bak));
      for (const QString &path : {src + "/keep.txt", dst + "/extra.txt",
                                  dir.filePath("rclone.conf")}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(path.endsWith(".conf") ? "" : "x");
      }
      QStringList args;
      if (c.isEmpty()) {
        auto sync = makeTask(JobOptions::Sync, true);
        sync->extra = "--dry-run=false";
        sync->source = src;
        sync->dest = ":local,global.dry_run=false:" + dst;
        args = sync->getOptions();
        delete sync;
      } else {
        QStringList filled = c;
        filled.replaceInStrings("@S", src);
        filled.replaceInStrings("@D", dst);
        filled.replaceInStrings("@B", bak);
        args = JobOptions::DryRunArguments(filled);
      }
      QProcess process;
      process.setWorkingDirectory(dir.path());
      process.start(rclone, QStringList{"--config", dir.filePath("rclone.conf")}
                                << args);
      QVERIFY(process.waitForFinished(60000));
      // A run rclone refused would leave the file too, and prove nothing.
      QVERIFY2(process.exitStatus() == QProcess::NormalExit &&
                   process.exitCode() == 0,
               qPrintable(args.join(' ') + "\n" +
                          QString::fromUtf8(process.readAllStandardError())));
      QCOMPARE(QDir(dst).entryList(QDir::Files), QStringList{"extra.txt"});
      QVERIFY2(QDir(bak).entryList(QDir::Files).isEmpty(),
               qPrintable(args.join(' ')));
    }
  }

  // A path's global.max_delete or global.max_delete_size caps the run like
  // the flag does (addConfigToContext in fs/newfs.go), so it stopped a
  // sync's check partway. The check renames them the way it drops the flags.
  // A pattern is never opened as a path and keeps its text, and bisync keeps
  // its caps.
  void previewsDropCapsSetInPaths() {
    const QStringList saved{
        "sync",
        "--exclude",
        "a,global.max_delete=1:b",
        "--exclude=a,global.max_delete=1:b",
        "src",
        ":local,global.max_delete=1:dst",
        "--backup-dir=:local,global.max_delete_size=1k,global.max_delete:bak",
        ":local,global.max_delete_sizes=1:k"};
    const QStringList expected{
        "sync",
        "--dry-run",
        "--exclude",
        "a,global.max_delete=1:b",
        "--exclude=a,global.max_delete=1:b",
        "src",
        ":local,ignored.global.max_delete=1:dst",
        "--backup-dir=:local,ignored.global.max_delete_size=1k,"
        "ignored.global.max_delete:bak",
        ":local,global.max_delete_sizes=1:k"};
    QCOMPARE(JobOptions::DryRunArguments(saved), expected);
    QCOMPARE(JobOptions::DryRunArguments(expected), expected);
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"bisync", "a,global.max_delete=50:", "b:"}),
             (QStringList{"bisync", "--dry-run", "a,global.max_delete=50:",
                          "b:"}));

    // The task's own run keeps them, and so does what the summary reads.
    auto sync = makeTask(JobOptions::Sync, false);
    sync->dest = ":local,global.max_delete=1:dst";
    QCOMPARE(sync->getOptions().last(), sync->dest);
    QVERIFY2(sync->getPreviewOptions().last().contains("ignored.global."),
             qPrintable(sync->getPreviewOptions().join(' ')));
    delete sync;
  }

  // The same caps through the installed rclone: each of the three deletes
  // is listed and the run ends cleanly.
  void pathCapsLeaveTheCheckWholeInRealRclone() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("rclone is not on PATH and RCLONE_BROWSER_TEST_RCLONE is unset");
    }
    const QStringList dests{":local,global.max_delete=1:@D",
                            ":local,global.max_delete_size=1B:@D"};
    for (const QString &form : dests) {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      const QString src = dir.filePath("src");
      const QString dst = dir.filePath("dst");
      QVERIFY(QDir().mkpath(src) && QDir().mkpath(dst));
      for (const QString &path : {dst + "/a.txt", dst + "/b.txt",
                                  dst + "/c.txt", dir.filePath("rclone.conf")}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(path.endsWith(".conf") ? "" : "x");
      }
      QString dest = form;
      dest.replace("@D", dst);
      const QStringList args =
          JobOptions::DryRunArguments(QStringList{"sync", src, dest});
      QProcess process;
      process.setWorkingDirectory(dir.path());
      process.start(rclone, QStringList{"--config", dir.filePath("rclone.conf")}
                                << args);
      QVERIFY(process.waitForFinished(60000));
      const QString log = QString::fromUtf8(process.readAllStandardError());
      QVERIFY2(process.exitStatus() == QProcess::NormalExit &&
                   process.exitCode() == 0,
               qPrintable(args.join(' ') + "\n" + log));
      QVERIFY2(log.count("Skipped delete as --dry-run is set") == 3,
               qPrintable(args.join(' ') + "\n" + log));
      QCOMPARE(QDir(dst).entryList(QDir::Files).size(), 3);
    }
  }

  // rclone reads an option only from a connection string, which ends at a
  // colon (fs/fspath.Parse refuses one that doesn't). A folder name or a
  // pattern with no colon after it keeps its text, or the dry run looks at a
  // folder that doesn't exist and lists no deletes. Every remote in a union's
  // list of upstreams still has its options renamed.
  void previewsRenameOnlyWhatRcloneReadsAsAnOption() {
    const QStringList saved{
        "sync",
        "--include",
        "x,global.dry_run=1",
        "D:/t/dst,global.dry_run=1",
        "remote:dst,global.dry_run=1/sub",
        "--backup-dir=D:/t/b,global.max_delete=1",
        ":union,upstreams=':local:A :local,global.dry_run=false:B':",
        ":union,upstreams=':local:A :local,global.max_delete=1:B':"};
    const QStringList expected{
        "sync",
        "--dry-run",
        "--include",
        "x,global.dry_run=1",
        "D:/t/dst,global.dry_run=1",
        "remote:dst,global.dry_run=1/sub",
        "--backup-dir=D:/t/b,global.max_delete=1",
        ":union,upstreams=':local:A :local,ignored.global.dry_run=false:B':",
        ":union,upstreams=':local:A :local,ignored.global.max_delete=1:B':"};
    QCOMPARE(JobOptions::DryRunArguments(saved), expected);

    auto sync = makeTask(JobOptions::Sync, true);
    sync->dest = "D:/t/dst,global.dry_run=1";
    QCOMPARE(sync->getOptions().last(), sync->dest);
    delete sync;
  }

  // The same through the installed rclone: the delete of the one file rclone
  // would remove is listed, and nothing changes.
  void previewsOfLookAlikeNamesListTheirDeletesInRealRclone() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("rclone is not on PATH and RCLONE_BROWSER_TEST_RCLONE is unset");
    }
    struct Case {
      QString dstName;
      QString dstFile;
      QStringList extra;
    };
    const QList<Case> cases{
        {"dst,global.dry_run=1", "extra.txt", {}},
        {"dst", "x,global.dry_run=1", {"--include", "x,global.dry_run=1"}},
    };
    for (const Case &c : cases) {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      const QString src = dir.filePath("src");
      const QString dst = dir.filePath(c.dstName);
      QVERIFY(QDir().mkpath(src) && QDir().mkpath(dst));
      for (const QString &path : {src + "/keep.txt", dst + "/" + c.dstFile,
                                  dir.filePath("rclone.conf")}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(path.endsWith(".conf") ? "" : "x");
      }
      const QStringList args = JobOptions::DryRunArguments(
          QStringList{"sync"} << c.extra << src << dst);
      QProcess process;
      process.setWorkingDirectory(dir.path());
      process.start(rclone, QStringList{"--config", dir.filePath("rclone.conf")}
                                << args);
      QVERIFY(process.waitForFinished(60000));
      const QString log = QString::fromUtf8(process.readAllStandardError());
      QVERIFY2(process.exitStatus() == QProcess::NormalExit &&
                   process.exitCode() == 0,
               qPrintable(args.join(' ') + "\n" + log));
      QVERIFY2(log.count("Skipped delete as --dry-run is set") == 1,
               qPrintable(args.join(' ') + "\n" + log));
      QCOMPARE(QDir(dst).entryList(QDir::Files), QStringList{c.dstFile});
    }
  }

  // A download of several selected items saves its --filter rules ahead of
  // the command. A history Dry Run put its --dry-run between the first
  // --filter and its rule, and rclone refused the run as an unknown command.
  // The command now goes first and the rules keep their order.
  void historyDryRunsOfSelectedItemsKeepTheirRules() {
    const QStringList saved{"--filter", "+ /a.txt", "--filter=+ /b/**",
                            "--filter", "- *",      "sync",
                            "--max-delete", "5",    "src:",
                            "dst:"};
    QCOMPARE(JobOptions::DryRunArguments(saved),
             (QStringList{"sync", "--dry-run", "--filter", "+ /a.txt",
                          "--filter=+ /b/**", "--filter", "- *", "src:",
                          "dst:"}));
    // Bisync is still told apart and keeps its cap.
    QCOMPARE(JobOptions::DryRunArguments(QStringList{
                 "--filter", "+ /a", "bisync", "--max-delete", "50", "a:", "b:"}),
             (QStringList{"bisync", "--dry-run", "--filter", "+ /a",
                          "--max-delete", "50", "a:", "b:"}));

    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("rclone is not on PATH and RCLONE_BROWSER_TEST_RCLONE is unset");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = dir.filePath("src");
    const QString dst = dir.filePath("dst");
    QVERIFY(QDir().mkpath(src) && QDir().mkpath(dst));
    for (const QString &path : {src + "/keep.txt", src + "/skip.txt",
                                dir.filePath("rclone.conf")}) {
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(path.endsWith(".conf") ? "" : "x");
    }
    const QStringList args = JobOptions::DryRunArguments(
        QStringList{"--filter", "+ /keep.txt", "--filter", "- *", "copy", src,
                    dst});
    QProcess process;
    process.setWorkingDirectory(dir.path());
    process.start(rclone, QStringList{"--config", dir.filePath("rclone.conf")}
                              << args);
    QVERIFY(process.waitForFinished(60000));
    const QString log = QString::fromUtf8(process.readAllStandardError());
    QVERIFY2(process.exitStatus() == QProcess::NormalExit &&
                 process.exitCode() == 0,
             qPrintable(args.join(' ') + "\n" + log));
    QVERIFY2(log.count("Skipped copy as --dry-run is set") == 1 &&
                 log.contains("keep.txt") && !log.contains("skip.txt"),
             qPrintable(args.join(' ') + "\n" + log));
    QVERIFY(QDir(dst).entryList(QDir::Files).isEmpty());
  }

  // pflag gives --exclude, --include and --filter the next argument whatever
  // it looks like (measured on v1.76.0: `rclone sync --exclude -n` is a real
  // run that deletes, and `--filter "- run=*"` skips run=1.txt). A preview
  // once read that -n as its dry run and ran for real.
  void previewsReadPatternsAsValues() {
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"sync", "--exclude", "-n", "a:", "b:"}),
             (QStringList{"sync", "--dry-run", "--exclude", "-n", "a:", "b:"}));
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"sync", "--filter", "- run=*", "a:", "b:"}),
             (QStringList{"sync", "--dry-run", "--filter", "- run=*", "a:",
                          "b:"}));
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"sync", "--include", "-vn=x", "a:", "b:"}),
             (QStringList{"sync", "--dry-run", "--include", "-vn=x", "a:",
                          "b:"}));
    // Only letters make a short cluster.
    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"sync", "-1n=x", "a:", "b:"}),
             (QStringList{"sync", "--dry-run", "-1n=x", "a:", "b:"}));

    // A bisync's conflict option comes after the app's own --dry-run.
    auto bisync = makeTask(JobOptions::Bisync, false);
    bisync->conflictResolve = "newer";
    const QStringList preview = bisync->getPreviewOptions();
    QCOMPARE(preview.count("--dry-run"), 1);
    QCOMPARE(preview.value(1), QString("--dry-run"));
    QVERIFY2(preview.indexOf("--conflict-resolve") > 1,
             qPrintable(preview.join(' ')));
    delete bisync;
  }

  // Bisync's --max-delete is a percentage that aborts before anything
  // changes, and its dry run stops at --max-delete-size on the same file
  // the real run does (both measured on v1.76.0). Dropping either made the
  // preview pass a run that aborts, or abort one that passes.
  void bisyncPreviewsKeepTheirCaps() {
    auto bisync = makeTask(JobOptions::Bisync, false);
    bisync->extra = "--max-delete 90 --max-delete-size=3k --dry-run=false";
    const QStringList preview = bisync->getPreviewOptions();
    QCOMPARE(preview.value(1), QString("--dry-run"));
    const qsizetype cap = preview.indexOf("--max-delete");
    QVERIFY2(cap > 0 && preview.value(cap + 1) == "90",
             qPrintable(preview.join(' ')));
    QVERIFY2(preview.contains("--max-delete-size=3k"),
             qPrintable(preview.join(' ')));
    QVERIFY2(!preview.contains("--dry-run=false"),
             qPrintable(preview.join(' ')));
    delete bisync;

    QCOMPARE(JobOptions::DryRunArguments(
                 QStringList{"bisync", "--max-delete", "90", "a:", "b:"}),
             (QStringList{"bisync", "--dry-run", "--max-delete", "90", "a:",
                          "b:"}));
  }

  // The deletion summary warns from the caps the run will use, wherever
  // they came from, and rclone takes the last of a repeated flag.
  void readsTheCapsARunWillUse() {
    auto sync = makeTask(JobOptions::Sync, false);
    sync->maxDelete = 100;
    sync->extra = "--max-delete=5 --max-delete-size 2M";
    JobOptions::DeleteCaps caps = JobOptions::DeleteCapsIn(sync->getOptions());
    QCOMPARE(caps.count, 5);
    QCOMPARE(caps.bytes, 2 * 1024 * 1024);
    delete sync;

    caps = JobOptions::DeleteCapsIn({"sync", "--max-delete", "3", "a:", "b:"});
    QCOMPARE(caps.count, 3);
    QCOMPARE(caps.bytes, -1);
    caps = JobOptions::DeleteCapsIn(
        {"sync", "--max-delete", "3", "--max-delete=-1", "a:", "b:"});
    QCOMPARE(caps.count, -1);
    caps = JobOptions::DeleteCapsIn({"sync", "--max-delete-size=off", "a:"});
    QCOMPARE(caps.bytes, -1);
    caps = JobOptions::DeleteCapsIn({"sync", "--exclude=*.tmp", "a:", "b:"});
    QCOMPARE(caps.count, -1);
    QCOMPARE(caps.bytes, -1);
  }

  // rclone reads --max-delete with Go's fmt.Sscanln: spaces around the
  // number are fine, a leading 0 is octal and _ can split digits. Each value
  // checked on v1.76.0 through options/get.
  void readsTheCountTheWayRcloneDoes() {
    const struct {
      const char *text;
      qint64 count;
    } cases[] = {{" 5", 5},      {"010", 8},     {"0x10", 16},
                 {"1_000", 1000}, {"08", -1},     {"-3", -1},
                 {"5x", -1},     {"", -1}};
    for (const auto &c : cases) {
      const JobOptions::DeleteCaps caps =
          JobOptions::DeleteCapsIn({"sync", "--max-delete", c.text, "a:"});
      QVERIFY2(caps.count == c.count,
               qPrintable(QStringLiteral("%1 gave %2").arg(c.text).arg(caps.count)));
    }
  }

  // rclone's SizeSuffix, values checked on v1.76.0 through options/get.
  void readsSizesTheWayRcloneDoes() {
    QCOMPARE(JobOptions::SizeSuffixBytes("3k"), 3072);
    QCOMPARE(JobOptions::SizeSuffixBytes("10"), 10240);
    QCOMPARE(JobOptions::SizeSuffixBytes("10M"), 10485760);
    QCOMPARE(JobOptions::SizeSuffixBytes("10Mi"), 10485760);
    QCOMPARE(JobOptions::SizeSuffixBytes("10MiB"), 10485760);
    QCOMPARE(JobOptions::SizeSuffixBytes("10Ki"), 10240);
    QCOMPARE(JobOptions::SizeSuffixBytes("10KI"), 10240);
    QCOMPARE(JobOptions::SizeSuffixBytes("1.5G"), Q_INT64_C(1610612736));
    QCOMPARE(JobOptions::SizeSuffixBytes("2b"), 2);
    QCOMPARE(JobOptions::SizeSuffixBytes("2B"), 2);
    QCOMPARE(JobOptions::SizeSuffixBytes("off"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes("10MB"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes(" 5k"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes("-5k"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes(""), -1);
    // Go's ParseFloat takes _ between digits and hex with a p exponent,
    // and rclone refuses NaN, infinity and anything of 8 EiB or more.
    QCOMPARE(JobOptions::SizeSuffixBytes("1_000"), Q_INT64_C(1024000));
    QCOMPARE(JobOptions::SizeSuffixBytes("0x1p4"), Q_INT64_C(16384));
    QCOMPARE(JobOptions::SizeSuffixBytes("0x10"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes("1e16"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes("Infk"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes("NaNk"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes("8Ei"), -1);
    QCOMPARE(JobOptions::SizeSuffixBytes("1biB"), -1);
  }

  void dryRunDefaultsFalse() {
    JobOptions jo;
    QVERIFY2(!jo.dryRun, "dryRun must default to false");
  }

  void dryRunTrueProducesDryRunFlag() {
    const JobOptions::Operation ops[] = {JobOptions::Copy, JobOptions::Move,
                                         JobOptions::Sync, JobOptions::Bisync};
    const char *names[] = {"Copy", "Move", "Sync", "Bisync"};
    for (int i = 0; i < 4; ++i) {
      auto jo = makeTask(ops[i], true);
      QStringList args = jo->getOptions();
      QVERIFY2(argsContain(args, "--dry-run"),
               qPrintable(QString("dryRun=true must produce --dry-run for %1").arg(names[i])));
      delete jo;
    }
  }

  void dryRunFalseNeverProducesDryRunFlag() {
    const JobOptions::Operation ops[] = {JobOptions::Copy, JobOptions::Move,
                                         JobOptions::Sync, JobOptions::Bisync};
    const char *names[] = {"Copy", "Move", "Sync", "Bisync"};
    for (int i = 0; i < 4; ++i) {
      auto jo = makeTask(ops[i], false);
      QStringList args = jo->getOptions();
      QVERIFY2(!argsContain(args, "--dry-run"),
               qPrintable(QString("dryRun=false must NOT produce --dry-run for %1")
                              .arg(names[i])));
      delete jo;
    }
  }

  void dryRunNotPersistedThroughRoundTrip() {
    auto jo = makeTask(JobOptions::Sync, true);
    jo->description = "test-persist";
    jo->uniqueId =
        QUuid::fromString("{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}");

    QList<JobOptions *> tasks;
    tasks.append(jo);

    QByteArray bytes;
    QBuffer out(&bytes);
    QVERIFY2(out.open(QIODevice::WriteOnly), "failed to open write buffer");
    QString error;
    QVERIFY2(WriteJobOptionsStore(&out, tasks, &error),
             qPrintable("failed to write store: " + error));
    out.close();

    QBuffer in(&bytes);
    QVERIFY2(in.open(QIODevice::ReadOnly), "failed to open read buffer");
    JobOptionsStoreLoadResult loaded = ReadJobOptionsStore(&in);
    QVERIFY2(loaded.error.isEmpty(),
             qPrintable("failed to read store: " + loaded.error));
    QCOMPARE(loaded.tasks.size(), 1);
    QVERIFY2(!loaded.tasks.first()->dryRun,
             "dryRun must NOT survive save/load round-trip");

    QStringList args = loaded.tasks.first()->getOptions();
    QVERIFY2(!argsContain(args, "--dry-run"),
             "loaded task must NOT produce --dry-run");

    ClearJobOptionsList(&loaded.tasks);
    ClearJobOptionsList(&tasks);
  }

  void dryRunAppearsExactlyOnce() {
    auto jo = makeTask(JobOptions::Sync, true);
    jo->sync = true;
    jo->syncTiming = JobOptions::After;
    jo->skipNewer = true;
    jo->verbose = true;
    jo->compare = true;
    jo->compareOption = JobOptions::Checksum;

    QStringList args = jo->getOptions();
    int count = 0;
    for (const auto &a : args) {
      if (a == "--dry-run")
        ++count;
    }
    QVERIFY2(count == 1,
             qPrintable(QString("--dry-run must appear exactly once, got %1")
                            .arg(count)));
    delete jo;
  }

  void togglingDryRunProducesCleanArgs() {
    auto jo = makeTask(JobOptions::Copy, true);
    QStringList withDry = jo->getOptions();
    QVERIFY2(argsContain(withDry, "--dry-run"), "initial dry-run missing");

    jo->dryRun = false;
    QStringList withoutDry = jo->getOptions();
    QVERIFY2(!argsContain(withoutDry, "--dry-run"),
             "after clearing dryRun, --dry-run must not appear");
    delete jo;
  }

  void backupDirDateExpansionsUnique() {
    auto jo = makeTask(JobOptions::Sync, false);
    jo->backupDir = "remote:backups/{date}";

    const QString first = argAfter(jo->getOptions(), "--backup-dir");
    const QString second = argAfter(jo->getOptions(), "--backup-dir");
    QVERIFY2(!first.isEmpty(), "first backup-dir arg missing");
    QVERIFY2(!second.isEmpty(), "second backup-dir arg missing");
    QVERIFY2(first != second,
             "backup-dir {date} expansions must be unique per command");
    QVERIFY2(QRegularExpression(
                 "^remote:backups/\\d{4}-\\d{2}-\\d{2}_\\d{6}_\\d{3}_[0-9a-z]+_[0-9a-z]{4}$")
                 .match(first)
                 .hasMatch(),
             "backup-dir {date} expansion format changed unexpectedly");
    delete jo;
  }

  void freeFormExtraOptionsPreserveQuotes() {
    auto jo = makeTask(JobOptions::Copy, false);
    jo->extra = "--metadata \"display name=Quarterly Report\" --suffix \"old copy\"";

    QStringList args = jo->getOptions();
    QVERIFY2(args.contains("--metadata"), "quoted extra flag missing");
    QVERIFY2(args.contains("display name=Quarterly Report"),
             "quoted extra value with spaces was split");
    QVERIFY2(args.contains("--suffix"), "second quoted extra flag missing");
    QVERIFY2(args.contains("old copy"), "second quoted extra value was split");
    QVERIFY2(!args.contains("\"display"),
             "quote characters leaked into parsed extra args");
    delete jo;
  }

  void backupRetentionPrunesOldSnapshots() {
    QString parent;
    QStringList targets;
    const bool planned = BuildBackupRetentionPlan(
        "remote:backups/{date}", 2,
        QStringList()
            << "2026-06-17_010203_004_abcd_0001/"
            << "not-a-backup/"
            << "2026-06-18_010203_004_abcd_0002/"
            << "2026-06-16_010203_004_abcd_0000/",
        &parent, &targets);
    QVERIFY2(planned, "backup retention plan was not created");
    QCOMPARE(parent, QString("remote:backups"));
    QCOMPARE(targets,
             QStringList({"remote:backups/2026-06-16_010203_004_abcd_0000"}));
  }

  void nestedBackupTemplatesPruneDatedSegment() {
    QString parent;
    QStringList targets;
    const bool planned = BuildBackupRetentionPlan(
        "remote:backups/snap-{date}/deleted", 1,
        QStringList()
            << "snap-2026-06-17_010203"
            << "snap-2026-06-18_010203"
            << "manual-2026-06-10_010203",
        &parent, &targets);
    QVERIFY2(planned, "nested backup retention plan was not created");
    QCOMPARE(parent, QString("remote:backups"));
    QCOMPARE(targets,
             QStringList({"remote:backups/snap-2026-06-17_010203"}));
  }

  void fixedBackupDirsAndKeepAllNeverPrune() {
    QString parent;
    QStringList targets;
    QVERIFY2(!BuildBackupRetentionPlan("remote:backups/current", 2,
                                       QStringList() << "current", &parent,
                                       &targets),
             "fixed backup dir should not create a retention plan");
    QVERIFY2(targets.isEmpty(), "fixed backup dir produced delete targets");
    QVERIFY2(!BuildBackupRetentionPlan("remote:backups/{date}", 0,
                                       QStringList() << "2026-06-18_010203",
                                       &parent, &targets),
             "keep-all setting should not create a retention plan");
    QVERIFY2(targets.isEmpty(), "keep-all setting produced delete targets");
  }

  void externalCallbackUrlsMustBeValidHttp() {
    QUrl parsed;
    QString error;
    QVERIFY2(ParseHttpUrl("https://example.com/hook?token=abc", &parsed, &error),
             "valid HTTPS URL was rejected");
    QVERIFY2(parsed.scheme() == "https" && parsed.host() == "example.com",
             "valid HTTPS URL parsed incorrectly");
    QVERIFY2(!ParseHttpUrl("file:///C:/secret.txt", &parsed, &error),
             "file URL was accepted");
    QVERIFY2(error.contains("http://"), "file URL error was not actionable");
    QVERIFY2(!ParseHttpUrl("https://", &parsed, &error),
             "hostless HTTPS URL was accepted");
  }

  // A task saved without transfers or checkers (hand-written, or from an
  // older tasks.json) used to pass "--transfers ''", which stops rclone.
  void aTaskWithoutTransferCountsUsesRcloneDefaults() {
    QByteArray bytes = R"({"version":1,"tasks":[{"description":"bare",
        "jobType":2,"operation":1,"source":"/src","dest":"remote:dst",
        "isFolder":true,
        "uniqueId":"{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"}]})";
    QBuffer in(&bytes);
    QVERIFY(in.open(QIODevice::ReadOnly));
    JobOptionsStoreLoadResult loaded = ReadJobOptionsStoreJson(&in);
    QVERIFY2(loaded.error.isEmpty(), qPrintable(loaded.error));
    QCOMPARE(loaded.tasks.size(), 1);

    const QStringList args = loaded.tasks.first()->getOptions();
    for (const QString &flag : {"--transfers", "--checkers", "--contimeout",
                                "--timeout", "--retries", "--low-level-retries"}) {
      QVERIFY2(!args.contains(flag), qPrintable(flag + " in " + args.join(' ')));
    }
    QVERIFY2(!args.contains(QString()) && !args.contains("s"),
             qPrintable(args.join(' ')));

    JobOptions *jo = loaded.tasks.first();
    jo->transfers = "4";
    jo->checkers = "8";
    jo->connectTimeout = "60";
    jo->idleTimeout = "300";
    jo->retries = "3";
    jo->lowLevelRetries = "10";
    const QStringList set = jo->getOptions();
    QCOMPARE(argAfter(set, "--transfers"), QString("4"));
    QCOMPARE(argAfter(set, "--checkers"), QString("8"));
    QCOMPARE(argAfter(set, "--contimeout"), QString("60s"));
    QCOMPARE(argAfter(set, "--timeout"), QString("300s"));
    QCOMPARE(argAfter(set, "--retries"), QString("3"));
    QCOMPARE(argAfter(set, "--low-level-retries"), QString("10"));
    ClearJobOptionsList(&loaded.tasks);
  }
};

QTEST_MAIN(DryRunContractTest)
#include "dryrun_contract_test.moc"
