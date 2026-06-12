#pragma once

#include <QByteArray>
#include <QString>

struct VfsUploadState {
  bool valid = false;
  int pendingFiles = 0;
  quint64 pendingBytes = 0;
  bool bytesKnown = true;
  QString error;

  bool hasPendingUploads() const { return pendingFiles > 0; }
};

VfsUploadState ParseVfsQueueState(const QByteArray &json);
VfsUploadState ParseVfsStatsUploadState(const QByteArray &json);
QString FormatUploadByteSize(quint64 size);
