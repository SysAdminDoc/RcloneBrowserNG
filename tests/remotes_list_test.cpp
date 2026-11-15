#include "combine_remote_dialog.h"
#include "item_model.h"
#include "job_history.h"
#include "test_rclone.h"
#include "main_window.h"
#include "remote_daemon.h"
#include "remote_widget.h"
#include "utils.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>
#include <memory>

// A crypt remote's backing remote used to be hidden unconditionally by an
// async `rclone config dump` callback, with no preference and no way back:
// the remotes list painted every remote and then silently dropped some of
// them a moment later. That is the "only 8 of my 15 remotes are displayed,
// all 15 flash up on refresh" report. Hiding is opt-in now, and when it is
// on the list says how many remotes it swallowed.
class RemotesListTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mSettingsDir;
  QTemporaryDir mConfigDir;
  QString mRclone;
  QString mConfigPath;

  static int visibleRemoteCount(QListWidget *remotes) {
    int visible = 0;
    for (int i = 0; i < remotes->count(); ++i) {
      if (!remotes->item(i)->isHidden()) {
        ++visible;
      }
    }
    return visible;
  }

  // Mirrors the parsing in MainWindow::rcloneGetVersion so the pre-acknowledged
  // security-floor warning matches the string the window will compute.
  QString detectedRcloneVersion() const {
    QProcess probe;
    probe.start(mRclone, QStringList() << "version");
    if (!probe.waitForFinished(30000)) {
      return QString();
    }
    QString first = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    const int lineBreak = first.indexOf('\n');
    if (lineBreak != -1) {
      first.truncate(lineBreak);
    }
    return first.trimmed().replace("rclone v", "").replace("-DEV", "");
  }

  void setHideCryptBackends(bool hide) {
    QSettings settings;
    settings.setValue("Settings/hideCryptBackends", hide);
    settings.sync();
  }

  static void switchToConfig(const QString &path) {
    QSettings settings;
    settings.setValue("Settings/rcloneConf", path);
    settings.sync();
    SetRcloneConf(path);
  }

  // "one" holds a.txt, "two two" holds Old Photos/b.txt.
  QString writeCombineFixture(const QString &fileName) {
    QDir root(mConfigDir.path());
    root.mkpath("combine-a");
    root.mkpath("combine-b/Old Photos");
    for (const QString &relative :
         {QStringLiteral("combine-a/a.txt"),
          QStringLiteral("combine-b/Old Photos/b.txt")}) {
      QFile file(root.filePath(relative));
      if (file.open(QIODevice::WriteOnly)) {
        file.write("x");
      }
    }
    const QString path = root.filePath(fileName);
    QFile config(path);
    if (config.open(QIODevice::WriteOnly | QIODevice::Text)) {
      config.write("[one]\ntype = alias\nremote = " +
                   root.filePath("combine-a").toUtf8() +
                   "\n\n[two two]\ntype = alias\nremote = " +
                   root.filePath("combine-b").toUtf8() + "\n");
    }
    return path;
  }

  QStringList rcloneLsf(const QString &config, const QString &target) const {
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(mRclone, QStringList{"--config", config, "lsf", target});
    if (!process.waitForFinished(60000)) {
      process.kill();
      return {"timed out"};
    }
    QStringList lines = QString::fromUtf8(process.readAll())
                            .split('\n', Qt::SkipEmptyParts);
    for (QString &line : lines) {
      line = line.trimmed();
    }
    if (process.exitCode() != 0) {
      lines.prepend(QString("exit %1").arg(process.exitCode()));
    }
    lines.sort();
    return lines;
  }

  static QStringList remoteNames(QListWidget *remotes) {
    QStringList names;
    for (int i = 0; i < remotes->count(); ++i) {
      names << remotes->item(i)->text();
    }
    return names;
  }

  static void setCombineRow(CombineRemoteDialog *dialog, const QString &remote,
                            bool ticked, const QString &folder,
                            const QString &path) {
    QTableWidget *table = dialog->table();
    for (int row = 0; row < table->rowCount(); ++row) {
      if (table->item(row, CombineRemoteDialog::IncludeColumn)->text() !=
          remote) {
        continue;
      }
      table->item(row, CombineRemoteDialog::IncludeColumn)
          ->setCheckState(ticked ? Qt::Checked : Qt::Unchecked);
      if (!folder.isEmpty()) {
        table->item(row, CombineRemoteDialog::FolderColumn)->setText(folder);
      }
      if (!path.isEmpty()) {
        table->item(row, CombineRemoteDialog::PathColumn)->setText(path);
      }
    }
  }

  static void clickOk(QWidget *dialog) {
    if (auto *buttons = dialog->findChild<QDialogButtonBox *>()) {
      if (auto *ok = buttons->button(QDialogButtonBox::Ok)) {
        ok->click();
      }
    }
  }

private slots:
  void initTestCase() {
    QVERIFY(mSettingsDir.isValid());
    QVERIFY(mConfigDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       mSettingsDir.path());
    QCoreApplication::setOrganizationName("rclone-browser-remotes-test");
    QCoreApplication::setApplicationName("rclone-browser-remotes-test");

    mRclone = FindRcloneForTests();
    if (mRclone.isEmpty()) {
      QSKIP("rclone is not on PATH");
    }

    // A crypt remote whose backing store is a plain local remote: the exact
    // shape that made the backing remote disappear.
    mConfigPath = QDir(mConfigDir.path()).filePath("rclone.conf");
    QFile config(mConfigPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    // The description carries a colon on purpose: `listremotes --long`
    // prints `localdisk: alias My main disk: backup`, which the old
    // split-on-colon parser threw away, dropping the remote from the list.
    config.write("[localdisk]\ntype = alias\nremote = " +
                 mConfigDir.path().toUtf8() +
                 "\ndescription = My main disk: backup\n\n"
                 "[secret]\ntype = crypt\nremote = localdisk:\n"
                 "password = 3AXGGH8DsHTe5-vwtLbAOA\n");
    config.close();

    QSettings settings;
    settings.setValue("Settings/rclone", mRclone);
    settings.setValue("Settings/rcloneConf", mConfigPath);
    // Keep the window off the network and away from modal dialogs. The
    // security-floor warning is shown once per version string, so it has to
    // be pre-acknowledged for the exact version this rclone reports or an
    // offscreen QMessageBox blocks the run.
    settings.setValue("Settings/checkRcloneBrowserUpdates", false);
    settings.setValue("Settings/checkRcloneUpdates", false);
    settings.setValue("Settings/rcloneCveWarnedVersion", detectedRcloneVersion());
    settings.sync();

    SetRclone(mRclone);
    SetRcloneConf(mConfigPath);
  }

  void bothRemotesAreListedByDefault() {
    setHideCryptBackends(false);
    std::unique_ptr<MainWindow> window(new MainWindow(true));
    auto *remotes = window->findChild<QListWidget *>("remotes");
    QVERIFY(remotes != nullptr);

    QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), 2, 120000);
    // The hiding pass is a second, asynchronous `rclone config dump`, so give
    // it room to land and prove it did not fire.
    QTest::qWait(2000);
    QCOMPARE(visibleRemoteCount(remotes), 2);

    auto *notice = window->findChild<QLabel *>("remotesHiddenNotice");
    QVERIFY(notice != nullptr);
    QVERIFY(!notice->isVisible());

    // A remote whose description contains a colon keeps its type, so the
    // icon still resolves, and the description reaches the tooltip.
    QListWidgetItem *localdisk = nullptr;
    for (int i = 0; i < remotes->count(); ++i) {
      if (remotes->item(i)->text() == QLatin1String("localdisk")) {
        localdisk = remotes->item(i);
      }
    }
    QVERIFY2(localdisk != nullptr, "the described remote must still be listed");
    QCOMPARE(localdisk->data(Qt::UserRole).toString(), QStringLiteral("alias"));
    QVERIFY2(localdisk->toolTip().contains(QLatin1String("My main disk: backup")),
             qPrintable(localdisk->toolTip()));
  }

  void backingRemoteIsHiddenAndCountedWhenAskedFor() {
    setHideCryptBackends(true);
    std::unique_ptr<MainWindow> window(new MainWindow(true));
    auto *remotes = window->findChild<QListWidget *>("remotes");
    QVERIFY(remotes != nullptr);

    QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), 2, 120000);
    QTRY_COMPARE_WITH_TIMEOUT(visibleRemoteCount(remotes), 1, 120000);

    for (int i = 0; i < remotes->count(); ++i) {
      auto *item = remotes->item(i);
      if (item->text() == QLatin1String("localdisk")) {
        QVERIFY2(item->isHidden(), "the crypt backend is the hidden one");
      } else {
        QVERIFY2(!item->isHidden(), "the crypt remote itself stays listed");
      }
    }

    // Vanishing without explanation was the whole complaint.
    auto *notice = window->findChild<QLabel *>("remotesHiddenNotice");
    QVERIFY(notice != nullptr);
    QVERIFY(!notice->text().isEmpty());
    QVERIFY2(notice->text().contains(QLatin1String("1 remote is hidden")),
             qPrintable(notice->text()));
    QVERIFY(notice->text().contains(QLatin1String("Preferences")));

    setHideCryptBackends(false);
  }

  void reportersEightRemotesAreAllListed() {
    // Issue #16: drive, sftp, two alias, two crypt, OneDrive and pCloud
    // remotes. 2.0.2 dropped the sftp and pCloud remotes a moment after the
    // list painted, because the two crypt remotes sit on top of them.
    const QString configPath =
        QDir(mConfigDir.path()).filePath("issue16.conf");
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray dir = mConfigDir.path().toUtf8();
    config.write("[gdrive]\ntype = drive\nscope = drive\n\n"
                 "[nas]\ntype = sftp\nhost = nas.invalid\nuser = backup\n\n"
                 "[documents]\ntype = alias\nremote = " + dir + "\n\n"
                 "[pictures]\ntype = alias\nremote = " + dir + "\n\n"
                 "[nas-crypt]\ntype = crypt\nremote = nas:vault\n"
                 "password = 3AXGGH8DsHTe5-vwtLbAOA\n\n"
                 "[pcloud-crypt]\ntype = crypt\nremote = pcloud:vault\n"
                 "password = 3AXGGH8DsHTe5-vwtLbAOA\n\n"
                 "[onedrive]\ntype = onedrive\ndrive_type = personal\n\n"
                 "[pcloud]\ntype = pcloud\n");
    config.close();

    auto useConfig = [](const QString &path) {
      QSettings settings;
      settings.setValue("Settings/rcloneConf", path);
      settings.sync();
      SetRcloneConf(path);
    };
    useConfig(configPath);
    struct Restore {
      std::function<void()> fn;
      ~Restore() { fn(); }
    } restore{[&]() {
      useConfig(mConfigPath);
      setHideCryptBackends(false);
    }};

    const QStringList expected = {"documents",  "gdrive",   "nas",
                                  "nas-crypt",  "onedrive", "pcloud",
                                  "pcloud-crypt", "pictures"};
    auto names = [](QListWidget *remotes, bool visibleOnly) {
      QStringList out;
      for (int i = 0; i < remotes->count(); ++i) {
        if (!visibleOnly || !remotes->item(i)->isHidden()) {
          out << remotes->item(i)->text();
        }
      }
      out.sort();
      return out;
    };

    {
      setHideCryptBackends(false);
      std::unique_ptr<MainWindow> window(new MainWindow(true));
      auto *remotes = window->findChild<QListWidget *>("remotes");
      QVERIFY(remotes != nullptr);
      QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), 8, 120000);
      QTest::qWait(2000);
      QCOMPARE(names(remotes, true), expected);
    }

    {
      // Opting in to hiding takes out exactly the two backing remotes and
      // says so.
      setHideCryptBackends(true);
      std::unique_ptr<MainWindow> window(new MainWindow(true));
      auto *remotes = window->findChild<QListWidget *>("remotes");
      QVERIFY(remotes != nullptr);
      QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), 8, 120000);
      QTRY_COMPARE_WITH_TIMEOUT(names(remotes, true).size(), 6, 120000);
      QStringList hidden = expected;
      for (const QString &name : names(remotes, true)) {
        hidden.removeAll(name);
      }
      QCOMPARE(hidden, QStringList({"nas", "pcloud"}));
      auto *notice = window->findChild<QLabel *>("remotesHiddenNotice");
      QVERIFY(notice != nullptr);
      QVERIFY2(notice->text().contains(QLatin1String("2 remotes are hidden")),
               qPrintable(notice->text()));
    }
  }

  void commonRemoteTypesGetTheirOwnIcon() {
    // These all showed the grey question mark, including alias and webdav,
    // which most setups have.
    const QStringList types = {
        "alias",   "archive",     "box",         "chunker",  "cloudinary",
        "combine", "compress",    "filen",       "hasher",   "http",
        "huaweidrive", "iclouddrive", "memory",  "pcloud",   "protondrive",
        "smb",     "union",       "webdav",      "zoho"};
    const QString configPath =
        QDir(mConfigDir.path()).filePath("icon-types.conf");
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    for (const QString &type : types) {
      config.write(QString("[my-%1]\ntype = %1\n\n").arg(type).toUtf8());
    }
    config.close();
    switchToConfig(configPath);
    struct Restore {
      QString path;
      ~Restore() { switchToConfig(path); }
    } restore{mConfigPath};

    setHideCryptBackends(false);
    std::unique_ptr<MainWindow> window(new MainWindow(true));
    auto *remotes = window->findChild<QListWidget *>("remotes");
    QVERIFY(remotes != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), int(types.size()), 120000);

    const QSize probe(32, 32);
    auto pixels = [&](const QIcon &icon) {
      return icon.pixmap(probe).toImage().convertToFormat(
          QImage::Format_ARGB32);
    };
    const QImage unknown =
        pixels(QIcon(":/remotes/images/unknown.png"));
    const QImage unknownDark =
        pixels(QIcon(":/remotes/images/unknown_inv.png"));
    for (int i = 0; i < remotes->count(); ++i) {
      QListWidgetItem *item = remotes->item(i);
      const QString type = item->data(Qt::UserRole).toString();
      QVERIFY2(types.contains(type), qPrintable(type));
      const QImage shown = pixels(item->icon());
      QVERIFY2(shown != unknown && shown != unknownDark,
               qPrintable(type + " still shows the unknown icon"));
      // Both themes need a file, since the list swaps to the white set on a
      // dark palette.
      QVERIFY2(QFile::exists(":/remotes/images/" + type + ".png") &&
                   QFile::exists(":/remotes/images/" + type + "_inv.png"),
               qPrintable(type + " is missing a light or dark icon"));
    }
  }

  void dualPaneLocalFilesystemListsTheDisk() {
    // Issue #20: the dual pane's "(Local filesystem)" choice opened a pane
    // named "/", so every listing asked rclone for "/:C:/" and failed with
    // "rclone could not list". Open it the way a user does and list the drive
    // that holds the test's temp folder through the real rclone.
    setHideCryptBackends(false);
    std::unique_ptr<MainWindow> window(new MainWindow(true));
    auto *remotes = window->findChild<QListWidget *>("remotes");
    auto *tabs = window->findChild<QTabWidget *>("tabs");
    QVERIFY(remotes != nullptr);
    QVERIFY(tabs != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), 2, 120000);

    QPushButton *dualPane = nullptr;
    for (auto *button : window->findChildren<QPushButton *>()) {
      if (button->text() == QLatin1String("Dual Pane")) {
        dualPane = button;
      }
    }
    QVERIFY(dualPane != nullptr);
    remotes->setCurrentRow(0);
    remotes->item(0)->setSelected(true);
    QVERIFY(dualPane->isEnabled());

    // Record and close every message box, so a "Listing failed" warning
    // fails the test with its text instead of blocking the run.
    QStringList dialogs;
    QTimer watcher;
    QObject::connect(&watcher, &QTimer::timeout, [&dialogs]() {
      for (QWidget *top : QApplication::topLevelWidgets()) {
        if (auto *input = qobject_cast<QInputDialog *>(top)) {
          if (input->isVisible()) {
            input->setTextValue("(Local filesystem)");
            input->accept();
          }
        } else if (auto *box = qobject_cast<QMessageBox *>(top)) {
          if (box->isVisible()) {
            dialogs << box->text() + "\n" + box->informativeText();
            box->done(QMessageBox::Ok);
          }
        }
      }
    });
    watcher.start(50);

    const int tabsBefore = tabs->count();
    dualPane->click();
    QTRY_COMPARE_WITH_TIMEOUT(tabs->count(), tabsBefore + 1, 30000);
    const QString leftName = remotes->item(0)->text();
    QCOMPARE(tabs->tabText(tabs->currentIndex()),
             leftName + " | Local filesystem");

    auto *splitter = qobject_cast<QSplitter *>(tabs->currentWidget());
    QVERIFY(splitter != nullptr);
    QCOMPARE(splitter->count(), 2);
    auto *localPane = qobject_cast<RemoteWidget *>(splitter->widget(1));
    QVERIFY(localPane != nullptr);
    auto *model = localPane->findChild<ItemModel *>();
    QVERIFY(model != nullptr);

    // Windows lists each drive as a root and asks rclone only when one is
    // opened; elsewhere the single "/" root is listed straight away.
    const QString tempPath =
        QDir::fromNativeSeparators(QDir(mConfigDir.path()).absolutePath());
    QModelIndex driveIndex;
    for (int row = 0; row < model->rowCount(QModelIndex()); ++row) {
      const QModelIndex candidate = model->index(row, 0, QModelIndex());
      const QString root =
          QDir::fromNativeSeparators(model->path(candidate).path());
      if (tempPath.startsWith(root, Qt::CaseInsensitive) &&
          (!driveIndex.isValid() ||
           root.size() > model->path(driveIndex).path().size())) {
        driveIndex = candidate;
      }
    }
    QVERIFY2(driveIndex.isValid(), qPrintable(tempPath));

    // The first folder of the temp path has to show up in the listing.
    const QString driveRoot =
        QDir::fromNativeSeparators(model->path(driveIndex).path());
    const QString firstFolder =
        tempPath.mid(driveRoot.size()).section('/', 0, 0, QString::SectionSkipEmpty);
    QVERIFY(!firstFolder.isEmpty());

    auto listed = [&]() {
      const int rows = model->rowCount(driveIndex);
      for (int row = 0; row < rows; ++row) {
        if (model->index(row, 0, driveIndex)
                .data(Qt::DisplayRole)
                .toString()
                .compare(firstFolder, Qt::CaseInsensitive) == 0) {
          return true;
        }
      }
      return false;
    };
    QTRY_VERIFY2_WITH_TIMEOUT(listed() || !dialogs.isEmpty(),
                              qPrintable(dialogs.join("\n---\n")), 120000);
    QVERIFY2(dialogs.isEmpty(), qPrintable(dialogs.join("\n---\n")));
    QVERIFY2(listed(), qPrintable(firstFolder));
  }

  // With a remote rclone set in Preferences, the window lists and browses
  // that rclone's remotes, not the ones in the local config, and turns off
  // the setup buttons that would edit this computer's config instead.
  void remoteDaemonModeBrowsesTheOtherRclone() {
    setHideCryptBackends(false);
    QTemporaryDir farDir;
    QVERIFY(farDir.isValid());
    QDir(farDir.path()).mkpath("served/Projects");
    // Not "far": windef.h defines that away.
    QFile servedFile(QDir(farDir.path()).filePath("served/far.txt"));
    QVERIFY(servedFile.open(QIODevice::WriteOnly));
    servedFile.write("x");
    servedFile.close();
    const QString daemonConfig = QDir(farDir.path()).filePath("daemon.conf");
    QFile config(daemonConfig);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write("[nasonly]\ntype = alias\nremote = " +
                 QDir(farDir.path()).filePath("served").toUtf8() + "\n");
    config.close();

    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();
    QProcess daemon;
#if defined(Q_OS_WIN32)
    daemon.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
          args->flags |= CREATE_NO_WINDOW;
        });
#endif
    daemon.start(mRclone, {"rcd", "--config", daemonConfig, "--rc-addr",
                           QString("127.0.0.1:%1").arg(port), "--rc-user",
                           "far", "--rc-pass", "far pass"});
    QVERIFY(daemon.waitForStarted(10000));
    auto stopDaemon = qScopeGuard([&daemon]() {
      RemoteDaemon::Save(RemoteDaemon::Settings());
      daemon.kill();
      daemon.waitForFinished(5000);
    });

    RemoteDaemon::Settings remote;
    remote.enabled = true;
    remote.url = QString("http://127.0.0.1:%1").arg(port);
    remote.user = "far";
    remote.password = "far pass";
    RemoteDaemon::Save(remote);
    // Late answers to the polls can land after this test returns.
    auto up = std::make_shared<bool>(false);
    auto answers = [this, up]() {
      RemoteDaemon::Call("rc/noopauth", QJsonObject(), this,
                         [up](const QJsonObject &, const QString &error) {
                           *up = *up || error.isEmpty();
                         });
      return *up;
    };
    QTRY_VERIFY_WITH_TIMEOUT(answers(), 30000);

    std::unique_ptr<MainWindow> window(new MainWindow(true));
    auto *remotes = window->findChild<QListWidget *>("remotes");
    auto *tabs = window->findChild<QTabWidget *>("tabs");
    QVERIFY(remotes != nullptr);
    QVERIFY(tabs != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(remoteNames(remotes), QStringList{"nasonly"},
                              60000);
    QVERIFY(!window->findChild<QPushButton *>("config")->isEnabled());
    QVERIFY(!window->findChild<QPushButton *>("newRemote")->isEnabled());

    auto *open = window->findChild<QPushButton *>("open");
    QVERIFY(open != nullptr);
    remotes->setCurrentRow(0);
    remotes->item(0)->setSelected(true);
    const int tabsBefore = tabs->count();
    open->click();
    QTRY_COMPARE_WITH_TIMEOUT(tabs->count(), tabsBefore + 1, 30000);
    auto *pane = qobject_cast<RemoteWidget *>(tabs->currentWidget());
    QVERIFY(pane != nullptr);
    auto *model = pane->findChild<ItemModel *>();
    QVERIFY(model != nullptr);

    auto listedNames = [model]() {
      QStringList names;
      const QModelIndex root = model->index(0, 0, QModelIndex());
      for (int row = 0; row < model->rowCount(root); ++row) {
        names << model->index(row, 0, root).data(Qt::DisplayRole).toString();
      }
      names.sort();
      return names;
    };
    QTRY_COMPARE_WITH_TIMEOUT(listedNames(),
                              (QStringList{"Projects", "far.txt"}), 60000);
  }

  void failingPostCommandIsReportedRatherThanSwallowed() {
    // The post-transfer hook used to run through QProcess::startDetached, so
    // a command that failed to launch or exited non-zero looked exactly like
    // one that worked.
    std::unique_ptr<MainWindow> window(new MainWindow(false));
    const int errorsBefore = window->backgroundErrorCount();
    const int historyBefore = JobHistoryStore::Load().size();

#if defined(Q_OS_WIN)
    const QString command = "echo disk full 1>&2 & exit /b 3";
#else
    const QString command = "echo disk full >&2; exit 3";
#endif
    window->runPostCommand(command, "nightly backup");

    QTRY_VERIFY_WITH_TIMEOUT(window->backgroundErrorCount() > errorsBefore,
                             30000);
    const QString message = window->lastBackgroundErrorMessage();
    QVERIFY2(message.contains(QLatin1String("exited 3")), qPrintable(message));
    // The last line of output is the one that says why.
    QVERIFY2(message.contains(QLatin1String("disk full")), qPrintable(message));

    QTRY_VERIFY_WITH_TIMEOUT(JobHistoryStore::Load().size() > historyBefore,
                             30000);
    const QVector<JobHistoryEntry> history = JobHistoryStore::Load();
    const JobHistoryEntry &entry = history.last();
    QCOMPARE(entry.exitCode, 3);
    QVERIFY(!entry.success);
    QVERIFY2(entry.name.contains(QLatin1String("nightly backup")),
             qPrintable(entry.name));
    QVERIFY(entry.source.contains(QLatin1String("exit")));
  }

  void successfulPostCommandStaysQuiet() {
    std::unique_ptr<MainWindow> window(new MainWindow(false));
    const int errorsBefore = window->backgroundErrorCount();
    const int historyBefore = JobHistoryStore::Load().size();

#if defined(Q_OS_WIN)
    const QString command = "exit /b 0";
#else
    const QString command = "exit 0";
#endif
    window->runPostCommand(command, "nightly backup");

    QTest::qWait(3000);
    QCOMPARE(window->backgroundErrorCount(), errorsBefore);
    QCOMPARE(JobHistoryStore::Load().size(), historyBefore);
  }

  void combineFromTheRemoteMenuMakesOneRemote() {
    // Issue #18: putting several remotes into one mount needs an rclone
    // "combine" remote, which rclone's own setup only offers as a single
    // quoted line to type. Right-click a remote, tick another, and the new
    // remote has to show both through the real rclone.
    const QString configPath = writeCombineFixture("combine-menu.conf");
    switchToConfig(configPath);
    struct Restore {
      std::function<void()> fn;
      ~Restore() { fn(); }
    } restore{[&]() { switchToConfig(mConfigPath); }};

    std::unique_ptr<MainWindow> window(new MainWindow(true));
    window->show();
    auto *remotes = window->findChild<QListWidget *>("remotes");
    QVERIFY(remotes != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), 2, 120000);
    QListWidgetItem *twoTwo = nullptr;
    for (int i = 0; i < remotes->count(); ++i) {
      if (remotes->item(i)->text() == QLatin1String("two two")) {
        twoTwo = remotes->item(i);
      }
    }
    QVERIFY(twoTwo != nullptr);

    QStringList messages;
    bool picked = false;
    bool filled = false;
    QStringList preticked;
    QTimer watcher;
    QObject::connect(&watcher, &QTimer::timeout, [&]() {
      if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
        for (QAction *action : menu->actions()) {
          if (action->text() == QLatin1String("Combine With Other Remotes...")) {
            picked = true;
            menu->setActiveAction(action);
            QTest::keyClick(menu, Qt::Key_Return);
            return;
          }
        }
      }
      for (QWidget *top : QApplication::topLevelWidgets()) {
        if (auto *dialog = qobject_cast<CombineRemoteDialog *>(top)) {
          if (dialog->isVisible() && !filled) {
            filled = true;
            for (const CombineUpstream &upstream : dialog->upstreams()) {
              preticked << upstream.remote;
            }
            dialog->nameField()->setText("together");
            setCombineRow(dialog, "one", true, QString(), QString());
            setCombineRow(dialog, "two two", true, "Pictures", "Old Photos");
            clickOk(dialog);
          }
        } else if (auto *box = qobject_cast<QMessageBox *>(top)) {
          if (box->isVisible()) {
            messages << box->text();
            box->done(QMessageBox::Ok);
          }
        }
      }
    });
    watcher.start(50);

    emit remotes->customContextMenuRequested(
        remotes->visualItemRect(twoTwo).center());
    QVERIFY2(picked, "the remote's menu offers to combine it");
    QVERIFY2(filled, "choosing it opens the combine dialog");
    QCOMPARE(preticked, QStringList({"two two"}));

    QTRY_VERIFY2_WITH_TIMEOUT(remoteNames(remotes).contains("together") ||
                                  !messages.isEmpty(),
                              qPrintable(messages.join("\n")), 120000);
    QVERIFY2(messages.isEmpty(), qPrintable(messages.join("\n")));

    QCOMPARE(rcloneLsf(configPath, "together:"),
             QStringList({"Pictures/", "one/"}));
    QCOMPARE(rcloneLsf(configPath, "together:one"), QStringList({"a.txt"}));
    QCOMPARE(rcloneLsf(configPath, "together:Pictures"),
             QStringList({"b.txt"}));
  }

  void newRemoteOfTypeCombineOpensTheGuidedDialog() {
    const QString configPath = writeCombineFixture("combine-new.conf");
    switchToConfig(configPath);
    struct Restore {
      std::function<void()> fn;
      ~Restore() { fn(); }
    } restore{[&]() { switchToConfig(mConfigPath); }};

    std::unique_ptr<MainWindow> window(new MainWindow(true));
    auto *remotes = window->findChild<QListWidget *>("remotes");
    auto *newRemote = window->findChild<QPushButton *>("newRemote");
    QVERIFY(remotes != nullptr);
    QVERIFY(newRemote != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), 2, 120000);

    QStringList messages;
    bool askedForType = false;
    QString nameCarriedOver;
    QTimer watcher;
    QObject::connect(&watcher, &QTimer::timeout, [&]() {
      for (QWidget *top : QApplication::topLevelWidgets()) {
        if (!top->isVisible()) {
          continue;
        }
        if (auto *dialog = qobject_cast<CombineRemoteDialog *>(top)) {
          if (nameCarriedOver.isNull()) {
            nameCarriedOver = dialog->nameField()->text();
            setCombineRow(dialog, "one", true, QString(), QString());
            setCombineRow(dialog, "two two", true, QString(), QString());
            clickOk(dialog);
          }
        } else if (auto *box = qobject_cast<QMessageBox *>(top)) {
          messages << box->text();
          box->done(QMessageBox::Ok);
        } else if (qobject_cast<QProgressDialog *>(top) == nullptr &&
                   top->windowTitle() == QLatin1String("Create Remote") &&
                   !askedForType) {
          askedForType = true;
          for (auto *edit : top->findChildren<QLineEdit *>()) {
            if (edit->parentWidget() == top) {
              edit->setText("everything");
            }
          }
          auto *provider = top->findChild<QComboBox *>();
          QVERIFY(provider != nullptr);
          provider->setCurrentText("combine");
          clickOk(top);
        }
      }
    });
    watcher.start(50);

    newRemote->click();
    QTRY_VERIFY2_WITH_TIMEOUT(remoteNames(remotes).contains("everything") ||
                                  !messages.isEmpty(),
                              qPrintable(messages.join("\n")), 120000);
    QVERIFY2(messages.isEmpty(), qPrintable(messages.join("\n")));
    QVERIFY(askedForType);
    QCOMPARE(nameCarriedOver, QStringLiteral("everything"));
    QCOMPARE(rcloneLsf(configPath, "everything:"),
             QStringList({"one/", "two two/"}));
  }
};

QTEST_MAIN(RemotesListTest)
#include "remotes_list_test.moc"
