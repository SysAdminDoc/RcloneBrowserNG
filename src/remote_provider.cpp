#include "remote_provider.h"

namespace {
QString valueToString(const QJsonObject &object, const char *key) {
  return object.value(QLatin1String(key)).toString().trimmed();
}
} // namespace

QVector<RemoteProvider> ParseRemoteProviders(const QByteArray &json,
                                             QString *error) {
  if (error) {
    error->clear();
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) {
      *error = "Failed to parse rclone config/providers output: " +
               parseError.errorString();
    }
    return {};
  }

  const QJsonArray providers =
      doc.object().value(QLatin1String("providers")).toArray();
  QVector<RemoteProvider> result;
  result.reserve(providers.size());

  for (const QJsonValue &value : providers) {
    const QJsonObject object = value.toObject();
    if (object.isEmpty() || object.value(QLatin1String("Hide")).toBool()) {
      continue;
    }

    RemoteProvider provider;
    provider.name = valueToString(object, "Name");
    provider.prefix = valueToString(object, "Prefix");
    provider.description = valueToString(object, "Description");

    if (provider.prefix.isEmpty()) {
      provider.prefix = provider.name;
    }
    if (provider.name.isEmpty() || provider.prefix.isEmpty()) {
      continue;
    }

    result.append(provider);
  }

  std::sort(result.begin(), result.end(),
            [](const RemoteProvider &left, const RemoteProvider &right) {
              return QString::localeAwareCompare(
                         RemoteProviderDisplayName(left),
                         RemoteProviderDisplayName(right)) < 0;
            });

  if (result.isEmpty() && error) {
    *error = "rclone config/providers returned no usable providers.";
  }

  return result;
}

QString RemoteProviderDisplayName(const RemoteProvider &provider) {
  if (provider.description.isEmpty()) {
    return QString("%1 (%2)").arg(provider.name, provider.prefix);
  }
  return QString("%1 (%2) - %3")
      .arg(provider.name, provider.prefix, provider.description);
}
