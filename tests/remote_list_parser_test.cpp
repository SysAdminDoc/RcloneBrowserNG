#include "remote_list_parser.h"

#include <QTest>

using RemoteListParser::ParseJson;
using RemoteListParser::ParseLong;
using RemoteListParser::Remote;
using RemoteListParser::TooltipFor;

// The remotes list used to be built by splitting each `listremotes --long`
// line on ':' and requiring exactly two parts. Verified against rclone
// v1.75.0: a remote with a description prints
//   localdisk: alias My main disk: backup
// which has three parts, so the remote was skipped outright; without the
// description it still produced a type of "  alias  " that no icon matched.
class RemoteListParserTest : public QObject {
  Q_OBJECT

private slots:
  void parsesJsonOutput() {
    const QByteArray output = R"([
{"name":"localdisk","type":"alias","source":"file","description":""},
{"name":"secret","type":"crypt","source":"file","description":"Encrypted archive"}
])";
    QString error;
    const auto remotes = ParseJson(output, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(remotes.size(), 2);
    QCOMPARE(remotes[0].name, QStringLiteral("localdisk"));
    QCOMPARE(remotes[0].type, QStringLiteral("alias"));
    QCOMPARE(remotes[0].source, QStringLiteral("file"));
    QVERIFY(remotes[0].description.isEmpty());
    QCOMPARE(remotes[1].description, QStringLiteral("Encrypted archive"));
  }

  void jsonKeepsTheTypeWhenTheDescriptionHoldsAColon() {
    // The case that dropped the remote from the list entirely.
    const QByteArray output =
        R"([{"name":"localdisk","type":"alias","source":"file","description":"My main disk: backup"}])";
    const auto remotes = ParseJson(output);
    QCOMPARE(remotes.size(), 1);
    QCOMPARE(remotes[0].type, QStringLiteral("alias"));
    QCOMPARE(remotes[0].description, QStringLiteral("My main disk: backup"));
  }

  void jsonReportsMalformedOutput() {
    QString error;
    QVERIFY(ParseJson("not json at all", &error).isEmpty());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(ParseJson(R"({"name":"x"})", &error).isEmpty());
    QVERIFY2(error.contains("array"), qPrintable(error));

    // A nameless entry cannot be shown, but must not sink the whole list.
    error.clear();
    const auto remotes =
        ParseJson(R"([{"type":"alias"},{"name":"ok","type":"crypt"}])", &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(remotes.size(), 1);
    QCOMPARE(remotes[0].name, QStringLiteral("ok"));
  }

  void parsesLongOutput() {
    // Exactly what rclone v1.75.0 printed, padding and all.
    const QByteArray output =
        "localdisk: alias My main disk: backup\n"
        "secret:    crypt \n"
        "plain:     drive\n";
    const auto remotes = ParseLong(output);
    QCOMPARE(remotes.size(), 3);

    QCOMPARE(remotes[0].name, QStringLiteral("localdisk"));
    QCOMPARE(remotes[0].type, QStringLiteral("alias"));
    QCOMPARE(remotes[0].description, QStringLiteral("My main disk: backup"));

    QCOMPARE(remotes[1].name, QStringLiteral("secret"));
    QCOMPARE(remotes[1].type, QStringLiteral("crypt"));
    QVERIFY(remotes[1].description.isEmpty());

    // No description column at all.
    QCOMPARE(remotes[2].name, QStringLiteral("plain"));
    QCOMPARE(remotes[2].type, QStringLiteral("drive"));
    QVERIFY(remotes[2].description.isEmpty());
  }

  void longOutputSkipsLinesWithoutAName() {
    const auto remotes = ParseLong("\n:orphan\nreal: drive\n   \n");
    QCOMPARE(remotes.size(), 1);
    QCOMPARE(remotes[0].name, QStringLiteral("real"));
  }

  void bothParsersAgreeOnTheSameRemotes() {
    const auto fromJson = ParseJson(
        R"([{"name":"localdisk","type":"alias","description":"My main disk: backup"}])");
    const auto fromLong = ParseLong("localdisk: alias My main disk: backup\n");
    QCOMPARE(fromJson.size(), fromLong.size());
    QCOMPARE(fromJson[0].name, fromLong[0].name);
    QCOMPARE(fromJson[0].type, fromLong[0].type);
    QCOMPARE(fromJson[0].description, fromLong[0].description);
  }

  void tooltipShowsTypeAndDescription() {
    Remote remote;
    remote.type = "crypt";
    QCOMPARE(TooltipFor(remote), QStringLiteral("crypt"));

    remote.description = "Encrypted archive";
    QCOMPARE(TooltipFor(remote),
             QStringLiteral("crypt\nEncrypted archive"));

    // A remote whose type could not be read still says something useful
    // rather than showing an empty tooltip.
    Remote unknown;
    unknown.description = "Something";
    QCOMPARE(TooltipFor(unknown), QStringLiteral("unknown type\nSomething"));
  }
};

QTEST_APPLESS_MAIN(RemoteListParserTest)
#include "remote_list_parser_test.moc"
