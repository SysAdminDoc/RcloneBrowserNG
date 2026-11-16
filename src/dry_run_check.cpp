#include "dry_run_check.h"
#include "rc_sync_request.h"
#include "remote_daemon.h"
#include "utils.h"

namespace DryRunCheck {

namespace {

// rclone config dump answers at once. A dump that hangs is treated as one
// that couldn't be read, so the dry run doesn't start.
constexpr int kDumpTimeoutMs = 60000;

} // namespace

QString RefusalFor(int exitCode, const QByteArray &output,
                   const QStringList &environment) {
  QJsonObject dump;
  if (exitCode != 0 || !RcSyncRequest::ParseConfigDump(output, &dump)) {
    QString why =
        QString::fromUtf8(output).trimmed().section(QChar('\n'), -1);
    if (why.isEmpty()) {
      why = QString("rclone exited with %1").arg(exitCode);
    }
    return QString("rclone.conf couldn't be read (%1) to check for a "
                   "global.dry_run that would make this dry run a real "
                   "run. Nothing was run.")
        .arg(why);
  }
  const QString setter = RcSyncRequest::DryRunGlobalReason(dump, environment);
  if (setter.isEmpty()) {
    return QString();
  }
  return QString("%1 would turn --dry-run off and make this dry run a real "
                 "run. Nothing was run.")
      .arg(setter);
}

void Run(const QStringList &args, const QProcessEnvironment &environment,
         QObject *context, const Callback &callback) {
  if (!RcSyncRequest::IsDryRun(args)) {
    if (callback) {
      callback(QString());
    }
    return;
  }
  const QStringList dumpArgs = RcSyncRequest::ConfigDumpArguments(args);
  if (RemoteDaemon::IsEnabled()) {
    // The remote rclone's environment isn't known here.
    RemoteDaemon::RunCommand(
        dumpArgs, context,
        [callback](int code, const QByteArray &output) {
          if (callback) {
            callback(RefusalFor(code, output, QStringList()));
          }
        });
    return;
  }

  const QStringList checked =
      (environment.isEmpty() ? QProcessEnvironment::systemEnvironment()
                             : environment)
          .toStringList();
  auto *dump = new QProcess(context);
  UseRclonePassword(dump);
  auto *timer = new QTimer(dump);
  timer->setSingleShot(true);
  QObject::connect(timer, &QTimer::timeout, dump, [dump]() { dump->kill(); });
  QObject::connect(dump, &QProcess::errorOccurred, context,
                   [dump, callback](QProcess::ProcessError error) {
                     if (error != QProcess::FailedToStart) {
                       return;
                     }
                     dump->deleteLater();
                     if (callback) {
                       callback(RefusalFor(-1, dump->errorString().toUtf8(),
                                           QStringList()));
                     }
                   });
  QObject::connect(
      dump,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      context,
      [dump, checked, callback](int code, QProcess::ExitStatus status) {
        const QByteArray out = dump->readAllStandardOutput();
        const QByteArray err = dump->readAllStandardError();
        dump->deleteLater();
        if (!callback) {
          return;
        }
        if (status != QProcess::NormalExit) {
          callback(RefusalFor(-1, "rclone config dump didn't finish",
                              checked));
          return;
        }
        // A failed dump says why on stderr.
        callback(RefusalFor(code, code == 0 || err.trimmed().isEmpty() ? out
                                                                       : err,
                            checked));
      });
  timer->start(kDumpTimeoutMs);
  dump->start(GetRclone(), GetRcloneConf() + dumpArgs, QIODevice::ReadOnly);
}

} // namespace DryRunCheck
