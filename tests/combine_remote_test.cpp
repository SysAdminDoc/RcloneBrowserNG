#include "combine_remote.h"
#include "combine_remote_dialog.h"
#include "test_rclone.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

Q_DECLARE_METATYPE(CombineUpstream)

namespace {
CombineUpstream up(const QString &folder, const QString &remote,
                   const QString &path = QString()) {
  return CombineUpstream{folder, remote, path};
}

QPushButton *createButton(CombineRemoteDialog &dialog) {
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
}

int rowOf(CombineRemoteDialog &dialog, const QString &remote) {
  QTableWidget *table = dialog.table();
  for (int row = 0; row < table->rowCount(); ++row) {
    if (table->item(row, CombineRemoteDialog::IncludeColumn)->text() ==
        remote) {
      return row;
    }
  }
  return -1;
}

void tick(CombineRemoteDialog &dialog, const QString &remote) {
  const int row = rowOf(dialog, remote);
  QVERIFY2(row >= 0, qPrintable(remote));
  dialog.table()
      ->item(row, CombineRemoteDialog::IncludeColumn)
      ->setCheckState(Qt::Checked);
}

void setCell(CombineRemoteDialog &dialog, const QString &remote, int column,
             const QString &text) {
  const int row = rowOf(dialog, remote);
  QVERIFY2(row >= 0, qPrintable(remote));
  dialog.table()->item(row, column)->setText(text);
}

const QList<CombineCandidate> kCandidates = {
    {"gdrive", "drive"}, {"two two", "alias"}, {"nas", "sftp"}};
const QStringList kExisting = {"gdrive", "two two", "nas"};
} // namespace

// rclone's combine backend wants every remote written into one
// `upstreams` line, which is where people trying it by hand get stuck
// (issue #18). These cover the line the app builds for them and the
// mistakes rclone itself saves without a word.
class CombineRemoteTest : public QObject {
  Q_OBJECT

private slots:
  void valueJoinsEntriesWithSpaces() {
    QCOMPARE(CombineUpstreamsValue(
                 {up("Drive", "gdrive"), up("Backup", "nas", "vault/2024")}),
             QStringLiteral("Drive=gdrive: Backup=nas:vault/2024"));
  }

  void valueQuotesAnEntryThatHasASpace() {
    // Unquoted, rclone splits this into "My", "Files=two" and "two:Old",
    // and the remote fails to open.
    QCOMPARE(CombineUpstreamsValue({up("My Files", "two two", "Old Photos"),
                                    up("Drive", "gdrive")}),
             QStringLiteral("\"My Files=two two:Old Photos\" Drive=gdrive:"));
  }

  void valueDoublesQuotes() {
    // Folder names can't hold a quote, but a path on the remote can.
    QCOMPARE(CombineUpstreamsValue({up("Notes", "gdrive", "say \"hi\"")}),
             QStringLiteral("\"Notes=gdrive:say \"\"hi\"\"\""));
  }

  void valueKeepsAnEqualsSignInThePath() {
    // rclone splits on the first '=', so one later on is part of the path.
    QCOMPARE(CombineUpstreamsValue({up("p", "gdrive", "sub=dir")}),
             QStringLiteral("p=gdrive:sub=dir"));
  }

  void valueTrimsWhatWasTyped() {
    QCOMPARE(CombineUpstreamsValue({up("  Drive ", " gdrive ", " Photos ")}),
             QStringLiteral("Drive=gdrive:Photos"));
  }

  void problem_data() {
    QTest::addColumn<QList<CombineUpstream>>("upstreams");
    QTest::addColumn<QString>("expected");

    QTest::newRow("nothing ticked")
        << QList<CombineUpstream>() << QString("Tick at least one remote");
    QTest::newRow("empty folder")
        << QList<CombineUpstream>{up("  ", "gdrive")}
        << QString("Give gdrive a folder name");
    QTest::newRow("dot") << QList<CombineUpstream>{up(".", "gdrive")}
                         << QString("can't be used as a folder name");
    QTest::newRow("dot dot") << QList<CombineUpstream>{up("..", "gdrive")}
                             << QString("can't be used as a folder name");
    QTest::newRow("slash") << QList<CombineUpstream>{up("a/b", "gdrive")}
                           << QString("can't contain /");
    QTest::newRow("backslash")
        << QList<CombineUpstream>{up("a\\b", "gdrive")}
        << QString("can't contain \\");
    QTest::newRow("equals") << QList<CombineUpstream>{up("a=b", "gdrive")}
                            << QString("can't contain =");
    // A mounted Windows drive can't show these.
    QTest::newRow("colon") << QList<CombineUpstream>{up("a:b", "gdrive")}
                           << QString("can't contain :");
    QTest::newRow("quote") << QList<CombineUpstream>{up("a\"b", "gdrive")}
                           << QString("can't contain \"");
    QTest::newRow("star") << QList<CombineUpstream>{up("a*", "gdrive")}
                          << QString("can't contain *");
    QTest::newRow("pipe") << QList<CombineUpstream>{up("a|b", "gdrive")}
                          << QString("can't contain |");
    QTest::newRow("same folder")
        << QList<CombineUpstream>{up("Files", "gdrive"), up("Files", "nas")}
        << QString("Two remotes use the folder name \"Files\"");
    // rclone accepts these two, and a Windows mount then shows one folder.
    QTest::newRow("same folder in another case")
        << QList<CombineUpstream>{up("Files", "gdrive"), up("files", "nas")}
        << QString("Two remotes use the folder name \"files\"");
    QTest::newRow("no remote") << QList<CombineUpstream>{up("Files", "")}
                               << QString("Every row needs a remote");
    QTest::newRow("fine")
        << QList<CombineUpstream>{up("Drive", "gdrive"),
                                  up("My Files", "two two", "a=b c"),
                                  up("Backups (old)", "nas", "say \"hi\"")}
        << QString();
  }

  void problem() {
    QFETCH(QList<CombineUpstream>, upstreams);
    QFETCH(QString, expected);
    const QString actual = CombineUpstreamsProblem(upstreams);
    if (expected.isEmpty()) {
      QVERIFY2(actual.isEmpty(), qPrintable(actual));
    } else {
      QVERIFY2(actual.contains(expected), qPrintable(actual));
    }
  }

  void createArgumentsNeverPrompt() {
    QCOMPARE(CombineCreateArguments(" all ", {up("Drive", "gdrive")}),
             QStringList({"config", "create", "all", "combine", "upstreams",
                          "Drive=gdrive:", "--non-interactive"}));
  }

  void rcloneOpensTheRemoteItCreates() {
    const QString rclone = FindRcloneForTests();
    if (rclone.isEmpty()) {
      QSKIP("no rclone; set RCLONE_BROWSER_TEST_RCLONE");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir root(dir.path());
    QVERIFY(root.mkpath("first"));
    QVERIFY(root.mkpath("second/Old Photos=2024"));
    auto touch = [&](const QString &relative) {
      QFile file(root.filePath(relative));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("x");
    };
    touch("first/a.txt");
    touch("second/Old Photos=2024/b.txt");

    // A remote name with a space, a folder with a space, and a path with a
    // space and an '=' in it.
    const QString config = root.filePath("rclone.conf");
    {
      QFile file(config);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
      file.write("[one]\ntype = alias\nremote = " +
                 root.filePath("first").toUtf8() +
                 "\n\n[two two]\ntype = alias\nremote = " +
                 root.filePath("second").toUtf8() + "\n");
    }

    auto run = [&](const QStringList &args, QString *output) {
      QProcess process;
      process.setProcessChannelMode(QProcess::MergedChannels);
      process.start(rclone, QStringList{"--config", config} + args);
      if (!process.waitForFinished(60000)) {
        process.kill();
        *output = "timed out";
        return -1;
      }
      *output = QString::fromUtf8(process.readAll()).trimmed();
      return process.exitCode();
    };

    QString output;
    QCOMPARE(run(CombineCreateArguments(
                     "together",
                     {up("Plain", "one"),
                      up("My Old Photos", "two two", "Old Photos=2024")}),
                 &output),
             0);

    QCOMPARE(run({"lsf", "together:"}, &output), 0);
    QStringList folders = output.split('\n', Qt::SkipEmptyParts);
    for (QString &folder : folders) {
      folder = folder.trimmed();
    }
    folders.sort();
    QCOMPARE(folders, QStringList({"My Old Photos/", "Plain/"}));

    QCOMPARE(run({"lsf", "together:Plain"}, &output), 0);
    QCOMPARE(output, QStringLiteral("a.txt"));
    QCOMPARE(run({"lsf", "together:My Old Photos"}, &output), 0);
    QCOMPARE(output, QStringLiteral("b.txt"));
  }

  void dialogTicksTheRemoteItWasOpenedFrom() {
    CombineRemoteDialog dialog(kCandidates, kExisting, QString(), "two two");
    QCOMPARE(dialog.table()->rowCount(), 3);
    const QList<CombineUpstream> picked = dialog.upstreams();
    QCOMPARE(picked.size(), 1);
    QCOMPARE(picked.first().remote, QStringLiteral("two two"));
    // The folder starts out as the remote's own name.
    QCOMPARE(picked.first().folder, QStringLiteral("two two"));
    QVERIFY(picked.first().path.isEmpty());
  }

  void dialogBuildsWhatWasTickedAndTyped() {
    CombineRemoteDialog dialog(kCandidates, kExisting, "all-drives",
                               QString());
    QCOMPARE(dialog.nameField()->text(), QStringLiteral("all-drives"));
    tick(dialog, "nas");
    tick(dialog, "gdrive");
    setCell(dialog, "nas", CombineRemoteDialog::FolderColumn, "Backups");
    setCell(dialog, "nas", CombineRemoteDialog::PathColumn, " vault/2024 ");

    QPushButton *create = createButton(dialog);
    QVERIFY(create != nullptr);
    create->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.remoteName(), QStringLiteral("all-drives"));
    QCOMPARE(CombineUpstreamsValue(dialog.upstreams()),
             QStringLiteral("gdrive=gdrive: Backups=nas:vault/2024"));
  }

  void dialogWontReplaceAnExistingRemote() {
    // `rclone config create` overwrites a remote that has the same name.
    CombineRemoteDialog dialog(kCandidates, kExisting, "gdrive", QString());
    tick(dialog, "nas");
    QVERIFY(dialog.problem().contains("already exists"));
    createButton(dialog)->click();
    QVERIFY(dialog.result() != int(QDialog::Accepted));
  }

  void dialogWontAcceptFoldersThatCollide() {
    CombineRemoteDialog dialog(kCandidates, kExisting, "all", QString());
    tick(dialog, "gdrive");
    tick(dialog, "nas");
    setCell(dialog, "nas", CombineRemoteDialog::FolderColumn, "GDRIVE");
    QVERIFY(dialog.problem().contains("Two remotes use the folder name"));
    createButton(dialog)->click();
    QVERIFY(dialog.result() != int(QDialog::Accepted));
  }

  void dialogNeedsAName() {
    CombineRemoteDialog dialog(kCandidates, kExisting, QString(), "gdrive");
    QVERIFY(dialog.problem().contains("Enter a name"));
    dialog.nameField()->setText("a:b");
    QVERIFY(dialog.problem().contains("Enter a name"));
    createButton(dialog)->click();
    QVERIFY(dialog.result() != int(QDialog::Accepted));
    dialog.nameField()->setText("drives");
    QVERIFY2(dialog.problem().isEmpty(), qPrintable(dialog.problem()));
  }
};

QTEST_MAIN(CombineRemoteTest)
#include "combine_remote_test.moc"
