#include "layout_assertions.h"
#include "test_rclone.h"

#include "icon_cache.h"
#include "job_options.h"
#include "main_window.h"
#include "remote_widget.h"
#include "transfer_dialog.h"
#include "utils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLineEdit>
#include <QMenuBar>
#include <QSignalSpy>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSizeGrip>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
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
  QTemporaryDir mConfigDir;
  QString mRclone;

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

    // Two remotes of the same backend type, which is what puts the
    // server-side option on offer, plus one of a different type to prove the
    // option withdraws.
    QVERIFY(mConfigDir.isValid());
    mRclone = FindRcloneForTests();
    const QString configPath = QDir(mConfigDir.path()).filePath("rclone.conf");
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write("[one]\ntype = alias\nremote = " +
                 mConfigDir.path().toUtf8() +
                 "\n\n[two]\ntype = alias\nremote = " +
                 mConfigDir.path().toUtf8() +
                 "\n\n[other]\ntype = memory\n");
    config.close();

    QSettings settings;
    settings.setValue("Settings/rclone", mRclone);
    settings.setValue("Settings/rcloneConf", configPath);
    settings.sync();
    if (!mRclone.isEmpty()) {
      SetRclone(mRclone);
      SetRcloneConf(configPath);
    }
  }

  // The option was gated on ui.checkServerSide->isVisible(). That is false
  // for a child of a hidden window, and QDialog::exec() hides the dialog
  // before returning, so getJobOptions() always read false and
  // --server-side-across-configs could never reach the command line. It is
  // also false whenever the Advanced tab is not the current one, which made
  // a saved task depend on which tab was last clicked.
  void serverSideSurvivesAHiddenDialogAndABackgroundTab() {
    if (mRclone.isEmpty()) {
      QSKIP("rclone is not on PATH");
    }
    TransferDialog dialog(false, false, "one", QDir(), true);
    auto *source = dialog.findChild<QLineEdit *>("textSource");
    auto *dest = dialog.findChild<QLineEdit *>("textDest");
    auto *serverSide = dialog.findChild<QCheckBox *>("checkServerSide");
    QVERIFY(source != nullptr);
    QVERIFY(dest != nullptr);
    QVERIFY(serverSide != nullptr);

    source->setText("one:from");
    dest->setText("two:to");
    // The remote types are read by a background rclone process now, so the
    // option appears a moment after the paths do.
    QTRY_VERIFY_WITH_TIMEOUT(!serverSide->isHidden(), 30000);
    serverSide->setChecked(true);

    // Sit on the first tab, the way a user who ticked the box and went back
    // to check their paths would, and never show the dialog at all, which is
    // the state exec() leaves behind.
    auto *tabs = dialog.findChild<QTabWidget *>();
    QVERIFY(tabs != nullptr);
    tabs->setCurrentIndex(0);
    QVERIFY(!dialog.isVisible());
    QVERIFY2(!serverSide->isVisible(),
             "the widget really is invisible here; that is the whole point");

    JobOptions *options = dialog.getJobOptions();
    QVERIFY(options != nullptr);
    QVERIFY2(options->serverSideAcrossConfigs,
             "the ticked option was dropped");
    QVERIFY2(options->getOptions().contains("--server-side-across-configs"),
             qPrintable(options->getOptions().join(' ')));
  }

  // The other half: a tick left behind after the user retargets the transfer
  // must not survive into the command line.
  void retargetingTheTransferDropsAStaleServerSideTick() {
    if (mRclone.isEmpty()) {
      QSKIP("rclone is not on PATH");
    }
    TransferDialog dialog(false, false, "one", QDir(), true);
    auto *source = dialog.findChild<QLineEdit *>("textSource");
    auto *dest = dialog.findChild<QLineEdit *>("textDest");
    auto *serverSide = dialog.findChild<QCheckBox *>("checkServerSide");
    QVERIFY(source != nullptr);
    QVERIFY(dest != nullptr);
    QVERIFY(serverSide != nullptr);

    source->setText("one:from");
    dest->setText("two:to");
    QTRY_VERIFY_WITH_TIMEOUT(!serverSide->isHidden(), 30000);
    serverSide->setChecked(true);

    // A different backend on the far end: the provider cannot do this copy.
    dest->setText("other:to");
    QTRY_VERIFY_WITH_TIMEOUT(serverSide->isHidden(), 30000);
    QVERIFY(serverSide->isChecked());

    JobOptions *options = dialog.getJobOptions();
    QVERIFY(options != nullptr);
    QVERIFY(!options->serverSideAcrossConfigs);
    QVERIFY(!options->getOptions().contains("--server-side-across-configs"));
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

  void nonDeletingTransfersAcceptWithoutAPreview() {
    // Sync and --delete-excluded get a dry-run pass before they run. Every
    // other operation must still accept straight through: routing all of
    // them through the preview would spawn an rclone process on every
    // single transfer and stall the dialog.
    TransferDialog dialog(false, false, "remote", QDir(), true);
    // done() validates both paths before accepting, so fill them in or the
    // dialog refuses for a reason that has nothing to do with the preview.
    auto *source = dialog.findChild<QLineEdit *>("textSource");
    auto *dest = dialog.findChild<QLineEdit *>("textDest");
    QVERIFY(source != nullptr);
    QVERIFY(dest != nullptr);
    source->setText(QDir::tempPath());
    dest->setText("remote:backup");

    auto *copy = dialog.findChild<QRadioButton *>("rbCopy");
    QVERIFY(copy != nullptr);
    copy->setChecked(true);
    auto *deleteExcluded =
        dialog.findChild<QCheckBox *>("checkDeleteExcluded");
    QVERIFY(deleteExcluded != nullptr);
    deleteExcluded->setChecked(false);

    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(buttons != nullptr);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    emit buttons->accepted();
    QCOMPARE(accepted.count(), 1);
    QVERIFY(!dialog.isVisible());
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
