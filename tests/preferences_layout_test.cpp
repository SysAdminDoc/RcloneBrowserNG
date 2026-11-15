#include "preferences_dialog.h"

#include "layout_assertions.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>

// Issue #13 regression coverage: dynamically created Preferences controls
// used to be inserted through qobject_cast<QFormLayout*>/<QVBoxLayout*> on
// layouts that are QGridLayouts in the .ui. Every cast failed, so the
// controls stayed parented to the dialog itself and painted over the tab
// bar at the top-left corner (and the SOCKS proxy field effectively never
// appeared). All of them are .ui widgets now; this test locks in that each
// one exists, lives inside a QGroupBox, and participates in its parent's
// layout.
class PreferencesLayoutTest : public QObject {
  Q_OBJECT

private:

  static void verifyManaged(QDialog &dialog, QWidget *widget,
                            const char *name) {
    QVERIFY2(widget != nullptr, name);
    QVERIFY2(widget->parentWidget() != &dialog,
             "widget must not be a stray child of the dialog");
    QVERIFY2(qobject_cast<QGroupBox *>(widget->parentWidget()) != nullptr,
             "widget must be inside a group box");
    QVERIFY2(widget->parentWidget()->layout() != nullptr,
             "group box must have a layout");
    QVERIFY2(LayoutAssertions::IsManagedByParentLayout(widget),
             "widget must participate in the group box layout");
  }

private slots:
  void controlsLiveInManagedLayouts() {
    PreferencesDialog dialog;
    verifyManaged(dialog, dialog.findChild<QSpinBox *>("maxConcurrentTransfers"),
                  "maxConcurrentTransfers");
    verifyManaged(dialog, dialog.findChild<QPlainTextEdit *>("defaultExclude"),
                  "defaultExclude");
    verifyManaged(dialog, dialog.findChild<QLineEdit *>("socks_proxy"),
                  "socks_proxy");
    verifyManaged(dialog, dialog.findChild<QComboBox *>("mountPreset"),
                  "mountPreset");
    verifyManaged(dialog, dialog.findChild<QLabel *>("mountPresetFlags"),
                  "mountPresetFlags");
    verifyManaged(dialog, dialog.findChild<QCheckBox *>("startMinimized"),
                  "startMinimized");
    verifyManaged(dialog, dialog.findChild<QPushButton *>("backupConfig"),
                  "backupConfig");
    verifyManaged(dialog, dialog.findChild<QPushButton *>("restoreConfig"),
                  "restoreConfig");
  }

  void transferControlsMovedToTransfersTab() {
    // The General page used to hold every transfer default, making it too
    // tall and clipping the exclude editor and Backup/Restore buttons.
    PreferencesDialog dialog;
    auto *transfersTab = dialog.findChild<QWidget *>("tab_transfers");
    QVERIFY(transfersTab != nullptr);
    const QStringList transferControls = {
        "defaultDownloadDir",  "defaultUploadDir",   "defaultDownloadOptions",
        "defaultUploadOptions", "defaultRcloneOptions",
        "maxConcurrentTransfers", "defaultExclude"};
    for (const QString &name : transferControls) {
      auto *widget = dialog.findChild<QWidget *>(name);
      QVERIFY2(widget != nullptr, qPrintable(name));
      QVERIFY2(transfersTab->isAncestorOf(widget), qPrintable(name));
    }
  }

  void startMinimizedImpliesAlwaysShowInTray() {
    PreferencesDialog dialog;
    auto *startMinimized = dialog.findChild<QCheckBox *>("startMinimized");
    auto *alwaysShowInTray = dialog.findChild<QCheckBox *>("alwaysShowInTray");
    QVERIFY(startMinimized != nullptr);
    QVERIFY(alwaysShowInTray != nullptr);
    alwaysShowInTray->setChecked(false);
    QVERIFY(!startMinimized->isChecked());
    startMinimized->setChecked(true);
    QVERIFY(alwaysShowInTray->isChecked());
    alwaysShowInTray->setChecked(false);
    QVERIFY(!startMinimized->isChecked());

    // Belt and braces (PR #14): even if the checkbox pair is forced out of
    // sync (signals blocked, as programmatic code could do), the getter
    // must still report the tray icon as required.
    startMinimized->setChecked(true);
    {
      const QSignalBlocker blocker(alwaysShowInTray);
      alwaysShowInTray->setChecked(false);
    }
    QVERIFY(dialog.getAlwaysShowInTray());
  }

  void cryptBackendHidingIsOptInAndManaged() {
    // Hiding a configured remote with no way to bring it back is what made
    // seven of a reporter's fifteen remotes disappear, so the control has to
    // exist, be reachable, and start off.
    PreferencesDialog dialog;
    auto *hide = dialog.findChild<QCheckBox *>("hideCryptBackends");
    QVERIFY(hide != nullptr);
    QVERIFY2(LayoutAssertions::IsManagedByParentLayout(hide),
             "hideCryptBackends");
    QVERIFY2(!hide->isChecked(), "crypt backends are visible by default");
    QVERIFY(!dialog.getHideCryptBackends());
    hide->setChecked(true);
    QVERIFY(dialog.getHideCryptBackends());
  }

  void remoteDaemonSettingsHaveTheirOwnTab() {
    PreferencesDialog dialog;
    auto *tab = dialog.findChild<QWidget *>("tab_remoteDaemon");
    QVERIFY(tab != nullptr);
    auto *enabled = dialog.findChild<QCheckBox *>("remoteDaemonEnabled");
    auto *url = dialog.findChild<QLineEdit *>("remoteDaemonUrl");
    auto *user = dialog.findChild<QLineEdit *>("remoteDaemonUser");
    auto *password = dialog.findChild<QLineEdit *>("remoteDaemonPassword");
    auto *test = dialog.findChild<QPushButton *>("remoteDaemonTest");
    auto *status = dialog.findChild<QLabel *>("remoteDaemonStatus");
    auto *warning = dialog.findChild<QLabel *>("remoteDaemonWarning");
    auto *certificate = dialog.findChild<QLineEdit *>("remoteDaemonCertificate");
    auto *trust = dialog.findChild<QPushButton *>("remoteDaemonTrust");
    verifyManaged(dialog, enabled, "remoteDaemonEnabled");
    verifyManaged(dialog, url, "remoteDaemonUrl");
    verifyManaged(dialog, user, "remoteDaemonUser");
    verifyManaged(dialog, password, "remoteDaemonPassword");
    verifyManaged(dialog, test, "remoteDaemonTest");
    verifyManaged(dialog, status, "remoteDaemonStatus");
    verifyManaged(dialog, warning, "remoteDaemonWarning");
    verifyManaged(dialog, certificate, "remoteDaemonCertificate");
    verifyManaged(dialog, trust, "remoteDaemonTrust");
    // Offered only after a test meets a certificate nobody trusted.
    QVERIFY(trust->isHidden());
    for (QWidget *widget : {static_cast<QWidget *>(enabled),
                            static_cast<QWidget *>(url),
                            static_cast<QWidget *>(test)}) {
      QVERIFY(tab->isAncestorOf(widget));
    }
    QCOMPARE(password->echoMode(), QLineEdit::Password);

    // The address fields follow the checkbox.
    enabled->setChecked(true);
    QVERIFY(url->isEnabled());
    QVERIFY(password->isEnabled());
    enabled->setChecked(false);
    QVERIFY(!url->isEnabled());
    QVERIFY(!password->isEnabled());
    QVERIFY(!certificate->isEnabled());

    // Plain http to another computer is called out right under the address,
    // with no dialog in the way.
    url->setText("http://nas.local:5572");
    QVERIFY(warning->isHidden());
    enabled->setChecked(true);
    QVERIFY(certificate->isEnabled());
    QVERIFY(!warning->isHidden());
    QVERIFY2(warning->text().contains("--rc-cert"), qPrintable(warning->text()));
    for (const char *safe : {"https://nas.local:5572", "http://127.0.0.1:5572",
                             "http://localhost:5572"}) {
      url->setText(safe);
      QVERIFY2(warning->isHidden(), safe);
    }

    url->setText("  nas.local:5572  ");
    QVERIFY(!warning->isHidden());
    user->setText("admin");
    password->setText("pass word");
    const QString pasted = "sha256 Fingerprint=" + QString(64, QChar('a'));
    certificate->setText(pasted);
    RemoteDaemon::Settings typed = dialog.getRemoteDaemon();
    QVERIFY(typed.enabled);
    QCOMPARE(typed.url, QString("nas.local:5572"));
    QCOMPARE(typed.user, QString("admin"));
    QCOMPARE(typed.password, QString("pass word"));
    const QStringList pairs(32, QString("AA"));
    QCOMPARE(typed.certificateSha256, pairs.join(QChar(':')));
    // Something that isn't a fingerprint isn't saved as one.
    certificate->setText("not a fingerprint");
    typed = dialog.getRemoteDaemon();
    QVERIFY(typed.certificateSha256.isEmpty());
    certificate->clear();

    // Something that isn't an address is caught before any request.
    url->setText("ftp://nas.local");
    QTest::mouseClick(test, Qt::LeftButton);
    QVERIFY2(status->text().contains("Type an address"),
             qPrintable(status->text()));
    QVERIFY(test->isEnabled());
  }

  void mountPresetShowsExactFlags() {
    PreferencesDialog dialog;
    auto *preset = dialog.findChild<QComboBox *>("mountPreset");
    auto *flags = dialog.findChild<QLabel *>("mountPresetFlags");
    QVERIFY(preset != nullptr);
    QVERIFY(flags != nullptr);
    QVERIFY(preset->count() >= 4);
    preset->setCurrentIndex(1);
    QVERIFY(flags->text().contains("--vfs-cache-mode off"));
    QVERIFY(flags->text().contains("--poll-interval 1m"));
  }
};

QTEST_MAIN(PreferencesLayoutTest)
#include "preferences_layout_test.moc"
