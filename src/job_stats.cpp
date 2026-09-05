#include "job_stats.h"

namespace JobStats {

LogLine ParseLogLine(const QByteArray &raw) {
  LogLine line;
  const QJsonDocument document = QJsonDocument::fromJson(raw);
  if (!document.isObject()) {
    return line;
  }
  line.isJson = true;

  const QJsonObject object = document.object();
  line.message = object.value("msg").toString();
  line.level = object.value("level").toString();
  line.object = object.value("object").toString();
  line.time = object.value("time").toString();

  if (!object.contains("stats")) {
    return line;
  }

  const QJsonObject stats = object.value("stats").toObject();
  line.stats.present = true;
  line.stats.bytes = stats.value("bytes").toDouble();
  line.stats.totalBytes = stats.value("totalBytes").toDouble();
  line.stats.speed = stats.value("speed").toDouble();
  line.stats.eta = stats.value("eta").toDouble();
  line.stats.elapsedTime = stats.value("elapsedTime").toDouble();
  line.stats.errors = stats.value("errors").toInt();
  line.stats.checks = stats.value("checks").toInt();
  line.stats.totalChecks = stats.value("totalChecks").toInt();
  line.stats.transfers = stats.value("transfers").toInt();
  line.stats.totalTransfers = stats.value("totalTransfers").toInt();

  const QJsonArray transferring = stats.value("transferring").toArray();
  line.stats.transferring.reserve(transferring.size());
  for (const QJsonValue &value : transferring) {
    const QJsonObject entry = value.toObject();
    TransferringFile file;
    file.name = entry.value("name").toString();
    if (file.name.isEmpty()) {
      continue;
    }
    file.percentage = entry.value("percentage").toInt();
    file.speed = entry.value("speed").toDouble();
    file.eta = entry.value("eta").toDouble();
    line.stats.transferring.append(file);
  }

  return line;
}

QString FormatDuration(double seconds) {
  if (seconds <= 0) {
    return QString();
  }
  const int total = static_cast<int>(seconds);
  const int hours = total / 3600;
  const int minutes = (total % 3600) / 60;
  const int secs = total % 60;
  if (hours > 0) {
    return QString("%1h%2m%3s").arg(hours).arg(minutes).arg(secs);
  }
  if (minutes > 0) {
    return QString("%1m%2s").arg(minutes).arg(secs);
  }
  return QString("%1s").arg(secs);
}

int PercentComplete(double bytes, double totalBytes) {
  if (totalBytes <= 0) {
    return 0;
  }
  const int percent = static_cast<int>(bytes / totalBytes * 100);
  return qBound(0, percent, 100);
}

QString FormatCount(int done, int total) {
  if (total > 0) {
    return QString("%1 / %2").arg(done).arg(total);
  }
  if (done > 0) {
    return QString::number(done);
  }
  return QString();
}

QString ElideTransferName(const QString &name) {
  if (name.length() <= 47) {
    return name;
  }
  return name.left(25) + "..." + name.right(19);
}

} // namespace JobStats
