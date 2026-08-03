#include "mount_backend.h"
#include "mount_health.h"
#include "mount_options.h"

#include <QTemporaryDir>
#include <QTemporaryFile>
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

  void mountPresetsExposeExactFlags() {
    const QVector<MountPreset> presets = MountPresets();
    QCOMPARE(presets.size(), 4);
    QCOMPARE(presets.at(0).id, QStringLiteral("balanced"));
    QCOMPARE(MountPresetFlags(QStringLiteral("streaming")),
             QStringLiteral("--vfs-cache-mode off --dir-cache-time 5m "
                            "--poll-interval 1m"));
    QVERIFY(MountPresetArguments(QStringLiteral("offline"))
                .contains(QStringLiteral("--vfs-cache-max-size")));
  }

  void legacyDefaultMigratesToBalanced() {
    QTemporaryFile file;
    QVERIFY(file.open());
    QSettings settings(file.fileName(), QSettings::IniFormat);
    settings.setValue("Settings/mount", "--vfs-cache-mode writes");
    const MountOptionState state = LoadMountOptionState(settings);
    QCOMPARE(state.presetId, QStringLiteral("balanced"));
    QVERIFY(state.expertOptions.isEmpty());
  }

  void legacyCustomOptionsArePreserved() {
    QTemporaryFile file;
    QVERIFY(file.open());
    QSettings settings(file.fileName(), QSettings::IniFormat);
    const QString custom = "--network-mode --dir-cache-time 30s";
    settings.setValue("Settings/mount", custom);
    const MountOptionState state = LoadMountOptionState(settings);
    QCOMPARE(state.presetId, QStringLiteral("custom"));
    QCOMPARE(state.expertOptions, custom);
  }

  void incompatibleExpertFlagsAreRejected() {
    const MountOptionValidation validation = ValidateMountOptions(
        QStringLiteral("balanced"), "--vfs-cache-mode full", false, false);
    QVERIFY(!validation.valid);
    QVERIFY(validation.error.contains("--vfs-cache-mode"));

    const MountOptionValidation cacheValidation = ValidateMountOptions(
        QStringLiteral("streaming"), "--vfs-cache-max-size 5G", false,
        false);
    QVERIFY(!cacheValidation.valid);
    QVERIFY(cacheValidation.error.contains("incompatible"));
  }

  void mountOptionsPreserveExpertFlags() {
    QString error;
    const QStringList options = BuildMountOptions(
        QStringLiteral("balanced"), "--network-mode", true, false, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(options.contains(QStringLiteral("--vfs-cache-mode")));
    QVERIFY(options.contains(QStringLiteral("--network-mode")));
    QVERIFY(options.contains(QStringLiteral("--read-only")));
  }

  void mountHealthRejectsMissingPoint() {
    const MountHealthProbeResult result =
        ProbeMountPoint(QDir::temp().filePath("rclone-browser-ng-missing"));
    QVERIFY(!result.healthy);
    QVERIFY(!result.detail.isEmpty());
  }

  void mountHealthAcceptsReadyDirectory() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const MountHealthProbeResult result = ProbeMountPoint(directory.path());
    QVERIFY2(result.healthy, qPrintable(result.detail));
  }
};

QTEST_MAIN(MountBackendTest)
#include "mount_backend_test.moc"
