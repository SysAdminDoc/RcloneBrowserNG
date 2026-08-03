#include "cross_remote_search.h"
#include "folder_compare.h"
#include "interface_polish.h"
#include "main_window.h"
#include "preferences_dialog.h"
#include "schedule_dialog.h"
#include "transfer_dialog.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QLineEdit>
#include <QStandardPaths>
#include <QTest>

class AccessibilitySmokeTest : public QObject {
  Q_OBJECT

private:
  static QWidget *findAccessible(QWidget *root, const QString &name) {
    for (QWidget *widget : root->findChildren<QWidget *>()) {
      if (widget->accessibleName() == name) {
        return widget;
      }
    }
    return nullptr;
  }

  static void verifyAccessibleControls(QWidget *root,
                                       const QStringList &names) {
    for (const QString &name : names) {
      QWidget *widget = findAccessible(root, name);
      QVERIFY2(widget != nullptr, qPrintable(name));
      QVERIFY2(!widget->size().isEmpty(),
               qPrintable(name + " must have a non-zero layout size"));
    }
  }

  static void verifyFocusChain(QWidget *root) {
    QList<QWidget *> focusable;
    for (QWidget *widget : root->findChildren<QWidget *>()) {
      if (widget->isVisible() && widget->isEnabled() &&
          widget->focusPolicy() != Qt::NoFocus) {
        focusable.append(widget);
      }
    }
    QVERIFY2(focusable.size() >= 2,
             "dialog must expose at least two keyboard-focusable controls");

    QWidget *first = focusable.first();
    first->setFocus(Qt::OtherFocusReason);
    QVERIFY2(first->focusPolicy() != Qt::NoFocus,
             "first focusable control must accept focus");

    QSet<QWidget *> chain;
    QWidget *current = first;
    for (int i = 0; i < focusable.size() + 1 && current; ++i) {
      if (chain.contains(current)) {
        break;
      }
      chain.insert(current);
      current = current->nextInFocusChain();
    }
    QVERIFY2(chain.size() >= 2,
             "dialog focus chain must contain multiple controls");
  }

  static void verifySurface(QWidget *root, const QStringList &controls) {
    root->show();
    root->adjustSize();
    QApplication::processEvents();
    QVERIFY2(!root->size().isEmpty(), "surface must have a non-zero size");
    verifyAccessibleControls(root, controls);
    verifyFocusChain(root);
  }

private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("RcloneBrowserNG-tests");
    QCoreApplication::setApplicationName("accessibility-smoke");
    QStandardPaths::setTestModeEnabled(true);
    UiPolish::ApplyApplicationStyle(false);
    QVERIFY(qApp->styleSheet().contains(":focus"));
  }

  void mainWindowControls() {
    MainWindow window(false);
    verifySurface(
        &window,
        {"Filter remotes", "Open rclone configuration",
         "Create a new rclone remote", "Refresh remotes",
         "Open selected remote", "Staged transfers awaiting review",
         "Run all staged transfers", "Clear staging queue",
         "Filter saved tasks", "Dry run selected tasks", "Run selected tasks",
         "Edit selected task", "Delete selected tasks",
         "Copy selected task command"});
    if (auto *schedule = findAccessible(&window, "Schedule selected task")) {
      QVERIFY(schedule->focusPolicy() != Qt::NoFocus);
      QVERIFY(!schedule->size().isEmpty());
      QVERIFY(findAccessible(&window, "Unschedule selected task") != nullptr);
    }
    window.close();
  }

  void transferDialogControls() {
    TransferDialog dialog(false, false, "remote", QDir(), true);
    verifySurface(&dialog,
                  {"Transfer source", "Transfer destination",
                   "Heartbeat URL for monitoring",
                   "Webhook URL for notifications", "Pre-job command",
                   "Post-job command", "Backup directory pattern",
                   "Backup retention count",
                   "Verify integrity after transfer", "Run transfer",
                   "Enqueue transfer for later", "Save transfer as task"});
    dialog.close();
  }

  void preferencesDialogControls() {
    PreferencesDialog dialog;
    verifySurface(&dialog,
                  {"Max concurrent transfers", "Default exclude patterns",
                   "SOCKS proxy", "Backup rclone config",
                   "Restore rclone config", "Start minimized to system tray"});
    dialog.close();
  }

  void scheduleDialogControls() {
    ScheduleDialog dialog("Accessibility smoke task");
    verifySurface(&dialog, {"Schedule interval", "Start time",
                            "Schedule preview", "OK", "Cancel"});
    auto *interval = dialog.findChild<QComboBox *>("scheduleInterval");
    auto *cron = dialog.findChild<QLineEdit *>("scheduleCronExpression");
    QVERIFY(interval != nullptr);
    QVERIFY(cron != nullptr);
    interval->setCurrentIndex(5);
    QVERIFY(cron->isVisible());
    cron->setText("0 2 * * 1-5");
    QVERIFY(dialog.findChild<QLabel *>("schedulePreview") != nullptr);
    dialog.close();
  }

  void remoteActionDialogs() {
    CrossRemoteSearchDialog search({"drive", "backup"}, nullptr);
    verifySurface(&search,
                  {"Search query with history", "Start search", "Cancel search",
                   "File type filter", "Minimum file size",
                   "Maximum file size", "Search results", "Close"});
    search.close();

    FolderCompareDialog compare(
        "source", "destination", {},
        [](const QString &, const QString &, const QString &,
           const QStringList &) {});
    verifySurface(&compare,
                  {"Compare source path", "Compare destination path",
                   "Run folder comparison", "Compare status filter",
                   "Filter compare paths", "Queue copy to destination repair",
                   "Queue copy to source repair",
                   "Queue delete from destination repair",
                   "Queue delete from source repair"});
    compare.close();
  }
};

QTEST_MAIN(AccessibilitySmokeTest)
#include "accessibility_smoke_test.moc"
