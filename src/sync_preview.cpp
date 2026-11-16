#include "sync_preview.h"

namespace SyncPreview {

namespace {

// A replaced file's old copy is logged under its name at the destination,
// and the copy that replaces it under the source's name. The two can differ
// in case on a case-insensitive remote (Windows), in Unicode normalization,
// or through --name-transform (measured on v1.76.0: A.txt copied over a.txt,
// and a.txt copied as A.TXT with all,uppercase), so they're paired the way
// rclone's march pairs names: NFC, then Go's strings.ToLower, which lowers
// each character on its own (I with a dot above becomes a plain i). Unicode
// case folding leaves that one alone, and so does QChar::toLower, which
// skips a character whose full lowercase is longer than one character. In
// Unicode only U+0130 has one.
QString PairKey(const QString &path) {
  const QList<uint> points =
      path.normalized(QString::NormalizationForm_C).toUcs4();
  std::u32string lowered;
  lowered.reserve(static_cast<size_t>(points.size()));
  for (const uint point : points) {
    lowered.push_back(point == 0x130
                          ? U'i'
                          : QChar::toLower(static_cast<char32_t>(point)));
  }
  return QString::fromStdU32String(lowered);
}

// With --suffix the old copy moves to its own name plus the suffix, or with
// --suffix-keep-extension to the suffix before the extensions ("a.tar.gz"
// to "a-old.tar.gz", measured).
bool LooksLikeBackupName(const QString &object, const QString &target) {
  const QString from = PairKey(object);
  const QString to = PairKey(target);
  if (to.size() <= from.size()) {
    return false;
  }
  const qsizetype name = from.lastIndexOf(QChar('/')) + 1;
  qsizetype split = from.size();
  while (split >= name) {
    if (to.startsWith(QStringView(from).left(split)) &&
        to.endsWith(QStringView(from).mid(split))) {
      return true;
    }
    if (split == 0) {
      break;
    }
    split = from.lastIndexOf(QChar('.'), split - 1);
  }
  return false;
}

// The values Go's strconv.ParseBool reads as false, which pflag uses for a
// bool flag's =value.
bool IsGoFalse(QStringView value) {
  return value == u"0" || value == u"f" || value == u"F" ||
         value == u"false" || value == u"FALSE" || value == u"False";
}

// The options EffectiveArguments carries over, by flag, with the name a
// connection string's global. option uses for it. Bisync's own flags and
// the filter's aren't in rclone's main config, which is all global. sets.
struct ReadOption {
  const char *flag;
  const char *global;
  bool list; // a stringArray, whose environment value is a CSV list
};
const ReadOption kReadOptions[] = {
    {"backup-dir", "backup_dir", false},
    {"backup-dir1", nullptr, false},
    {"backup-dir2", nullptr, false},
    {"suffix", "suffix", false},
    {"track-renames", "track_renames", false},
    {"fix-case", "fix_case", false},
    {"delete-excluded", nullptr, false},
    {"max-delete", "max_delete", false},
    {"max-delete-size", "max_delete_size", false},
    {"files-from", nullptr, true},
    {"files-from-raw", nullptr, true},
    {"files-from0", nullptr, true},
};

bool IsConfigParam(QChar c) {
  const char16_t u = c.unicode();
  return (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') ||
         (u >= u'0' && u <= u'9') || u == u'_' || u == u'.';
}

// The key=value options in a connection string, read the way rclone's
// fs/fspath.Parse reads them: "remote,a=1,b:path" or ":backend,a='x:y':path",
// where a key with no value is "true" and a doubled quote inside quotes is
// one quote. Empty for a local path, a plain "remote:path" and anything
// rclone refuses.
QList<QPair<QString, QString>> ConnectionOptions(const QString &path) {
  using Options = QList<QPair<QString, QString>>;
  Options options;
  if (!path.contains(QLatin1Char(':'))) {
    return options;
  }
  enum class State { Name, Param, Value, Quoted, AfterQuote };
  State state = State::Name;
  qsizetype prev = 0;
  QString param;
  QChar quote;
  bool doubled = false;
  for (qsizetype i = 0; i < path.size(); i++) {
    const QChar c = path.at(i);
    switch (state) {
    case State::Name:
      if (i == 0 && c == QLatin1Char(':')) {
        break; // an on-the-fly backend
      }
      if (c == QLatin1Char('/') || c == QLatin1Char('\\') ||
          c == QLatin1Char(':')) {
        return Options(); // a local path, or a remote with no options
      }
      if (c == QLatin1Char(',')) {
        if (i == 0 || (i == 1 && path.at(0) == QLatin1Char(':'))) {
          return Options(); // no name
        }
        prev = i + 1;
        state = State::Param;
      }
      break;
    case State::Param:
      if (c == QLatin1Char(':') || c == QLatin1Char(',') ||
          c == QLatin1Char('=')) {
        param = path.mid(prev, i - prev);
        if (param.isEmpty()) {
          return Options();
        }
        prev = i + 1;
        if (c == QLatin1Char('=')) {
          state = State::Value;
        } else {
          options.append({param, QStringLiteral("true")});
          if (c == QLatin1Char(':')) {
            return options;
          }
        }
      } else if (!IsConfigParam(c)) {
        return Options();
      }
      break;
    case State::Value:
      if ((c == QLatin1Char('\'') || c == QLatin1Char('"')) && i == prev) {
        quote = c;
        state = State::Quoted;
        prev = i + 1;
        doubled = false;
      } else if (c == QLatin1Char(':') || c == QLatin1Char(',')) {
        options.append({param, path.mid(prev, i - prev)});
        prev = i + 1;
        if (c == QLatin1Char(':')) {
          return options;
        }
        state = State::Param;
      }
      break;
    case State::Quoted:
      if (c == quote) {
        state = State::AfterQuote;
      }
      break;
    case State::AfterQuote:
      if (c == QLatin1Char(':') || c == QLatin1Char(',')) {
        QString value = path.mid(prev, i - 1 - prev);
        if (doubled) {
          value.replace(QString(2, quote), QString(quote));
        }
        options.append({param, value});
        prev = i + 1;
        if (c == QLatin1Char(':')) {
          return options;
        }
        state = State::Param;
      } else if (c == quote) {
        state = State::Quoted; // a doubled quote
        doubled = true;
      } else {
        return Options();
      }
      break;
    }
  }
  return Options(); // it ended before its path
}

} // namespace

QString CommandName(Command command) {
  switch (command) {
  case Command::Copy:
    return QStringLiteral("Copy");
  case Command::Move:
    return QStringLiteral("Move");
  case Command::Bisync:
    return QStringLiteral("Bisync");
  case Command::Sync:
    break;
  }
  return QStringLiteral("Sync");
}

RunFlags FlagsIn(const QStringList &arguments) {
  static const QStringList kPatternFlags{QStringLiteral("--exclude"),
                                         QStringLiteral("--include"),
                                         QStringLiteral("--filter")};
  static const QStringList kStringFlags{
      QStringLiteral("--backup-dir"), QStringLiteral("--backup-dir1"),
      QStringLiteral("--backup-dir2"), QStringLiteral("--suffix")};
  static const QStringList kBoolFlags{QStringLiteral("--track-renames"),
                                      QStringLiteral("--fix-case"),
                                      QStringLiteral("--delete-excluded")};
  // The last value of each, by its place in the lists above.
  bool strings[4] = {false, false, false, false};
  bool bools[3] = {false, false, false};
  for (qsizetype i = 0; i < arguments.size(); i++) {
    const QString &arg = arguments.at(i);
    if (kPatternFlags.contains(arg)) {
      i++; // its pattern, which can look like a flag
      continue;
    }
    const qsizetype equals = arg.indexOf(QLatin1Char('='));
    const QString name = equals > 0 ? arg.left(equals) : arg;
    const qsizetype stringAt = kStringFlags.indexOf(name);
    const qsizetype boolAt = kBoolFlags.indexOf(name);
    if (stringAt >= 0) {
      // A string flag takes the next argument when it has no =value, even
      // one that starts with --.
      QString value;
      if (equals > 0) {
        value = arg.mid(equals + 1);
      } else if (i + 1 < arguments.size()) {
        value = arguments.at(++i);
      }
      strings[stringAt] = !value.isEmpty();
    } else if (boolAt >= 0) {
      bools[boolAt] =
          equals < 0 || !IsGoFalse(QStringView(arg).mid(equals + 1));
    }
  }
  RunFlags flags;
  flags.backup = strings[0] || strings[1] || strings[2] || strings[3];
  flags.renames = bools[0] || bools[1];
  flags.deleteExcluded = bools[2];
  return flags;
}

QStringList EffectiveArguments(const QStringList &arguments,
                               const QProcessEnvironment &environment) {
  QStringList defaults;
  for (const ReadOption &option : kReadOptions) {
    const QString flag = QString::fromLatin1(option.flag);
    const QString key =
        "RCLONE_" + flag.toUpper().replace(QLatin1Char('-'), QLatin1Char('_'));
    if (!environment.contains(key)) {
      continue;
    }
    const QString value = environment.value(key);
    if (option.list && value.isEmpty()) {
      continue; // an empty list
    }
    defaults << "--" + flag + "=" + value;
  }

  QStringList globals;
  for (const QString &arg : arguments) {
    // A path, or the value of a flag that names one (--backup-dir=remote,
    // global.suffix=.old:bak).
    QString path = arg;
    if (arg.startsWith(QLatin1String("--"))) {
      const qsizetype equals = arg.indexOf(QLatin1Char('='));
      if (equals < 0) {
        continue;
      }
      path = arg.mid(equals + 1);
    } else if (arg.startsWith(QLatin1Char('-'))) {
      continue;
    }
    const QList<QPair<QString, QString>> options = ConnectionOptions(path);
    for (const QPair<QString, QString> &option : options) {
      if (!option.first.startsWith(QLatin1String("global."))) {
        continue;
      }
      const QString name = option.first.mid(7);
      for (const ReadOption &known : kReadOptions) {
        if (known.global != nullptr && name == QLatin1String(known.global)) {
          globals << "--" + QString::fromLatin1(known.flag) + "=" +
                         option.second;
        }
      }
    }
  }
  return defaults + arguments + globals;
}

QStringList DryRunExtras(Command command,
                         const QStringList &effectiveArguments) {
  if (command != Command::Move) {
    return {};
  }
  for (const QString &arg : effectiveArguments) {
    const QString name = arg.section(QLatin1Char('='), 0, 0);
    if (name == QLatin1String("--files-from") ||
        name == QLatin1String("--files-from-raw") ||
        name == QLatin1String("--files-from0")) {
      return {};
    }
  }
  // Rules from --filter come before the ones from --filter-from, and an
  // --exclude next to an --include makes rclone log an error, so it's a
  // --filter rule. No real file has this name.
  return {QStringLiteral("--filter"),
          QStringLiteral("- /.rclone-browser-ng-dry-run-matches-nothing")};
}

QProcessEnvironment DryRunEnvironment(QProcessEnvironment environment,
                                      Command command) {
  if (command != Command::Bisync) {
    environment.remove(QStringLiteral("RCLONE_MAX_DELETE"));
    environment.remove(QStringLiteral("RCLONE_MAX_DELETE_SIZE"));
  }
  return environment;
}

Summary Parse(const QByteArray &jsonLog, int maxDeletionsListed,
              Command command, RunFlags flags) {
  Summary summary;
  summary.command = command;
  summary.flags = flags;
  // A replaced file's old copy going into the backup is a "move" (or "move
  // to a.txt.bak" with --suffix) next to the "copy" that replaces it. A
  // --track-renames or --fix-case rename is a "move to <new name>" with no
  // copy, and under `rclone move` the transfer itself is a move, logged
  // after the old copy's when there is one (all measured on v1.76.0).
  struct LoggedMove {
    QString object;
    QString target; // empty for a plain "move"
    qint64 size = 0;
  };
  QList<LoggedMove> moves;
  QSet<QString> copied;

  const QList<QByteArray> lines = jsonLog.split('\n');
  for (const QByteArray &rawLine : lines) {
    const QByteArray line = rawLine.trimmed();
    if (line.isEmpty()) {
      continue;
    }

    const QJsonDocument document = QJsonDocument::fromJson(line);
    if (!document.isObject()) {
      continue; // plain-text noise from an older rclone
    }
    const QJsonObject object = document.object();

    if (object.value("level").toString() == QLatin1String("error")) {
      const QString message = object.value("msg").toString().trimmed();
      if (!message.isEmpty() && summary.error.isEmpty()) {
        summary.error = message;
      }
      continue;
    }

    const QString skipped = object.value("skipped").toString();
    if (skipped.isEmpty()) {
      continue;
    }

    const qint64 size = static_cast<qint64>(object.value("size").toDouble());
    const QString path = object.value("object").toString();

    const bool toBackup = skipped == QLatin1String("move into backup dir");
    if (skipped == QLatin1String("delete") || toBackup) {
      summary.toDelete++;
      if (toBackup) {
        summary.toBackup++;
      }
      if (size > 0) {
        summary.deleteBytes += size;
      }
      if (summary.deletions.size() < maxDeletionsListed) {
        summary.deletions << path;
      } else {
        summary.moreDeletions++;
      }
    } else if (skipped == QLatin1String("move")) {
      moves.append({path, QString(), size});
    } else if (skipped.startsWith(QLatin1String("move to "))) {
      moves.append({path, skipped.mid(8), size});
    } else if (skipped == QLatin1String("copy")) {
      copied.insert(PairKey(path));
      summary.toTransfer++;
      if (size > 0) {
        summary.transferBytes += size;
      }
    } else if (skipped == QLatin1String("update modification time")) {
      // Not "update directory metadata", which -M logs for a folder.
      summary.toUpdate++;
    }
  }

  // --fix-case renames a destination file whose name differs from the
  // source's only in case or Unicode form "to" the source's name. When the
  // file is replaced too, the real run then moves the renamed file's old
  // copy into the backup, which the dry run doesn't log: its rename hands
  // the replace no file to back up (measured on v1.76.0).
  QList<LoggedMove> others;
  QSet<QString> caseRenamed;
  for (const LoggedMove &move : std::as_const(moves)) {
    if (flags.renames && !move.target.isEmpty() &&
        move.target != move.object &&
        PairKey(move.target) == PairKey(move.object)) {
      summary.renamed++;
      caseRenamed.insert(PairKey(move.object));
    } else {
      others.append(move);
    }
  }

  QSet<QString> transferred;
  if (command == Command::Move) {
    // One move per file is the transfer, the last one logged for it. Any
    // other is its old copy going into the backup.
    QHash<QString, qint64> lastSize;
    QHash<QString, int> count;
    for (const LoggedMove &move : std::as_const(others)) {
      const QString key = PairKey(move.object);
      lastSize.insert(key, move.size);
      count[key]++;
    }
    for (auto it = count.constBegin(); it != count.constEnd(); ++it) {
      transferred.insert(it.key());
      summary.moved++;
      summary.replacedIntoBackup += it.value() - 1;
      summary.toTransfer++;
      const qint64 size = lastSize.value(it.key());
      if (size > 0) {
        summary.transferBytes += size;
      }
    }
  } else {
    transferred = copied;
    for (const LoggedMove &move : std::as_const(others)) {
      // A rename is always "move to" another name, so a plain move without
      // a copy found for it is still an old copy going into the backup.
      // Without --track-renames or --fix-case nothing renames, so a "move
      // to" a name --name-transform made is one too.
      if (!flags.renames || copied.contains(PairKey(move.object)) ||
          move.target.isEmpty() ||
          LooksLikeBackupName(move.object, move.target)) {
        summary.replacedIntoBackup++;
      } else {
        summary.renamed++;
      }
    }
  }

  if (flags.backup) {
    for (const QString &key : std::as_const(caseRenamed)) {
      if (transferred.contains(key)) {
        summary.replacedIntoBackup++;
      }
    }
  }
  return summary;
}

QString Headline(const Summary &summary) {
  QStringList parts;
  if (summary.toTransfer > 0) {
    parts << QString("%1 to transfer").arg(summary.toTransfer);
  }
  if (summary.toUpdate > 0) {
    parts << QString("%1 timestamp update%2")
                 .arg(summary.toUpdate)
                 .arg(summary.toUpdate == 1 ? "" : "s");
  }
  if (summary.renamed > 0) {
    parts << QString("%1 to rename").arg(summary.renamed);
  }
  const int deleted = summary.toDelete - summary.toBackup;
  if (deleted > 0) {
    parts << QString("%1 to delete").arg(deleted);
  }
  if (summary.toBackup > 0) {
    parts << QString("%1 to move into the backup").arg(summary.toBackup);
  }
  if (parts.isEmpty()) {
    return QStringLiteral("Nothing to do: the destination already matches.");
  }
  return parts.join(", ") + ".";
}

namespace {

QString Files(int count) {
  return QString("%1 file%2").arg(count).arg(count == 1 ? "" : "s");
}

QString Removing(const Summary &summary) {
  if (summary.toBackup == 0) {
    return QStringLiteral("deleting");
  }
  return summary.toDelete > summary.toBackup
             ? QStringLiteral("deleting or moving")
             : QStringLiteral("moving");
}

// "a", "a and b", "a, b and c".
QString JoinWithAnd(const QStringList &parts) {
  if (parts.size() < 2) {
    return parts.join(QString());
  }
  return parts.mid(0, parts.size() - 1).join(", ") + " and " + parts.last();
}

// With a backup, a timestamp update can be an upload: on a remote that
// can't set a file's time in place, rclone uploads the file again and its
// old copy goes into the backup, which counts like a replaced file's
// (measured on v1.76.0 against rclone's own WebDAV server). The dry run
// logs only the update.
int Reuploads(const Summary &summary) {
  return summary.flags.backup ? summary.toUpdate : 0;
}

} // namespace

QString DeletionTitle(const Summary &summary) {
  const QString run = CommandName(summary.command);
  const int moved = summary.toBackup;
  const int deleted = summary.toDelete - moved;
  if (summary.toDelete == 0) {
    QStringList parts;
    if (summary.moved > 0) {
      parts << "move " + Files(summary.moved);
    }
    if (summary.renamed > 0) {
      parts << "rename " + Files(summary.renamed);
    }
    if (summary.replacedIntoBackup > 0) {
      parts << "move old copies of " + Files(summary.replacedIntoBackup) +
                   " into the backup";
    }
    if (Reuploads(summary) > 0) {
      parts << "update the time on " + Files(Reuploads(summary));
    }
    if (parts.isEmpty()) {
      return run + " could stop at a delete cap";
    }
    return run + " will " + JoinWithAnd(parts);
  }
  if (moved == 0) {
    return run + " will delete " + Files(deleted);
  }
  if (deleted == 0) {
    return run + " will move " + Files(moved) + " into the backup";
  }
  return QString("%1 will delete %2 and move %3 into the backup")
      .arg(run, Files(deleted))
      .arg(moved);
}

QString NoDeleteDetail(const Summary &summary) {
  QStringList lines;
  lines << "Nothing is removed from the destination, but rclone can count "
           "parts of this " +
               CommandName(summary.command).toLower() +
               " against the delete cap.";
  if (summary.replacedIntoBackup > 0) {
    lines << "Each file it replaces has its old copy moved into the backup "
             "(--backup-dir or --suffix).";
  }
  if (summary.renamed > 0) {
    lines << "A rename at the destination counts on a remote that can't "
             "move files.";
  }
  if (summary.moved > 0) {
    lines << "A moved file counts when it replaces one at the destination, "
             "or when rclone has to copy it and delete the original, as it "
             "does between two different remotes.";
  }
  if (Reuploads(summary) > 0) {
    lines << "A remote that can't change a file's time uploads the file "
             "again, and its old copy moves into the backup.";
  }
  return lines.join(' ');
}

QString RemovalDetail(const Summary &summary, const QString &size) {
  const bool moves = summary.toBackup > 0;
  const bool deletes = summary.toDelete > summary.toBackup;
  // Only a sync is sure why: a bisync removes a file to match the other
  // side, or for reasons of its own (with --delete-excluded it lists files
  // that are on both sides).
  QString why;
  if (summary.command != Command::Sync) {
    why = "rclone's dry run lists these files, and this " +
          CommandName(summary.command).toLower() + " ";
  } else if (summary.flags.deleteExcluded) {
    why = "These files exist at the destination and not at the source, or "
          "a filter leaves them out and --delete-excluded is on, so Sync ";
  } else {
    why = "These files exist at the destination and not at the source, so "
          "Sync ";
  }
  if (!moves) {
    return why +
           "removes them. Recovering them depends on whether the remote "
           "keeps a trash or versions. Freed: " +
           size + ".";
  }
  if (!deletes) {
    return why +
           "moves them into the backup (--backup-dir or --suffix) instead "
           "of deleting them. rclone still counts each one as a delete for "
           "the delete cap. Size: " +
           size + ".";
  }
  return why +
         "keeps some of them in the backup (--backup-dir or --suffix) and "
         "removes the rest, and rclone counts every one as a delete for the "
         "delete cap. Size: " +
         size + ".";
}

QString RemovalListTitle(const Summary &summary) {
  const QString run = CommandName(summary.command).toLower();
  if (summary.toBackup == 0) {
    return "Files this " + run + " will delete";
  }
  if (summary.toDelete == summary.toBackup) {
    return "Files this " + run + " will move into the backup";
  }
  return "Files this " + run + " will delete or move into the backup";
}

QStringList CapWarnings(const Summary &summary, qint64 countCap,
                        qint64 bytesCap,
                        const std::function<QString(qint64)> &niceSize) {
  QStringList warnings;
  // A bisync's dry run kept both caps. rclone turns the count cap off for
  // the bisync's own deletes, since bisync reads --max-delete as a
  // percentage and checks it before changing anything. The dry run stops
  // at --max-delete-size on the same file the real run does, but it can't
  // see what moving a file into the backup deletes first (measured on
  // v1.76.0: the real run then aborts and needs --resync).
  const bool bisync = summary.command == Command::Bisync;
  if (bisync) {
    countCap = -1;
  }
  const QString run = CommandName(summary.command).toLower();
  const QString removing = Removing(summary);
  // A move can count against both caps, and the dry run logs none of it
  // (measured on v1.76.0):
  //  - before a move lands on a file that's already there (an older copy
  //    in the backup, or the file a Move replaces), rclone's
  //    operations.move deletes that file and counts it, when the remote
  //    can move files;
  //  - a remote that can copy but not move (S3, or the memory backend)
  //    copies the file and then deletes the original, which counts, while
  //    a file already there is overwritten without a count;
  //  - a move the remote refuses, like a rename onto another drive, does
  //    both: the file already there is deleted first, then the copy and
  //    delete;
  //  - a rename that only changes case or Unicode normalization goes
  //    through a temporary name, two moves, so a copy and delete each on a
  //    remote that can't move.
  //  - a --fix-case rename hands rclone no file at the new name, so it
  //    deletes nothing first.
  // So a file moved into the backup counts at most three times (once for
  // being removed), and any other move, a replaced file's old copy, a
  // rename, a Move transfer or the old copy of a file uploaded again for
  // its time, at most twice.
  const qint64 reuploads = Reuploads(summary);
  const qint64 otherMoves = static_cast<qint64>(summary.replacedIntoBackup) +
                            summary.renamed + summary.moved + reuploads;
  const bool extra = summary.toBackup > 0 || otherMoves > 0;
  const qint64 mostCounted =
      static_cast<qint64>(summary.toDelete - summary.toBackup) +
      3 * static_cast<qint64>(summary.toBackup) + 2 * otherMoves;
  QStringList could;
  if (countCap >= 0) {
    if (summary.toDelete > countCap && !extra) {
      warnings << QString("The delete cap is %1, so this %2 stops with an "
                          "error after deleting %1 of them. Raise the cap if "
                          "all of these should go.")
                      .arg(countCap)
                      .arg(run);
    } else if (summary.toDelete > countCap) {
      // How many get through first depends on the counts above.
      warnings << QString("The delete cap is %1, so this %2 stops with an "
                          "error partway through %3 them. Raise the cap if "
                          "all of these should go.")
                      .arg(countCap)
                      .arg(run, removing);
    } else if (mostCounted > countCap) {
      could << QString("the delete cap of %1").arg(countCap);
    }
  }
  if (bytesCap >= 0) {
    if (summary.deleteBytes > bytesCap && !bisync) {
      warnings << QString("--max-delete-size is %1, and these files come to "
                          "%2, so this %3 stops with an error before %4 "
                          "them all.")
                      .arg(niceSize(bytesCap), niceSize(summary.deleteBytes),
                           run, removing);
    } else if (extra) {
      could << QString("--max-delete-size (%1)").arg(niceSize(bytesCap));
    }
  }
  if (!could.isEmpty()) {
    QStringList why;
    if (summary.toBackup > 0 || summary.replacedIntoBackup > 0) {
      why << "Moving a file into the backup can count as more than one "
             "delete. rclone also counts an older copy of the same name that "
             "it removes from the backup, and on a remote that can't move "
             "files it copies each one and then deletes it.";
    }
    if (summary.renamed > 0) {
      why << "A rename counts too on a remote that can't move files, since "
             "rclone copies the file to its new name and deletes the old "
             "one.";
    }
    if (summary.moved > 0) {
      why << "Move counts a file when it replaces one at the destination, "
             "and when it has to copy the file and delete the original, as "
             "it does between two different remotes.";
    }
    if (reuploads > 0) {
      why << "On a remote that can't change a file's time, rclone uploads "
             "the file again and counts moving its old copy into the backup.";
    }
    warnings << QString("%1 This %2 could stop at %3 before it's done.")
                    .arg(why.join(' '), run, could.join(" or "));
  }
  return warnings;
}

} // namespace SyncPreview
