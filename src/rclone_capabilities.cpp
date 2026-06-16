#include "rclone_capabilities.h"
#include "utils.h"

namespace {
bool versionAtLeast(const QString &ver, const char *min) {
  if (ver.isEmpty()) {
    return false;
  }
  return compareVersion(ver.toStdString(), min) != 2;
}
} // namespace

bool RcloneCapabilities::hasNameTransform() const {
  return versionAtLeast(rcloneVersion, "1.74");
}

bool RcloneCapabilities::hasListCutoff() const {
  return versionAtLeast(rcloneVersion, "1.74");
}

bool RcloneCapabilities::hasJsonLog() const {
  return versionAtLeast(rcloneVersion, "1.56");
}

bool RcloneCapabilities::hasBisync() const {
  return versionAtLeast(rcloneVersion, "1.58");
}

bool RcloneCapabilities::hasJobBatch() const {
  return versionAtLeast(rcloneVersion, "1.72");
}

QString RcloneCapabilities::summary() const {
  QStringList lines;
  lines << QString("Rclone Browser NG v%1").arg(RCLONE_BROWSER_VERSION);
  lines << QString("Qt %1 (built against %2)")
               .arg(qVersion(), QT_VERSION_STR);
  lines << QString("OS: %1 %2 (%3)")
               .arg(QSysInfo::productType(), QSysInfo::productVersion(),
                    QSysInfo::currentCpuArchitecture());
  lines << QString("rclone: %1").arg(
      rcloneVersion.isEmpty() ? "not detected" : "v" + rcloneVersion);
  lines << QString("rclone path: %1").arg(
      rclonePath.isEmpty() ? "(default PATH lookup)" : rclonePath);
  lines << QString("Config: %1").arg(
      configPath.isEmpty() ? "(rclone default)" : configPath);

  if (!mountBackend.isEmpty()) {
    lines << QString("Mount backend: %1").arg(mountBackend);
  }

  lines << "";
  lines << "Feature support:";
  lines << QString("  --use-json-log: %1").arg(hasJsonLog() ? "yes" : "no");
  lines << QString("  --name-transform: %1").arg(
      hasNameTransform() ? "yes" : "needs rclone >= 1.74");
  lines << QString("  --list-cutoff: %1").arg(
      hasListCutoff() ? "yes" : "needs rclone >= 1.74");
  lines << QString("  bisync: %1").arg(
      hasBisync() ? "yes" : "needs rclone >= 1.58");
  lines << QString("  job/batch RC: %1").arg(
      hasJobBatch() ? "yes" : "needs rclone >= 1.72");

  return lines.join('\n');
}

namespace Diagnostics {

namespace {
struct LogEntry {
  QString source;
  QString line;
};
QList<LogEntry> &logBuffer() {
  static QList<LogEntry> buf;
  return buf;
}
constexpr int kMaxLogEntries = 200;
} // namespace

QString redactSecrets(const QString &text) {
  QString out = text;
  static const QRegularExpression patterns[] = {
      QRegularExpression(R"((--rc-pass)\s+\S+)"),
      QRegularExpression(R"((pass|password|token|secret|key|credential)[\s=:]+\S+)",
                         QRegularExpression::CaseInsensitiveOption),
      QRegularExpression(R"((RCLONE_CONFIG_PASS)=\S+)"),
      QRegularExpression(R"((Authorization:\s*(?:Basic|Bearer))\s+\S+)",
                         QRegularExpression::CaseInsensitiveOption),
  };
  for (const auto &re : patterns) {
    out.replace(re, "\\1 <redacted>");
  }
  return out;
}

void appendLog(const QString &source, const QString &line) {
  auto &buf = logBuffer();
  if (buf.size() >= kMaxLogEntries) {
    buf.removeFirst();
  }
  buf.append({source, line});
}

QString recentLog() {
  QStringList lines;
  for (const auto &entry : logBuffer()) {
    lines << QString("[%1] %2").arg(entry.source, entry.line);
  }
  return lines.join('\n');
}

} // namespace Diagnostics

RcloneCapabilities RcloneCapabilities::detect() {
  RcloneCapabilities caps;

  auto settings = GetSettings();
  caps.rcloneVersion =
      settings->value("Settings/rcloneVersion").toString();
  caps.rclonePath = settings->value("Settings/rclone").toString();
  caps.configPath = settings->value("Settings/rcloneConf").toString();
  caps.qtVersion = QString(qVersion());

#if defined(Q_OS_WIN32)
  caps.mountBackend = "WinFsp";
#elif defined(Q_OS_MACOS)
  caps.mountBackend = "macFUSE / fuse-t / nfsmount";
#elif defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
  caps.mountBackend = "not supported";
#else
  caps.mountBackend = "FUSE";
#endif

  return caps;
}
