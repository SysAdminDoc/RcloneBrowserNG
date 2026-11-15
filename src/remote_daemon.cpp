#include "remote_daemon.h"
#include "job_options_store.h"
#include "utils.h"

namespace {
const char *const kEnabledKey = "Settings/remoteDaemon";
const char *const kUrlKey = "Settings/remoteDaemonUrl";
const char *const kUserKey = "Settings/remoteDaemonUser";
const char *const kPasswordKey = "Settings/remoteDaemonPassword";
const char *const kCertificateKey = "Settings/remoteDaemonCertificateSha256";

QNetworkAccessManager *manager() {
  static QPointer<QNetworkAccessManager> instance;
  if (instance.isNull()) {
    instance = new QNetworkAccessManager(QCoreApplication::instance());
  }
  return instance;
}

// "gdrive:path" becomes "gdrive,shared_with_me:path", the connection string
// form of a backend flag.
QString withConnectionParameter(const QString &target,
                                const QString &parameter) {
  if (target.startsWith(QChar(':'))) {
    return target; // an on-the-fly backend, never a Drive remote
  }
  const qsizetype colon = target.indexOf(QChar(':'));
  if (colon <= 0) {
    return target;
  }
  return target.left(colon) + QChar(',') + parameter + target.mid(colon);
}

// "dir/a.txt" gives ("dir", "a.txt"), "a.txt" gives ("", "a.txt"), and the
// root keeps its slash: "/a.txt" gives ("/", "a.txt"), "C:/a.txt" ("C:/", ...).
QPair<QString, QString> splitParent(const QString &path) {
  QString trimmed = path;
  while (trimmed.size() > 1 && trimmed.endsWith(QChar('/'))) {
    trimmed.chop(1);
  }
  const qsizetype slash = trimmed.lastIndexOf(QChar('/'));
  if (slash < 0) {
    return {QString(), trimmed};
  }
  QString parent = trimmed.left(slash);
  if (parent.isEmpty() || parent.endsWith(QChar(':'))) {
    parent = trimmed.left(slash + 1);
  }
  return {parent, trimmed.mid(slash + 1)};
}
} // namespace

namespace RemoteDaemon {

Settings Load() {
  auto settings = GetSettings();
  Settings loaded;
  loaded.url = NormalizeUrl(settings->value(kUrlKey).toString());
  loaded.user = settings->value(kUserKey).toString();
  loaded.password = UnprotectJobOptionSensitiveValue(
      settings->value(kPasswordKey).toString());
  loaded.certificateSha256 =
      RcCertificate::Normalize(settings->value(kCertificateKey).toString());
  loaded.enabled =
      settings->value(kEnabledKey, false).toBool() && !loaded.url.isEmpty();
  return loaded;
}

void Save(const Settings &value) {
  auto settings = GetSettings();
  settings->setValue(kEnabledKey, value.enabled);
  settings->setValue(kUrlKey, value.url.trimmed());
  settings->setValue(kUserKey, value.user);
  settings->setValue(kPasswordKey,
                     ProtectJobOptionSensitiveValue(value.password));
  settings->setValue(kCertificateKey,
                     RcCertificate::Normalize(value.certificateSha256));
}

bool IsEnabled() {
  // Drag and drop asks this on every mouse move, so it skips decrypting the
  // password that Load() does.
  auto settings = GetSettings();
  return settings->value(kEnabledKey, false).toBool() &&
         !NormalizeUrl(settings->value(kUrlKey).toString()).isEmpty();
}

QString NormalizeUrl(const QString &url) {
  QString text = url.trimmed();
  if (text.isEmpty()) {
    return QString();
  }
  if (!text.contains(QStringLiteral("://"))) {
    text.prepend(QStringLiteral("http://"));
  }
  const QUrl parsed(text, QUrl::StrictMode);
  if (!parsed.isValid() || parsed.host().isEmpty() ||
      (parsed.scheme() != QLatin1String("http") &&
       parsed.scheme() != QLatin1String("https"))) {
    return QString();
  }
  if (!text.endsWith(QChar('/'))) {
    text.append(QChar('/'));
  }
  return text;
}

bool IsPlainHttpToNetwork(const QString &url) {
  const QString normalized = NormalizeUrl(url);
  if (normalized.isEmpty()) {
    return false;
  }
  const QUrl parsed(normalized);
  if (parsed.scheme() != QLatin1String("http")) {
    return false;
  }
  // QUrl lowercases the host and drops an IPv6 address's brackets.
  const QString host = parsed.host();
  if (host == QLatin1String("localhost")) {
    return false;
  }
  const QHostAddress address(host);
  return address.isNull() || !address.isLoopback();
}

QString PlainHttpWarning(const QString &url) {
  if (!IsPlainHttpToNetwork(url)) {
    return QString();
  }
  return QStringLiteral(
      "This address is plain http, so the user name, the password and "
      "everything you browse cross the network unencrypted. Anyone who "
      "picks up that login can run commands on that computer. Start rclone "
      "with --rc-cert and --rc-key and use https, or reach it through an SSH "
      "tunnel at http://localhost.");
}

void Call(const Settings &settings, const QString &endpoint,
          const QJsonObject &payload, QObject *context,
          const Callback &callback, int timeoutMs) {
  const bool tracksContext = context != nullptr;
  const QPointer<QObject> contextGuard(context);
  auto deliver = [tracksContext, contextGuard,
                  callback](const QJsonObject &result, const QString &error) {
    if (!callback || (tracksContext && contextGuard.isNull())) {
      return;
    }
    callback(result, error);
  };

  if (settings.url.isEmpty()) {
    QTimer::singleShot(0, manager(), [deliver]() {
      deliver(QJsonObject(),
              QStringLiteral("No address is set for the remote rclone."));
    });
    return;
  }

  QNetworkRequest request(QUrl(settings.url + endpoint));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  if (!settings.user.isEmpty() || !settings.password.isEmpty()) {
    request.setRawHeader(
        "Authorization",
        "Basic " +
            (settings.user + ":" + settings.password).toUtf8().toBase64());
  }
  RcCertificate::Prepare(request, settings.certificateSha256);
  QNetworkReply *reply = manager()->post(
      request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

  const QString trusted = settings.certificateSha256;
  RcCertificate::Guard(reply, trusted);

  if (timeoutMs > 0) {
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(timeoutMs);
  }

  const QString url = settings.url;
  QObject::connect(reply, &QNetworkReply::finished, manager(),
                   [reply, deliver, url, trusted]() {
    reply->deleteLater();
    const QByteArray raw = reply->readAll();
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Before the timeout case, since refusing a certificate aborts too.
    QString presented;
    const QString certificateError =
        RcCertificate::Problem(reply, url, trusted, &presented);
    if (!certificateError.isEmpty()) {
      QJsonObject result;
      if (!presented.isEmpty()) {
        result.insert(kPresentedCertificateKey, presented);
      }
      deliver(result, certificateError);
      return;
    }
    if (reply->error() == QNetworkReply::OperationCanceledError) {
      deliver(QJsonObject(),
              QString("The remote rclone at %1 didn't answer in time.")
                  .arg(url));
      return;
    }
    if (status == 401) {
      deliver(QJsonObject(),
              QString("The remote rclone at %1 didn't accept the user name "
                      "and password.")
                  .arg(url));
      return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
      const QString text = QString::fromUtf8(raw).trimmed();
      deliver(QJsonObject(), text.isEmpty() ? reply->errorString() : text);
      return;
    }
    const QJsonObject object = doc.object();
    if (object.contains("error")) {
      deliver(object, object.value("error").toString());
      return;
    }
    if (reply->error() != QNetworkReply::NoError) {
      deliver(object, reply->errorString());
      return;
    }
    deliver(object, QString());
  });
}

void Call(const QString &endpoint, const QJsonObject &payload,
          QObject *context, const Callback &callback, int timeoutMs) {
  Call(Load(), endpoint, payload, context, callback, timeoutMs);
}

QJsonArray CommandArguments(const QStringList &args, const QString &config) {
  QJsonArray arg;
  if (!config.isEmpty() && !args.contains(QStringLiteral("--config"))) {
    arg.append(QStringLiteral("--config"));
    arg.append(config);
  }
  for (qsizetype i = 1; i < args.size(); ++i) {
    arg.append(args.at(i));
  }
  return arg;
}

void RunCommand(const QStringList &args, QObject *context,
                const CommandCallback &callback) {
  const Settings settings = Load();
  Call(
      settings, "config/paths", QJsonObject(), context,
      [settings, args, context, callback](const QJsonObject &paths,
                                          const QString &pathsError) {
        if (!pathsError.isEmpty() && paths.isEmpty()) {
          if (callback) {
            callback(1, pathsError.toUtf8());
          }
          return;
        }
        QJsonObject payload;
        payload.insert("command", args.value(0));
        payload.insert("arg",
                       CommandArguments(args, paths.value("config").toString()));
        payload.insert("returnType", "COMBINED_OUTPUT");
        Call(
            settings, "core/command", payload, context,
            [callback](const QJsonObject &result, const QString &error) {
              if (!callback) {
                return;
              }
              // rclone exiting non-zero is a 200 with "error": true and the
              // output in "result". A string error is the daemon refusing.
              const QByteArray output =
                  result.value("result").toString().toUtf8();
              if (!error.isEmpty()) {
                callback(1, output.isEmpty() ? error.toUtf8() : output);
                return;
              }
              callback(result.value("error").toBool() ? 1 : 0, output);
            },
            0);
      });
}

QJsonObject ListPayload(const QString &target, bool showHidden,
                        bool driveShared, bool recursiveFilesOnly) {
  QJsonObject opt;
  opt.insert("noMimeType", true);
  if (recursiveFilesOnly) {
    opt.insert("recurse", true);
    opt.insert("filesOnly", true);
  }

  QJsonObject payload;
  payload.insert("fs", driveShared
                           ? withConnectionParameter(target, "shared_with_me")
                           : target);
  payload.insert("remote", QString());
  payload.insert("opt", opt);
  if (!showHidden) {
    // The same two rules GetShowHidden() passes on the command line. Probed
    // against rclone v1.75.1: this flat shape is the one operations/list
    // honours.
    QJsonObject filter;
    filter.insert("ExcludeRule",
                  QJsonArray{QStringLiteral(".*/**"), QStringLiteral(".*")});
    payload.insert("_filter", filter);
  }
  return payload;
}

Request Mkdir(const QString &remote, const QString &folder) {
  Request request;
  request.endpoint = QStringLiteral("operations/mkdir");
  request.payload.insert("fs", remote + ":" + folder);
  request.payload.insert("remote", QString());
  return request;
}

Request MoveTo(const QString &remote, const QString &from, const QString &to,
               bool isFolder) {
  Request request;
  if (isFolder) {
    request.endpoint = QStringLiteral("sync/move");
    request.payload.insert("srcFs", remote + ":" + from);
    request.payload.insert("dstFs", remote + ":" + to);
    request.payload.insert("deleteEmptySrcDirs", true);
    return request;
  }
  const auto source = splitParent(from);
  const auto destination = splitParent(to);
  request.endpoint = QStringLiteral("operations/movefile");
  request.payload.insert("srcFs", remote + ":" + source.first);
  request.payload.insert("srcRemote", source.second);
  request.payload.insert("dstFs", remote + ":" + destination.first);
  request.payload.insert("dstRemote", destination.second);
  return request;
}

QVector<RemoteListParser::Remote> RemotesFromDump(const QJsonObject &dump) {
  QVector<RemoteListParser::Remote> remotes;
  // QJsonObject iterates its keys in sorted order.
  for (auto it = dump.begin(); it != dump.end(); ++it) {
    const QJsonObject section = it.value().toObject();
    RemoteListParser::Remote remote;
    remote.name = it.key();
    remote.type = section.value("type").toString();
    remote.description = section.value("description").toString();
    remote.source = QStringLiteral("file");
    remotes.append(remote);
  }
  return remotes;
}

QString DescribeVersion(const QJsonObject &version, const QString &url) {
  QString line = QString("Remote rclone %1 at %2")
                     .arg(version.value("version").toString(), url);
  const QString os = version.value("os").toString();
  const QString arch = version.value("arch").toString();
  if (!os.isEmpty()) {
    line += QString(" (%1%2)").arg(os, arch.isEmpty() ? QString()
                                                       : ", " + arch);
  }
  return line;
}

QString LocalOnlyReason() {
  return QStringLiteral("Not available with a remote rclone. This one needs "
                        "rclone on this computer.");
}

} // namespace RemoteDaemon
