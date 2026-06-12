#include "job_options_store.h"

#include <QBuffer>
#include <QDebug>
#include <cstdlib>

namespace {
void require(bool condition, const QString &message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

JobOptions *makeTask() {
  auto jo = new JobOptions(false);
  jo->description = "Nightly upload";
  jo->operation = JobOptions::Sync;
  jo->sync = true;
  jo->syncTiming = JobOptions::After;
  jo->skipNewer = true;
  jo->transfers = "4";
  jo->checkers = "8";
  jo->source = "C:/data";
  jo->dest = "remote:backup";
  jo->isFolder = true;
  jo->DriveSharedWithMe = true;
  jo->uniqueId = QUuid::fromString("{11111111-2222-3333-4444-555555555555}");
  return jo;
}

void writeLegacyTask(QIODevice *device, const JobOptions &jo) {
  QDataStream stream(device);
  stream.setVersion(QDataStream::Qt_5_2);
  stream << jo.myName() << JobOptions::classVersion << jo.description
         << static_cast<quint32>(jo.jobType)
         << static_cast<quint32>(jo.operation) << jo.sync
         << static_cast<quint32>(jo.syncTiming) << jo.skipNewer
         << jo.skipExisting << jo.compare
         << static_cast<quint32>(jo.compareOption) << jo.verbose
         << jo.sameFilesystem << jo.dontUpdateModified << jo.transfers
         << jo.checkers << jo.bandwidth << jo.minSize << jo.minAge
         << jo.maxAge << jo.maxDepth << jo.connectTimeout << jo.idleTimeout
         << jo.retries << jo.lowLevelRetries << jo.deleteExcluded
         << jo.excluded << jo.extra << jo.DriveSharedWithMe << jo.source
         << jo.dest << jo.isFolder << jo.uniqueId;
}

void requireTaskMatches(const JobOptions *task) {
  require(task->description == "Nightly upload", "description changed");
  require(task->operation == JobOptions::Sync, "operation changed");
  require(task->syncTiming == JobOptions::After, "sync timing changed");
  require(task->skipNewer, "skip-newer flag changed");
  require(task->source == "C:/data", "source changed");
  require(task->dest == "remote:backup", "dest changed");
  require(task->isFolder, "folder flag changed");
  require(task->DriveSharedWithMe, "Drive shared flag changed");
  require(task->uniqueId ==
              QUuid::fromString("{11111111-2222-3333-4444-555555555555}"),
          "unique id changed");
}
} // namespace

int main() {
  QList<JobOptions *> tasks;
  tasks.append(makeTask());

  QByteArray bytes;
  QBuffer out(&bytes);
  require(out.open(QIODevice::WriteOnly), "failed to open write buffer");
  QString error;
  require(WriteJobOptionsStore(&out, tasks, &error),
          "failed to write new store: " + error);
  out.close();

  QBuffer in(&bytes);
  require(in.open(QIODevice::ReadOnly), "failed to open read buffer");
  JobOptionsStoreLoadResult loaded = ReadJobOptionsStore(&in);
  require(loaded.error.isEmpty(), "failed to read new store: " + loaded.error);
  require(!loaded.migratedFromLegacy, "new store was marked legacy");
  require(loaded.tasks.size() == 1, "new store task count changed");
  requireTaskMatches(loaded.tasks.first());
  ClearJobOptionsList(&loaded.tasks);

  QByteArray legacyBytes;
  QBuffer legacyOut(&legacyBytes);
  require(legacyOut.open(QIODevice::WriteOnly),
          "failed to open legacy write buffer");
  writeLegacyTask(&legacyOut, *tasks.first());
  legacyOut.close();

  QBuffer legacyIn(&legacyBytes);
  require(legacyIn.open(QIODevice::ReadOnly),
          "failed to open legacy read buffer");
  JobOptionsStoreLoadResult legacy = ReadJobOptionsStore(&legacyIn);
  require(legacy.error.isEmpty(),
          "failed to read legacy store: " + legacy.error);
  require(legacy.migratedFromLegacy, "legacy store was not marked migrated");
  require(legacy.tasks.size() == 1, "legacy store task count changed");
  requireTaskMatches(legacy.tasks.first());
  ClearJobOptionsList(&legacy.tasks);

  QByteArray newerBytes;
  QBuffer newerOut(&newerBytes);
  require(newerOut.open(QIODevice::WriteOnly),
          "failed to open newer write buffer");
  QDataStream newerStream(&newerOut);
  newerStream.setVersion(QDataStream::Qt_5_2);
  newerStream << QString("RcloneBrowserNG.tasks") << qint32(999) << qint32(0);
  newerOut.close();

  QBuffer newerIn(&newerBytes);
  require(newerIn.open(QIODevice::ReadOnly),
          "failed to open newer read buffer");
  JobOptionsStoreLoadResult newer = ReadJobOptionsStore(&newerIn);
  require(newer.tasks.isEmpty(), "newer schema unexpectedly returned tasks");
  require(newer.error.contains("stored task schema is newer"),
          "newer schema error was not actionable");

  ClearJobOptionsList(&tasks);
  return 0;
}
