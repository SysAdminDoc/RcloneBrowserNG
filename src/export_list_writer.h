#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QString>

enum class ExportListFormat {
  Text,
  Csv,
};

bool WriteExportListFromLsjson(QIODevice *device, const QByteArray &json,
                               ExportListFormat format, QString *error);
