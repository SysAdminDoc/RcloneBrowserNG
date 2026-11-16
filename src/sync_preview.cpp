#include "sync_preview.h"

namespace SyncPreview {

Summary Parse(const QByteArray &jsonLog, int maxDeletionsListed) {
  Summary summary;
  // Copies and moves logged per object. A replaced file's old copy going
  // into the backup is a "move" (or "move to a.txt.bak" with --suffix) next
  // to the "copy" that replaces it. A --track-renames rename is a "move to
  // <new name>" with no copy, since the old name is gone from the source, and
  // under `rclone move` the transfer itself is a move, so one of an object's
  // moves there is the transfer (all measured on v1.76.0).
  QHash<QString, QPair<int, int>> transfers;

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
    } else if (skipped == QLatin1String("move") ||
               skipped.startsWith(QLatin1String("move to "))) {
      transfers[path].second++;
    } else if (skipped == QLatin1String("copy")) {
      transfers[path].first++;
      summary.toTransfer++;
      if (size > 0) {
        summary.transferBytes += size;
      }
    } else if (skipped.startsWith(QLatin1String("update"))) {
      summary.toUpdate++;
    }
  }

  for (const QPair<int, int> &counts : std::as_const(transfers)) {
    const int copies = counts.first;
    const int moves = counts.second;
    summary.replacedIntoBackup += copies > 0 ? moves : qMax(0, moves - 1);
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

} // namespace

QString DeletionTitle(const Summary &summary) {
  const int moved = summary.toBackup;
  const int deleted = summary.toDelete - moved;
  if (summary.toDelete == 0) {
    return "Sync will move old copies of " +
           Files(summary.replacedIntoBackup) + " into the backup";
  }
  if (moved == 0) {
    return "Sync will delete " + Files(deleted);
  }
  if (deleted == 0) {
    return "Sync will move " + Files(moved) + " into the backup";
  }
  return QString("Sync will delete %1 and move %2 into the backup")
      .arg(Files(deleted))
      .arg(moved);
}

QStringList CapWarnings(const Summary &summary, qint64 countCap,
                        qint64 bytesCap,
                        const std::function<QString(qint64)> &niceSize) {
  QStringList warnings;
  const QString removing = Removing(summary);
  // Moving a file into the backup can count against both caps more than
  // once, and the dry run logs none of the extra counts:
  //  - an older copy of the same name already in the backup is deleted
  //    first and counted (local, measured on v1.76.0 with --backup-dir and
  //    with --suffix);
  //  - a remote that can copy but not move (S3, or the memory backend
  //    measured) copies the file and then deletes the original, which
  //    counts, while an older copy there is overwritten without a count;
  //  - a backup on another drive does both, since the local backend's Move
  //    deletes the older copy before the rename fails and it falls back to
  //    copy and delete (read from rclone's operations.Move).
  // So a file moved into the backup counts at most three times, and a
  // replaced file's old copy, not a delete itself, at most twice.
  const bool intoBackup = summary.toBackup > 0 || summary.replacedIntoBackup > 0;
  const qint64 mostCounted =
      static_cast<qint64>(summary.toDelete - summary.toBackup) +
      3 * static_cast<qint64>(summary.toBackup) +
      2 * static_cast<qint64>(summary.replacedIntoBackup);
  QStringList could;
  if (countCap >= 0) {
    if (summary.toDelete > countCap && !intoBackup) {
      warnings << QString("The delete cap is %1, so this sync stops with an "
                          "error after deleting %1 of them. Raise the cap if "
                          "all of these should go.")
                      .arg(countCap);
    } else if (summary.toDelete > countCap) {
      // How many get through first depends on the counts above.
      warnings << QString("The delete cap is %1, so this sync stops with an "
                          "error partway through %2 them. Raise the cap if "
                          "all of these should go.")
                      .arg(countCap)
                      .arg(removing);
    } else if (mostCounted > countCap) {
      could << QString("the delete cap of %1").arg(countCap);
    }
  }
  if (bytesCap >= 0) {
    if (summary.deleteBytes > bytesCap) {
      warnings << QString("--max-delete-size is %1, and these files come to "
                          "%2, so this sync stops with an error before %3 "
                          "them all.")
                      .arg(niceSize(bytesCap), niceSize(summary.deleteBytes),
                           removing);
    } else if (intoBackup) {
      could << QString("--max-delete-size (%1)").arg(niceSize(bytesCap));
    }
  }
  if (!could.isEmpty()) {
    warnings << QString("Moving a file into the backup can count as more than "
                        "one delete. rclone also counts an older copy of the "
                        "same name that it removes from the backup, and on a "
                        "remote that can't move files it copies each one and "
                        "then deletes it. This sync could stop at %1 before "
                        "it's done.")
                    .arg(could.join(" or "));
  }
  return warnings;
}

} // namespace SyncPreview
