#pragma once

#include "big_uint.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QList>
#include <QRandomGenerator>

// Clearsigns text with a throwaway DSA key (2048-bit p, 256-bit q) made with
// GnuPG 2.4 for these tests. The app never trusts it: only tests pass it in
// as a trusted key.
namespace PgpTestSigner {

inline const char kPublicKey[] = R"(-----BEGIN PGP PUBLIC KEY BLOCK-----

mQMuBGr6Q/sRCADWU2pBVnNGe2GRd1RPx1fMTEKVMP9+0ohJbZ6K72AS19o4HLiz
9rZ+X+AQWgw7PB8hFbjA3D5axyIBtQgA2iGFBNnpgS8tMFkgK3Rw8Allx6EniSOc
TxbMFOT23qySgxf3o1HTSw8rxDnV+FjDkMum3VYhHQSuaFxI7tmTdi4acmZsvqz8
oLFITXFxKM0IdYTWVcY+bLvQ8mDIPr9Ha3xRJAThGrH6NyF/9NUhXa0InM9fC/A1
lUk40685o3UQeImxzwRExONn6tuk8OfUPHlRgSjYex08GedxGrFTP14QMAFSIPCt
MzFIy2BILnq2KrUN+RCZJRAdwlcUbgvPdtu7AQD7LRvrDTSSThupHi+osiZ0ihJ+
7iP1xoZFy7G4rNISzwf9EomLsLEYWydiwBpazrqiBnY6xq9ylwnbLlIg+5jJlmXY
qXJT2cZO4ErIamijO9QaVpfKYiZa8LJahPbew2NMxvGRDg03Pg5u0vfkc2OkNnCg
fhs3NnWcMQl3lBleaOHg53/3Aa3qUC0CQNXzqWsFvKsfDj38scXSdS0TmlZTHmNW
+LAHGY7TNH8xL+c/Vyw7nMGAKeGci8L2K1F4nwEHSjuzYNn0T8sGYjxOMdKP0EV2
bENgPRKpz3xnCOAdbNj9HwtEUEu/15YoyeZZwk5u8nrfSGtUHCx5e2ZZabHk/cf/
5Bip+OVlsMg/DJB7A5dca72Blt3UPvLNW5b2FmjySQf/Tg15rI5pE3LWRt47dQhw
8KyJ9+z5sf4u75LztDVwktqvU89g7tLnwePCdRcB7JJE8wSnwYP9d7c6PBRTqV5c
1KBovfknaQP3aRfK3vjZvi9ok2L4snofKaCoL8MhGA7wnQjz9dcEvqrRZ+eeOjPw
V22kHgTc5/n2DmurE1TMbUFlB732Ax8FAOx477yg6BsnEUTWWyaiP90/DSFO5XDt
KY5zJqK0wKyKAERrV/dqX80G1I/CCUQATUvBwF2Y4Q4L2Ix1ML94GDY3Vms9rXbq
p69WtXhibtEwE0SvKqqMQkGZUIbRBG+d+tjW7hI9NbQ0EplnRVFavz162/e1J0Xm
17QxUmNsb25lIEJyb3dzZXIgTkcgdGVzdCBrZXkgPHRlc3RAZXhhbXBsZS5pbnZh
bGlkPoiQBBMRCAA4FiEEBTQOhp/zCtZmRSm2naix5JtrbC4FAmr6Q/sCGwMFCwkI
BwIGFQoJCAsCBBYCAwECHgECF4AACgkQnaix5JtrbC4jpwD/VbGpf3BvTZmy1WV+
7oABnNSfoyP9YXZ+Dl+SZ0QqYs8BAMSEmRZzfsVoIQM1/gJSuk4cMe5XLr04LlDb
wzLmoTrg
=3kuX
-----END PGP PUBLIC KEY BLOCK-----
)";
inline const char kFingerprint[] = "05340E869FF30AD6664529B69DA8B1E49B6B6C2E";

inline BigUint Hex(const char *hex) {
  return BigUint::FromBytes(QByteArray::fromHex(hex));
}
inline BigUint P() { return Hex("d6536a415673467b619177544fc757cc4c429530ff7ed288496d9e8aef6012d7da381cb8b3f6b67e5fe0105a0c3b3c1f2115b8c0dc3e5ac72201b50800da218504d9e9812f2d3059202b7470f00965c7a12789239c4f16cc14e4f6deac928317f7a351d34b0f2bc439d5f858c390cba6dd56211d04ae685c48eed993762e1a72666cbeacfca0b1484d717128cd087584d655c63e6cbbd0f260c83ebf476b7c512404e11ab1fa37217ff4d5215dad089ccf5f0bf035954938d3af39a375107889b1cf0444c4e367eadba4f0e7d43c79518128d87b1d3c19e7711ab1533f5e1030015220f0ad333148cb60482e7ab62ab50df9109925101dc257146e0bcf76dbbb"); }
inline BigUint Q() { return Hex("fb2d1beb0d34924e1ba91e2fa8b226748a127eee23f5c68645cbb1b8acd212cf"); }
inline BigUint G() { return Hex("12898bb0b1185b2762c01a5acebaa206763ac6af729709db2e5220fb98c99665d8a97253d9c64ee04ac86a68a33bd41a5697ca62265af0b25a84f6dec3634cc6f1910e0d373e0e6ed2f7e47363a43670a07e1b3736759c31097794195e68e1e0e77ff701adea502d0240d5f3a96b05bcab1f0e3dfcb1c5d2752d139a56531e6356f8b007198ed3347f312fe73f572c3b9cc18029e19c8bc2f62b51789f01074a3bb360d9f44fcb06623c4e31d28fd045766c43603d12a9cf7c6708e01d6cd8fd1f0b44504bbfd79628c9e659c24e6ef27adf486b541c2c797b665969b1e4fdc7ffe418a9f8e565b0c83f0c907b03975c6bbd8196ddd43ef2cd5b96f61668f249"); }
inline BigUint Y() { return Hex("4e0d79ac8e691372d646de3b750870f0ac89f7ecf9b1fe2eef92f3b4357092daaf53cf60eed2e7c1e3c2751701ec9244f304a7c183fd77b73a3c1453a95e5cd4a068bdf9276903f76917cadef8d9be2f689362f8b27a1f29a0a82fc321180ef09d08f3f5d704beaad167e79e3a33f0576da41e04dce7f9f60e6bab1354cc6d416507bdf6031f0500ec78efbca0e81b271144d65b26a23fdd3f0d214ee570ed298e7326a2b4c0ac8a00446b57f76a5fcd06d48fc20944004d4bc1c05d98e10e0bd88c7530bf78183637566b3dad76eaa7af56b578626ed1301344af2aaa8c4241995086d1046f9dfad8d6ee123d35b43412996745515abf3d7adbf7b52745e6d7"); }
inline BigUint X() { return Hex("66c0a69f7fda83200ff70985d5e193b859ec613762f71ef81ffab353617bb856"); }

inline QByteArray Mpi(const BigUint &value) {
  const int bits = value.bitLength();
  QByteArray out;
  out.append(static_cast<char>((bits >> 8) & 0xFF));
  out.append(static_cast<char>(bits & 0xFF));
  out.append(value.toBytes());
  return out;
}

inline QByteArray Be(quint32 value, int bytes) {
  QByteArray out;
  for (int shift = 8 * (bytes - 1); shift >= 0; shift -= 8) {
    out.append(static_cast<char>((value >> shift) & 0xFF));
  }
  return out;
}

inline QByteArray TrimTrailing(QByteArray line) {
  while (line.endsWith(' ') || line.endsWith('\t')) {
    line.chop(1);
  }
  return line;
}

inline quint32 Crc24(const QByteArray &data) {
  quint32 crc = 0xB704CEu;
  for (const char c : data) {
    crc ^= static_cast<quint32>(static_cast<unsigned char>(c)) << 16;
    for (int i = 0; i < 8; ++i) {
      crc <<= 1;
      if (crc & 0x1000000u) {
        crc ^= 0x1864CFBu;
      }
    }
  }
  return crc & 0xFFFFFFu;
}

// A cleartext-signed message over lines, the way `gpg --clearsign` lays it
// out: dash escapes, a v4 DSA signature of the canonical text with SHA-256,
// and an armored signature with its checksum.
inline QByteArray Clearsign(const QList<QByteArray> &lines) {
  QList<QByteArray> trimmed;
  for (const QByteArray &line : lines) {
    trimmed.append(TrimTrailing(line));
  }
  const QByteArray canonical = trimmed.join("\r\n");
  const QByteArray fingerprint = QByteArray::fromHex(kFingerprint);

  QByteArray hashedSubpackets;
  hashedSubpackets.append(static_cast<char>(5));
  hashedSubpackets.append(static_cast<char>(2));
  hashedSubpackets.append(
      Be(static_cast<quint32>(QDateTime::currentSecsSinceEpoch()), 4));
  hashedSubpackets.append(static_cast<char>(22));
  hashedSubpackets.append(static_cast<char>(33));
  hashedSubpackets.append(static_cast<char>(4));
  hashedSubpackets.append(fingerprint);

  QByteArray hashedPart;
  hashedPart.append(static_cast<char>(4));    // version
  hashedPart.append(static_cast<char>(0x01)); // canonical text
  hashedPart.append(static_cast<char>(17));   // DSA
  hashedPart.append(static_cast<char>(8));    // SHA-256
  hashedPart.append(Be(static_cast<quint32>(hashedSubpackets.size()), 2));
  hashedPart.append(hashedSubpackets);

  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(canonical);
  hash.addData(hashedPart);
  QByteArray trailer;
  trailer.append(static_cast<char>(0x04));
  trailer.append(static_cast<char>(0xFF));
  trailer.append(Be(static_cast<quint32>(hashedPart.size()), 4));
  hash.addData(trailer);
  const QByteArray digest = hash.result();

  const BigUint p = P();
  const BigUint q = Q();
  const BigUint z = BigUint::FromBytes(digest.left(32));
  const BigUint qMinusTwo = BigUint::Sub(q, BigUint::FromUint(2));
  BigUint r;
  BigUint s;
  while (r.isZero() || s.isZero()) {
    QByteArray random(48, '\0');
    for (char &c : random) {
      c = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    const BigUint k = BigUint::Add(
        BigUint::Mod(BigUint::FromBytes(random),
                     BigUint::Sub(q, BigUint::FromUint(1))),
        BigUint::FromUint(1));
    r = BigUint::Mod(BigUint::PowMod(G(), k, p), q);
    const BigUint kInverse = BigUint::PowMod(k, qMinusTwo, q);
    s = BigUint::MulMod(
        kInverse, BigUint::Mod(BigUint::Add(z, BigUint::MulMod(X(), r, q)), q),
        q);
  }

  QByteArray unhashed;
  unhashed.append(static_cast<char>(9));
  unhashed.append(static_cast<char>(16));
  unhashed.append(fingerprint.right(8));

  QByteArray body = hashedPart;
  body.append(Be(static_cast<quint32>(unhashed.size()), 2));
  body.append(unhashed);
  body.append(digest.left(2));
  body.append(Mpi(r));
  body.append(Mpi(s));

  QByteArray packet;
  packet.append(static_cast<char>(0xC2)); // new format, tag 2
  Q_ASSERT(body.size() < 192);
  packet.append(static_cast<char>(body.size()));
  packet.append(body);

  QByteArray out = "-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA256\n\n";
  for (const QByteArray &line : lines) {
    if (line.startsWith('-')) {
      out.append("- ");
    }
    out.append(line);
    out.append('\n');
  }
  out.append("-----BEGIN PGP SIGNATURE-----\n\n");
  const QByteArray base64 = packet.toBase64();
  for (int i = 0; i < base64.size(); i += 64) {
    out.append(base64.mid(i, 64));
    out.append('\n');
  }
  out.append('=');
  out.append(Be(Crc24(packet), 3).toBase64());
  out.append("\n-----END PGP SIGNATURE-----\n");
  return out;
}

// The same, for text with "\n" line endings and an optional final newline.
inline QByteArray Clearsign(const QByteArray &text) {
  QList<QByteArray> lines = text.split('\n');
  if (text.endsWith('\n')) {
    lines.removeLast();
  }
  return Clearsign(lines);
}

} // namespace PgpTestSigner
