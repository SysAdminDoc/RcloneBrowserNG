#include "remote_provider.h"

#include <QTest>

class RemoteProviderTest : public QObject {
  Q_OBJECT
private slots:
  void parseFiltersHiddenProviders() {
    const QByteArray json = R"({
      "providers": [
        {"Name":"drive","Prefix":"drive","Description":"Google Drive","Hide":false},
        {"Name":"hidden","Prefix":"hidden","Description":"Hidden Backend","Hide":true},
        {"Name":"protondrive","Prefix":"protondrive","Description":"Proton Drive","Hide":false},
        {"Name":"smb","Prefix":"smb","Description":"","Hide":false}
      ]
    })";
    QString error;
    const QVector<RemoteProvider> providers = ParseRemoteProviders(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(providers.size(), 3);
    QCOMPARE(providers[0].prefix, QString("drive"));
    QCOMPARE(providers[1].prefix, QString("protondrive"));
    QCOMPARE(RemoteProviderDisplayName(providers[2]), QString("smb (smb)"));
  }

  void malformedJsonReturnsError() {
    QString error;
    const QVector<RemoteProvider> providers =
        ParseRemoteProviders(QByteArray("{"), &error);
    QVERIFY(providers.isEmpty());
    QVERIFY(error.contains("Failed to parse rclone config/providers output"));
  }
};

QTEST_MAIN(RemoteProviderTest)
#include "remote_provider_test.moc"
