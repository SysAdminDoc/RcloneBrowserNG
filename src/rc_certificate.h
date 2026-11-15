#pragma once

#include "pch.h"

// Trusting an rclone rcd's TLS certificate by its SHA-256 fingerprint, for a
// daemon on another computer that runs with a self-signed --rc-cert. Both
// RemoteDaemon::Call and the rc engine guard their requests with it.
namespace RcCertificate {

// "ab cd ...", "AB:CD:..." or OpenSSL's "sha256 Fingerprint=AB:CD:..." all
// become "AB:CD:...". Empty unless the text holds exactly 32 bytes of hex.
QString Normalize(const QString &text);
// The SHA-256 fingerprint of a certificate's DER bytes, in the same form.
QString Fingerprint(const QSslCertificate &certificate);
// Both normalize to the same non-empty fingerprint.
bool Matches(const QString &a, const QString &b);

// The error for a refused certificate. trusted is the saved fingerprint,
// empty when none was saved.
QString Refusal(const QString &url, const QString &trusted,
                const QString &presented);

// Keeps requests made under a trusted fingerprint on connections of their
// own. Qt sends a request down an open connection without a new handshake,
// so otherwise a request that trusts no fingerprint could reuse a connection
// that only a trusted fingerprint let through. Call it before posting.
void Prepare(QNetworkRequest &request, const QString &trusted);

// Holds a request to the daemon's certificate: one this computer doesn't
// trust passes only when its fingerprint is trusted, and when a fingerprint
// is trusted any other certificate is refused before the login goes out.
// Call it right after posting.
void Guard(QNetworkReply *reply, const QString &trusted);
// Once a guarded reply has finished: empty when its certificate was fine,
// otherwise Refusal's message, with the fingerprint the daemon presented in
// presented.
QString Problem(QNetworkReply *reply, const QString &url,
                const QString &trusted, QString *presented = nullptr);

} // namespace RcCertificate
