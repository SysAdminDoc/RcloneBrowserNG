#include "app_identity.h"
#include "identity_migration.h"
#include "utils.h"

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  ApplyAppIdentity();
  // rclone can run this before the main window ever has, so it needs the
  // settings copied over just the same.
  RunSharedIdentityMigration();

  QString error;
  const QString password = ReadRcloneConfigPassword(&error);
  if (password.isEmpty()) {
    qCritical().noquote() << error;
    return 1;
  }

  QTextStream(stdout) << password << Qt::endl;
  return 0;
}
