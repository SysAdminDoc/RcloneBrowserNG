#include "rc_sync_request.h"

namespace RcSyncRequest {

namespace {

QString endpointFor(const QString &command) {
  if (command == "copy" || command == "move" || command == "sync" ||
      command == "bisync") {
    return "sync/" + command;
  }
  return QString();
}

// Numeric option types have to be sent as JSON numbers. A string in their
// place is rejected with 400 ("cannot unmarshal string into Go struct field
// ConfigInfo.Transfers of type int"), which is loud, but there is no reason
// to send it wrong in the first place.
bool isNumericType(const QString &type) {
  return type == "int" || type == "int64" || type == "uint32" ||
         type == "uint" || type == "int32";
}

// Everything else, including Duration, SizeSuffix and BwTimetable, takes the
// CLI token verbatim. That is not a shortcut: rclone parses the JSON string
// with the same parser the command line uses, so `--min-size 50` and
// {"MinSize": "50"} both mean 50 KiB. Converting to a number here would
// change the meaning, because {"MinSize": 50} means 50 bytes.
QJsonValue encode(const OptionSpec &spec, const QString &token, bool *ok) {
  *ok = true;
  if (isNumericType(spec.type)) {
    bool parsed = false;
    const qint64 value = token.toLongLong(&parsed);
    if (!parsed) {
      *ok = false;
      return QJsonValue();
    }
    return QJsonValue(value);
  }
  return QJsonValue(token);
}

void insertValue(QJsonObject &section, const OptionSpec &spec,
                 const QJsonValue &value) {
  if (spec.type == "stringArray" || spec.type == "CommaSepList") {
    QJsonArray existing = section.value(spec.key).toArray();
    existing.append(value);
    section.insert(spec.key, existing);
    return;
  }
  section.insert(spec.key, value);
}

} // namespace

OptionIndex IndexOptions(const QJsonObject &optionsInfo) {
  OptionIndex index;
  for (const QString &section : {QStringLiteral("main"),
                                 QStringLiteral("filter")}) {
    const QJsonArray options = optionsInfo.value(section).toArray();
    for (const QJsonValue &entry : options) {
      const QJsonObject option = entry.toObject();
      const QString name = option.value("Name").toString();
      const QString fieldName = option.value("FieldName").toString();
      if (name.isEmpty() || fieldName.isEmpty()) {
        continue;
      }
      // A prefixed option belongs to a backend and cannot be set through
      // _config, which only reshapes the global structs.
      if (!option.value("NoPrefix").toBool(true)) {
        continue;
      }
      OptionSpec spec;
      spec.section = section;
      spec.key = fieldName.section(QChar('.'), -1);
      spec.type = option.value("Type").toString();
      index.insert("--" + QString(name).replace(QChar('_'), QChar('-')), spec);
    }
  }
  return index;
}

Request Build(const QStringList &args, const QString &group,
              const OptionIndex &index) {
  Request request;

  if (args.isEmpty()) {
    request.reason = QStringLiteral("no command");
    return request;
  }
  request.endpoint = endpointFor(args.first());
  if (request.endpoint.isEmpty()) {
    request.reason =
        QString("%1 is not a sync operation").arg(args.first());
    return request;
  }
  if (index.isEmpty()) {
    request.reason = QStringLiteral("no option metadata from the daemon");
    return request;
  }

  QJsonObject config;
  QJsonObject filter;
  QStringList positional;

  for (int i = 1; i < args.size(); i++) {
    QString token = args.at(i);

    if (!token.startsWith(QChar('-')) || token == "-") {
      positional.append(token);
      continue;
    }

    // Both `--transfers 4` and `--transfers=4` reach here; the second form
    // can come from the free-text extra options field.
    QString inlineValue;
    bool hasInlineValue = false;
    const int equals = token.indexOf(QChar('='));
    if (equals > 0) {
      inlineValue = token.mid(equals + 1);
      hasInlineValue = true;
      token = token.left(equals);
    }

    // --verbose and --stats shape the CLI's own console output. There is no
    // console here: the job's progress is read from core/stats, so these two
    // change nothing about what gets transferred. They are dropped rather
    // than refused because getOptions() emits both on every single job, and
    // refusing them would mean this route never ran at all.
    if (token == "--verbose" || token == "-v") {
      continue;
    }
    if (token == "--stats") {
      if (!hasInlineValue) {
        i++;
      }
      continue;
    }

    const auto found = index.constFind(token);
    if (found == index.constEnd()) {
      request.reason =
          QString("%1 is not an option this rclone exposes over the remote "
                  "control API")
              .arg(token);
      return request;
    }
    const OptionSpec &spec = found.value();
    QJsonObject &section = spec.section == "filter" ? filter : config;

    if (spec.type == "bool") {
      if (hasInlineValue) {
        insertValue(section, spec,
                    QJsonValue(inlineValue.compare("false",
                                                   Qt::CaseInsensitive) != 0));
      } else {
        insertValue(section, spec, QJsonValue(true));
      }
      continue;
    }

    QString value;
    if (hasInlineValue) {
      value = inlineValue;
    } else {
      if (i + 1 >= args.size()) {
        request.reason = QString("%1 has no value").arg(token);
        return request;
      }
      value = args.at(++i);
    }

    bool ok = false;
    const QJsonValue encoded = encode(spec, value, &ok);
    if (!ok) {
      request.reason =
          QString("%1 expects a number and got \"%2\"").arg(token, value);
      return request;
    }
    insertValue(section, spec, encoded);
  }

  if (positional.size() != 2) {
    request.reason = QString("expected a source and a destination, got %1")
                         .arg(positional.size());
    return request;
  }

  request.source = positional.at(0);
  request.dest = positional.at(1);

  QJsonObject payload;
  if (request.endpoint == "sync/bisync") {
    payload.insert("path1", request.source);
    payload.insert("path2", request.dest);
  } else {
    payload.insert("srcFs", request.source);
    payload.insert("dstFs", request.dest);
  }
  payload.insert("_async", true);
  payload.insert("_group", group);
  if (!config.isEmpty()) {
    payload.insert("_config", config);
  }
  if (!filter.isEmpty()) {
    payload.insert("_filter", filter);
  }

  request.payload = payload;
  request.usable = true;
  return request;
}

} // namespace RcSyncRequest
