#include <QDebug>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <cstdlib>

namespace {
void require(bool condition, const QString &message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

struct RcSite {
  QString file;
  int line;
  QString text;
};

QList<RcSite> scanForRcPatterns(const QString &srcDir) {
  QList<RcSite> sites;
  QDir dir(srcDir);
  const QStringList files = dir.entryList(QStringList() << "*.cpp" << "*.h",
                                          QDir::Files);
  for (const QString &fileName : files) {
    QFile file(dir.filePath(fileName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      continue;
    }
    QTextStream in(&file);
    int lineNo = 0;
    while (!in.atEnd()) {
      ++lineNo;
      const QString line = in.readLine();
      if (line.contains("--rc-addr") || line.contains("--rc-no-auth") ||
          line.contains("\"rcd\"") ||
          (line.contains("\"rc\"") && line.contains("args"))) {
        sites.append({fileName, lineNo, line.trimmed()});
      }
    }
  }
  return sites;
}
} // namespace

int main() {
  QString srcDir = QDir::currentPath() + "/../src";
  if (!QDir(srcDir).exists()) {
    srcDir = QDir::currentPath() + "/src";
  }
  if (!QDir(srcDir).exists()) {
    srcDir = QDir::currentPath() + "/../../src";
  }
  require(QDir(srcDir).exists(),
          "Cannot find src/ directory — run from build/ or repo root");

  const QList<RcSite> sites = scanForRcPatterns(srcDir);
  require(!sites.isEmpty(), "Found zero RC command sites — test is broken");

  qInfo() << "Found" << sites.size() << "RC-related lines across source files";

  bool hasRcdStartup = false;
  bool hasMountRc = false;
  bool hasRcNoAuth = false;
  QStringList noAuthViolations;

  QHash<QString, QStringList> fileLines;
  QDir dir(srcDir);
  for (const QString &fileName :
       dir.entryList(QStringList() << "*.cpp", QDir::Files)) {
    QFile file(dir.filePath(fileName));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      fileLines[fileName] = in.readAll().split('\n');
    }
  }

  for (const auto &site : sites) {
    if (site.text.contains("--rc-no-auth")) {
      hasRcNoAuth = true;
      noAuthViolations << QString("%1:%2: %3")
                              .arg(site.file)
                              .arg(site.line)
                              .arg(site.text);
    }

    if (site.text.contains("\"rcd\"")) {
      hasRcdStartup = true;
      const QStringList &lines = fileLines.value(site.file);
      bool foundAuth = false;
      int start = qMax(0, site.line - 5);
      int end = qMin(lines.size(), site.line + 15);
      for (int i = start; i < end; ++i) {
        if (lines[i].contains("--rc-user") ||
            lines[i].contains("--rc-pass")) {
          foundAuth = true;
          break;
        }
      }
      require(foundAuth,
              QString("rcd startup at %1:%2 has no --rc-user/--rc-pass within "
                      "±15 lines")
                  .arg(site.file)
                  .arg(site.line));
    }

    if (site.text.contains("--rc-addr")) {
      hasMountRc = true;
      const QStringList &lines = fileLines.value(site.file);
      bool foundAuth = false;
      int start = qMax(0, site.line - 10);
      int end = qMin(lines.size(), site.line + 15);
      for (int i = start; i < end; ++i) {
        if (lines[i].contains("--rc-user") ||
            lines[i].contains("--rc-pass") ||
            lines[i].contains("mRcUser") || lines[i].contains("mRcPass") ||
            lines[i].contains("rcUser") || lines[i].contains("rcPass")) {
          foundAuth = true;
          break;
        }
      }
      require(foundAuth,
              QString("RC endpoint at %1:%2 has no auth credentials within "
                      "±15 lines")
                  .arg(site.file)
                  .arg(site.line));
    }
  }

  require(!hasRcNoAuth,
          "SECURITY: --rc-no-auth found in source:\n  " +
              noAuthViolations.join("\n  "));
  require(hasRcdStartup, "Expected at least one rcd startup site");
  require(hasMountRc, "Expected at least one --rc-addr site");

  qInfo() << "All RC security regression checks passed.";
  return 0;
}
