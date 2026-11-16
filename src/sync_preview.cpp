#include "sync_preview.h"

namespace SyncPreview {

namespace {

// A replaced file's old copy is logged under its name at the destination,
// and the copy that replaces it under the source's name. The two can differ
// in case on a case-insensitive remote (Windows), in Unicode normalization,
// or through --name-transform (measured on v1.76.0: A.txt copied over a.txt,
// and a.txt copied as A.TXT with all,uppercase), so they're paired folded.
QString PairKey(const QString &path) {
  return path.normalized(QString::NormalizationForm_C).toCaseFolded();
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

} // namespace

QString CommandName(Command command) {
  switch (command) {
  case Command::Copy:
    return QStringLiteral("Copy");
  case Command::Move:
    return QStringLiteral("Move");
  case Command::Sync:
    break;
  }
  return QStringLiteral("Sync");
}

Summary Parse(const QByteArray &jsonLog, int maxDeletionsListed,
              Command command) {
  Summary summary;
  summary.command = command;
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
    } else if (skipped.startsWith(QLatin1String("update"))) {
      summary.toUpdate++;
    }
  }

  if (command == Command::Move) {
    // One move per file is the transfer, the last one logged for it. Any
    // other is its old copy going into the backup.
    QHash<QString, qint64> lastSize;
    QHash<QString, int> count;
    for (const LoggedMove &move : std::as_const(moves)) {
      const QString key = PairKey(move.object);
      lastSize.insert(key, move.size);
      count[key]++;
    }
    for (auto it = count.constBegin(); it != count.constEnd(); ++it) {
      summary.moved++;
      summary.replacedIntoBackup += it.value() - 1;
      summary.toTransfer++;
      const qint64 size = lastSize.value(it.key());
      if (size > 0) {
        summary.transferBytes += size;
      }
    }
    return summary;
  }

  for (const LoggedMove &move : std::as_const(moves)) {
    // A rename is always "move to" another name, so a plain move without
    // a copy found for it is still an old copy going into the backup.
    if (copied.contains(PairKey(move.object)) || move.target.isEmpty() ||
        LooksLikeBackupName(move.object, move.target)) {
      summary.replacedIntoBackup++;
    } else {
      summary.renamed++;
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
  return lines.join(' ');
}

QStringList CapWarnings(const Summary &summary, qint64 countCap,
                        qint64 bytesCap,
                        const std::function<QString(qint64)> &niceSize) {
  QStringList warnings;
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
  // So a file moved into the backup counts at most three times (once for
  // being removed), and any other move, a replaced file's old copy, a
  // rename or a Move transfer, at most twice.
  const qint64 otherMoves = static_cast<qint64>(summary.replacedIntoBackup) +
                            summary.renamed + summary.moved;
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
    if (summary.deleteBytes > bytesCap) {
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
    warnings << QString("%1 This %2 could stop at %3 before it's done.")
                    .arg(why.join(' '), run, could.join(" or "));
  }
  return warnings;
}

} // namespace SyncPreview
