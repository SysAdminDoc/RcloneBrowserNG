#include "sync_preview.h"

namespace SyncPreview {

Summary Parse(const QByteArray &jsonLog, int maxDeletionsListed) {
  Summary summary;

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
      summary.replacedIntoBackup++;
    } else if (skipped == QLatin1String("copy")) {
      summary.toTransfer++;
      if (size > 0) {
        summary.transferBytes += size;
      }
    } else if (skipped.startsWith(QLatin1String("update"))) {
      summary.toUpdate++;
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
  // A file moved into the backup over an older copy of the same name makes
  // rclone delete that copy first, and it counts against both caps
  // (measured on v1.76.0 with --backup-dir and with --suffix). The dry run
  // doesn't log it, so at most every move counts twice.
  const bool intoBackup = summary.toBackup > 0 || summary.replacedIntoBackup > 0;
  const qint64 mostCounted = static_cast<qint64>(summary.toDelete) +
                             summary.toBackup + summary.replacedIntoBackup;
  QStringList could;
  if (countCap >= 0) {
    if (summary.toDelete > countCap) {
      warnings << QString("The delete cap is %1, so this sync stops with an "
                          "error after %2 %1 of them. Raise the cap if all "
                          "of these should go.")
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
    warnings << QString("If the backup already holds older copies under the "
                        "same names, rclone deletes each of those first and "
                        "counts it as another delete, so this sync could "
                        "stop at %1 before it's done.")
                    .arg(could.join(" or "));
  }
  return warnings;
}

} // namespace SyncPreview
