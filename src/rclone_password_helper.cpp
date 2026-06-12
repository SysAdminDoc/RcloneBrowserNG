#include "utils.h"

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  app.setApplicationName("rclone-browser");
  app.setOrganizationName("rclone-browser");

  QString error;
  const QString password = ReadRcloneConfigPassword(&error);
  if (password.isEmpty()) {
    qCritical().noquote() << error;
    return 1;
  }

  QTextStream(stdout) << password << Qt::endl;
  return 0;
}
