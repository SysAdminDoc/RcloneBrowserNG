#include "main_window.h"
#include "utils.h"

#include <QDir>
#include <QLabel>
#include <QListWidget>
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

    mRclone = QStandardPaths::findExecutable("rclone");
    if (mRclone.isEmpty()) {
      QSKIP("rclone is not on PATH");
    }

    // A crypt remote whose backing store is a plain local remote: the exact
    // shape that made the backing remote disappear.
    mConfigPath = QDir(mConfigDir.path()).filePath("rclone.conf");
    QFile config(mConfigPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write("[localdisk]\ntype = alias\nremote = " +
                 mConfigDir.path().toUtf8() +
                 "\n\n"
                 "[secret]\ntype = crypt\nremote = localdisk:\n"
                 "password = 3AXGGH8DsHTe5-vwtLbAOA\n");
    config.close();

    QSettings settings;
    settings.setValue("Settings/rclone", mRclone);
    settings.setValue("Settings/rcloneConf", mConfigPath);
    // Keep the window off the network and away from modal dialogs.
    settings.setValue("Settings/checkRcloneBrowserUpdates", false);
    settings.setValue("Settings/checkRcloneUpdates", false);
    settings.setValue("Settings/rcloneCveWarnedVersion", "suppressed");
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
};

QTEST_MAIN(RemotesListTest)
#include "remotes_list_test.moc"
