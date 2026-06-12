#include "mount_backend.h"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    qCritical() << message;
    ++failures;
  }
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);

  MacMountBackendFacts facts;
  facts.macFuseVersion = "5.2.0";
  facts.nfsMountSupported = true;
  MountBackendPlan plan = PlanMacMountBackend(facts);
  expect(plan.command == "mount", "macFUSE 5.2 should use rclone mount");
  expect(plan.argsBeforeRemote.isEmpty(),
         "macFUSE should not get fuse-t-specific options");
  expect(plan.warningText.isEmpty(), "macFUSE 5.2 should not warn");

  facts = MacMountBackendFacts();
  facts.fuseTInstalled = true;
  facts.macOsMajorVersion = 26;
  facts.nfsMountSupported = true;
  plan = PlanMacMountBackend(facts);
  expect(plan.command == "mount", "fuse-t should use rclone mount");
  expect(plan.argsBeforeRemote == (QStringList() << "-o" << "backend=fskit"),
         "fuse-t on macOS 26+ should request FSKit");

  facts.userMountOptions = QStringList() << "-o" << "backend=nfs";
  plan = PlanMacMountBackend(facts);
  expect(plan.argsBeforeRemote.isEmpty(),
         "explicit user fuse backend should not be overridden");

  facts.userMountOptions = QStringList() << "--some-backend=not-fuse";
  plan = PlanMacMountBackend(facts);
  expect(plan.argsBeforeRemote == (QStringList() << "-o" << "backend=fskit"),
         "unrelated backend option should not suppress fuse-t FSKit");

  facts = MacMountBackendFacts();
  facts.macFuseVersion = "5.1.3";
  facts.nfsMountSupported = true;
  plan = PlanMacMountBackend(facts);
  expect(plan.command == "nfsmount",
         "old macFUSE should fall back to nfsmount when available");
  expect(plan.warningText.isEmpty(), "nfsmount fallback should avoid warning");

  facts = MacMountBackendFacts();
  facts.macFuseVersion = "5.1.3";
  facts.nfsMountSupported = false;
  plan = PlanMacMountBackend(facts);
  expect(plan.command == "mount",
         "old macFUSE without alternatives should still attempt mount");
  expect(!plan.warningText.isEmpty(),
         "old macFUSE without alternatives should warn");

  facts = MacMountBackendFacts();
  facts.nfsMountSupported = false;
  plan = PlanMacMountBackend(facts);
  expect(plan.command == "mount",
         "missing backend without nfsmount should still attempt mount");
  expect(!plan.warningText.isEmpty(),
         "missing backend without nfsmount should warn");

  return failures == 0 ? 0 : 1;
}
