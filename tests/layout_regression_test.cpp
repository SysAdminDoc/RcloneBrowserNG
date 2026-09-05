#include "layout_assertions.h"

#include "icon_cache.h"
#include "main_window.h"
#include "remote_widget.h"
#include "transfer_dialog.h"

#include <QComboBox>
#include <QDir>
#include <QMenuBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSizeGrip>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <memory>

// Issue #13 was fixed only inside the Preferences dialog. The same pattern
// survived in three more places: the transfer dialog cast modeGroup to
// QHBoxLayout and tab3 to QFormLayout, and the remote browser cast the button
// bar to QHBoxLayout, while all three are QGridLayouts in the .ui. Every cast
// returned null, so the Bisync radio, the bandwidth timetable button, the
// performance preset combo and the Google Drive trash button were built and
// then never added to a layout. They are .ui widgets now; this locks that in.
class LayoutRegressionTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mSettingsDir;

  static void verifyManaged(QWidget *widget, const char *name) {
    QVERIFY2(widget != nullptr, name);
    QVERIFY2(LayoutAssertions::IsManagedByParentLayout(widget), name);
  }

private slots:
  void initTestCase() {
    // TransferDialog reads and writes QSettings. Keep that inside a temporary
    // directory so the suite cannot touch the real user configuration.
    QVERIFY(mSettingsDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       mSettingsDir.path());
    QCoreApplication::setOrganizationName("rclone-browser-layout-test");
    QCoreApplication::setApplicationName("rclone-browser-layout-test");
  }

  void transferDialogControlsLiveInManagedLayouts() {
    TransferDialog dialog(false, false, "remote", QDir(), true);

    auto *bisync = dialog.findChild<QRadioButton *>("rbBisync");
    verifyManaged(bisync, "rbBisync");
    // The operation is read off this radio when the dialog is accepted, so an
    // unreachable button means Bisync can never be selected.
    QCOMPARE(bisync->text(), QStringLiteral("Bisync"));
    auto *sync = dialog.findChild<QRadioButton *>("rbSync");
    QVERIFY(sync != nullptr);
    QCOMPARE(bisync->parentWidget(), sync->parentWidget());

    auto *bandwidthEdit = dialog.findChild<QToolButton *>("buttonBandwidthEdit");
    verifyManaged(bandwidthEdit, "buttonBandwidthEdit");
    QCOMPARE(bandwidthEdit->accessibleName(),
             QStringLiteral("Edit bandwidth timetable"));

    auto *preset = dialog.findChild<QComboBox *>("cbPreset");
    verifyManaged(preset, "cbPreset");
    QVERIFY2(preset->count() >= 5, "performance presets are populated");
  }

  void remoteWidgetTrashButtonLivesInManagedLayout() {
    IconCache icons(nullptr);
    RemoteWidget widget(&icons, "remote", false, true, false);
    auto *trash = widget.findChild<QPushButton *>("buttonTrash");
    verifyManaged(trash, "buttonTrash");
    auto *shared = widget.findChild<QWidget *>("checkBoxShared");
    QVERIFY(shared != nullptr);
    QCOMPARE(trash->parentWidget(), shared->parentWidget());
  }

  void presetSelectionIsNotRestoredFromSettings() {
    // cbPreset became a named .ui widget, which puts it in reach of the
    // ReadSettings/WriteSettings recursion. Restoring a stored index would
    // fire the handler and overwrite the transfers/checkers/bandwidth values
    // that same pass is restoring, so the dialog must always open on Custom.
    {
      QSettings settings;
      settings.beginGroup("Transfer");
      settings.setValue("cbPreset", 3);
      settings.setValue("spinTransfers", 7);
      settings.endGroup();
      settings.sync();
    }

    TransferDialog dialog(false, false, "remote", QDir(), true);
    QCOMPARE(dialog.findChild<QComboBox *>("cbPreset")->currentIndex(), 0);
    QCOMPARE(dialog.findChild<QSpinBox *>("spinTransfers")->value(), 7);
  }

  // A named-widget checklist goes stale the moment someone adds a control.
  // Sweep instead: every non-window child widget of a laid-out parent has to
  // participate in that parent's layout, so any future insertion through a
  // failing cast is caught wherever it happens.
  void noOrphanedChildWidgets_data() {
    QTest::addColumn<QString>("surface");
    QTest::newRow("TransferDialog") << QStringLiteral("TransferDialog");
    QTest::newRow("RemoteWidget") << QStringLiteral("RemoteWidget");
    QTest::newRow("MainWindow") << QStringLiteral("MainWindow");
  }

  void noOrphanedChildWidgets() {
    QFETCH(QString, surface);
    IconCache icons(nullptr);
    std::unique_ptr<QWidget> root;
    if (surface == QLatin1String("TransferDialog")) {
      root.reset(new TransferDialog(false, false, "remote", QDir(), true));
    } else if (surface == QLatin1String("MainWindow")) {
      root.reset(new MainWindow(false));
    } else {
      root.reset(new RemoteWidget(&icons, "remote", false, true, false));
    }

    QStringList orphans;
    for (QWidget *child : root->findChildren<QWidget *>()) {
      QWidget *parent = child->parentWidget();
      if (parent == nullptr || parent->layout() == nullptr) {
        continue; // the parent manages its children by hand
      }
      if (child->isWindow()) {
        continue;
      }
      // Window chrome is positioned by QDialog/QMainWindow themselves rather
      // than by a layout.
      if (qobject_cast<QSizeGrip *>(child) != nullptr ||
          qobject_cast<QMenuBar *>(child) != nullptr ||
          qobject_cast<QStatusBar *>(child) != nullptr ||
          qobject_cast<QToolBar *>(child) != nullptr) {
        continue;
      }
      if (LayoutAssertions::IsManagedByParentLayout(child)) {
        continue;
      }
      // Widgets a layout owns indirectly (a QScrollArea viewport, a spin box
      // line edit, a combo view) are children of a laid-out parent but are
      // placed by their owning widget, not by the layout.
      if (parent->metaObject()->className() != root->metaObject()->className()) {
        continue;
      }
      orphans << QStringLiteral("%1 (%2)")
                     .arg(child->objectName().isEmpty()
                              ? QStringLiteral("<unnamed>")
                              : child->objectName(),
                          QString::fromLatin1(child->metaObject()->className()));
    }
    QVERIFY2(orphans.isEmpty(),
             qPrintable(surface + " has widgets outside every layout: " +
                        orphans.join(", ")));
  }

  void presetSelectionStillAppliesItsValues() {
    TransferDialog dialog(false, false, "remote", QDir(), true);
    auto *preset = dialog.findChild<QComboBox *>("cbPreset");
    QVERIFY(preset != nullptr);
    // "Many small files (16 transfers, 32 checkers)"
    preset->setCurrentIndex(3);
    QCOMPARE(dialog.findChild<QSpinBox *>("spinTransfers")->value(), 16);
    QCOMPARE(dialog.findChild<QSpinBox *>("spinCheckers")->value(), 32);
  }
};

QTEST_MAIN(LayoutRegressionTest)
#include "layout_regression_test.moc"
