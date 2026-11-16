#include "rc_sync_request.h"
#include "rclone_number.h"

#include <algorithm>
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

    // A _config Dump is accepted and nothing is dumped, even with LogLevel
    // DEBUG beside it (measured on v1.76.0), and the daemon's own output
    // isn't read after it starts. A separate rclone dumps.
    if (token == "--dump") {
      options.reason =
          QStringLiteral("--dump does nothing for a job the daemon runs");
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

  // fs.ConfigInfo's Reload (fs/config.go on v1.76.0) refuses these two
  // before the CLI runs anything, and a _config sent over rc skips it. The
  // daemon used the --compare-dest and ignored the --copy-dest, and failed
  // every file over a long --partial-suffix where the CLI stops at once.
  // Sent to a separate rclone, the run ends with rclone's own error.
  if (!options.config.value("CompareDest").toArray().isEmpty() &&
      !options.config.value("CopyDest").toArray().isEmpty()) {
    options.reason =
        QStringLiteral("rclone refuses --compare-dest with --copy-dest");
    return options;
  }
  // Go's len counts bytes.
  if (options.config.value("PartialSuffix").toString().toUtf8().size() > 16) {
    options.reason =
        QStringLiteral("rclone refuses a --partial-suffix over 16 bytes");
    return options;
  }

  // Reload also raises these to 1 when they're 0 or less, which a _config
  // skips too. On the daemon --low-level-retries 0 or -1 crashes it,
  // --checkers 0 hangs the run, --checkers -1 and --transfers -1 panic the
  // job, and --transfers 0 copies nothing (a source with a folder fails
  // setting the folder's time).
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
  // rclone skips a path's global.<option> for a job started over rc
  // (addConfigToContext in fs/newfs.go checks IsRCRequest), so a
  // ":local,global.backup_dir=...:" destination deleted here what the CLI
  // moves into the backup, and the deletion check had promised the backup
  // (measured on v1.76.0). The CLI in the daemon applies it. Any argument
  // can be a path, and a remote nested in another's option counts too.
  for (const QString &arg : args) {
    if (arg.contains(QLatin1String(",global."))) {
      request.reason = QStringLiteral(
          "a path sets a global. option, which rclone ignores for a job "
          "started over rc");
      return request;
    }
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
  // v1.76.0). Its --resilient copy retry would read --retries from _config
  // above, but --resilient itself sends the run to core/command today.
  request.retries =
      request.endpoint == QLatin1String("sync/bisync") ? 1 : options.retries;
  request.retriesSleepMs = options.retriesSleepMs;
  request.usable = true;
  return request;
}

QString ConfigGlobalsReason(const QJsonObject &dump,
                            const QStringList &environment) {
  // addConfigToContext (fs/newfs.go) reads global.<option> through
  // config.Get, which takes the remote's section and its RCLONE_CONFIG_
  // variables, and skips it when IsRCRequest. Measured on v1.76.0 with
  // [bk] type = alias and global.backup_dir = bk:bak: the CLI moved every
  // file into bak, and sync/sync deleted them. A value counts as well, since
  // an alias's remote = :local,global.backup_dir=...: opens with that option.
  // Any section counts, because a run can open one through another remote's
  // options or a plain path ([local]). Matching is wider than rclone's, which
  // only costs a job its live stats. A connection string ends its options
  // with a ':' ahead of the path, so a value with no ':' after ",global." is
  // a plain name (D:/photos,global.copy) and carries nothing.
  const auto carriesGlobal = [](const QString &value) {
    const qsizetype at =
        value.indexOf(QLatin1String(",global."), 0, Qt::CaseInsensitive);
    return at >= 0 && value.indexOf(QLatin1Char(':'), at) > at;
  };
  QStringList sections;
  for (auto it = dump.constBegin(); it != dump.constEnd(); ++it) {
    const QJsonObject section = it.value().toObject();
    for (auto key = section.constBegin(); key != section.constEnd(); ++key) {
      if (key.key().startsWith(QLatin1String("global."), Qt::CaseInsensitive) ||
          carriesGlobal(key.value().toString())) {
        sections << it.key();
        break;
      }
    }
  }
  if (!sections.isEmpty()) {
    return QString("the %1 %2 in rclone.conf %3 a global. option, which "
                   "rclone ignores for a job started over rc")
        .arg(sections.join(QStringLiteral(", ")),
             sections.size() == 1 ? QStringLiteral("remote")
                                  : QStringLiteral("remotes"),
             sections.size() == 1 ? QStringLiteral("sets")
                                  : QStringLiteral("set"));
  }
  // A section's own variable (ConfigToEnv in fs/config.go gives
  // RCLONE_CONFIG_BK_GLOBAL.BACKUP_DIR), or a path in any RCLONE_ variable,
  // RCLONE_BACKUP_DIR among them. Windows reads names in any case.
  for (const QString &entry : environment) {
    const qsizetype equals = entry.indexOf(QLatin1Char('='), 1);
    const QString name = equals > 0 ? entry.left(equals) : entry;
    const QString upper = name.toUpper();
    if (!upper.startsWith(QLatin1String("RCLONE_"))) {
      continue;
    }
    const bool sectionKey = upper.startsWith(QLatin1String("RCLONE_CONFIG_")) &&
                            upper.contains(QLatin1String("_GLOBAL."));
    const bool pathValue = equals > 0 && carriesGlobal(entry.mid(equals + 1));
    if (sectionKey || pathValue) {
      return QString("%1 sets a global. option, which rclone ignores for a "
                     "job started over rc")
          .arg(name);
    }
  }
  return QString();
}

bool IsDryRun(const QStringList &args) {
  bool on = false;
  for (const QString &arg : args) {
    if (arg == QLatin1String("--")) {
      break;
    }
    const qsizetype equals = arg.indexOf(QLatin1Char('='));
    const QString name = equals > 0 ? arg.left(equals) : arg;
    bool value = true;
    const bool valueOk =
        equals < 0 || parseGoBool(arg.mid(equals + 1), &value);
    if (name == QLatin1String("--dry-run")) {
      // A value rclone can't read stops the run, so it counts as on.
      on = !valueOk || value;
      continue;
    }
    if (name.size() < 2 || name.startsWith(QLatin1String("--")) ||
        !name.startsWith(QLatin1Char('-'))) {
      continue;
    }
    // A short flag cluster: an = value belongs to its last letter.
    bool letters = true;
    for (qsizetype k = 1; letters && k < name.size(); k++) {
      const char16_t c = name.at(k).unicode();
      letters = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
    }
    if (!letters || !name.contains(QLatin1Char('n'))) {
      continue;
    }
    on = !(equals > 0 && name.endsWith(QLatin1Char('n')) && valueOk &&
           !value);
  }
  return on;
}

QString DryRunGlobalReason(const QJsonObject &dump,
                           const QStringList &environment) {
  // addConfigToContext (fs/newfs.go, v1.76.0) writes a section's
  // global.dry_run into the global config when the path opens, after the
  // flags were read, and config.Get matches the key exactly. A connection
  // string in a value ("remote = :local,global.dry_run=false:/x") opens with
  // it too. A bare key in a connection string means true.
  const auto turnsItOff = [](const QString &value) {
    bool on = false;
    return !parseGoBool(value, &on) || !on;
  };
  const auto carriesOff = [&turnsItOff](const QString &value) {
    const QString needle = QStringLiteral(",global.dry_run");
    for (qsizetype at = value.indexOf(needle); at >= 0;
         at = value.indexOf(needle, at + 1)) {
      const qsizetype end = at + needle.size();
      if (end >= value.size() || value.indexOf(QLatin1Char(':'), end) < 0) {
        continue;
      }
      const QChar next = value.at(end);
      if (next != QLatin1Char('=')) {
        continue; // ',' or ':' leaves it true, anything else is another key
      }
      qsizetype start = end + 1;
      qsizetype stop = start;
      if (start < value.size() && (value.at(start) == QLatin1Char('\'') ||
                                   value.at(start) == QLatin1Char('"'))) {
        const QChar quote = value.at(start);
        start++;
        stop = value.indexOf(quote, start);
        if (stop < 0) {
          return true; // rclone refuses it, and a refusal is safe to ask for
        }
      } else {
        while (stop < value.size() && value.at(stop) != QLatin1Char(',') &&
               value.at(stop) != QLatin1Char(':')) {
          stop++;
        }
      }
      if (turnsItOff(value.mid(start, stop - start))) {
        return true;
      }
    }
    return false;
  };
  for (auto it = dump.constBegin(); it != dump.constEnd(); ++it) {
    // Keys above the first section land in DEFAULT, which no remote reads.
    if (it.key() == QLatin1String("DEFAULT")) {
      continue;
    }
    const QJsonObject section = it.value().toObject();
    for (auto key = section.constBegin(); key != section.constEnd(); ++key) {
      const QString value = key.value().toString();
      if ((key.key() == QLatin1String("global.dry_run") &&
           turnsItOff(value)) ||
          carriesOff(value)) {
        return QString("the %1 remote's global.dry_run in rclone.conf")
            .arg(it.key());
      }
    }
  }
  // ConfigToEnv (fs/config.go) gives RCLONE_CONFIG_BK_GLOBAL.DRY_RUN for
  // [bk], and Windows reads names in any case. A path in any RCLONE_
  // variable opens with its options too.
  for (const QString &entry : environment) {
    const qsizetype equals = entry.indexOf(QLatin1Char('='), 1);
    if (equals < 0) {
      continue;
    }
    const QString name = entry.left(equals);
    const QString upper = name.toUpper();
    const QString value = entry.mid(equals + 1);
    if (!upper.startsWith(QLatin1String("RCLONE_"))) {
      continue;
    }
    if ((upper.startsWith(QLatin1String("RCLONE_CONFIG_")) &&
         upper.endsWith(QLatin1String("_GLOBAL.DRY_RUN")) &&
         turnsItOff(value)) ||
        carriesOff(value)) {
      return name;
    }
  }
  return QString();
}

QStringList ConfigDumpArguments(const QStringList &args) {
  // A check never waits on a password prompt.
  QStringList dump{QStringLiteral("config"), QStringLiteral("dump"),
                   QStringLiteral("--ask-password=false")};
  for (qsizetype i = 0; i < args.size(); i++) {
    const QString &arg = args.at(i);
    if (arg == QLatin1String("--")) {
      break;
    }
    if (arg == QLatin1String("--config") ||
        arg == QLatin1String("--password-command")) {
      dump << arg;
      if (i + 1 < args.size()) {
        dump << args.at(i + 1);
        i++;
      }
      continue;
    }
    if (arg.startsWith(QLatin1String("--config=")) ||
        arg.startsWith(QLatin1String("--password-command="))) {
      dump << arg;
    }
  }
  return dump;
}

bool ParseConfigDump(const QByteArray &output, QJsonObject *dump) {
  QJsonParseError error;
  QJsonDocument document = QJsonDocument::fromJson(output, &error);
  if (error.error != QJsonParseError::NoError) {
    // A notice like "Config file not found" comes before the object, which
    // rclone prints starting at the beginning of a line.
    const qsizetype open =
        output.startsWith('{') ? 0 : output.indexOf("\n{");
    const qsizetype close = output.lastIndexOf('}');
    if (open < 0 || close < open) {
      return false;
    }
    document = QJsonDocument::fromJson(
        output.mid(open, close - open + 1).trimmed(), &error);
    if (error.error != QJsonParseError::NoError) {
      return false;
    }
  }
  if (!document.isObject()) {
    return false;
  }
  if (dump) {
    *dump = document.object();
  }
  return true;
}

namespace {
bool isDriveLetter(QChar c) {
#if defined(Q_OS_WIN)
  return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
         (c >= QLatin1Char('A') && c <= QLatin1Char('Z'));
#else
  Q_UNUSED(c);
  return false;
#endif
}
} // namespace

// The index of the ':' that ends a path's remote part, found the way rclone's
// fs/fspath.Parse finds it, or -1 for a local path. An on-the-fly remote
// (":local:C:/x") starts with a colon, and a quoted option value
// (":local,description='a:b':dir") can hold one. On Windows a name that is
// one ASCII letter is a drive (fs/driveletter, v1.76.0). Anywhere else, and
// for any other one-character name, it's a remote.
qsizetype RemoteColon(const QString &path) {
  enum class State { Name, Param, Value, Quoted, AfterQuote };
  State state = State::Name;
  qsizetype prev = 0;
  QChar quote;
  for (qsizetype i = 0; i < path.size(); i++) {
    const QChar c = path.at(i);
    switch (state) {
    case State::Name:
      if (i == 0 && c == QLatin1Char(':')) {
        break;
      }
      if (c == QLatin1Char('/') || c == QLatin1Char('\\')) {
        return -1;
      }
      if (c == QLatin1Char(':')) {
        return i == 1 && isDriveLetter(path.at(0)) ? -1 : i;
      }
      if (c == QLatin1Char(',')) {
        prev = i + 1;
        state = State::Param;
      }
      break;
    case State::Param:
      if (c == QLatin1Char(':')) {
        return i;
      }
      if (c == QLatin1Char('=')) {
        prev = i + 1;
        state = State::Value;
      }
      break;
    case State::Value:
      if ((c == QLatin1Char('\'') || c == QLatin1Char('"')) && i == prev) {
        quote = c;
        state = State::Quoted;
      } else if (c == QLatin1Char(':')) {
        return i;
      } else if (c == QLatin1Char(',')) {
        state = State::Param;
      }
      break;
    case State::Quoted:
      if (c == quote) {
        state = State::AfterQuote;
      }
      break;
    case State::AfterQuote:
      if (c == QLatin1Char(':')) {
        return i;
      }
      if (c == QLatin1Char(',')) {
        state = State::Param;
      } else if (c == quote) {
        state = State::Quoted; // a doubled quote
      } else {
        return -1;
      }
      break;
    }
  }
  return -1;
}

StatTarget StatTargetFor(const QString &path) {
  const qsizetype colon = RemoteColon(path);
  const QString prefix = colon >= 0 ? path.left(colon + 1) : QString();
  const QString rest = path.mid(colon + 1);
  // A local path splits at either separator, since Windows takes both and
  // a remote daemon's path can be a Windows one. On any other backend a
  // backslash is part of a name.
  const bool local = colon < 0 ||
                     prefix.startsWith(QLatin1String(":local:")) ||
                     prefix.startsWith(QLatin1String(":local,"));
  qsizetype separator = rest.lastIndexOf(QChar('/'));
  if (local) {
    separator = (std::max)(separator, rest.lastIndexOf(QChar('\\')));
  }
  StatTarget target;
  if (separator < 0) {
    target.fs = prefix;
    target.remote = rest;
    return target;
  }
  QString folder = rest.left(separator);
  // "C:" alone is the current folder on that drive and an empty folder is
  // the current one, so a root keeps its separator.
  if (local && (folder.isEmpty() || folder.endsWith(QChar(':')))) {
    folder = rest.left(separator + 1);
  }
  target.fs = prefix + folder;
  target.remote = rest.mid(separator + 1);
  return target;
}

QString DaemonConflictReason(const QJsonObject &config,
                             const QJsonObject &daemonMain) {
  // rc.AddConfig (fs/rc/context.go, v1.76.0) copies the daemon's config and
  // parses _config over it, so a key _config leaves out keeps the daemon's
  // value. options/get gives an unset list as null.
  const auto used = [&](const QString &key) {
    const QJsonValue value =
        config.contains(key) ? config.value(key) : daemonMain.value(key);
    return !value.toArray().isEmpty();
  };
  if (used(QStringLiteral("CompareDest")) && used(QStringLiteral("CopyDest"))) {
    return QStringLiteral(
        "rclone refuses --compare-dest with --copy-dest, and the daemon "
        "already has one of them set");
  }
  return QString();
}

} // namespace RcSyncRequest
