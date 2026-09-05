#include "lsjson_parser.h"

#include "remote_path.h"

namespace LsjsonParser {

QVector<QJsonObject> StreamSplitter::feed(const QByteArray &data) {
  QVector<QJsonObject> objects;

  const int previousSize = mBuffer.size();
  mBuffer.append(data);
  mHadData = true;

  for (int i = previousSize; i < mBuffer.size(); i++) {
    const char c = mBuffer.at(i);

    if (mInString) {
      if (c == '\\') {
        i++;
      } else if (c == '"') {
        mInString = false;
      }
      continue;
    }

    if (c == '"') {
      mInString = true;
      continue;
    }
    if (c == '{') {
      if (mBraceDepth == 0) {
        mObjectStart = i;
      }
      mBraceDepth++;
    } else if (c == '}') {
      mBraceDepth--;
      if (mBraceDepth == 0 && mObjectStart >= 0) {
        const QByteArray objectBytes =
            mBuffer.mid(mObjectStart, i - mObjectStart + 1);
        const QJsonDocument document = QJsonDocument::fromJson(objectBytes);
        if (document.isObject()) {
          objects.append(document.object());
        }
        mObjectStart = -1;
      }
    }
  }

  // Keep only the bytes of a record still being assembled, so the buffer does
  // not grow to the size of the whole listing.
  if (mObjectStart > 0) {
    mBuffer = mBuffer.mid(mObjectStart);
    mObjectStart = 0;
  } else if (mObjectStart < 0) {
    mBuffer.clear();
  }

  return objects;
}

QString FormatModTime(const QString &modTime) {
  const QDateTime parsed = QDateTime::fromString(modTime, Qt::ISODateWithMs);
  if (parsed.isValid()) {
    return parsed.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
  }
  // Some backends report a shape Qt will not take. Show the leading
  // date and time rather than nothing.
  if (modTime.length() >= 19) {
    return QString(modTime).left(19).replace('T', ' ');
  }
  return QString();
}

Entry DecodeEntry(const QJsonObject &object, const QString &parentPath) {
  Entry entry;
  entry.isFolder = object.value("IsDir").toBool();
  entry.name = object.value("Name").toString();
  entry.path = ChildRemotePathFromLsjson(parentPath, object);
  if (!entry.isFolder) {
    entry.size = static_cast<quint64>(object.value("Size").toDouble());
  }
  entry.modified = FormatModTime(object.value("ModTime").toString());
  return entry;
}

} // namespace LsjsonParser
