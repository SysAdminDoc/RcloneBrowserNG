// Renders every screen of the app offscreen and saves each as a PNG, so the
// interface can be reviewed in both themes and the README screenshots can be
// captured without a window ever reaching a real display.
//
//   QT_QPA_PLATFORM=offscreen QT_QPA_FONTDIR=C:/Windows/Fonts \
//     ui_gallery --out <folder> --theme light|dark --rclone <rclone>
//
// Not a test: nothing here passes or fails. It builds a throwaway settings
// folder and a demo rclone.conf whose only real remote is a local alias over
// a folder of sample files, then walks the main window, its dialogs and the
// app's other dialogs, grabbing each one.

#include "combine_remote_dialog.h"
#include "cross_remote_search.h"
#include "export_dialog.h"
#include "folder_compare.h"
#include "main_window.h"
#include "preferences_dialog.h"
#include "progress_dialog.h"
#include "rclone_security_floor.h"
#include "schedule_dialog.h"
#include "transfer_dialog.h"
#include "utils.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QTreeView>

#include <cstdlib>
#include <functional>

namespace {

QString gOut;
int gSaved = 0;

void say(const QString &text) {
  QTextStream(stdout) << text << Qt::endl;
}

void pump(int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
}

bool waitFor(const std::function<bool()> &condition, int ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    if (condition()) {
      return true;
    }
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return condition();
}

void save(QWidget *widget, const QString &name) {
  pump(150);
  const QString path = QDir(gOut).filePath(name + QStringLiteral(".png"));
  if (widget->grab().save(path)) {
    ++gSaved;
    say(QStringLiteral("saved ") + path);
  } else {
    say(QStringLiteral("could not save ") + path);
  }
}

// Saves each page of a tab widget under its own name.
void saveTabs(QWidget *root, QTabWidget *tabs, const QString &prefix) {
  if (tabs == nullptr) {
    save(root, prefix);
    return;
  }
  for (int i = 0; i < tabs->count(); ++i) {
    tabs->setCurrentIndex(i);
    QString label = tabs->tabText(i).toLower().remove(QLatin1Char('&'));
    label.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                  QStringLiteral("-"));
    while (label.endsWith(QLatin1Char('-'))) {
      label.chop(1);
    }
    save(root, prefix + QLatin1Char('-') + QString::number(i + 1) +
                   QLatin1Char('-') + label);
  }
}

void showDialog(QDialog *dialog, const QString &name, bool tabs = false) {
  dialog->show();
  pump(300);
  if (tabs) {
    saveTabs(dialog, dialog->findChild<QTabWidget *>(), name);
  } else {
    save(dialog, name);
  }
  // The smallest size the dialog allows, where rows used to overlap.
  if (auto *tabWidget = dialog->findChild<QTabWidget *>()) {
    tabWidget->setCurrentIndex(0);
  }
  dialog->resize(dialog->minimumSize());
  save(dialog, name + QStringLiteral("-min"));
  dialog->close();
}

// Runs trigger, which opens a modal dialog with exec(), then grabs the
// dialog and closes it, or answers yes when accept is set. Message boxes on
// the way are grabbed too.
void grabModal(const std::function<void()> &trigger, const QString &name,
               bool accept = false) {
  QTimer poll;
  poll.setInterval(100);
  int seen = 0;
  QObject::connect(&poll, &QTimer::timeout, [&]() {
    QWidget *modal = QApplication::activeModalWidget();
    if (modal == nullptr || !modal->isVisible()) {
      return;
    }
    poll.stop();
    pump(400);
    save(modal, seen == 0 ? name : name + QString::number(seen + 1));
    ++seen;
    if (auto *box = qobject_cast<QMessageBox *>(modal); box && accept) {
      for (QAbstractButton *button : box->buttons()) {
        const auto role = box->buttonRole(button);
        if (role == QMessageBox::YesRole || role == QMessageBox::AcceptRole) {
          button->click();
          break;
        }
      }
    } else if (auto *dialog = qobject_cast<QDialog *>(modal)) {
      dialog->reject();
    } else {
      modal->close();
    }
    poll.start();
  });
  poll.start();
  QTimer::singleShot(15000, &poll, [&poll]() {
    poll.stop();
    if (QWidget *modal = QApplication::activeModalWidget()) {
      modal->close();
    }
  });
  trigger();
  pump(500);
  poll.stop();
}

QAction *findAction(QWidget *root, const QString &text) {
  for (QAction *action : root->findChildren<QAction *>()) {
    if (action->text().remove(QLatin1Char('&')) == text) {
      return action;
    }
  }
  return nullptr;
}

QPushButton *findButton(QWidget *root, const QString &text) {
  for (QPushButton *button : root->findChildren<QPushButton *>()) {
    if (button->text().remove(QLatin1Char('&')) == text) {
      return button;
    }
  }
  return nullptr;
}

void writeFile(const QString &path, qint64 bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }
  QByteArray chunk(64 * 1024, 'x');
  for (qint64 left = bytes; left > 0; left -= chunk.size()) {
    file.write(chunk.constData(), (std::min)(left, qint64(chunk.size())));
  }
}

QString firstLine(const QString &program, const QStringList &args) {
  QProcess probe;
  probe.start(program, args);
  if (!probe.waitForFinished(30000)) {
    return QString();
  }
  QString text = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
  const qsizetype lineBreak = text.indexOf(QLatin1Char('\n'));
  if (lineBreak >= 0) {
    text.truncate(lineBreak);
  }
  return text.trimmed();
}

} // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  QCommandLineParser parser;
  parser.addOption({QStringLiteral("out"), QStringLiteral("Folder for PNGs"),
                    QStringLiteral("folder")});
  parser.addOption({QStringLiteral("theme"), QStringLiteral("light or dark"),
                    QStringLiteral("theme"), QStringLiteral("light")});
  parser.addOption({QStringLiteral("rclone"), QStringLiteral("rclone binary"),
                    QStringLiteral("path")});
  parser.process(app);
  gOut = parser.value(QStringLiteral("out"));
  const bool dark = parser.value(QStringLiteral("theme")) == QLatin1String("dark");
  const QString rclone = parser.value(QStringLiteral("rclone"));
  if (gOut.isEmpty() || rclone.isEmpty()) {
    say(QStringLiteral("usage: ui_gallery --out <folder> --theme light|dark "
                       "--rclone <rclone>"));
    return 2;
  }
  QDir().mkpath(gOut);
  // A dialog nobody answers would otherwise keep this running forever.
  QTimer::singleShot(300000, []() {
    say(QStringLiteral("gave up after 5 minutes, %1 captures").arg(gSaved));
    std::_Exit(3);
  });

  QTemporaryDir settingsDir;
  QTemporaryDir demoDir;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     settingsDir.path());
  QCoreApplication::setOrganizationName(QStringLiteral("rclone-browser-gallery"));
  QCoreApplication::setApplicationName(QStringLiteral("rclone-browser-gallery"));

  // Sample files with ordinary names and sizes, under a folder whose path
  // never shows in a capture.
  const QDir demo(demoDir.path());
  const QString documents = demo.filePath(QStringLiteral("Documents"));
  for (const char *folder : {"Invoices", "Photos 2024", "Projects", "Taxes"}) {
    QDir(documents).mkpath(QString::fromLatin1(folder));
  }
  writeFile(QDir(documents).filePath(QStringLiteral("Budget 2025.xlsx")),
            96 * 1024);
  writeFile(QDir(documents).filePath(QStringLiteral("Holiday video.mp4")),
            24 * 1024 * 1024);
  writeFile(QDir(documents).filePath(QStringLiteral("Meeting notes.txt")),
            3 * 1024);
  writeFile(QDir(documents).filePath(QStringLiteral("Quarterly report.pdf")),
            2300 * 1024);
  demo.mkpath(QStringLiteral("Copies"));

  const QString config = demo.filePath(QStringLiteral("rclone.conf"));
  {
    QFile file(config);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      say(QStringLiteral("could not write the demo rclone.conf"));
      return 1;
    }
    QTextStream conf(&file);
    conf << "[Documents]\ntype = alias\nremote = "
         << QDir::fromNativeSeparators(documents) << "\n\n"
         << "[gdrive]\ntype = drive\nscope = drive\n\n"
         << "[onedrive]\ntype = onedrive\n\n"
         << "[dropbox]\ntype = dropbox\n\n"
         << "[nas]\ntype = sftp\nhost = nas.local\nuser = backup\n\n"
         << "[s3-archive]\ntype = s3\nprovider = AWS\n\n"
         << "[photos-crypt]\ntype = crypt\nremote = nas:photos\n"
            "password = 3AXGGH8DsHTe5-vwtLbAOA\n\n"
         << "[proton]\ntype = protondrive\n";
  }

  {
    QSettings settings;
    settings.setValue(QStringLiteral("Settings/rclone"), rclone);
    settings.setValue(QStringLiteral("Settings/rcloneConf"), config);
    settings.setValue(QStringLiteral("Settings/darkMode"), dark);
    settings.setValue(QStringLiteral("Settings/checkRcloneBrowserUpdates"), false);
    settings.setValue(QStringLiteral("Settings/checkRcloneUpdates"), false);
    settings.setValue(
        QStringLiteral("Settings/rcloneCveWarnedVersion"),
        RcloneSecurityFloor::WarnedKey(
            firstLine(rclone, {QStringLiteral("version")})));
    settings.sync();
  }
  SetRclone(rclone);
  SetRcloneConf(config);

  const QString theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
  MainWindow window(true);
  window.resize(1280, 720);
  window.show();
  auto *remotes = window.findChild<QListWidget *>(QStringLiteral("remotes"));
  auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("tabs"));
  if (remotes == nullptr || tabs == nullptr) {
    say(QStringLiteral("main window has no remotes list or tabs"));
    return 1;
  }
  if (!waitFor([remotes]() { return remotes->count() >= 8; }, 60000)) {
    say(QStringLiteral("remotes never listed"));
  }
  pump(2500);

  // Remotes, with the second remote selected the way a user leaves it.
  tabs->setCurrentIndex(0);
  remotes->setCurrentRow(1);
  save(&window, QStringLiteral("remotes-") + theme);

  // Jobs and Tasks while empty.
  tabs->setCurrentIndex(1);
  save(&window, QStringLiteral("jobs-empty-") + theme);
  tabs->setCurrentIndex(2);
  save(&window, QStringLiteral("tasks-empty-") + theme);

  // Browse the local alias.
  tabs->setCurrentIndex(0);
  remotes->setCurrentRow(0);
  if (QPushButton *open = findButton(&window, QStringLiteral("Open"))) {
    const int before = tabs->count();
    open->click();
    waitFor([tabs, before]() { return tabs->count() > before; }, 20000);
    pump(3000);
    if (auto *tree = tabs->currentWidget()->findChild<QTreeView *>()) {
      tree->expand(tree->model()->index(0, 0));
      pump(2500);
      tree->setCurrentIndex(tree->model()->index(1, 0, tree->model()->index(0, 0)));
    }
    save(&window, QStringLiteral("browse-") + theme);
  }

  // A live transfer card, slowed down so it is still running when grabbed.
  QMetaObject::invokeMethod(
      &window, "addTransfer", Qt::DirectConnection,
      Q_ARG(QString, QStringLiteral("Copy Documents to Copies")),
      Q_ARG(QString, QStringLiteral("Documents:")),
      Q_ARG(QString, QDir::fromNativeSeparators(demo.filePath(QStringLiteral("Copies")))),
      Q_ARG(QStringList,
            QStringList({QStringLiteral("copy"), QStringLiteral("--bwlimit"),
                         QStringLiteral("100k"), QStringLiteral("--verbose"),
                         QStringLiteral("--stats"), QStringLiteral("1s"),
                         QStringLiteral("Documents:"),
                         QDir::fromNativeSeparators(demo.filePath(QStringLiteral("Copies")))})));
  pump(5000);
  tabs->setCurrentIndex(1);
  save(&window, QStringLiteral("jobs-running-") + theme);

  // Main window dialogs.
  for (const auto &[text, name] :
       QList<QPair<QString, QString>>{
           {QStringLiteral("Job History"), QStringLiteral("job-history")},
           {QStringLiteral("Remote Health..."), QStringLiteral("remote-health")},
           {QStringLiteral("About"), QStringLiteral("about")}}) {
    if (QAction *action = findAction(&window, text)) {
      grabModal([action]() { action->trigger(); }, name + QLatin1Char('-') + theme);
    } else {
      say(QStringLiteral("no action named ") + text);
    }
  }

  // Dialogs built on their own.
  {
    TransferDialog transfer(false, false, QStringLiteral("Documents"), QDir(),
                            true, &window);
    showDialog(&transfer, QStringLiteral("transfer-") + theme, true);
  }
  {
    PreferencesDialog preferences(&window);
    showDialog(&preferences, QStringLiteral("preferences-") + theme, true);
  }
  {
    ScheduleDialog schedule(QStringLiteral("Nightly photo backup"), &window);
    showDialog(&schedule, QStringLiteral("schedule-") + theme);
  }
  {
    CrossRemoteSearchDialog search({QStringLiteral("Documents"),
                                    QStringLiteral("gdrive")},
                                   &window);
    showDialog(&search, QStringLiteral("search-") + theme);
  }
  {
    FolderCompareDialog compare(
        QStringLiteral("Documents:Projects"), QStringLiteral("nas:projects"), {},
        [](const QString &, const QString &, const QString &,
           const QStringList &) {},
        &window);
    showDialog(&compare, QStringLiteral("compare-") + theme);
  }
  {
    ExportDialog exporter(QStringLiteral("Documents"), QDir(QStringLiteral("/")),
                          &window);
    showDialog(&exporter, QStringLiteral("export-") + theme, true);
  }
  {
    CombineRemoteDialog combine(
        {{QStringLiteral("Documents"), QStringLiteral("alias")},
         {QStringLiteral("gdrive"), QStringLiteral("drive")},
         {QStringLiteral("nas"), QStringLiteral("sftp")}},
        {QStringLiteral("Documents"), QStringLiteral("gdrive"),
         QStringLiteral("nas")},
        QStringLiteral("everything"), QStringLiteral("Documents"), &window);
    showDialog(&combine, QStringLiteral("combine-") + theme);
  }
  {
    // The dialog starts the process itself, as the app's callers expect.
    auto *process = new QProcess(&window);
    process->setProgram(rclone);
    process->setArguments({QStringLiteral("--config"), config,
                           QStringLiteral("size"), QStringLiteral("Documents:")});
    // Same arguments as Get Size in the browser, which keeps the dialog open.
    ProgressDialog progress(QStringLiteral("Get Size"),
                            QStringLiteral("Calculating..."),
                            QStringLiteral("Documents:"), process, &window,
                            false);
    progress.expand();
    progress.allowToClose();
    progress.show();
    process->waitForFinished(20000);
    pump(800);
    save(&progress, QStringLiteral("progress-") + theme);
    progress.close();
  }

  // Closing with a transfer running asks first, which is a screen too.
  grabModal([&window]() { window.close(); },
            QStringLiteral("quit-with-jobs-") + theme, true);
  pump(1500);
  say(QStringLiteral("%1 captures").arg(gSaved));
  return 0;
}
