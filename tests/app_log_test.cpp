#include "app_log.h"
#include "rclone_capabilities.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

// A scheduled transfer that fails overnight used to leave nothing behind:
// the Jobs tab error list and the support bundle both died with the process.
// Asked for three times upstream (kapitainsky#134, kapitainsky#233,
// mmozeiko#148).
class AppLogTest : public QObject {
  Q_OBJECT

private:
  QTemporaryDir mDir;

  static QString readLog() {
    QFile file(AppLog::LogFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      return QString();
    }
    return QString::fromUtf8(file.readAll());
  }

private slots:
  void initTestCase() {
    QVERIFY(mDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mDir.path());
    // AppLocalDataLocation follows these, so the log lands under the test's
    // own directory rather than the user's real profile.
    QCoreApplication::setOrganizationName("rclone-browser-log-test");
    QCoreApplication::setApplicationName("rclone-browser-log-test");
    AppLog::SetLevel(AppLog::Level::Info);
  }

  void init() {
    // Each case starts from an empty log directory.
    QDir(AppLog::LogDirectory()).removeRecursively();
    QDir().mkpath(AppLog::LogDirectory());
  }

  void writesTimestampedLinesWithSourceAndLevel() {
    AppLog::Write(AppLog::Level::Error, "job", "copy failed");
    const QString contents = readLog();
    QVERIFY2(!contents.isEmpty(), "the log file must exist and have content");
    QVERIFY2(contents.contains("[ERROR] job: copy failed"),
             qPrintable(contents));
    // ISO-8601 UTC, so lines from different machines sort together. Compare
    // against the UTC date: the local date is a day behind every evening in
    // the Americas, which failed this check after 7 PM Eastern.
    const QDate utcToday = QDateTime::currentDateTimeUtc().date();
    QVERIFY2(contents.contains(utcToday.toString(Qt::ISODate)) ||
                 contents.contains(utcToday.addDays(-1).toString(Qt::ISODate)),
             qPrintable(contents));
    QVERIFY2(QRegularExpression("\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}"
                                "\\.\\d{3}Z")
                 .match(contents)
                 .hasMatch(),
             qPrintable(contents));
  }

  void redactsSecretsBeforeWriting() {
    // A log a user attaches to a bug report must not carry their credentials.
    AppLog::Write(AppLog::Level::Error, "job",
                  "rclone rcd --rc-pass hunter2 --rc-user rclonebrowser");
    AppLog::Write(AppLog::Level::Error, "job",
                  "RCLONE_CONFIG_PASS=topsecret in environment");
    const QString contents = readLog();
    QVERIFY2(!contents.contains("hunter2"), qPrintable(contents));
    QVERIFY2(!contents.contains("topsecret"), qPrintable(contents));
    QVERIFY(contents.contains("--rc-pass"));
  }

  void honoursTheConfiguredLevel() {
    AppLog::SetLevel(AppLog::Level::Error);
    AppLog::Write(AppLog::Level::Info, "app", "chatty info line");
    AppLog::Write(AppLog::Level::Error, "app", "real problem");
    QString contents = readLog();
    QVERIFY2(!contents.contains("chatty info line"), qPrintable(contents));
    QVERIFY2(contents.contains("real problem"), qPrintable(contents));

    AppLog::SetLevel(AppLog::Level::Off);
    AppLog::Write(AppLog::Level::Error, "app", "written while off");
    contents = readLog();
    QVERIFY2(!contents.contains("written while off"), qPrintable(contents));

    AppLog::SetLevel(AppLog::Level::Info);
  }

  void levelNamesRoundTrip() {
    for (const QString &name : AppLog::LevelNames()) {
      QCOMPARE(AppLog::LevelName(AppLog::LevelFromName(name)), name);
    }
    // An unreadable setting must not silently disable logging.
    QCOMPARE(AppLog::LevelFromName("nonsense"), AppLog::Level::Info);
    QCOMPARE(AppLog::LevelFromName(QString()), AppLog::Level::Info);
    QCOMPARE(AppLog::LevelFromName("  ERROR  "), AppLog::Level::Error);
  }

  void rotatesAtTheSizeCapAndKeepsGenerations() {
    const QString base = AppLog::LogFilePath();
    // One line per write would take a very long time to reach 5 MB, so seed
    // the file at the cap and let the next write trigger the rotation.
    const int cap = AppLog::MaxBytes();
    for (int generation = 0; generation < AppLog::Generations() + 1;
         ++generation) {
      AppLog::Flush();
      QFile seed(base);
      QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Append));
      seed.write(QByteArray(cap, 'x'));
      seed.close();
      AppLog::Write(AppLog::Level::Error, "app",
                    QString("after rotation %1").arg(generation));
    }

    QVERIFY2(QFileInfo(base).size() < cap,
             "the live log restarts small after rotating");
    for (int i = 1; i <= AppLog::Generations(); ++i) {
      const QString rotated = QString("%1.%2").arg(base).arg(i);
      QVERIFY2(QFileInfo::exists(rotated), qPrintable(rotated));
    }
    // Only the configured number of generations is kept.
    QVERIFY2(!QFileInfo::exists(
                 QString("%1.%2").arg(base).arg(AppLog::Generations() + 1)),
             "older generations beyond the limit are dropped");
  }

  void diagnosticsOutputReachesTheFile() {
    // Install() routes Diagnostics::appendLog into the file so job output and
    // background errors are covered without every caller learning about a
    // second logger.
    Diagnostics::setLogCallback(
        [](const QString &source, const QString &line) {
          AppLog::Write(AppLog::Level::Info, source, line);
        });
    Diagnostics::appendLog("job", "rclone said something");
    QVERIFY2(readLog().contains("job: rclone said something"),
             qPrintable(readLog()));
    Diagnostics::setLogCallback(nullptr);
  }

  void saysOnceWhenTheLogCannotBeOpened() {
    // A log that couldn't be opened stayed empty without a word about why.
    // Fill the open file to the cap so the next line closes it and opens
    // whatever LogFilePath() names by then, which is a folder.
    AppLog::Write(AppLog::Level::Error, "app", "before");
    AppLog::Flush();
    {
      QFile seed(AppLog::LogFilePath());
      QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Append));
      seed.write(QByteArray(AppLog::MaxBytes(), 'x'));
    }

    const QString name = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(name + "-blocked");
    const QString blocked = AppLog::LogDirectory();
    const auto restore = qScopeGuard([name, blocked] {
      QCoreApplication::setApplicationName(name);
      QDir(blocked).removeRecursively();
    });
    QVERIFY(QDir().mkpath(AppLog::LogFilePath()));

    QTest::ignoreMessage(
        QtWarningMsg,
        QRegularExpression("^Can't open the log file .*rclonebrowser\\.log: "));
    QTest::failOnWarning(QRegularExpression("Can't open the log file"));
    AppLog::Write(AppLog::Level::Error, "app", "first line");
    AppLog::Write(AppLog::Level::Error, "app", "second line");
  }
};

QTEST_MAIN(AppLogTest)
#include "app_log_test.moc"
