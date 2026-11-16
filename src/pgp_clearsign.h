#pragma once

#include "pch.h"

// Checks an OpenPGP cleartext signature (RFC 4880 section 7, RFC 9580
// section 7), the way `rclone selfupdate` checks the SHA256SUMS list that
// sits next to every rclone release. Only what rclone's list needs is
// supported: v4 signatures of canonical text made with a v4 DSA key, hashed
// with SHA-1 or the SHA-2 family.
namespace PgpClearsign {

// rclone's release signing key, as rclone embeds it in
// cmd/selfupdate/verify.go. Fingerprint
// FBF7 37EC E9F8 AB18 604B  D2AC 9393 5E02 FF3B 54FA.
extern const char kRcloneReleaseKey[];

struct Verified {
  bool ok = false;
  // The signed text only: dash escapes removed, trailing spaces and tabs
  // dropped from each line, lines joined with "\n". Anything outside the
  // signed block is never returned.
  QByteArray text;
  // Why the message was refused, in plain words.
  QString error;
};

// Accepts message only if one of its signatures verifies against one of
// armoredKeys (ASCII-armored public key blocks).
Verified Verify(const QByteArray &message, const QList<QByteArray> &armoredKeys);

// The v4 fingerprint of the primary key in an armored block, as uppercase
// hex. Empty when the block can't be read or isn't a DSA key.
QString Fingerprint(const QByteArray &armoredKey);

} // namespace PgpClearsign
