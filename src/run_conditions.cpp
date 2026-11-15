#include "run_conditions.h"

namespace RunConditions {

Conditions FromArguments(const QStringList &arguments) {
  Conditions conditions;
  conditions.requireAcPower = arguments.contains(kRequireAcFlag);
  conditions.requireUnmetered = arguments.contains(kRequireUnmeteredFlag);
  return conditions;
}

QStringList ToArguments(const Conditions &conditions) {
  QStringList arguments;
  if (conditions.requireAcPower) {
    arguments << kRequireAcFlag;
  }
  if (conditions.requireUnmetered) {
    arguments << kRequireUnmeteredFlag;
  }
  return arguments;
}

QString SkipReason(const Conditions &conditions, PowerSource power,
                   Metering metering) {
  QStringList reasons;
  if (conditions.requireAcPower) {
    if (power == PowerSource::Battery) {
      reasons << "the computer is running on battery";
    } else if (power == PowerSource::Unknown) {
      reasons << "the app couldn't tell whether the computer is plugged in";
    }
  }
  if (conditions.requireUnmetered) {
    if (metering == Metering::Metered) {
      reasons << "the network connection is metered";
    } else if (metering == Metering::Unknown) {
      reasons << "the app couldn't tell whether the network is metered";
    }
  }
  return reasons.join(" and ");
}

PowerSource PowerSourceFromPmset(const QString &output) {
  static const QRegularExpression drawing("Now drawing from '([^']+)'");
  const QRegularExpressionMatch match = drawing.match(output);
  if (!match.hasMatch()) {
    return PowerSource::Unknown;
  }
  const QString source = match.captured(1);
  if (source == "AC Power") {
    return PowerSource::Ac;
  }
  // A UPS takes over when the wall power is gone, so it counts as battery.
  if (source == "Battery Power" || source == "UPS Power") {
    return PowerSource::Battery;
  }
  return PowerSource::Unknown;
}

PowerSource PowerSourceFromSupplies(const QList<Supply> &supplies) {
  bool sawBattery = false;
  bool sawOfflineSupply = false;
  for (const Supply &supply : supplies) {
    // A wireless mouse or headset reports its own battery with scope
    // "Device". It says nothing about what powers the computer.
    if (supply.scope.compare("Device", Qt::CaseInsensitive) == 0) {
      continue;
    }
    if (supply.type == "Battery") {
      sawBattery = true;
    } else if (supply.online == "1") {
      return PowerSource::Ac;
    } else {
      sawOfflineSupply = true;
    }
  }
  if (sawBattery) {
    return PowerSource::Battery;
  }
  // A desktop usually lists no supply at all, and it runs off the wall.
  return sawOfflineSupply ? PowerSource::Unknown : PowerSource::Ac;
}

PowerSource CurrentPowerSource() {
#if defined(Q_OS_WIN)
  SYSTEM_POWER_STATUS status;
  if (!GetSystemPowerStatus(&status)) {
    return PowerSource::Unknown;
  }
  if (status.ACLineStatus == 1) {
    return PowerSource::Ac;
  }
  if (status.ACLineStatus == 0) {
    return PowerSource::Battery;
  }
  return PowerSource::Unknown;
#elif defined(Q_OS_MACOS)
  QProcess pmset;
  pmset.start("/usr/bin/pmset", {"-g", "ps"});
  if (!pmset.waitForFinished(5000) || pmset.exitCode() != 0) {
    return PowerSource::Unknown;
  }
  return PowerSourceFromPmset(
      QString::fromUtf8(pmset.readAllStandardOutput()));
#elif defined(Q_OS_LINUX)
  const QDir dir("/sys/class/power_supply");
  QList<Supply> supplies;
  for (const QString &name :
       dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const auto read = [&dir, &name](const QString &attribute) {
      QFile file(dir.filePath(name + "/" + attribute));
      if (!file.open(QIODevice::ReadOnly)) {
        return QString();
      }
      return QString::fromUtf8(file.readAll()).trimmed();
    };
    supplies.append({read("type"), read("online"), read("scope")});
  }
  return PowerSourceFromSupplies(supplies);
#else
  return PowerSource::Unknown;
#endif
}

bool CanCheckMetering() {
  return QNetworkInformation::loadBackendByFeatures(
      QNetworkInformation::Feature::Metered);
}

Metering CurrentMetering() {
  if (!CanCheckMetering()) {
    return Metering::Unknown;
  }
  const QNetworkInformation *info = QNetworkInformation::instance();
  if (!info) {
    return Metering::Unknown;
  }
  // Some backends deliver their first report through the event loop.
  const QDeadlineTimer deadline(2000);
  while (info->reachability() == QNetworkInformation::Reachability::Unknown &&
         !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  if (info->reachability() == QNetworkInformation::Reachability::Unknown) {
    return Metering::Unknown;
  }
  return info->isMetered() ? Metering::Metered : Metering::Unmetered;
}

QString SkipReasonNow(const Conditions &conditions) {
  const PowerSource power = conditions.requireAcPower ? CurrentPowerSource()
                                                      : PowerSource::Unknown;
  const Metering metering =
      conditions.requireUnmetered ? CurrentMetering() : Metering::Unknown;
  return SkipReason(conditions, power, metering);
}

} // namespace RunConditions
