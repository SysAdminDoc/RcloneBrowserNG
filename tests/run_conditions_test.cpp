#include "run_conditions.h"

#include <QTest>

using RunConditions::Conditions;
using RunConditions::Metering;
using RunConditions::PowerSource;
using RunConditions::Supply;

Q_DECLARE_METATYPE(RunConditions::Conditions)
Q_DECLARE_METATYPE(RunConditions::PowerSource)
Q_DECLARE_METATYPE(RunConditions::Metering)
Q_DECLARE_METATYPE(QList<RunConditions::Supply>)

namespace {
Conditions make(bool ac, bool unmetered) {
  Conditions conditions;
  conditions.requireAcPower = ac;
  conditions.requireUnmetered = unmetered;
  return conditions;
}
} // namespace

class RunConditionsTest : public QObject {
  Q_OBJECT

private slots:
  void argumentsRoundTrip_data() {
    QTest::addColumn<bool>("ac");
    QTest::addColumn<bool>("unmetered");
    QTest::newRow("none") << false << false;
    QTest::newRow("ac") << true << false;
    QTest::newRow("unmetered") << false << true;
    QTest::newRow("both") << true << true;
  }

  void argumentsRoundTrip() {
    QFETCH(bool, ac);
    QFETCH(bool, unmetered);
    const QStringList args = RunConditions::ToArguments(make(ac, unmetered));
    // As the scheduler hands them back to --run-task.
    const Conditions read = RunConditions::FromArguments(
        QStringList{"RcloneBrowser", "--run-task", "Nightly"} + args);
    QCOMPARE(read.requireAcPower, ac);
    QCOMPARE(read.requireUnmetered, unmetered);
    QCOMPARE(read.any(), ac || unmetered);
  }

  void flagsAreTheOnesTheSchedulersWrite() {
    QCOMPARE(RunConditions::ToArguments(make(true, true)),
             (QStringList{"--require-ac", "--require-unmetered"}));
  }

  void skipReason_data() {
    QTest::addColumn<Conditions>("conditions");
    QTest::addColumn<PowerSource>("power");
    QTest::addColumn<Metering>("metering");
    QTest::addColumn<QString>("reason");

    QTest::newRow("nothing asked, on battery and metered")
        << make(false, false) << PowerSource::Battery << Metering::Metered
        << QString();
    QTest::newRow("plugged in")
        << make(true, false) << PowerSource::Ac << Metering::Unknown
        << QString();
    QTest::newRow("on battery")
        << make(true, false) << PowerSource::Battery << Metering::Unmetered
        << QString("the computer is running on battery");
    QTest::newRow("power unknown")
        << make(true, false) << PowerSource::Unknown << Metering::Unmetered
        << QString("the app couldn't tell whether the computer is plugged in");
    QTest::newRow("unmetered")
        << make(false, true) << PowerSource::Battery << Metering::Unmetered
        << QString();
    QTest::newRow("metered")
        << make(false, true) << PowerSource::Ac << Metering::Metered
        << QString("the network connection is metered");
    QTest::newRow("metering unknown")
        << make(false, true) << PowerSource::Ac << Metering::Unknown
        << QString("the app couldn't tell whether the network is metered");
    QTest::newRow("both fail")
        << make(true, true) << PowerSource::Battery << Metering::Metered
        << QString("the computer is running on battery and the network "
                   "connection is metered");
    QTest::newRow("both hold")
        << make(true, true) << PowerSource::Ac << Metering::Unmetered
        << QString();
  }

  void skipReason() {
    QFETCH(Conditions, conditions);
    QFETCH(PowerSource, power);
    QFETCH(Metering, metering);
    QFETCH(QString, reason);
    QCOMPARE(RunConditions::SkipReason(conditions, power, metering), reason);
  }

  void nothingAskedSkipsNothing() {
    // No probe runs when nothing was asked for, so this holds on any machine.
    QCOMPARE(RunConditions::SkipReasonNow(make(false, false)), QString());
  }

  void pmsetOutput_data() {
    QTest::addColumn<QString>("output");
    QTest::addColumn<PowerSource>("power");
    QTest::newRow("laptop plugged in")
        << QString("Now drawing from 'AC Power'\n"
                   " -InternalBattery-0 (id=4653155)\t100%; charged; 0:00 "
                   "remaining present: true\n")
        << PowerSource::Ac;
    QTest::newRow("laptop on battery")
        << QString("Now drawing from 'Battery Power'\n"
                   " -InternalBattery-0 (id=4653155)\t84%; discharging; 5:12 "
                   "remaining present: true\n")
        << PowerSource::Battery;
    QTest::newRow("desktop on a UPS after a power cut")
        << QString("Now drawing from 'UPS Power'\n") << PowerSource::Battery;
    QTest::newRow("mac mini") << QString("Now drawing from 'AC Power'\n")
                              << PowerSource::Ac;
    QTest::newRow("nothing") << QString() << PowerSource::Unknown;
    QTest::newRow("something new")
        << QString("Now drawing from 'Solar Power'\n") << PowerSource::Unknown;
  }

  void pmsetOutput() {
    QFETCH(QString, output);
    QFETCH(PowerSource, power);
    QCOMPARE(RunConditions::PowerSourceFromPmset(output), power);
  }

  void linuxSupplies_data() {
    QTest::addColumn<QList<Supply>>("supplies");
    QTest::addColumn<PowerSource>("power");

    const Supply mainsOn{"Mains", "1", QString()};
    const Supply mainsOff{"Mains", "0", QString()};
    const Supply battery{"Battery", QString(), "System"};
    const Supply usbCharger{"USB", "1", QString()};
    const Supply mouse{"Battery", QString(), "Device"};

    QTest::newRow("desktop with no supplies") << QList<Supply>{}
                                              << PowerSource::Ac;
    QTest::newRow("desktop with a wireless mouse")
        << QList<Supply>{mouse} << PowerSource::Ac;
    QTest::newRow("laptop plugged in")
        << QList<Supply>{battery, mainsOn} << PowerSource::Ac;
    QTest::newRow("laptop unplugged")
        << QList<Supply>{mainsOff, battery} << PowerSource::Battery;
    QTest::newRow("laptop charging over USB-C")
        << QList<Supply>{battery, mainsOff, usbCharger} << PowerSource::Ac;
    QTest::newRow("battery with no charger listed")
        << QList<Supply>{battery} << PowerSource::Battery;
    QTest::newRow("charger listed but offline, no battery")
        << QList<Supply>{mainsOff} << PowerSource::Unknown;
  }

  void linuxSupplies() {
    QFETCH(QList<Supply>, supplies);
    QFETCH(PowerSource, power);
    QCOMPARE(RunConditions::PowerSourceFromSupplies(supplies), power);
  }
};

QTEST_MAIN(RunConditionsTest)
#include "run_conditions_test.moc"
