#include "mount_backend.h"

#include <QTest>

class MountBackendTest : public QObject {
  Q_OBJECT

private slots:
  void macFuse52() {
    MacMountBackendFacts facts;
    facts.macFuseVersion = "5.2.0";
    facts.nfsMountSupported = true;
    MountBackendPlan plan = PlanMacMountBackend(facts);
    QVERIFY2(plan.command == "mount",
             "macFUSE 5.2 should use rclone mount");
    QVERIFY2(plan.argsBeforeRemote.isEmpty(),
             "macFUSE should not get fuse-t-specific options");
    QVERIFY2(plan.warningText.isEmpty(),
             "macFUSE 5.2 should not warn");
  }

  void fuseTOnMacOS26() {
    MacMountBackendFacts facts;
    facts.fuseTInstalled = true;
    facts.macOsMajorVersion = 26;
    facts.nfsMountSupported = true;
    MountBackendPlan plan = PlanMacMountBackend(facts);
    QVERIFY2(plan.command == "mount",
             "fuse-t should use rclone mount");
    QVERIFY2(plan.argsBeforeRemote == (QStringList() << "-o" << "backend=fskit"),
             "fuse-t on macOS 26+ should request FSKit");
  }

  void userExplicitFuseBackend() {
    MacMountBackendFacts facts;
    facts.fuseTInstalled = true;
    facts.macOsMajorVersion = 26;
    facts.nfsMountSupported = true;
    facts.userMountOptions = QStringList() << "-o" << "backend=nfs";
    MountBackendPlan plan = PlanMacMountBackend(facts);
    QVERIFY2(plan.argsBeforeRemote.isEmpty(),
             "explicit user fuse backend should not be overridden");
  }

  void unrelatedBackendOption() {
    MacMountBackendFacts facts;
    facts.fuseTInstalled = true;
    facts.macOsMajorVersion = 26;
    facts.nfsMountSupported = true;
    facts.userMountOptions = QStringList() << "--some-backend=not-fuse";
    MountBackendPlan plan = PlanMacMountBackend(facts);
    QVERIFY2(plan.argsBeforeRemote == (QStringList() << "-o" << "backend=fskit"),
             "unrelated backend option should not suppress fuse-t FSKit");
  }

  void oldMacFuseWithNfsmount() {
    MacMountBackendFacts facts;
    facts.macFuseVersion = "5.1.3";
    facts.nfsMountSupported = true;
    MountBackendPlan plan = PlanMacMountBackend(facts);
    QVERIFY2(plan.command == "nfsmount",
             "old macFUSE should fall back to nfsmount when available");
    QVERIFY2(plan.warningText.isEmpty(),
             "nfsmount fallback should avoid warning");
  }

  void oldMacFuseWithoutAlternatives() {
    MacMountBackendFacts facts;
    facts.macFuseVersion = "5.1.3";
    facts.nfsMountSupported = false;
    MountBackendPlan plan = PlanMacMountBackend(facts);
    QVERIFY2(plan.command == "mount",
             "old macFUSE without alternatives should still attempt mount");
    QVERIFY2(!plan.warningText.isEmpty(),
             "old macFUSE without alternatives should warn");
  }

  void missingBackendWithoutNfsmount() {
    MacMountBackendFacts facts;
    facts.nfsMountSupported = false;
    MountBackendPlan plan = PlanMacMountBackend(facts);
    QVERIFY2(plan.command == "mount",
             "missing backend without nfsmount should still attempt mount");
    QVERIFY2(!plan.warningText.isEmpty(),
             "missing backend without nfsmount should warn");
  }
};

QTEST_MAIN(MountBackendTest)
#include "mount_backend_test.moc"
