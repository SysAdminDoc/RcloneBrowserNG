#include "remote_list_parser.h"

namespace RemoteListParser {

QVector<Remote> ParseJson(const QByteArray &output, QString *error) {
  if (error) {
    error->clear();
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    if (error) {
      *error = parseError.errorString();
    }
    return {};
  }
  if (!document.isArray()) {
    if (error) {
      *error = "listremotes --json did not return an array";
    }
    return {};
  }

  QVector<Remote> remotes;
  const QJsonArray array = document.array();
  remotes.reserve(array.size());
  for (const QJsonValue &value : array) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject object = value.toObject();
    Remote remote;
    remote.name = object.value("name").toString();
    if (remote.name.isEmpty()) {
      continue;
    }
    remote.type = object.value("type").toString();
    remote.description = object.value("description").toString();
    remote.source = object.value("source").toString();
    remotes.append(remote);
  }
  return remotes;
}

QVector<Remote> ParseLong(const QByteArray &output) {
  QVector<Remote> remotes;
  const QStringList lines =
      QString::fromUtf8(output).split(QChar(0x0a), Qt::SkipEmptyParts);
  for (const QString &rawLine : lines) {
    const QString line = rawLine.trimmed();
    if (line.isEmpty()) {
      continue;
    }
    // Remote names cannot contain a colon, so the first one always ends the
    // name. Everything after it is the padded type and description.
    const int colon = line.indexOf(QChar(':'));
    if (colon <= 0) {
      continue;
    }

    Remote remote;
    remote.name = line.left(colon).trimmed();
    if (remote.name.isEmpty()) {
      continue;
    }

    const QString rest = line.mid(colon + 1).trimmed();
    if (!rest.isEmpty()) {
      const int space = rest.indexOf(QRegularExpression("\\s"));
      if (space < 0) {
        remote.type = rest;
      } else {
        remote.type = rest.left(space);
        remote.description = rest.mid(space + 1).trimmed();
      }
    }
    remotes.append(remote);
  }
  return remotes;
}

QString TooltipFor(const Remote &remote) {
  const QString type = remote.type.isEmpty() ? QStringLiteral("unknown type")
                                             : remote.type;
  if (remote.description.trimmed().isEmpty()) {
    return type;
  }
  return QString("%1\n%2").arg(type, remote.description.trimmed());
}

} // namespace RemoteListParser
