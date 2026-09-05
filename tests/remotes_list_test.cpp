#include "job_history.h"
#include "test_rclone.h"
#include "main_window.h"
#include "utils.h"

#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

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
};

QTEST_MAIN(RemotesListTest)
#include "remotes_list_test.moc"
