#include "app_log.h"

#include "rclone_capabilities.h"
#include "utils.h"

namespace AppLog {

namespace {

constexpr int kMaxBytes = 5 * 1024 * 1024;
constexpr int kGenerations = 3;

QMutex &mutex() {
  static QMutex m;
  return m;
}

Level &levelRef() {
  static Level level = Level::Info;
  return level;
}

QtMessageHandler &previousHandler() {
  static QtMessageHandler handler = nullptr;
  return handler;
}

QFile &logFile() {
  static QFile file;
  return file;
}

// Callers already hold the mutex.
void rotateIfNeededLocked() {
  QFile &file = logFile();
  if (file.isOpen() && file.size() < kMaxBytes) {
    return;
  }
  if (file.isOpen()) {
    file.close();
  }

  const QString base = LogFilePath();
  if (QFileInfo::exists(base) && QFileInfo(base).size() >= kMaxBytes) {
    // Drop the oldest, then shuffle the rest down one.
    QFile::remove(QString("%1.%2").arg(base).arg(kGenerations));
    for (int i = kGenerations - 1; i >= 1; --i) {
      const QString from = QString("%1.%2").arg(base).arg(i);
      const QString to = QString("%1.%2").arg(base).arg(i + 1);
      if (QFileInfo::exists(from)) {
        QFile::remove(to);
        QFile::rename(from, to);
      }
    }
    QFile::remove(QString("%1.1").arg(base));
    QFile::rename(base, QString("%1.1").arg(base));
  }

  file.setFileName(base);
  file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

QString levelTag(Level level) {
  switch (level) {
  case Level::Off:
    return "OFF";
  case Level::Error:
    return "ERROR";
  case Level::Warning:
    return "WARN";
  case Level::Info:
    return "INFO";
  case Level::Debug:
    return "DEBUG";
  }
  return "INFO";
}

Level levelForQtType(QtMsgType type) {
  switch (type) {
  case QtDebugMsg:
    return Level::Debug;
  case QtInfoMsg:
    return Level::Info;
  case QtWarningMsg:
    return Level::Warning;
  case QtCriticalMsg:
  case QtFatalMsg:
    return Level::Error;
  }
  return Level::Info;
}

void messageHandler(QtMsgType type, const QMessageLogContext &context,
                    const QString &message) {
  Write(levelForQtType(type), "qt", message);
  if (type == QtFatalMsg) {
    // Last chance before the process goes down.
    Flush();
  }
  if (previousHandler()) {
    previousHandler()(type, context, message);
  }
}

} // namespace

int MaxBytes() { return kMaxBytes; }
int Generations() { return kGenerations; }

QString LogDirectory() {
  QDir dir(QStandardPaths::writableLocation(
      QStandardPaths::AppLocalDataLocation));
  const QString path = dir.absoluteFilePath("logs");
  QDir().mkpath(path);
  return path;
}

QString LogFilePath() {
  return QDir(LogDirectory()).absoluteFilePath("rclonebrowser.log");
}

Level CurrentLevel() { return levelRef(); }

void SetLevel(Level level) {
  QMutexLocker locker(&mutex());
  levelRef() = level;
}

QStringList LevelNames() {
  return {"Off", "Error", "Warning", "Info", "Debug"};
}

QString LevelName(Level level) {
  switch (level) {
  case Level::Off:
    return "Off";
  case Level::Error:
    return "Error";
  case Level::Warning:
    return "Warning";
  case Level::Info:
    return "Info";
  case Level::Debug:
    return "Debug";
  }
  return "Info";
}

Level LevelFromName(const QString &name) {
  const QString needle = name.trimmed().toLower();
  if (needle == "off") {
    return Level::Off;
  }
  if (needle == "error") {
    return Level::Error;
  }
  if (needle == "warning") {
    return Level::Warning;
  }
  if (needle == "debug") {
    return Level::Debug;
  }
  return Level::Info;
}

void Write(Level level, const QString &source, const QString &message) {
  if (level == Level::Off) {
    return;
  }

  QMutexLocker locker(&mutex());
  if (levelRef() == Level::Off || static_cast<int>(level) >
                                      static_cast<int>(levelRef())) {
    return;
  }

  rotateIfNeededLocked();
  QFile &file = logFile();
  if (!file.isOpen()) {
    return;
  }

  // Same redaction the support bundle uses, so a log a user attaches to a
  // bug report cannot carry their rclone credentials.
  const QString safe = Diagnostics::redactSecrets(message).trimmed();
  const QString line =
      QString("%1 [%2] %3: %4\n")
          .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
               levelTag(level), source, safe);
  file.write(line.toUtf8());
  file.flush();
}

void Flush() {
  QMutexLocker locker(&mutex());
  if (logFile().isOpen()) {
    logFile().flush();
  }
}

void Install() {
  {
    auto settings = GetSettings();
    SetLevel(LevelFromName(
        settings->value("Settings/logLevel", "Info").toString()));
  }

  {
    QMutexLocker locker(&mutex());
    rotateIfNeededLocked();
  }

  // Everything already routed to the in-memory diagnostics buffer also lands
  // in the file, so job output and background errors are covered without
  // every caller learning about a second logger.
  Diagnostics::setLogCallback([](const QString &source, const QString &line) {
    Write(Level::Info, source, line);
  });

  previousHandler() = qInstallMessageHandler(messageHandler);

  Write(Level::Info, "app",
        QString("Rclone Browser NG %1 started").arg(RCLONE_BROWSER_VERSION));
  qAddPostRoutine([]() { Flush(); });
}

} // namespace AppLog
