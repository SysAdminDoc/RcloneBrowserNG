#include "preferences_dialog.h"

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
  // QLayout::indexOf only sees direct items, so widgets placed in a
  // nested layout (e.g. the backup/restore QHBoxLayout row) need a
  // recursive search.
  static bool layoutContains(QLayout *layout, const QWidget *widget) {
    for (int i = 0; i < layout->count(); ++i) {
      QLayoutItem *item = layout->itemAt(i);
      if (item->widget() == widget) {
        return true;
      }
      if (item->layout() != nullptr && layoutContains(item->layout(), widget)) {
        return true;
      }
    }
    return false;
  }

  static void verifyManaged(QDialog &dialog, QWidget *widget,
                            const char *name) {
    QVERIFY2(widget != nullptr, name);
    QVERIFY2(widget->parentWidget() != &dialog,
             "widget must not be a stray child of the dialog");
    QVERIFY2(qobject_cast<QGroupBox *>(widget->parentWidget()) != nullptr,
             "widget must be inside a group box");
    QLayout *layout = widget->parentWidget()->layout();
    QVERIFY2(layout != nullptr, "group box must have a layout");
    QVERIFY2(layoutContains(layout, widget),
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
