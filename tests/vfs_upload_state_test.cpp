#include "vfs_upload_state.h"

#include <QTest>

class VfsUploadStateTest : public QObject {
  Q_OBJECT
private slots:
  void parseQueueJson() {
    const QByteArray json = R"({
      "queue": [
        {"name": "alpha.bin", "id": 1, "size": 1048576, "uploading": true},
        {"name": "beta.bin", "id": 2, "size": 512, "uploading": false}
      ]
    })";
    const VfsUploadState state = ParseVfsQueueState(json);
    QVERIFY2(state.valid, qPrintable(state.error));
    QCOMPARE(state.pendingFiles, 2);
    QCOMPARE(state.pendingBytes, quint64(1049088));
    QVERIFY(state.bytesKnown);
  }

  void parseStatsJson() {
    const QByteArray json = R"({
      "diskCache": {
        "bytesUsed": 123456789,
        "uploadsInProgress": 1,
        "uploadsQueued": 3
      }
    })";
    const VfsUploadState state = ParseVfsStatsUploadState(json);
    QVERIFY2(state.valid, qPrintable(state.error));
    QCOMPARE(state.pendingFiles, 4);
    QVERIFY(!state.bytesKnown);
  }

  void emptyQueueIsClean() {
    const VfsUploadState state = ParseVfsQueueState("{}");
    QVERIFY(state.valid);
    QVERIFY(!state.hasPendingUploads());
  }

  void byteFormatter() {
    QCOMPARE(FormatUploadByteSize(1536), QString("1.5 K"));
  }
};

QTEST_MAIN(VfsUploadStateTest)
#include "vfs_upload_state_test.moc"
