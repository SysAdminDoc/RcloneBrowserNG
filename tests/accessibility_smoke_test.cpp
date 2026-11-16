#include "cross_remote_search.h"
#include "folder_compare.h"
#include "interface_polish.h"
#include "list_of_job_options.h"
#include "main_window.h"
#include "preferences_dialog.h"
#include "rclone_installer.h"
#include "schedule_dialog.h"
#include "transfer_dialog.h"
#include "utils.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <functional>

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

  // The manager's hint promised a Delete key that was never wired up, so the
  // only way to drop one bookmark was to clear them all.
  void bookmarkManagerRemovesTheSelectedBookmark() {
    const auto cleanup =
        qScopeGuard([] { GetSettings()->remove("Settings/bookmarks"); });
    GetSettings()->setValue(
        "Settings/bookmarks",
        QStringList{"first:docs", "second:photos", "third:music"});

    MainWindow window(false);
    QMenu *bookmarks = nullptr;
    for (QMenu *menu : window.findChildren<QMenu *>()) {
      if (menu->title() == "&Bookmarks") {
        bookmarks = menu;
      }
    }
    QVERIFY(bookmarks != nullptr);
    emit bookmarks->aboutToShow();
    QAction *manage = nullptr;
    for (QAction *action : bookmarks->actions()) {
      if (action->text().startsWith("Manage Bookmarks")) {
        manage = action;
      }
    }
    QVERIFY(manage != nullptr);

    QString problem;
    bool removeStartedDisabled = false;
    bool removeEnabledBySelection = false;
    QTimer::singleShot(0, &window, [&]() {
      QDialog *dialog = nullptr;
      for (QWidget *top : QApplication::topLevelWidgets()) {
        auto *candidate = qobject_cast<QDialog *>(top);
        if (candidate && candidate->isVisible() &&
            candidate->windowTitle() == "Manage Bookmarks") {
          dialog = candidate;
        }
      }
      if (!dialog) {
        problem = "the manager dialog never opened";
        return;
      }
      auto *list = dialog->findChild<QListWidget *>();
      QPushButton *remove = nullptr;
      for (QPushButton *button : dialog->findChildren<QPushButton *>()) {
        if (button->text() == "Remove") {
          remove = button;
        }
      }
      if (!list || !remove) {
        problem = "the manager has no list or no Remove button";
        dialog->reject();
        return;
      }
      removeStartedDisabled = !remove->isEnabled();
      list->setCurrentRow(1);
      removeEnabledBySelection = remove->isEnabled();
      remove->click();
      dialog->accept();
    });
    manage->trigger();

    QVERIFY2(problem.isEmpty(), qPrintable(problem));
    QVERIFY(removeStartedDisabled);
    QVERIFY(removeEnabledBySelection);
    QCOMPARE(GetSettings()->value("Settings/bookmarks").toStringList(),
             (QStringList{"first:docs", "third:music"}));
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

  // rclone 1.76.0 refuses a timetable time without a colon or with a sign,
  // and only when the job starts. The editor marks one, keeps OK off until
  // it's fixed and leaves it out of the preview and the field.
  void bandwidthEditorMarksTimesRcloneRefuses() {
    TransferDialog dialog(false, false, "remote", QDir(), true);
    auto *field = dialog.findChild<QLineEdit *>("textBandwidth");
    auto *edit = dialog.findChild<QToolButton *>("buttonBandwidthEdit");
    QVERIFY(field != nullptr);
    QVERIFY(edit != nullptr);

    const auto openEditor = [&](const std::function<void(QDialog *)> &drive) {
      QString problem = "the timetable editor never opened";
      QTimer::singleShot(0, &dialog, [&]() {
        for (QWidget *top : QApplication::topLevelWidgets()) {
          auto *candidate = qobject_cast<QDialog *>(top);
          if (candidate && candidate->isVisible() &&
              candidate->windowTitle() == "Bandwidth Timetable") {
            problem.clear();
            drive(candidate);
            // Still open when the drive didn't press OK.
            if (candidate->isVisible()) {
              candidate->reject();
            }
            return;
          }
        }
      });
      edit->click();
      return problem;
    };
    const auto okOf = [](QDialog *editor) {
      auto *buttons = editor->findChild<QDialogButtonBox *>();
      return buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
    };

    field->setText("0800,1M 09:00,2M +08:00,3M");
    QStringList seen;
    QString problem = openEditor([&](QDialog *editor) {
      auto *table = editor->findChild<QTableWidget *>();
      auto *preview = editor->findChild<QLabel *>("bandwidthPreview");
      auto *message = editor->findChild<QLabel *>("bandwidthProblem");
      QPushButton *ok = okOf(editor);
      if (!table || !preview || !message || !ok || table->rowCount() != 3) {
        seen << "the editor is missing a part or a row";
        return;
      }
      seen << table->item(0, 0)->toolTip() << table->item(1, 0)->toolTip()
           << table->item(2, 0)->toolTip()
           << (ok->isEnabled() ? "OK on" : "OK off") << preview->text()
           << message->text();
      table->item(0, 0)->setText("08:00");
      table->item(2, 0)->setText("Mon-10:00");
      seen << (ok->isEnabled() ? "OK on" : "OK off") << preview->text()
           << table->item(0, 0)->toolTip() << message->text();
      ok->click();
    });
    QVERIFY2(problem.isEmpty(), qPrintable(problem));
    const QString clock = "Write the time as HH:MM, like 08:00.";
    QCOMPARE(seen,
             (QStringList{clock, "", clock, "OK off",
                          "Preview: --bwlimit \"09:00,2M\"",
                          "Row 1: " + clock + " 2 rows need fixing.", "OK on",
                          "Preview: --bwlimit \"08:00,1M 09:00,2M "
                          "Mon-10:00,3M\"",
                          "", ""}));
    QCOMPARE(field->text(), QString("08:00,1M 09:00,2M Mon-10:00,3M"));

    // A plain rate opens as one row at 00:00 and comes back unchanged.
    field->setText("10M");
    seen.clear();
    problem = openEditor([&](QDialog *editor) {
      auto *table = editor->findChild<QTableWidget *>();
      if (!table || table->rowCount() != 1) {
        seen << "expected one row";
        return;
      }
      seen << table->item(0, 0)->text() << table->item(0, 1)->text();
      okOf(editor)->click();
    });
    QVERIFY2(problem.isEmpty(), qPrintable(problem));
    QCOMPARE(seen, (QStringList{"00:00", "10M"}));
    QCOMPARE(field->text(), QString("10M"));

    // Typed straight into the field, it stops the run and shows the tab.
    auto *source = dialog.findChild<QLineEdit *>("textSource");
    auto *tabs = dialog.findChild<QTabWidget *>("tabWidget");
    QVERIFY(source != nullptr);
    QVERIFY(tabs != nullptr);
    source->setText(QDir::tempPath());
    tabs->setCurrentIndex(0);
    field->setText("+08:00,1M");
    QDialog &base = dialog;
    // A result the dialog never sets, so a refused OK is told apart from
    // one that closed the dialog either way.
    base.setResult(-1);
    base.done(QDialog::Accepted);
    QCOMPARE(dialog.result(), -1);
    QCOMPARE(field->property("fieldState").toString(), QString("error"));
    QVERIFY(tabs->currentWidget()->isAncestorOf(field));
    field->setText("08:00,1M");
    QCOMPARE(field->property("fieldState").toString(), QString());
    base.done(QDialog::Accepted);
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
  }

  // The status bar's Limit is passed to every new job as --bwlimit, so a
  // value rclone would refuse is marked and never saved.
  void statusBarLimitRefusesWhatRcloneRefuses() {
    const auto cleanup = qScopeGuard(
        [] { GetSettings()->remove("Settings/globalBandwidthLimit"); });
    GetSettings()->setValue("Settings/globalBandwidthLimit", "0800,1M");

    MainWindow window(false);
    auto *limit = qobject_cast<QLineEdit *>(
        findAccessible(&window, "Global bandwidth limit"));
    QVERIFY(limit != nullptr);
    // One saved by an older build is marked as soon as the window opens,
    // and runs leave it out instead of rclone refusing every one of them.
    QCOMPARE(limit->property("fieldState").toString(), QString("error"));
    QVERIFY2(limit->toolTip().contains("Transfers run without a limit"),
             qPrintable(limit->toolTip()));
    JobOptions job;
    QVERIFY2(!job.getOptions().contains("--bwlimit"),
             qPrintable(job.getOptions().join(' ')));

    limit->setText("08:00,512k 0900,off");
    emit limit->editingFinished();
    QCOMPARE(limit->property("fieldState").toString(), QString("error"));
    QVERIFY(limit->toolTip().contains("HH:MM"));
    QVERIFY2(limit->toolTip().contains("It isn't saved."),
             qPrintable(limit->toolTip()));
    QCOMPARE(GetSettings()->value("Settings/globalBandwidthLimit").toString(),
             QString("0800,1M"));

    limit->setText("08:00,512k 09:00,off");
    emit limit->editingFinished();
    QCOMPARE(limit->property("fieldState").toString(), QString());
    QCOMPARE(GetSettings()->value("Settings/globalBandwidthLimit").toString(),
             QString("08:00,512k 09:00,off"));
    const QStringList options = job.getOptions();
    const qsizetype bwlimit = options.indexOf("--bwlimit");
    QVERIFY2(bwlimit >= 0 &&
                 options.value(bwlimit + 1) == "08:00,512k 09:00,off",
             qPrintable(options.join(' ')));
    window.close();
  }

  // A saved task whose own limit rclone refuses says what to fix instead
  // of starting a job rclone stops at once.
  void taskWithARefusedLimitSaysSoInsteadOfStarting() {
    auto *task = new JobOptions(false);
    task->description = "Old Limit";
    task->operation = JobOptions::Copy;
    task->source = QDir::tempPath();
    task->dest = QDir(QDir::tempPath()).filePath("old-limit-dst");
    task->bandwidth = "0800,1M";
    ListOfJobOptions::getInstance()->Persist(task);
    const auto forget = qScopeGuard([task] {
      ListOfJobOptions::getInstance()->Forget(task);
      delete task;
    });

    auto said = std::make_shared<QString>();
    QPointer<QTimer> poll = new QTimer(this);
    poll->setInterval(20);
    connect(poll, &QTimer::timeout, poll, [poll, said]() {
      auto *box =
          qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (box == nullptr || box->windowTitle() != "Task didn't start") {
        return;
      }
      *said = box->text();
      poll->stop();
      poll->deleteLater();
      box->accept();
    });
    poll->start();

    MainWindow window(false);
    auto *list = window.findChild<QListWidget *>("tasksListWidget");
    QVERIFY(list != nullptr);
    for (int i = 0; i < list->count(); ++i) {
      auto *item = static_cast<JobOptionsListWidgetItem *>(list->item(i));
      item->setSelected(item->GetData() == task);
    }
    QCOMPARE(list->selectedItems().size(), 1);
    window.findChild<QPushButton *>("buttonRunTask")->click();
    if (poll) {
      poll->deleteLater();
    }
    QVERIFY2(said->contains("\"Old Limit\" didn't start because rclone won't "
                            "accept its bandwidth limit. 0800,1M: Write the "
                            "time as HH:MM"),
             qPrintable(*said));
    window.close();
  }

  void preferencesDialogControls() {
    PreferencesDialog dialog;
        verifySurface(&dialog,
                  {"Max concurrent transfers", "Mount preset",
                   "Mount preset flags", "Default exclude patterns",
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

  // First start on a computer with no rclone anywhere: the window asks to
  // download it, and "Not now" leaves a hint instead of a silent window.
  void firstStartWithoutRcloneAsks() {
    const QString name =
        RcloneInstaller::BinaryName(RcloneInstaller::PlatformTag());
    if (QFileInfo::exists(QDir(RcloneInstaller::InstallDir()).filePath(name))) {
      QSKIP("An rclone sits beside this test, so there's nothing to ask.");
    }
    QDir(RcloneInstaller::FallbackDir()).removeRecursively();
    const QByteArray path = qgetenv("PATH");
    const QString rclone = GetRclone();
    auto restore = qScopeGuard([path, rclone] {
      qputenv("PATH", path);
      SetRclone(rclone);
    });
    // Only the folders holding an rclone go; Qt's own DLLs stay reachable.
    QStringList kept;
    for (const QString &dir :
         QString::fromLocal8Bit(path).split(QDir::listSeparator(),
                                            Qt::SkipEmptyParts)) {
      if (!QFileInfo::exists(QDir(dir).filePath(name))) {
        kept << dir;
      }
    }
    qputenv("PATH", kept.join(QDir::listSeparator()).toLocal8Bit());
    SetRclone(QString());
    GetSettings()->remove("Settings/rclone");
    QVERIFY(QStandardPaths::findExecutable("rclone").isEmpty());

    auto asked = std::make_shared<bool>(false);
    auto *poll = new QTimer(this);
    poll->setInterval(20);
    connect(poll, &QTimer::timeout, poll, [poll, asked]() {
      auto *box =
          qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (box == nullptr || box->windowTitle() != "rclone not found") {
        return;
      }
      for (QAbstractButton *button : box->buttons()) {
        if (button->text() == "Not now") {
          *asked = true;
          poll->stop();
          poll->deleteLater();
          button->click();
          return;
        }
      }
    });
    poll->start();

    MainWindow window(true);
    const auto statusSays = [&window](const QString &text) {
      for (QLabel *label : window.findChildren<QLabel *>()) {
        if (label->text().contains(text)) {
          return true;
        }
      }
      return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(statusSays("No rclone yet"), 10000);
    QVERIFY(*asked);
    QVERIFY(GetSettings()->value("Settings/rclone").toString().isEmpty());
  }
};

QTEST_MAIN(AccessibilitySmokeTest)
#include "accessibility_smoke_test.moc"
