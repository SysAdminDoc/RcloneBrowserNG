#if defined(_WIN32)
// GetIfTable2 needs winsock2 ahead of windows.h, which pch.h pulls in.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#endif

#include "adaptive_bandwidth.h"

#if defined(Q_OS_MACOS)
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#endif

namespace AdaptiveBandwidth {

QList<InterfaceCounter> ParseProcNetDev(const QString &text) {
  QList<InterfaceCounter> counters;
  const QStringList lines = text.split('\n');
  for (const QString &line : lines) {
    // The two header lines have no colon. A long counter can run straight
    // into the name ("eth0:123456"), so split on the colon, not on spaces.
    const qsizetype colon = line.indexOf(':');
    if (colon < 0) {
      continue;
    }
    const QStringList fields =
        line.mid(colon + 1).split(' ', Qt::SkipEmptyParts);
    if (fields.size() < 9) {
      continue;
    }
    bool receivedOk = false;
    bool sentOk = false;
    InterfaceCounter counter;
    counter.name = line.left(colon).trimmed();
    counter.received = fields.at(0).toULongLong(&receivedOk);
    counter.sent = fields.at(8).toULongLong(&sentOk);
    if (receivedOk && sentOk && !counter.name.isEmpty()) {
      counters.append(counter);
    }
  }
  return counters;
}

QList<InterfaceCounter> ReadInterfaceCounters(bool *ok) {
  QList<InterfaceCounter> counters;
  bool read = false;
#if defined(Q_OS_WIN)
  MIB_IF_TABLE2 *table = nullptr;
  if (GetIfTable2(&table) == NO_ERROR && table) {
    read = true;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
      const MIB_IF_ROW2 &row = table->Table[i];
      // Each network card also shows up once per filter driver bound to it,
      // with the same counters, and VPN adapters carry traffic that the card
      // carries again. Only the card itself is counted.
      if (!row.InterfaceAndOperStatusFlags.HardwareInterface ||
          row.InterfaceAndOperStatusFlags.FilterInterface ||
          row.Type == IF_TYPE_SOFTWARE_LOOPBACK ||
          row.OperStatus != IfOperStatusUp) {
        continue;
      }
      InterfaceCounter counter;
      counter.name = QString::number(row.InterfaceLuid.Value);
      counter.received = row.InOctets;
      counter.sent = row.OutOctets;
      counters.append(counter);
    }
    FreeMibTable(table);
  }
#elif defined(Q_OS_MACOS)
  ifaddrs *addresses = nullptr;
  if (getifaddrs(&addresses) == 0) {
    read = true;
    for (ifaddrs *a = addresses; a; a = a->ifa_next) {
      if (!a->ifa_addr || a->ifa_addr->sa_family != AF_LINK || !a->ifa_data ||
          (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP)) {
        continue;
      }
      // Ethernet and Wi-Fi are en0, en1 and so on. utun, ipsec, awdl and
      // bridge interfaces carry traffic that an en interface carries too.
      const QString name = QString::fromLocal8Bit(a->ifa_name);
      if (!name.startsWith("en")) {
        continue;
      }
      const auto *data = static_cast<const if_data *>(a->ifa_data);
      InterfaceCounter counter;
      counter.name = name;
      counter.received = data->ifi_ibytes;
      counter.sent = data->ifi_obytes;
      counter.wraps32 = true;
      counters.append(counter);
    }
    freeifaddrs(addresses);
  }
#elif defined(Q_OS_LINUX)
  QFile file("/proc/net/dev");
  if (file.open(QIODevice::ReadOnly)) {
    read = true;
    const QList<InterfaceCounter> all =
        ParseProcNetDev(QString::fromUtf8(file.readAll()));
    for (const InterfaceCounter &counter : all) {
      // A physical card has a device link. lo, tun, wg, docker and veth
      // interfaces don't.
      if (QFileInfo::exists("/sys/class/net/" + counter.name + "/device")) {
        counters.append(counter);
      }
    }
  }
#endif
  if (ok) {
    *ok = read;
  }
  return counters;
}

namespace {
quint64 grewBy(quint64 before, quint64 after, bool wraps32) {
  if (after >= before) {
    return after - before;
  }
  if (wraps32 && before <= 0xFFFFFFFFull) {
    return (0x100000000ull - before) + after;
  }
  // The counter reset, for instance when the card was re-plugged.
  return 0;
}
} // namespace

quint64 BytesBetween(const QList<InterfaceCounter> &before,
                     const QList<InterfaceCounter> &after) {
  QHash<QString, InterfaceCounter> earlier;
  for (const InterfaceCounter &counter : before) {
    earlier.insert(counter.name, counter);
  }
  quint64 total = 0;
  for (const InterfaceCounter &counter : after) {
    const auto it = earlier.constFind(counter.name);
    if (it == earlier.constEnd()) {
      continue;
    }
    total += grewBy(it->received, counter.received, counter.wraps32);
    total += grewBy(it->sent, counter.sent, counter.wraps32);
  }
  return total;
}

Controller::Action Controller::sample(quint64 interfaceBytes,
                                      quint64 rcloneBytes, qint64 msecs) {
  if (msecs <= 0) {
    return Action::None;
  }
  const double seconds = static_cast<double>(msecs) / 1000.0;
  const double rcloneOnWire =
      static_cast<double>(rcloneBytes) * (1.0 + kOverhead);
  const double onWire = static_cast<double>(interfaceBytes);
  mOtherBytesPerSecond =
      onWire > rcloneOnWire ? (onWire - rcloneOnWire) / seconds : 0.0;

  if (mOtherBytesPerSecond > kQuietBytesPerSecond) {
    ++mBusyReadings;
    mQuietReadings = 0;
  } else {
    ++mQuietReadings;
    mBusyReadings = 0;
  }

  if (!mThrottled && mBusyReadings >= kBusyReadingsToThrottle) {
    mThrottled = true;
    return Action::Throttle;
  }
  if (mThrottled && mQuietReadings >= kQuietReadingsToRelease) {
    mThrottled = false;
    return Action::Release;
  }
  return Action::None;
}

void Controller::reset() {
  mThrottled = false;
  mBusyReadings = 0;
  mQuietReadings = 0;
  mOtherBytesPerSecond = 0;
}

} // namespace AdaptiveBandwidth
