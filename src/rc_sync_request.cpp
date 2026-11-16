#include "rc_sync_request.h"
#include "rclone_number.h"

#include <limits>

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

// A number goes out the way the CLI reads the same token, which is
// fmt.Sscanln (measured on v1.76.0 with options/get): `--transfers 010` is
// 8, `0x10` is 16 and `1_0` is 10. A value the field can't hold is refused,
// so the CLI route reports it instead of the daemon.
//
// Everything else, including Duration, SizeSuffix and BwTimetable, takes the
// CLI token verbatim. That is not a shortcut: rclone parses the JSON string
// with the same parser the command line uses, so `--min-size 50` and
// {"MinSize": "50"} both mean 50 KiB. Converting to a number here would
// change the meaning, because {"MinSize": 50} means 50 bytes.
QJsonValue encode(const OptionSpec &spec, const QString &token, bool *ok) {
  *ok = true;
  if (isNumericType(spec.type)) {
    qint64 value = 0;
    bool fits = RcloneNumber::ScanInt(token, &value);
    if (spec.type == "int32") {
      fits = fits && value >= std::numeric_limits<qint32>::min() &&
             value <= std::numeric_limits<qint32>::max();
    } else if (spec.type == "uint32") {
      fits = fits && value >= 0 &&
             value <= std::numeric_limits<quint32>::max();
    } else if (spec.type == "uint") {
      fits = fits && value >= 0;
    }
    if (!fits) {
      *ok = false;
      return QJsonValue();
    }
    return QJsonValue(value);
  }
  return QJsonValue(token);
}

// rclone has one token bucket per process, and core/bwlimit is the only
// thing that moves it on a running daemon: a _config BwLimit is accepted
// and ignored (measured on v1.76.0, a 1 KiB/s limit copied at full speed).
// A job with its own limit runs as a separate rclone instead, which keeps
// the limit. --bwlimit-file isn't one of these: each transfer makes its own
// bucket from the job's config, so a _config BwLimitFile holds (v1.76.0,
// 3 MiB at 1M took 3.0 s, where BwLimit 1M took 0.1 s).
QString sharedLimitReason(const QString &flag) {
  if (flag == "--bwlimit" || flag == "--tpslimit" ||
      flag == "--tpslimit-burst") {
    return QString("%1 is one limit shared by every job on the daemon")
        .arg(flag);
  }
  return QString();
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

// rclone reads a bool flag's value with Go's strconv.ParseBool, so
// --checksum=0 turns it off and --checksum=yes stops rclone.
bool parseGoBool(const QString &text, bool *value) {
  static const QStringList kTrue{"1", "t", "T", "TRUE", "true", "True"};
  static const QStringList kFalse{"0", "f", "F", "FALSE", "false", "False"};
  *value = kTrue.contains(text);
  return *value || kFalse.contains(text);
}

// _config takes ConfigInfo.DeleteMode as fs.DeleteMode's number (measured
// on v1.76.0: 1 deletes before copying, 2 during, 3 after, which is also
// the default; the name as a string is refused with a 400).
int deleteModeFor(const QString &flag) {
  if (flag == "--delete-before") {
    return 1;
  }
  if (flag == "--delete-during") {
    return 2;
  }
  if (flag == "--delete-after") {
    return 3;
  }
  return 0;
}

} // namespace

OptionIndex IndexOptions(const QJsonObject &optionsInfo) {
  OptionIndex index;
  for (const QString &section : {QStringLiteral("main"),
                                 QStringLiteral("filter")}) {
    const QJsonArray options = optionsInfo.value(section).toArray();
    for (const auto &entry : options) {
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

void AddDeleteModes(OptionIndex *index, const QJsonObject &optionsGet) {
  if (!optionsGet.value("main").toObject().value("DeleteMode").isDouble()) {
    return;
  }
  OptionSpec spec;
  spec.section = QStringLiteral("main");
  spec.key = QStringLiteral("DeleteMode");
  spec.type = QStringLiteral("DeleteMode");
  for (const char *flag : {"--delete-before", "--delete-during",
                           "--delete-after"}) {
    index->insert(QString::fromLatin1(flag), spec);
  }
}

Options MapOptions(const QStringList &tokens, const OptionIndex &index) {
  Options options;
  bool deleteFlags[4] = {false, false, false, false};

  for (int i = 0; i < tokens.size(); i++) {
    QString token = tokens.at(i);

    if (!token.startsWith(QChar('-')) || token == "-") {
      options.positional.append(token);
      continue;
    }

    // Both `--transfers 4` and `--transfers=4` reach here; the second form
    // can come from the free-text extra options field.
    QString inlineValue;
    bool hasInlineValue = false;
    const qsizetype equals = token.indexOf(QChar('='));
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

    const QString limitReason = sharedLimitReason(token);
    if (!limitReason.isEmpty()) {
      options.reason = limitReason;
      return options;
    }

    // The retry loop belongs to the CLI (cmd.Run), not to the sync calls,
    // so _config Retries does nothing for a copy, move or sync over rc. The
    // job card runs the loop instead, and these two say how. Only bisync
    // reads them from _config (see Build).
    if (token == "--retries" || token == "--retries-sleep") {
      QString value;
      if (hasInlineValue) {
        value = inlineValue;
      } else if (i + 1 < tokens.size()) {
        value = tokens.at(++i);
      } else {
        options.reason = QString("%1 has no value").arg(token);
        return options;
      }
      if (token == "--retries") {
        qint64 retries = 0;
        if (!RcloneNumber::ScanInt(value, &retries)) {
          options.reason =
              QString("%1 expects a number and got \"%2\"").arg(token, value);
          return options;
        }
        // rclone runs a command at least once: --retries 0 and -1 both
        // come out as 1 (options/get on v1.76.0).
        options.retries = static_cast<int>(qBound(qint64(1), retries, qint64(1000000)));
      } else {
        qint64 nanoseconds = 0;
        if (!RcloneNumber::ParseGoDuration(value, &nanoseconds)) {
          options.reason =
              QString("%1 expects a duration such as 10s and got \"%2\"")
                  .arg(token, value);
          return options;
        }
        // time.Sleep returns at once for a negative duration.
        options.retriesSleepMs = nanoseconds <= 0 ? 0 : nanoseconds / 1000000;
      }
      const auto retrySpec = index.constFind(token);
      if (retrySpec != index.constEnd()) {
        options.retryConfig.insert(retrySpec.value().key,
                                   token == "--retries"
                                       ? QJsonValue(options.retries)
                                       : QJsonValue(value));
      }
      continue;
    }

    const auto found = index.constFind(token);
    if (found == index.constEnd()) {
      options.reason =
          QString("%1 is not an option this rclone exposes over the remote "
                  "control API")
              .arg(token);
      return options;
    }
    const OptionSpec &spec = found.value();

    const int deleteMode =
        spec.type == QLatin1String("DeleteMode") ? deleteModeFor(token) : 0;
    if (deleteMode != 0) {
      bool on = true;
      if (hasInlineValue && !parseGoBool(inlineValue, &on)) {
        options.reason = QString("%1 expects true or false and got \"%2\"")
                             .arg(token, inlineValue);
        return options;
      }
      // Each flag keeps the last value it was given, as on the CLI.
      deleteFlags[deleteMode] = on;
      continue;
    }
    QJsonObject &section =
        spec.section == "filter" ? options.filter : options.config;

    if (spec.type == "bool") {
      bool on = true;
      if (hasInlineValue && !parseGoBool(inlineValue, &on)) {
        options.reason = QString("%1 expects true or false and got \"%2\"")
                             .arg(token, inlineValue);
        return options;
      }
      insertValue(section, spec, QJsonValue(on));
      continue;
    }

    QString value;
    if (hasInlineValue) {
      value = inlineValue;
    } else {
      if (i + 1 >= tokens.size()) {
        options.reason = QString("%1 has no value").arg(token);
        return options;
      }
      value = tokens.at(++i);
    }

    bool ok = false;
    const QJsonValue encoded = encode(spec, value, &ok);
    if (!ok) {
      options.reason =
          QString("%1 expects a number and got \"%2\"").arg(token, value);
      return options;
    }
    insertValue(section, spec, encoded);
  }

  int chosen = 0;
  for (int mode = 1; mode <= 3; mode++) {
    if (!deleteFlags[mode]) {
      continue;
    }
    if (chosen != 0) {
      // The CLI stops with "Only one of --delete-before, --delete-during
      // or --delete-after can be used", and core/command says the same.
      options.reason = QStringLiteral(
          "more than one of --delete-before, --delete-during and "
          "--delete-after is set");
      return options;
    }
    chosen = mode;
  }
  if (chosen != 0) {
    options.config.insert("DeleteMode", chosen);
  }

  // The CLI raises these to 1 when they're 0 or less (fs.ConfigInfo's
  // Reload, fs/config.go on v1.76.0), and a _config sent over rc skips that
  // step. On the daemon --low-level-retries 0 panics it, --checkers 0 hangs
  // the run and --transfers 0 reports success with nothing copied.
  for (const char *key : {"LowLevelRetries", "Transfers", "Checkers"}) {
    const QString name = QString::fromLatin1(key);
    if (options.config.contains(name) &&
        options.config.value(name).toInteger() < 1) {
      options.config.insert(name, 1);
    }
  }

  options.ok = true;
  return options;
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

  const Options options = MapOptions(args.mid(1), index);
  if (!options.ok) {
    request.reason = options.reason;
    return request;
  }
  QJsonObject config = options.config;
  if (request.endpoint == QLatin1String("sync/bisync")) {
    // bisync --resilient retries its own copies with ci.Retries and
    // ci.RetriesInterval (cmd/bisync/queue.go, v1.76.0). Without them a
    // sync/bisync call takes the daemon's, measured 3 tries where the task
    // asked for 2. No bisync flag maps onto rc today (options/info lists
    // none), so --resilient itself still sends the run to core/command and
    // this only matters once one does.
    for (auto it = options.retryConfig.constBegin();
         it != options.retryConfig.constEnd(); ++it) {
      config.insert(it.key(), it.value());
    }
  }
  const QJsonObject &filter = options.filter;
  const QStringList &positional = options.positional;

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
  // The CLI's bisync runs through cmd.Run with Retry false, so it never
  // runs the whole bisync again whatever --retries says (cmd/bisync/cmd.go,
  // v1.76.0). Its own copy retry gets --retries through _config above.
  request.retries =
      request.endpoint == QLatin1String("sync/bisync") ? 1 : options.retries;
  request.retriesSleepMs = options.retriesSleepMs;
  request.usable = true;
  return request;
}

} // namespace RcSyncRequest
