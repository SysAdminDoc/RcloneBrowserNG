#include "export_list_writer.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace {

QString entryPath(const QJsonObject &entry) {
  QString path = entry.value("Path").toString();
  if (path.isEmpty()) {
    path = entry.value("Name").toString();
  }
  return path;
}

QString plainTextPath(QString path) {
  path.replace('\\', "\\\\");
  path.replace('\r', "\\r");
  path.replace('\n', "\\n");
  return path;
}

QString csvField(QString value) {
  value.replace('"', "\"\"");
  return '"' + value + '"';
}

QString formattedModTime(const QJsonObject &entry) {
  const QString raw = entry.value("ModTime").toString();
  QDateTime dt = QDateTime::fromString(raw, Qt::ISODateWithMs);
  if (!dt.isValid()) {
    dt = QDateTime::fromString(raw, Qt::ISODate);
  }
  if (dt.isValid()) {
    return dt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
  }
  if (raw.length() >= 19) {
    return QString(raw).left(19).replace('T', ' ');
  }
  return raw;
}

quint64 fileSize(const QJsonObject &entry) {
  return static_cast<quint64>(entry.value("Size").toDouble());
}

} // namespace

bool WriteExportListFromLsjson(QIODevice *device, const QByteArray &json,
                               ExportListFormat format, QString *error) {
  auto fail = [error](const QString &message) {
    if (error) {
      *error = message;
    }
    return false;
  };

  if (!device || !device->isWritable()) {
    return fail("Destination file is not writable.");
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    return fail(QString("Failed to parse rclone lsjson output at offset %1: %2")
                    .arg(parseError.offset)
                    .arg(parseError.errorString()));
  }
  if (!doc.isArray()) {
    return fail("rclone lsjson output was not a JSON array.");
  }

  QTextStream out(device);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  out.setCodec("UTF-8");
#endif

  for (const QJsonValue &value : doc.array()) {
    if (!value.isObject()) {
      return fail("rclone lsjson output contained a non-object entry.");
    }

    const QJsonObject entry = value.toObject();
    if (entry.value("IsDir").toBool()) {
      continue;
    }

    const QString path = entryPath(entry);
    if (path.isEmpty()) {
      return fail("rclone lsjson output contained a file without Path or Name.");
    }

    if (format == ExportListFormat::Text) {
      out << plainTextPath(path) << '\n';
    } else {
      out << csvField(path) << ',' << csvField(formattedModTime(entry)) << ','
          << fileSize(entry) << '\n';
    }
  }

  out.flush();
  if (out.status() != QTextStream::Ok) {
    return fail("Failed while writing the export file.");
  }

  return true;
}
