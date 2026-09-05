#pragma once

#include "pch.h"

// `rclone lsjson` output arrives in arbitrary chunks over a pipe, so the model
// cannot wait for a complete document before showing anything. This is the
// incremental splitter and the field decoder it feeds, lifted out of
// ItemModel::load so a test can drive the code the app actually runs.
namespace LsjsonParser {

struct Entry {
  bool isFolder = false;
  QString name;
  QString path;
  quint64 size = 0;
  // Already formatted for display, empty when rclone sent nothing usable.
  QString modified;
};

// Splits a byte stream into complete top-level JSON objects. Braces inside
// quoted strings and escapes are ignored, and an object may straddle any
// number of chunk boundaries.
class StreamSplitter {
public:
  // Returns the objects that this chunk completed.
  QVector<QJsonObject> feed(const QByteArray &data);

  // True once any bytes have been fed. The caller uses this to tell "rclone
  // printed nothing" from "rclone printed something we could not parse".
  bool hadData() const { return mHadData; }

private:
  QByteArray mBuffer;
  int mBraceDepth = 0;
  int mObjectStart = -1;
  bool mInString = false;
  bool mHadData = false;
};

// Maps one lsjson record onto the fields the tree shows. `parentPath` is the
// remote path of the directory being listed.
Entry DecodeEntry(const QJsonObject &object, const QString &parentPath);

// rclone reports ModTime as RFC3339, with or without fractional seconds and
// with per-backend precision. Returns an empty string when it cannot be read.
QString FormatModTime(const QString &modTime);

} // namespace LsjsonParser
