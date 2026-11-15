#include "remote_path.h"

#include <QJsonObject>
#include <QTest>

class RemotePathTest : public QObject {
  Q_OBJECT
private slots:
  void rootJoinPreservesPunctuation() {
    const QString special = "folder [x] (y) : trailing ";
    QCOMPARE(JoinRemotePath(".", special), special);
  }

  void nestedJoinPreservesPunctuation() {
    const QString special = "folder [x] (y) : trailing ";
    QCOMPARE(JoinRemotePath("parent", special), "parent/" + special);
  }

  void lsjsonPathPreferredOverName() {
    const QString special = "folder [x] (y) : trailing ";
    QJsonObject obj;
    obj.insert("Path", special);
    obj.insert("Name", "different fallback");
    QCOMPARE(ChildRemotePathFromLsjson("parent", obj), "parent/" + special);
  }

  void lsjsonPrejoinedPathNotDoubled() {
    const QString special = "folder [x] (y) : trailing ";
    QJsonObject obj;
    obj.insert("Path", "parent/" + special);
    QCOMPARE(ChildRemotePathFromLsjson("parent", obj), "parent/" + special);
  }

  void lsjsonNameFallback() {
    const QString special = "folder [x] (y) : trailing ";
    QJsonObject obj;
    obj.insert("Name", special);
    QCOMPARE(ChildRemotePathFromLsjson(".", obj), special);
  }

  void googlePhotosAlbumPath() {
    QVERIFY(IsGooglePhotosRecursiveAlbumPath("album/family"));
    QVERIFY(IsGooglePhotosRecursiveAlbumPath("shared-album/family"));
    QVERIFY(!IsGooglePhotosRecursiveAlbumPath("album"));
    QVERIFY(!IsGooglePhotosRecursiveAlbumPath("media/by-month"));
  }

  void localFilesystemRemoteIsTheOnTheFlyBackend() {
    // "/" + ":" + "C:/" is the "/:C:/" path rclone could not list (#20).
    QCOMPARE(LocalFilesystemRemote() + ":" + "C:/", QString(":local:C:/"));
  }

  void splitsConfiguredRemoteTargets() {
    QString remote;
    QString path;
    QVERIFY(SplitRemoteTarget("gdrive:Photos/2024", &remote, &path));
    QCOMPARE(remote, QString("gdrive"));
    QCOMPARE(path, QString("Photos/2024"));
    // Only the first colon separates; the rest belongs to the path.
    QVERIFY(SplitRemoteTarget("s3:bucket/a:b", &remote, &path));
    QCOMPARE(remote, QString("s3"));
    QCOMPARE(path, QString("bucket/a:b"));
  }

  void splitsLocalPaneBookmarks() {
    QString remote;
    QString path;
    QVERIFY2(SplitRemoteTarget(":local:C:/Users/me", &remote, &path),
             "a bookmark from the local pane must open");
    QCOMPARE(remote, LocalFilesystemRemote());
    QCOMPARE(path, QString("C:/Users/me"));
    // Saved by the old local pane before #20 was fixed.
    QVERIFY(SplitRemoteTarget("/:D:/Backups", &remote, &path));
    QCOMPARE(remote, LocalFilesystemRemote());
    QCOMPARE(path, QString("D:/Backups"));
  }

  void rejectsTargetsWithoutARemoteName() {
    QVERIFY(!SplitRemoteTarget("no-colon-here", nullptr, nullptr));
    QVERIFY(!SplitRemoteTarget(":nothing-after", nullptr, nullptr));
    QVERIFY(!SplitRemoteTarget("::path", nullptr, nullptr));
    QVERIFY(!SplitRemoteTarget(":path-only", nullptr, nullptr));
    QVERIFY(!SplitRemoteTarget(QString(), nullptr, nullptr));
  }
};

QTEST_MAIN(RemotePathTest)
#include "remote_path_test.moc"
