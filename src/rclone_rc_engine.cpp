#include "rclone_rc_engine.h"
#include "utils.h"

namespace {
constexpr int kRcStartTimeoutMs = 5000;
constexpr int kRcRequestTimeoutMs = 10000;

QString makeRcUrl(quint16 port) {
  return QString("http://localhost:%1/").arg(port);
}
} // namespace

RcloneRcEngine::RcloneRcEngine(QObject *parent) : QObject(parent) {}

RcloneRcEngine::~RcloneRcEngine() {
  if (mProcess && mProcess->state() != QProcess::NotRunning) {
    QString ignored;
    post("core/quit", QJsonObject(), &ignored);
    if (!mProcess->waitForFinished(3000)) {
      mProcess->kill();
      mProcess->waitForFinished();
    }
  }
}

bool RcloneRcEngine::ensureStarted(QString *error) {
  if (mProcess && mProcess->state() != QProcess::NotRunning) {
    QString pingError;
    post("rc/noopauth", QJsonObject(), &pingError);
    if (pingError.isEmpty()) {
      return true;
    }
  }

  if (mProcess) {
    mProcess->deleteLater();
    mProcess = nullptr;
  }

  const quint16 port = static_cast<quint16>(
      29000 + (QRandomGenerator::system()->bounded(20000)));
  mUrl = makeRcUrl(port);
  mUser = "rclonebrowser";
  mPass = MakeRcPassword();

  mProcess = new QProcess(this);
  mProcess->setProcessChannelMode(QProcess::MergedChannels);
  QStringList args;
  args << "rcd" << GetRcloneConf() << "--rc-addr"
       << QString("localhost:%1").arg(port) << "--rc-user" << mUser
       << "--rc-pass" << mPass << "--rc-job-expire-duration" << "1h";
  UseRclonePassword(mProcess);
  mProcess->start(GetRclone(), args, QIODevice::ReadOnly);
  if (!mProcess->waitForStarted(kRcStartTimeoutMs)) {
    if (error) {
      *error = "failed to start rclone rcd";
    }
    return false;
  }

  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < kRcStartTimeoutMs) {
    QString pingError;
    post("rc/noopauth", QJsonObject(), &pingError);
    if (pingError.isEmpty()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QThread::msleep(50);
  }

  if (error) {
    const QString output = QString::fromUtf8(mProcess->readAll()).trimmed();
    *error = output.isEmpty() ? "rclone rcd did not become ready" : output;
  }
  return false;
}

int RcloneRcEngine::runCommandAsync(const QStringList &args, QString *error) {
  if (args.isEmpty()) {
    if (error) {
      *error = "empty rclone command";
    }
    return -1;
  }

  if (!ensureStarted(error)) {
    return -1;
  }

  QJsonArray commandArgs;
  for (int i = 1; i < args.size(); i++) {
    commandArgs.append(args[i]);
  }

  QJsonObject payload;
  payload.insert("command", args.first());
  payload.insert("arg", commandArgs);
  payload.insert("returnType", "COMBINED_OUTPUT");
  payload.insert("_async", true);

  const QJsonObject response = post("core/command", payload, error);
  if (response.contains("jobid")) {
    return response.value("jobid").toInt(-1);
  }
  if (error && error->isEmpty()) {
    *error = "rclone rc did not return a job id";
  }
  return -1;
}

QJsonObject RcloneRcEngine::jobStatus(int jobId, QString *error) {
  QJsonObject payload;
  payload.insert("jobid", jobId);
  return post("job/status", payload, error);
}

QJsonObject RcloneRcEngine::coreStats(const QString &group, QString *error) {
  QJsonObject payload;
  if (!group.isEmpty()) {
    payload.insert("group", group);
  }
  return post("core/stats", payload, error);
}

bool RcloneRcEngine::stopJob(int jobId, QString *error) {
  QJsonObject payload;
  payload.insert("jobid", jobId);
  post("job/stop", payload, error);
  return !error || error->isEmpty();
}

QStringList RcloneRcEngine::rcCommandForDisplay(const QStringList &args) const {
  QJsonArray commandArgs;
  for (int i = 1; i < args.size(); i++) {
    commandArgs.append(args[i]);
  }

  QJsonObject payload;
  if (!args.isEmpty()) {
    payload.insert("command", args.first());
  }
  payload.insert("arg", commandArgs);
  payload.insert("returnType", "COMBINED_OUTPUT");
  payload.insert("_async", true);

  return QStringList() << QDir::toNativeSeparators(GetRclone()) << "rc"
                       << "--url" << mUrl << "--user" << mUser << "--pass"
                       << "<redacted>" << "--json"
                       << QString::fromUtf8(QJsonDocument(payload).toJson(
                              QJsonDocument::Compact))
                       << "core/command";
}

QJsonObject RcloneRcEngine::post(const QString &path, const QJsonObject &payload,
                                 QString *error) {
  if (error) {
    error->clear();
  }

  QNetworkRequest req = request(path);
  QNetworkReply *reply =
      mNetwork.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));

  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(kRcRequestTimeoutMs);
  loop.exec();

  if (!timer.isActive()) {
    reply->abort();
    reply->deleteLater();
    if (error) {
      *error = "rclone rc request timed out";
    }
    return QJsonObject();
  }

  const QByteArray raw = reply->readAll();
  const QNetworkReply::NetworkError networkError = reply->error();
  const QString networkErrorText = reply->errorString();
  reply->deleteLater();

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) {
      *error = raw.isEmpty() ? networkErrorText : QString::fromUtf8(raw);
    }
    return QJsonObject();
  }

  const QJsonObject object = doc.object();
  if (networkError != QNetworkReply::NoError || object.contains("error")) {
    if (error) {
      *error = object.value("error").toString(networkErrorText);
    }
    return object;
  }

  return object;
}

QNetworkRequest RcloneRcEngine::request(const QString &path) const {
  QUrl url(mUrl + path);
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  const QByteArray credential =
      QString("%1:%2").arg(mUser, mPass).toUtf8().toBase64();
  req.setRawHeader("Authorization", "Basic " + credential);
  return req;
}
