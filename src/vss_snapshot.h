#pragma once

#include "pch.h"
#include <functional>

// Reads a Windows source through a Volume Shadow Copy, so files another
// program holds open (a mail store, a database, a VM disk) still copy.
//
// Making a snapshot needs an administrator, and the app runs as the user, so
// the app starts an elevated copy of itself with --vss-helper. The helper
// snapshots the volume, links the snapshot into a session folder and waits.
// The transfer reads the source through that link. When the job's card
// finishes the app drops a release file and the helper deletes the snapshot.
// If the app goes away first the helper notices and cleans up anyway, and a
// later helper deletes anything a crash left behind.
namespace VssSnapshot {

inline constexpr char kHelperFlag[] = "--vss-helper";

// A drive-letter path such as C:\Users\me or C:/data. rclone reads a single
// letter before the colon as a drive, never as a remote name.
bool IsLocalDrivePath(const QString &source);

// "C:\" for C:\Users\me.
QString VolumeRoot(const QString &source);

// The source as seen through a snapshot linked at linkPath:
// C:\Users\me with D:\tmp\s\snapshot gives D:/tmp/s/snapshot/Users/me.
QString PathInSnapshot(const QString &source, const QString &linkPath);

// args with the source swapped for newSource. JobOptions::getOptions puts
// the source second to last and the destination last.
QStringList WithSource(QStringList args, const QString &newSource);

// Why a snapshot can't be used for this source, or empty when it can. Checks
// only what doesn't need elevation: the platform, the path shape and the
// kind of drive.
QString UnavailableReason(const QString &source);

struct HelperRequest {
  QString sessionDir;
  QString volume;
  qint64 parentPid = 0;
};

QStringList HelperArguments(const HelperRequest &request);
// Reads what follows kHelperFlag in a full argument list.
bool ParseHelperArguments(const QStringList &arguments,
                          HelperRequest *request);

// The files a session folder holds. The helper writes ready (the shadow
// copy's id), error and released. The app writes release.
QString LinkPath(const QString &sessionDir);
QString ReadyFile(const QString &sessionDir);
QString ErrorFile(const QString &sessionDir);
QString ReleaseFile(const QString &sessionDir);
QString ReleasedFile(const QString &sessionDir);
QString HelperPidFile(const QString &sessionDir);

// Where session folders live.
QString SessionRoot();

// The elevated side. Returns the process exit code.
int RunHelper(const QStringList &arguments);

// Win32_ShadowCopy.Create codes that clear up on their own within seconds:
// another snapshot being made (9), and a provider calling the snapshot off
// (10), which Windows does when a busy drive keeps the shadow storage from
// growing in time.
bool RetryableCreateCode(long code);

// Calls attempt until it succeeds, fails with a code RetryableCreateCode
// turns down, or has run attempts times, with pause between tries. attempt
// sets *code to the Create code, 0 on success.
bool CreateWithRetry(const std::function<bool(long *code)> &attempt,
                     const std::function<void()> &pause, int attempts = 2);

// The program Begin() starts elevated: this app, unless a test points it
// at a build of the app from outside.
void SetHelperProgram(const QString &path);

} // namespace VssSnapshot

// One snapshot for one job. Begin() calls back with a session once the
// snapshot is linked and readable, or with an error and no session.
class VssSession : public QObject {
  Q_OBJECT

public:
  using ReadyCallback =
      std::function<void(VssSession *session, const QString &error)>;

  static void Begin(const QString &source, QObject *context,
                    const ReadyCallback &callback);

  QString pathFor(const QString &source) const;
  QString sessionDir() const { return mSessionDir; }

  // Ends the snapshot when owner is destroyed or emits a signal connected
  // to release(). The session becomes owner's child.
  void releaseWith(QObject *owner);

  ~VssSession() override;

public slots:
  void release();

private:
  explicit VssSession(QString sessionDir, QObject *parent = nullptr);

  QString mSessionDir;
  bool mReleased = false;
};
