#include "rc_certificate.h"

namespace {

// What Guard learned during the handshake, kept on the reply.
const char *const kPresentedProperty = "rcCertificatePresented";
const char *const kRefusedProperty = "rcCertificateRefused";

// The fingerprint of the certificate the daemon presented. While the
// handshake is still being judged, the reply may not carry the peer
// certificate yet, and the errors name it instead.
QString peerFingerprint(const QNetworkReply *reply,
                        const QList<QSslError> &errors = {}) {
  QString fingerprint =
      RcCertificate::Fingerprint(reply->sslConfiguration().peerCertificate());
  for (const QSslError &error : errors) {
    if (!fingerprint.isEmpty()) {
      break;
    }
    fingerprint = RcCertificate::Fingerprint(error.certificate());
  }
  return fingerprint;
}

} // namespace

namespace RcCertificate {

QString Normalize(const QString &text) {
  QString value = text;
  const qsizetype equals = value.lastIndexOf(QChar('='));
  if (equals >= 0) {
    value = value.mid(equals + 1);
  }
  QString hex;
  for (const QChar c : value) {
    if ((c >= QChar('0') && c <= QChar('9')) ||
        (c >= QChar('a') && c <= QChar('f')) ||
        (c >= QChar('A') && c <= QChar('F'))) {
      hex.append(c.toUpper());
    } else if (c != QChar(':') && !c.isSpace()) {
      return QString();
    }
  }
  if (hex.size() != 64) {
    return QString();
  }
  QStringList pairs;
  for (qsizetype i = 0; i < hex.size(); i += 2) {
    pairs << hex.mid(i, 2);
  }
  return pairs.join(QChar(':'));
}

QString Fingerprint(const QSslCertificate &certificate) {
  if (certificate.isNull()) {
    return QString();
  }
  return Normalize(QString::fromLatin1(
      certificate.digest(QCryptographicHash::Sha256).toHex()));
}

bool Matches(const QString &a, const QString &b) {
  const QString left = Normalize(a);
  return !left.isEmpty() && left == Normalize(b);
}

QString Refusal(const QString &url, const QString &trusted,
                const QString &presented) {
  const QString shown =
      presented.isEmpty() ? QStringLiteral("none") : presented;
  if (Normalize(trusted).isEmpty()) {
    return QString("The remote rclone at %1 has a certificate this computer "
                   "doesn't trust. Its SHA-256 fingerprint is %2. If that "
                   "matches the certificate rclone was started with, trust "
                   "it in Preferences, Remote rclone.")
        .arg(url, shown);
  }
  return QString("The remote rclone at %1 has a different certificate from "
                 "the one you trusted, so the app didn't connect. Trusted: "
                 "%2. Presented: %3. If that computer's certificate was "
                 "replaced on purpose, clear the trusted certificate in "
                 "Preferences, Remote rclone, and test again.")
      .arg(url, Normalize(trusted), shown);
}

void Prepare(QNetworkRequest &request, const QString &trusted) {
  if (Normalize(trusted).isEmpty() ||
      request.url().scheme() != QLatin1String("https")) {
    return;
  }
  // The name a certificate is checked against is part of the key Qt keeps
  // open connections under, so pinned and unpinned requests never share
  // one. It's the host the URL already names, so the check doesn't change.
  request.setPeerVerifyName(request.url().host());
  // On an HTTP/2 connection Qt tells only the first queued request about
  // the handshake, so Guard couldn't stop the others from being sent.
  request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
}

void Guard(QNetworkReply *reply, const QString &trusted) {
  const QString pinned = Normalize(trusted);
  // A certificate this computer doesn't trust passes only when it's the one
  // the user trusted.
  QObject::connect(reply, &QNetworkReply::sslErrors, reply,
                   [reply, pinned](const QList<QSslError> &errors) {
                     const QString presented = peerFingerprint(reply, errors);
                     reply->setProperty(kPresentedProperty, presented);
                     if (Matches(pinned, presented)) {
                       reply->ignoreSslErrors(errors);
                     } else {
                       reply->setProperty(kRefusedProperty, true);
                     }
                   });
  // A trusted fingerprint holds for certificates the system does trust too.
  // Over HTTP/1.1 Qt waits for this before sending the request, and the
  // abort closes the connection, so nothing goes out on it.
  QObject::connect(reply, &QNetworkReply::encrypted, reply, [reply, pinned]() {
    const QString presented = peerFingerprint(reply);
    reply->setProperty(kPresentedProperty, presented);
    if (!pinned.isEmpty() && !Matches(pinned, presented)) {
      reply->setProperty(kRefusedProperty, true);
      reply->abort();
    }
  });
}

QString Problem(QNetworkReply *reply, const QString &url,
                const QString &trusted, QString *presented) {
  const QString pinned = Normalize(trusted);
  QString shown = reply->property(kPresentedProperty).toString();
  bool refused = reply->property(kRefusedProperty).toBool();
  // A request on a connection opened earlier gets no handshake signals.
  // Callers keep each fingerprint's connections apart, so this is a second
  // check that the reply really came from the trusted certificate.
  if (!refused && !pinned.isEmpty() &&
      reply->url().scheme() == QLatin1String("https")) {
    const QString peer = peerFingerprint(reply);
    if (!peer.isEmpty() && !Matches(pinned, peer)) {
      shown = peer;
      refused = true;
    }
  }
  if (!refused) {
    return QString();
  }
  if (presented != nullptr) {
    *presented = shown;
  }
  return Refusal(url, pinned, shown);
}

} // namespace RcCertificate
