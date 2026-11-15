#pragma once

#include "pch.h"

// Adaptive bandwidth: hold rclone to a floor rate while other apps use the
// network, and let it back up to the ceiling once they stop. Other traffic is
// whatever crosses the computer's network interfaces beyond what rclone
// itself moved in the same interval.
namespace AdaptiveBandwidth {

struct InterfaceCounter {
  QString name;
  quint64 received = 0;
  quint64 sent = 0;
  // macOS keeps 32-bit counters that wrap every 4 GiB.
  bool wraps32 = false;
};

// Linux: the text of /proc/net/dev, every interface in it.
QList<InterfaceCounter> ParseProcNetDev(const QString &text);

// The byte counters of the computer's physical network interfaces. Loopback,
// VPN tunnels and other virtual adapters are left out, so a transfer through
// a VPN isn't counted twice. ok is false where the platform can't say.
QList<InterfaceCounter> ReadInterfaceCounters(bool *ok);

// Bytes received plus sent between two readings, matched by interface name.
// An interface that appeared, vanished or reset in between counts as zero.
quint64 BytesBetween(const QList<InterfaceCounter> &before,
                     const QList<InterfaceCounter> &after);

class Controller {
public:
  enum class Action { None, Throttle, Release };

  // Other traffic below this rate is background noise, not another app.
  static constexpr double kQuietBytesPerSecond = 128.0 * 1024.0;
  // TLS, HTTP and TCP add a few percent on the wire to what rclone counts.
  static constexpr double kOverhead = 0.10;
  // Readings in a row before acting, so one burst doesn't flip the limit.
  static constexpr int kBusyReadingsToThrottle = 2;
  static constexpr int kQuietReadingsToRelease = 5;

  // interfaceBytes crossed the network and rcloneBytes were moved by rclone
  // during the same msecs. Returns what to do with the limit, if anything.
  Action sample(quint64 interfaceBytes, quint64 rcloneBytes, qint64 msecs);

  bool throttled() const { return mThrottled; }
  double otherBytesPerSecond() const { return mOtherBytesPerSecond; }
  void reset();

private:
  bool mThrottled = false;
  int mBusyReadings = 0;
  int mQuietReadings = 0;
  double mOtherBytesPerSecond = 0;
};

} // namespace AdaptiveBandwidth
