#include "remote_provider.h"

#include <QDebug>
#include <cstdlib>

namespace {
void require(bool condition, const QString &message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}
} // namespace

int main() {
  const QByteArray json = R"({
    "providers": [
      {
        "Name": "drive",
        "Prefix": "drive",
        "Description": "Google Drive",
        "Hide": false
      },
      {
        "Name": "hidden",
        "Prefix": "hidden",
        "Description": "Hidden Backend",
        "Hide": true
      },
      {
        "Name": "protondrive",
        "Prefix": "protondrive",
        "Description": "Proton Drive",
        "Hide": false
      },
      {
        "Name": "smb",
        "Prefix": "smb",
        "Description": "",
        "Hide": false
      }
    ]
  })";

  QString error;
  const QVector<RemoteProvider> providers = ParseRemoteProviders(json, &error);
  require(error.isEmpty(), "provider parse failed: " + error);
  require(providers.size() == 3, "hidden provider was not filtered");
  require(providers[0].prefix == "drive",
          "providers were not sorted by display name");
  require(providers[1].prefix == "protondrive",
          "new provider prefix was not preserved");
  require(RemoteProviderDisplayName(providers[2]) == "smb (smb)",
          "provider without description displayed incorrectly");

  const QVector<RemoteProvider> malformed =
      ParseRemoteProviders(QByteArray("{"), &error);
  require(malformed.isEmpty(), "malformed provider JSON unexpectedly parsed");
  require(error.contains("Failed to parse rclone config/providers output"),
          "malformed provider error was not actionable");

  return 0;
}
