#pragma once

#include "pch.h"
#include "rc_certificate.h"
#include "remote_list_parser.h"
#include <functional>

// Browsing and transferring through an rclone rcd that runs on another
// machine, a NAS or a server say, instead of the daemon the app starts for
// itself. The remotes, the files and every transfer then live on that
// machine: "local" paths are its disks, not this computer's.
namespace RemoteDaemon {

struct Settings {
  bool enabled = false;
  QString url;
  QString user;
  QString password;
  // The SHA-256 fingerprint of a TLS certificate the user chose to trust, in
  // RcCertificate::Normalize's form. A daemon with any other certificate is
  // refused, even one this computer trusts. Empty means only certificates
  // this computer already trusts are accepted.
  QString certificateSha256;
};

// Reads the Remote rclone settings. enabled is only true when an address is
// set too.
Settings Load();
// The password is stored the way saved task secrets are (DPAPI on Windows).
void Save(const Settings &settings);
bool IsEnabled();

// "nas:5572" becomes "http://nas:5572/". Returns an empty string for
// something that isn't an http(s) address with a host.
QString NormalizeUrl(const QString &url);

// True for an http:// address (or one with no scheme) whose host isn't this
// computer. The login then crosses the network in the clear on every request,
// and rclone's rc login is as good as a shell on that machine.
bool IsPlainHttpToNetwork(const QString &url);
// The Preferences warning for such an address, empty for any other.
QString PlainHttpWarning(const QString &url);

// A Call() refused over the daemon's certificate puts the certificate's
// fingerprint (RcCertificate's form) in its result under this key, so
// Preferences can offer to trust it.
inline constexpr char kPresentedCertificateKey[] = "presentedCertificateSha256";

using Callback =
    std::function<void(const QJsonObject &result, const QString &error)>;

// Posts payload to endpoint on the daemon in settings and calls back on the
// event loop. A context that goes away first means no call back. timeoutMs 0
// waits for as long as the call takes, which a folder move can need.
void Call(const Settings &settings, const QString &endpoint,
          const QJsonObject &payload, QObject *context,
          const Callback &callback, int timeoutMs = 30000);
// The same, with the saved settings.
void Call(const QString &endpoint, const QJsonObject &payload,
          QObject *context, const Callback &callback, int timeoutMs = 30000);

using CommandCallback =
    std::function<void(int exitCode, const QByteArray &output)>;

// Runs an rclone command line (args start with the command, "sync" say) on
// the remote rclone's machine and hands back its combined output, the way a
// local QProcess would. The daemon starts that rclone with only these
// arguments, so its config file is added from config/paths. A failure to
// reach the daemon comes back as exit code 1 with the reason as output.
void RunCommand(const QStringList &args, QObject *context,
                const CommandCallback &callback);

// The core/command arg list for args, with the daemon's config file put in
// front unless args already name one.
QJsonArray CommandArguments(const QStringList &args, const QString &config);

// operations/list for one folder. target is what the CLI would be given,
// "remote:path" or ":local:/path". driveShared is --drive-shared-with-me,
// which only exists as a connection string parameter over the API.
QJsonObject ListPayload(const QString &target, bool showHidden,
                        bool driveShared, bool recursiveFilesOnly);

struct Request {
  QString endpoint;
  QJsonObject payload;
};

// `rclone mkdir remote:folder` over the API. The whole path goes in fs,
// because ":local:" with an absolute remote path makes rclone create the
// folder relative to the daemon's working directory instead.
Request Mkdir(const QString &remote, const QString &folder);

// `rclone moveto remote:from remote:to` over the API: one operations/movefile
// for a file, a sync/move that removes the emptied source for a folder.
Request MoveTo(const QString &remote, const QString &from, const QString &to,
               bool isFolder);

// The remotes in a config/dump answer, sorted by name.
QVector<RemoteListParser::Remote> RemotesFromDump(const QJsonObject &dump);

// One status bar line for a core/version answer.
QString DescribeVersion(const QJsonObject &version, const QString &url);

// Tooltip for an action that needs rclone on this computer.
QString LocalOnlyReason();

} // namespace RemoteDaemon
