#include "rc_delete_batch.h"

namespace RcDeleteBatch {

namespace {

// "--drive-use-trash" becomes backend "drive" and "use_trash=true", and
// "--onedrive-hard-delete=false" becomes "onedrive" and "hard_delete=false".
// Anything that could break the connection string syntax is refused rather
// than quoted, since none of the flags the app adds need it.
bool backendParam(const QString &flag, QString *backend, QString *param) {
  if (!flag.startsWith(QStringLiteral("--"))) {
    return false;
  }
  QString body = flag.mid(2);
  QString value = QStringLiteral("true");
  const qsizetype equals = body.indexOf(QChar('='));
  if (equals >= 0) {
    value = body.mid(equals + 1);
    body = body.left(equals);
  }
  const qsizetype dash = body.indexOf(QChar('-'));
  if (dash <= 0 || dash == body.size() - 1) {
    return false;
  }
  QString option = body.mid(dash + 1);
  option.replace(QChar('-'), QChar('_'));

  static const QRegularExpression plainName(QStringLiteral("^[a-z0-9_]+$"));
  static const QRegularExpression plainValue(
      QStringLiteral("^[A-Za-z0-9_.+@-]+$"));
  if (!plainName.match(option).hasMatch() ||
      !plainValue.match(value).hasMatch()) {
    return false;
  }
  *backend = body.left(dash);
  *param = option + QChar('=') + value;
  return true;
}

// "gdrive:Photos/2024/a.jpg" becomes fs "gdrive<params>:Photos/2024/" and
// remote "a.jpg". The parent keeps its trailing slash so a Windows drive
// root stays "C:/" rather than turning into the drive-relative "C:".
bool splitTarget(const QString &target, const QString &params, QString *fs,
                 QString *leaf) {
  const qsizetype colon = target.indexOf(QChar(':'));
  if (colon <= 0) {
    return false;
  }
  QString path = target.mid(colon + 1);
  while (path.endsWith(QChar('/'))) {
    path.chop(1);
  }
  const qsizetype slash = path.lastIndexOf(QChar('/'));
  *leaf = slash >= 0 ? path.mid(slash + 1) : path;
  if (leaf->isEmpty()) {
    return false;
  }
  const QString parent = slash >= 0 ? path.left(slash + 1) : QString();
  *fs = target.left(colon) + params + QChar(':') + parent;
  return true;
}

} // namespace

Request Build(const Plan &plan, const RcSyncRequest::OptionIndex &index) {
  Request request;

  if (plan.items.size() < 2) {
    request.reason = QStringLiteral("a single item keeps its own job");
    return request;
  }

  QString backend;
  QString params;
  for (const QString &flag : plan.backendFlags) {
    QString flagBackend;
    QString param;
    if (!backendParam(flag, &flagBackend, &param)) {
      request.reason =
          QString("%1 can't be passed as a connection string parameter")
              .arg(flag);
      return request;
    }
    if (!backend.isEmpty() && flagBackend != backend) {
      request.reason = QString("%1 and %2 are for different backends")
                           .arg(plan.backendFlags.first(), flag);
      return request;
    }
    backend = flagBackend;
    params += QChar(',') + param;
  }

  QJsonObject config;
  if (!plan.extraOptions.isEmpty()) {
    if (index.isEmpty()) {
      request.reason = QStringLiteral("no option metadata from the daemon");
      return request;
    }
    const RcSyncRequest::Options options =
        RcSyncRequest::MapOptions(plan.extraOptions, index);
    if (!options.ok) {
      request.reason = options.reason;
      return request;
    }
    if (!options.positional.isEmpty()) {
      request.reason = QString("%1 in the default options is not an option")
                           .arg(options.positional.first());
      return request;
    }
    // A filter decides what `rclone delete` removes. operations/deletefile
    // removes the one object it's given whatever the filters say, so the
    // two routes would delete different things.
    if (!options.filter.isEmpty()) {
      request.reason = QStringLiteral(
          "the default options include a filter, which a batch delete "
          "would ignore");
      return request;
    }
    config = options.config;
  }

  QJsonArray inputs;
  for (const Item &item : plan.items) {
    QString fs;
    QString leaf;
    if (!splitTarget(item.target, params, &fs, &leaf)) {
      request.reason =
          QString("%1 has no parent folder to delete it from").arg(item.target);
      request.labels.clear();
      return request;
    }
    QJsonObject input;
    input.insert("_path", item.folder ? QStringLiteral("operations/purge")
                                      : QStringLiteral("operations/deletefile"));
    input.insert("fs", fs);
    input.insert("remote", leaf);
    inputs.append(input);
    request.labels.append(item.label);
  }

  request.payload.insert("inputs", inputs);
  if (!config.isEmpty()) {
    request.payload.insert("_config", config);
  }
  request.usable = true;
  return request;
}

QStringList Failures(const QJsonValue &output, const QStringList &labels) {
  QStringList lines;
  const QJsonArray results = output.toObject().value("results").toArray();
  for (int i = 0; i < labels.size(); i++) {
    if (i >= results.size()) {
      lines.append(QString("%1: rclone reported no result").arg(labels.at(i)));
      continue;
    }
    const QString error =
        results.at(i).toObject().value("error").toString().trimmed();
    if (!error.isEmpty()) {
      lines.append(QString("%1: %2").arg(labels.at(i), error));
    }
  }
  return lines;
}

} // namespace RcDeleteBatch
