#pragma once

#include "pch.h"

// Conditions a scheduled run can require before it starts. They travel on
// the scheduled command line, so the OS scheduler starts the app as usual and
// the app makes the call. That way a skipped run is logged with its reason on
// every platform, which the schedulers' own battery settings can't do.
namespace RunConditions {

inline constexpr char kRequireAcFlag[] = "--require-ac";
inline constexpr char kRequireUnmeteredFlag[] = "--require-unmetered";

struct Conditions {
  bool requireAcPower = false;
  bool requireUnmetered = false;
  bool any() const { return requireAcPower || requireUnmetered; }
};

enum class PowerSource { Ac, Battery, Unknown };
enum class Metering { Unmetered, Metered, Unknown };

Conditions FromArguments(const QStringList &arguments);
QStringList ToArguments(const Conditions &conditions);

// Empty when the run may go ahead, otherwise why it was skipped. A condition
// that can't be confirmed skips the run, since the user asked for a promise.
QString SkipReason(const Conditions &conditions, PowerSource power,
                   Metering metering);

// macOS: the output of `pmset -g ps`.
PowerSource PowerSourceFromPmset(const QString &output);

// Linux: one entry per /sys/class/power_supply device.
struct Supply {
  QString type;
  QString online;
  QString scope;
};
PowerSource PowerSourceFromSupplies(const QList<Supply> &supplies);

PowerSource CurrentPowerSource();

// Whether this system can tell a metered network from an unmetered one.
bool CanCheckMetering();
Metering CurrentMetering();

// Probes only what the conditions ask for and returns SkipReason's answer.
QString SkipReasonNow(const Conditions &conditions);

} // namespace RunConditions
