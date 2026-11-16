#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>
#include <memory>

// A stand-in rclone rc server on 127.0.0.1, for tests that have to decide
// what the daemon answers and when. Every POST goes to the handler with a
// Reply it can call at once, or keep and call later to hold the answer back
// while the test does something in between.
class FakeRcServer {
public:
  using Reply = std::function<void(const QJsonObject &answer)>;
  using Handler = std::function<void(const QString &path,
                                     const QJsonObject &body,
                                     const Reply &reply)>;

  explicit FakeRcServer(Handler handler) : mHandler(std::move(handler)) {
    mServer.listen(QHostAddress::LocalHost, 0);
    QObject::connect(&mServer, &QTcpServer::newConnection, &mServer,
                     [this]() {
                       while (QTcpSocket *socket =
                                  mServer.nextPendingConnection()) {
                         accept(socket);
                       }
                     });
  }

  bool isListening() const { return mServer.isListening(); }

  QString url() const {
    return QString("http://127.0.0.1:%1/").arg(mServer.serverPort());
  }

  // Every path posted so far, in order, without the leading slash.
  QStringList paths() const {
    QStringList all;
    for (const auto &request : mRequests) {
      all << request.first;
    }
    return all;
  }

  // The body of the last request to path, or an empty object.
  QJsonObject lastBody(const QString &path) const {
    for (auto it = mRequests.crbegin(); it != mRequests.crend(); ++it) {
      if (it->first == path) {
        return it->second;
      }
    }
    return QJsonObject();
  }

private:
  QTcpServer mServer;
  Handler mHandler;
  QList<QPair<QString, QJsonObject>> mRequests;

  void accept(QTcpSocket *socket) {
    auto buffer = std::make_shared<QByteArray>();
    QObject::connect(socket, &QTcpSocket::readyRead, socket,
                     [this, socket, buffer]() {
                       *buffer += socket->readAll();
                       readRequests(socket, buffer.get());
                     });
    QObject::connect(socket, &QTcpSocket::disconnected, socket,
                     &QObject::deleteLater);
  }

  // Qt keeps a connection open between requests and never sends a second
  // one down it before the first is answered, so one request is read at a
  // time, as far as the bytes so far go.
  void readRequests(QTcpSocket *socket, QByteArray *buffer) {
    for (;;) {
      const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        return;
      }
      const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
      qsizetype length = 0;
      for (const QByteArray &line : lines.mid(1)) {
        const qsizetype colon = line.indexOf(':');
        if (colon > 0 &&
            line.left(colon).trimmed().toLower() == "content-length") {
          length = line.mid(colon + 1).trimmed().toLongLong();
        }
      }
      if (buffer->size() < headerEnd + 4 + length) {
        return;
      }
      const QJsonObject body =
          QJsonDocument::fromJson(buffer->mid(headerEnd + 4, length))
              .object();
      buffer->remove(0, headerEnd + 4 + length);
      QString path = QString::fromUtf8(
          lines.value(0).trimmed().split(' ').value(1));
      if (path.startsWith(QChar('/'))) {
        path.remove(0, 1);
      }
      mRequests.append(qMakePair(path, body));
      const QPointer<QTcpSocket> guard(socket);
      mHandler(path, body, [guard](const QJsonObject &answer) {
        if (!guard) {
          return;
        }
        const QByteArray json =
            QJsonDocument(answer).toJson(QJsonDocument::Compact);
        guard->write("HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: " +
                     QByteArray::number(json.size()) + "\r\n\r\n" + json);
      });
    }
  }
};
