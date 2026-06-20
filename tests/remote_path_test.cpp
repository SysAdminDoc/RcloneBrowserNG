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
};

QTEST_MAIN(RemotePathTest)
#include "remote_path_test.moc"
