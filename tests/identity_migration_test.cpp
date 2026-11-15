#include "app_identity.h"
#include "identity_migration.h"
#include "job_options_store.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {
JobOptions *makeTask(const QString &description, const QString &source,
                     const QString &dest) {
  auto jo = new JobOptions(false);
  jo->description = description;
  jo->operation = JobOptions::Copy;
  jo->transfers = "4";
  jo->source = source;
  jo->dest = dest;
  jo->isFolder = true;
  jo->uniqueId = QUuid::createUuid();
  return jo;
}

// Writes tasks the way classic RcloneBrowser 1.8 does: one record after
// another with no header, class version 3 (or 2, from before tasks had ids).
void writeClassicTasks(const QString &path, const QList<JobOptions *> &tasks,
                       qint32 version = 3) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QDataStream stream(&file);
  stream.setVersion(QDataStream::Qt_5_2);
  for (const JobOptions *jo : tasks) {
    stream << jo->myName() << version << jo->description
           << static_cast<quint32>(jo->jobType)
           << static_cast<quint32>(jo->operation) << jo->sync
           << static_cast<quint32>(jo->syncTiming) << jo->skipNewer
           << jo->skipExisting << jo->compare
           << static_cast<quint32>(jo->compareOption) << jo->verbose
           << jo->sameFilesystem << jo->dontUpdateModified << jo->transfers
           << jo->checkers << jo->bandwidth << jo->minSize << jo->minAge
           << jo->maxAge << jo->maxDepth << jo->connectTimeout
           << jo->idleTimeout << jo->retries << jo->lowLevelRetries
           << jo->deleteExcluded << jo->excluded << jo->extra
           << jo->DriveSharedWithMe << jo->source << jo->dest
           << jo->isFolder;
    if (version >= 3) {
      stream << jo->uniqueId;
    }
  }
}

void writeJsonTasks(const QString &path, const QList<JobOptions *> &tasks) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QString error;
  QVERIFY2(WriteJobOptionsStoreJson(&file, tasks, &error), qPrintable(error));
}

void writeBytes(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(bytes), bytes.size());
}

QByteArray fileHash(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

QStringList descriptionsIn(const QString &jsonPath) {
  QFile file(jsonPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {"<missing>"};
  }
  JobOptionsStoreLoadResult loaded = ReadJobOptionsStoreJson(&file);
  QStringList names;
  for (const JobOptions *task : loaded.tasks) {
    names << task->description;
  }
  ClearJobOptionsList(&loaded.tasks);
  names.sort();
  return names;
}

// A legacy profile (the shared classic RcloneBrowser identity) and a new one,
// each an ini file and a data folder inside one temporary directory.
struct Profiles {
  QTemporaryDir root;
  QString legacyDir;
  QString dataDir;
  std::unique_ptr<QSettings> legacy;
  std::unique_ptr<QSettings> current;

  Profiles() {
    legacyDir = root.filePath("legacy-data");
    dataDir = root.filePath("ng-data");
    QDir().mkpath(legacyDir);
    legacy.reset(
        new QSettings(root.filePath("legacy.ini"), QSettings::IniFormat));
    current.reset(new QSettings(root.filePath("ng.ini"), QSettings::IniFormat));
  }

  QString legacyFile(const QString &name) const {
    return QDir(legacyDir).filePath(name);
  }
  QString dataFile(const QString &name) const {
    return QDir(dataDir).filePath(name);
  }

  IdentityMigrationResult migrate(
      const IdentityMigrationOptions &options = IdentityMigrationOptions()) {
    return MigrateFromSharedIdentity(*legacy, *current, legacyDir, dataDir,
                                     options);
  }
};
} // namespace

class IdentityMigrationTest : public QObject {
  Q_OBJECT

private slots:
  void ngNoLongerSharesClassicsIdentity() {
    // The whole of #17: classic RcloneBrowser and its portable launcher read
    // and write HKCU\Software\rclone-browser\rclone-browser and
    // %LOCALAPPDATA%\rclone-browser\rclone-browser.
    const QString savedOrg = QCoreApplication::organizationName();
    const QString savedApp = QCoreApplication::applicationName();

    QCoreApplication::setOrganizationName(kLegacyOrganization);
    QCoreApplication::setApplicationName(kLegacyAppName);
    const QString classicData =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString classicSettings = QSettings().fileName();

    ApplyAppIdentity();
    const QString ngData =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString ngSettings = QSettings().fileName();

    QCoreApplication::setOrganizationName(savedOrg);
    QCoreApplication::setApplicationName(savedApp);

    QVERIFY2(QDir::cleanPath(ngData).compare(QDir::cleanPath(classicData),
                                             Qt::CaseInsensitive) != 0,
             qPrintable(ngData));
    QVERIFY2(ngSettings.compare(classicSettings, Qt::CaseInsensitive) != 0,
             qPrintable(ngSettings));
  }

  void copiesTheSharedSettingsOnFirstStart() {
    Profiles p;
    p.legacy->setValue("Settings/rclone", "C:/Apps/rclone/rclone.exe");
    p.legacy->setValue("Settings/rcloneConf", "C:/Apps/rclone/rclone.conf");
    p.legacy->setValue("Settings/iconSize", "large");
    p.legacy->sync();

    const IdentityMigrationResult result = p.migrate();

    QCOMPARE(result.settingsCopied, 3);
    QCOMPARE(p.current->value("Settings/rclone").toString(),
             QString("C:/Apps/rclone/rclone.exe"));
    QCOMPARE(p.current->value("Settings/rcloneConf").toString(),
             QString("C:/Apps/rclone/rclone.conf"));
    QCOMPARE(p.current->value("Settings/iconSize").toString(),
             QString("large"));
    QVERIFY(p.current->contains(kIdentityMigrationKey));
    // Copied, not moved: classic still has its settings.
    QCOMPARE(p.legacy->allKeys().size(), 3);
    QVERIFY(!p.legacy->contains(kIdentityMigrationKey));
  }

  void keepsSettingsAlreadySavedUnderTheNewIdentity() {
    Profiles p;
    p.legacy->setValue("Settings/rclone", "C:/classic/rclone.exe");
    p.legacy->setValue("Settings/rcloneConf", "C:/classic/rclone.conf");
    p.legacy->sync();
    p.current->setValue("Settings/rclone", "C:/ng/rclone.exe");
    p.current->sync();

    const IdentityMigrationResult result = p.migrate();

    QCOMPARE(result.settingsCopied, 0);
    QCOMPARE(p.current->value("Settings/rclone").toString(),
             QString("C:/ng/rclone.exe"));
    QVERIFY(!p.current->contains("Settings/rcloneConf"));
  }

  void runsOnlyOnce() {
    Profiles p;
    p.legacy->setValue("Settings/rclone", "C:/first/rclone.exe");
    p.legacy->sync();
    QVERIFY(!p.migrate().alreadyDone);

    // The portable launcher rewrites the shared key on every start. After
    // the first migration NG must not pick that up again.
    p.legacy->setValue("Settings/rclone", "C:/launcher/rclone.exe");
    p.legacy->sync();
    QList<JobOptions *> classic{makeTask("Later", "C:/later", "b2:later")};
    writeClassicTasks(p.legacyFile("tasks.bin"), classic);
    ClearJobOptionsList(&classic);

    const IdentityMigrationResult again = p.migrate();
    QVERIFY(again.alreadyDone);
    QVERIFY(!again.changedAnything());
    QCOMPARE(p.current->value("Settings/rclone").toString(),
             QString("C:/first/rclone.exe"));
    QVERIFY(!QFile::exists(p.dataFile("tasks.json")));
  }

  void importsClassicTasksAndLeavesTasksBinAlone() {
    Profiles p;
    QList<JobOptions *> classic{
        makeTask("Photos", "C:/Users/me/Pictures", "gdrive:Pictures"),
        makeTask("Documents", "C:/Users/me/Documents", "nas:Documents"),
        makeTask("Music", "D:/Music", "pcloud:Music")};
    writeClassicTasks(p.legacyFile("tasks.bin"), classic);
    ClearJobOptionsList(&classic);
    const QByteArray before = fileHash(p.legacyFile("tasks.bin"));

    const IdentityMigrationResult result = p.migrate();

    QVERIFY2(result.classicTasksError.isEmpty(),
             qPrintable(result.classicTasksError));
    QCOMPARE(result.classicTasksImported, 3);
    QCOMPARE(descriptionsIn(p.dataFile("tasks.json")),
             QStringList({"Documents", "Music", "Photos"}));
    QCOMPARE(fileHash(p.legacyFile("tasks.bin")), before);
    QVERIFY(!QFile::exists(p.dataFile("tasks.bin")));
  }

  void doesNotDuplicateTasksAnOlderNgAlreadyImported() {
    Profiles p;
    // NG up to 2.0.2 read classic's tasks.bin and saved the tasks to its own
    // tasks.json in the same shared folder. Version 2 records carry no id,
    // so every read gives them a new one.
    QList<JobOptions *> classic{
        makeTask("Photos", "C:/Users/me/Pictures", "gdrive:Pictures"),
        makeTask("Added in classic later", "E:/Video", "onedrive:Video")};
    writeClassicTasks(p.legacyFile("tasks.bin"), classic, 2);
    QList<JobOptions *> ng{
        makeTask("Photos", "C:/Users/me/Pictures", "gdrive:Pictures"),
        makeTask("Made in NG", "C:/Code", "b2:code")};
    writeJsonTasks(p.legacyFile("tasks.json"), ng);
    ClearJobOptionsList(&classic);
    ClearJobOptionsList(&ng);
    writeBytes(p.legacyFile("job-history.json"), "{\"runs\":[]}");

    const IdentityMigrationResult result = p.migrate();

    QCOMPARE(result.filesCopied,
             QStringList({"tasks.json", "job-history.json"}));
    QCOMPARE(result.classicTasksImported, 1);
    QCOMPARE(descriptionsIn(p.dataFile("tasks.json")),
             QStringList({"Added in classic later", "Made in NG", "Photos"}));
    QVERIFY(QFile::exists(p.legacyFile("tasks.json")));
    QCOMPARE(fileHash(p.dataFile("job-history.json")),
             fileHash(p.legacyFile("job-history.json")));
  }

  void putsBackTheTasksFileNg202Renamed() {
    Profiles p;
    QList<JobOptions *> classic{makeTask("Photos", "C:/Pictures", "gdrive:")};
    writeClassicTasks(p.legacyFile("tasks.bin.corrupt"), classic);
    ClearJobOptionsList(&classic);

    const IdentityMigrationResult result = p.migrate();

    QVERIFY(result.restoredClassicTasksFile);
    QVERIFY(QFile::exists(p.legacyFile("tasks.bin")));
    QVERIFY(!QFile::exists(p.legacyFile("tasks.bin.corrupt")));
    QCOMPARE(result.classicTasksImported, 1);
  }

  void keepsTheRenamedFileWhenClassicStartedANewOne() {
    Profiles p;
    QList<JobOptions *> renamed{makeTask("Old", "C:/old", "gdrive:old")};
    writeClassicTasks(p.legacyFile("tasks.bin.corrupt"), renamed);
    QList<JobOptions *> current{makeTask("New", "C:/new", "gdrive:new")};
    writeClassicTasks(p.legacyFile("tasks.bin"), current);
    ClearJobOptionsList(&renamed);
    ClearJobOptionsList(&current);
    const QByteArray renamedBefore = fileHash(p.legacyFile("tasks.bin.corrupt"));

    const IdentityMigrationResult result = p.migrate();

    QVERIFY(!result.restoredClassicTasksFile);
    QCOMPARE(fileHash(p.legacyFile("tasks.bin.corrupt")), renamedBefore);
    QCOMPARE(descriptionsIn(p.dataFile("tasks.json")), QStringList({"New"}));
  }

  void leavesAnUnreadableClassicFileUntouched() {
    Profiles p;
    writeBytes(p.legacyFile("tasks.bin"), QByteArray("not a task store", 16));
    const QByteArray before = fileHash(p.legacyFile("tasks.bin"));

    const IdentityMigrationResult result = p.migrate();

    QVERIFY(!result.classicTasksError.isEmpty());
    QCOMPARE(result.classicTasksImported, 0);
    QCOMPARE(fileHash(p.legacyFile("tasks.bin")), before);
    QVERIFY(!QFile::exists(p.legacyFile("tasks.bin.corrupt")));
    QVERIFY(!QFile::exists(p.dataFile("tasks.json")));
    QVERIFY(result.summary().contains("left classic tasks.bin untouched"));
  }

  void neverWritesOverATasksJsonItCannotRead() {
    Profiles p;
    QDir().mkpath(p.dataDir);
    writeBytes(p.dataFile("tasks.json"), "{\"version\": 7, \"tasks\": []}");
    const QByteArray before = fileHash(p.dataFile("tasks.json"));
    QList<JobOptions *> classic{makeTask("Photos", "C:/Pictures", "gdrive:")};
    writeClassicTasks(p.legacyFile("tasks.bin"), classic);
    ClearJobOptionsList(&classic);

    const IdentityMigrationResult result = p.migrate();

    QCOMPARE(result.classicTasksImported, 0);
    QVERIFY(result.classicTasksError.contains("tasks.json"));
    QCOMPARE(fileHash(p.dataFile("tasks.json")), before);
  }

  void portableModeOnlySetsTheMarker() {
    Profiles p;
    p.legacy->setValue("Settings/rclone", "C:/classic/rclone.exe");
    p.legacy->sync();
    QList<JobOptions *> classic{makeTask("Photos", "C:/Pictures", "gdrive:")};
    writeClassicTasks(p.legacyFile("tasks.bin"), classic);
    ClearJobOptionsList(&classic);

    IdentityMigrationOptions portable;
    portable.copySettings = false;
    portable.importClassicTasks = false;
    const IdentityMigrationResult result = p.migrate(portable);

    QCOMPARE(result.settingsCopied, 0);
    QCOMPARE(result.classicTasksImported, 0);
    QVERIFY(!p.current->contains("Settings/rclone"));
    QVERIFY(p.current->contains(kIdentityMigrationKey));
  }

  void loadKeepsACopyOfAnUnreadableTasksJson() {
    QTemporaryDir dir;
    const QString json = dir.filePath("tasks.json");
    const QString bin = dir.filePath("tasks.bin");
    writeBytes(json, "{\"version\": 7, \"tasks\": []}");

    SavedTasksLoadResult first = LoadSavedTasks(json, bin);
    QVERIFY(!first.loaded);
    QVERIFY(first.error.contains("tasks.json"));
    QCOMPARE(fileHash(json + ".unreadable"), fileHash(json));

    // The same broken file on the next start reuses the copy.
    SavedTasksLoadResult second = LoadSavedTasks(json, bin);
    QVERIFY(!second.loaded);
    QVERIFY(!QFile::exists(json + ".unreadable2"));
  }

  void loadLeavesAnUnreadableTasksBinWhereItIs() {
    QTemporaryDir dir;
    const QString json = dir.filePath("tasks.json");
    const QString bin = dir.filePath("tasks.bin");
    writeBytes(bin, QByteArray("not a task store", 16));
    const QByteArray before = fileHash(bin);

    SavedTasksLoadResult result = LoadSavedTasks(json, bin);

    QVERIFY(!result.loaded);
    QVERIFY(result.error.contains("left as it is"));
    QCOMPARE(fileHash(bin), before);
    QVERIFY(!QFile::exists(bin + ".corrupt"));
    QVERIFY(!QFile::exists(json));
  }

  void loadReadsAClassicTasksBin() {
    QTemporaryDir dir;
    const QString json = dir.filePath("tasks.json");
    const QString bin = dir.filePath("tasks.bin");
    QList<JobOptions *> classic{makeTask("Photos", "C:/Pictures", "gdrive:"),
                                makeTask("Music", "D:/Music", "pcloud:")};
    writeClassicTasks(bin, classic);
    ClearJobOptionsList(&classic);

    SavedTasksLoadResult result = LoadSavedTasks(json, bin);

    QVERIFY(result.loaded);
    QVERIFY(result.fromLegacyFile);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.tasks.size(), 2);
    ClearJobOptionsList(&result.tasks);
  }
};

QTEST_MAIN(IdentityMigrationTest)
#include "identity_migration_test.moc"
