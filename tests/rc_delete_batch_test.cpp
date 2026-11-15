#include "rc_delete_batch.h"

#include <QTest>

// The job/batch body for deleting several selected items, and how a finished
// batch's per-item results turn into failures. What rclone does with the body
// is covered against a real daemon in rc_route_test.
class RcDeleteBatchTest : public QObject {
  Q_OBJECT

private:
  static RcDeleteBatch::Item item(const QString &target, bool folder) {
    RcDeleteBatch::Item entry;
    entry.target = target;
    entry.label = target.section(QChar(':'), 1);
    entry.message = "Delete " + entry.label;
    entry.folder = folder;
    entry.args << (folder ? "purge" : "delete") << target;
    return entry;
  }

  static RcSyncRequest::OptionIndex index() {
    RcSyncRequest::OptionIndex options;
    options.insert("--fast-list", {"main", "UseListR", "bool"});
    options.insert("--checkers", {"main", "Checkers", "int"});
    options.insert("--dry-run", {"main", "DryRun", "bool"});
    options.insert("--exclude", {"filter", "ExcludeRule", "stringArray"});
    return options;
  }

  static QJsonObject input(const RcDeleteBatch::Request &request, int i) {
    return request.payload.value("inputs").toArray().at(i).toObject();
  }

private slots:
  void filesAndFoldersSplitIntoParentAndLeaf() {
    RcDeleteBatch::Plan plan;
    plan.items << item("gd:Photos/2024/a.jpg", false)
               << item("gd:Photos/old", true) << item("gd:top.txt", false)
               << item("gd:Photos/trailing/", true);

    const RcDeleteBatch::Request request = RcDeleteBatch::Build(plan, index());
    QVERIFY2(request.usable, qPrintable(request.reason));
    QCOMPARE(request.payload.value("inputs").toArray().size(), 4);

    QCOMPARE(input(request, 0).value("_path").toString(),
             QStringLiteral("operations/deletefile"));
    QCOMPARE(input(request, 0).value("fs").toString(),
             QStringLiteral("gd:Photos/2024/"));
    QCOMPARE(input(request, 0).value("remote").toString(),
             QStringLiteral("a.jpg"));

    QCOMPARE(input(request, 1).value("_path").toString(),
             QStringLiteral("operations/purge"));
    QCOMPARE(input(request, 1).value("fs").toString(),
             QStringLiteral("gd:Photos/"));
    QCOMPARE(input(request, 1).value("remote").toString(),
             QStringLiteral("old"));

    // An item at the top of the remote has the remote itself as its parent.
    QCOMPARE(input(request, 2).value("fs").toString(), QStringLiteral("gd:"));
    QCOMPARE(input(request, 2).value("remote").toString(),
             QStringLiteral("top.txt"));

    QCOMPARE(input(request, 3).value("fs").toString(),
             QStringLiteral("gd:Photos/"));
    QCOMPARE(input(request, 3).value("remote").toString(),
             QStringLiteral("trailing"));

    QCOMPARE(request.labels,
             QStringList() << "Photos/2024/a.jpg" << "Photos/old" << "top.txt"
                           << "Photos/trailing/");
    QVERIFY(!request.payload.contains("_config"));
  }

  // A Windows drive root has to stay "C:/". Without the trailing slash it
  // would be the drive-relative "C:", which means the current folder on C.
  void aDriveRootParentKeepsItsSlash() {
    RcDeleteBatch::Plan plan;
    plan.items << item("local:C:/a.txt", false) << item("local:C:/b", true);

    const RcDeleteBatch::Request request = RcDeleteBatch::Build(plan, index());
    QVERIFY2(request.usable, qPrintable(request.reason));
    QCOMPARE(input(request, 0).value("fs").toString(),
             QStringLiteral("local:C:/"));
    QCOMPARE(input(request, 1).value("remote").toString(),
             QStringLiteral("b"));
  }

  void backendFlagsBecomeConnectionStringParameters() {
    RcDeleteBatch::Plan plan;
    plan.items << item("gd:a.txt", false) << item("gd:dir/b", true);
    plan.backendFlags << "--drive-shared-with-me" << "--drive-use-trash";

    RcDeleteBatch::Request request = RcDeleteBatch::Build(plan, index());
    QVERIFY2(request.usable, qPrintable(request.reason));
    QCOMPARE(input(request, 0).value("fs").toString(),
             QStringLiteral("gd,shared_with_me=true,use_trash=true:"));
    QCOMPARE(input(request, 1).value("fs").toString(),
             QStringLiteral("gd,shared_with_me=true,use_trash=true:dir/"));

    plan.items.clear();
    plan.items << item("od:a.txt", false) << item("od:b.txt", false);
    plan.backendFlags = QStringList() << "--onedrive-hard-delete=false";
    request = RcDeleteBatch::Build(plan, index());
    QVERIFY2(request.usable, qPrintable(request.reason));
    QCOMPARE(input(request, 1).value("fs").toString(),
             QStringLiteral("od,hard_delete=false:"));
  }

  void flagsThatCannotBeParametersKeepThePerItemRoute() {
    RcDeleteBatch::Plan plan;
    plan.items << item("gd:a.txt", false) << item("gd:b.txt", false);

    plan.backendFlags = QStringList() << "--drive-use-trash"
                                      << "--onedrive-hard-delete=false";
    QVERIFY(!RcDeleteBatch::Build(plan, index()).usable);

    plan.backendFlags = QStringList() << "--drive-impersonate=a,b:c";
    QVERIFY(!RcDeleteBatch::Build(plan, index()).usable);

    plan.backendFlags = QStringList() << "-v";
    QVERIFY(!RcDeleteBatch::Build(plan, index()).usable);

    plan.backendFlags = QStringList() << "--fast";
    QVERIFY(!RcDeleteBatch::Build(plan, index()).usable);
  }

  void defaultOptionsGoInTheBatchConfig() {
    RcDeleteBatch::Plan plan;
    plan.items << item("gd:a.txt", false) << item("gd:b.txt", false);
    plan.extraOptions << "--fast-list" << "--checkers" << "16"
                      << "--dry-run=false";

    const RcDeleteBatch::Request request = RcDeleteBatch::Build(plan, index());
    QVERIFY2(request.usable, qPrintable(request.reason));
    const QJsonObject config = request.payload.value("_config").toObject();
    QCOMPARE(config.value("UseListR").toBool(false), true);
    QCOMPARE(config.value("Checkers").toInt(), 16);
    QCOMPARE(config.value("DryRun").toBool(true), false);
  }

  void defaultOptionsTheBatchCannotCarryKeepThePerItemRoute() {
    RcDeleteBatch::Plan plan;
    plan.items << item("gd:a.txt", false) << item("gd:b.txt", false);

    // rclone ignores an unknown _config key without an error, so a flag
    // missing from the daemon's own table must never be sent.
    plan.extraOptions = QStringList() << "--one-file-system";
    RcDeleteBatch::Request request = RcDeleteBatch::Build(plan, index());
    QVERIFY(!request.usable);
    QVERIFY2(request.reason.contains("--one-file-system"),
             qPrintable(request.reason));

    // A filter changes what `rclone delete` removes, and a batch delete
    // would ignore it.
    plan.extraOptions = QStringList() << "--exclude" << "*.keep";
    request = RcDeleteBatch::Build(plan, index());
    QVERIFY(!request.usable);
    QVERIFY2(request.reason.contains("filter"), qPrintable(request.reason));

    plan.extraOptions = QStringList() << "--fast-list";
    QVERIFY(!RcDeleteBatch::Build(plan, RcSyncRequest::OptionIndex()).usable);

    plan.extraOptions = QStringList() << "stray";
    QVERIFY(!RcDeleteBatch::Build(plan, index()).usable);
  }

  void aSingleItemOrARootKeepsThePerItemRoute() {
    RcDeleteBatch::Plan plan;
    plan.items << item("gd:a.txt", false);
    QVERIFY(!RcDeleteBatch::Build(plan, index()).usable);

    plan.items << item("gd:", true);
    const RcDeleteBatch::Request request = RcDeleteBatch::Build(plan, index());
    QVERIFY(!request.usable);
    QVERIFY(request.labels.isEmpty());

    plan.items.clear();
    plan.items << item("gd:a.txt", false) << item("no-colon", false);
    QVERIFY(!RcDeleteBatch::Build(plan, index()).usable);
  }

  // The shape rclone 1.75.1 returns: {} for an input that worked and an
  // object with "error" for one that didn't, while the job as a whole still
  // reports success.
  void failuresNameEachItemThatFailed() {
    const QStringList labels = QStringList() << "a.txt" << "b.txt" << "dir";
    const QJsonObject failed{
        {"error", "object not found"},
        {"input", QJsonObject{{"remote", "b.txt"}}},
        {"path", "operations/deletefile"},
        {"status", 404}};
    const QJsonObject output{
        {"results", QJsonArray{QJsonObject(), failed, QJsonObject()}}};

    QCOMPARE(RcDeleteBatch::Failures(output, labels),
             QStringList() << "b.txt: object not found");

    const QJsonObject allGood{
        {"results",
         QJsonArray{QJsonObject(), QJsonObject(), QJsonObject()}}};
    QVERIFY(RcDeleteBatch::Failures(allGood, labels).isEmpty());
  }

  void missingResultsCountAsFailures() {
    const QStringList labels = QStringList() << "a.txt" << "b.txt";
    const QJsonObject shortOutput{{"results", QJsonArray{QJsonObject()}}};
    QCOMPARE(RcDeleteBatch::Failures(shortOutput, labels),
             QStringList() << "b.txt: rclone reported no result");

    QCOMPARE(RcDeleteBatch::Failures(QJsonValue(), labels).size(), 2);
    QCOMPARE(RcDeleteBatch::Failures(QJsonValue("text"), labels).size(), 2);
  }
};

QTEST_MAIN(RcDeleteBatchTest)
#include "rc_delete_batch_test.moc"
