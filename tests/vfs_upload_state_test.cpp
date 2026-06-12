#include "vfs_upload_state.h"

#include <QDebug>
#include <cstdlib>

namespace {

void require(bool condition, const QString &message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

} // namespace

int main() {
  const QByteArray queueJson = R"({
    "queue": [
      {"name": "alpha.bin", "id": 1, "size": 1048576, "uploading": true},
      {"name": "beta.bin", "id": 2, "size": 512, "uploading": false}
    ]
  })";

  const VfsUploadState queue = ParseVfsQueueState(queueJson);
  require(queue.valid, "vfs/queue parser rejected valid JSON: " + queue.error);
  require(queue.pendingFiles == 2, "vfs/queue parser counted files wrongly");
  require(queue.pendingBytes == 1049088,
          "vfs/queue parser summed queued bytes wrongly");
  require(queue.bytesKnown, "vfs/queue parser lost known byte total");

  const QByteArray statsJson = R"({
    "diskCache": {
      "bytesUsed": 123456789,
      "uploadsInProgress": 1,
      "uploadsQueued": 3
    }
  })";

  const VfsUploadState stats = ParseVfsStatsUploadState(statsJson);
  require(stats.valid, "vfs/stats parser rejected valid JSON: " + stats.error);
  require(stats.pendingFiles == 4, "vfs/stats parser counted uploads wrongly");
  require(!stats.bytesKnown,
          "vfs/stats fallback should not treat bytesUsed as dirty bytes");

  const VfsUploadState empty = ParseVfsQueueState("{}");
  require(empty.valid, "empty vfs/queue response should be valid");
  require(!empty.hasPendingUploads(), "empty queue should be clean");

  require(FormatUploadByteSize(1536) == "1.5 K",
          "byte formatter did not match existing size display style");

  return 0;
}
