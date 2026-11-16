#include "pgp_clearsign.h"

#include "big_uint.h"

namespace PgpClearsign {

const char kRcloneReleaseKey[] = R"(-----BEGIN PGP PUBLIC KEY BLOCK-----

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
EtC4nj5EImssVk/xmU3axw==
=VUqh
-----END PGP PUBLIC KEY BLOCK-----
)";

namespace {

const QByteArray kSignedBegin = "-----BEGIN PGP SIGNED MESSAGE-----";
const QByteArray kSignatureBlock = "PGP SIGNATURE";
const QByteArray kKeyBlock = "PGP PUBLIC KEY BLOCK";

constexpr int kTagSignature = 2;
constexpr int kTagPublicKey = 6;
constexpr int kAlgoDsa = 17;
constexpr int kSigCanonicalText = 0x01;

struct DsaKey {
  BigUint p;
  BigUint q;
  BigUint g;
  BigUint y;
  QByteArray fingerprint;
};

struct Packet {
  int tag = 0;
  QByteArray body;
};

struct Signature {
  int hashAlgorithm = 0;
  // Version through the end of the hashed subpackets: what the hash covers
  // after the text.
  QByteArray hashedPart;
  QByteArray quickCheck;
  BigUint r;
  BigUint s;
  QByteArray issuerFingerprint;
  QByteArray issuerKeyId;
  qint64 created = 0;
  qint64 expiresAfter = 0;
};

QByteArray TrimTrailing(QByteArray line) {
  qsizetype end = line.size();
  while (end > 0 && (line.at(end - 1) == ' ' || line.at(end - 1) == '\t' ||
                     line.at(end - 1) == '\r')) {
    --end;
  }
  line.truncate(end);
  return line;
}

QList<QByteArray> Lines(const QByteArray &data) {
  QList<QByteArray> lines = data.split('\n');
  for (QByteArray &line : lines) {
    if (line.endsWith('\r')) {
      line.chop(1);
    }
  }
  return lines;
}

bool IsBlank(const QByteArray &line) { return TrimTrailing(line).isEmpty(); }

quint32 Crc24(const QByteArray &data) {
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

quint32 ReadBig(const QByteArray &data, qsizetype pos, int bytes) {
  quint32 value = 0;
  for (int i = 0; i < bytes; ++i) {
    value = (value << 8) | static_cast<unsigned char>(data.at(pos + i));
  }
  return value;
}

// Reads the armored block of the given type that starts at lines[*index] and
// leaves *index just past its END line.
bool Dearmor(const QList<QByteArray> &lines, int *index, const QByteArray &type,
             QByteArray *packets, QString *error) {
  int i = *index;
  if (i >= lines.size() ||
      TrimTrailing(lines.at(i)) != "-----BEGIN " + type + "-----") {
    *error = QStringLiteral("the %1 block is missing")
                 .arg(QString::fromLatin1(type));
    return false;
  }
  ++i;
  // Armor headers ("Version: ...", "Comment: ..."), then a blank line.
  while (i < lines.size() && !IsBlank(lines.at(i))) {
    if (!lines.at(i).contains(':')) {
      *error = QStringLiteral("the %1 block has no blank line after its "
                              "headers")
                   .arg(QString::fromLatin1(type));
      return false;
    }
    ++i;
  }
  ++i;

  const QByteArray end = "-----END " + type + "-----";
  QByteArray base64;
  QByteArray checksum;
  bool ended = false;
  for (; i < lines.size(); ++i) {
    const QByteArray line = lines.at(i).trimmed();
    if (line == end) {
      ended = true;
      ++i;
      break;
    }
    if (!checksum.isEmpty()) {
      *error = QStringLiteral("the %1 block has text after its checksum")
                   .arg(QString::fromLatin1(type));
      return false;
    }
    if (line.startsWith('=')) {
      checksum = line.mid(1);
      if (checksum.size() != 4) {
        *error = QStringLiteral("the %1 block's checksum is malformed")
                     .arg(QString::fromLatin1(type));
        return false;
      }
      continue;
    }
    base64 += line;
  }
  if (!ended) {
    *error = QStringLiteral("the %1 block never ends")
                 .arg(QString::fromLatin1(type));
    return false;
  }

  const auto decoded = QByteArray::fromBase64Encoding(
      base64, QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded || decoded.decoded.isEmpty()) {
    *error = QStringLiteral("the %1 block isn't valid base64")
                 .arg(QString::fromLatin1(type));
    return false;
  }
  if (!checksum.isEmpty()) {
    const auto crc = QByteArray::fromBase64Encoding(
        checksum, QByteArray::AbortOnBase64DecodingErrors);
    if (!crc || crc.decoded.size() != 3 ||
        ReadBig(crc.decoded, 0, 3) != Crc24(decoded.decoded)) {
      *error = QStringLiteral("the %1 block's checksum doesn't match")
                   .arg(QString::fromLatin1(type));
      return false;
    }
  }
  *packets = decoded.decoded;
  *index = i;
  return true;
}

bool ReadPackets(const QByteArray &data, QList<Packet> *packets,
                 QString *error) {
  qsizetype pos = 0;
  while (pos < data.size()) {
    const auto header = static_cast<unsigned char>(data.at(pos++));
    if (!(header & 0x80)) {
      *error = QStringLiteral("a packet header is malformed");
      return false;
    }
    Packet packet;
    qsizetype length = 0;
    if (header & 0x40) {
      packet.tag = header & 0x3F;
      if (pos >= data.size()) {
        *error = QStringLiteral("a packet is cut short");
        return false;
      }
      const auto first = static_cast<unsigned char>(data.at(pos++));
      if (first < 192) {
        length = first;
      } else if (first < 224) {
        if (pos >= data.size()) {
          *error = QStringLiteral("a packet is cut short");
          return false;
        }
        length = ((first - 192) << 8) +
                 static_cast<unsigned char>(data.at(pos++)) + 192;
      } else if (first == 255) {
        if (pos + 4 > data.size()) {
          *error = QStringLiteral("a packet is cut short");
          return false;
        }
        length = ReadBig(data, pos, 4);
        pos += 4;
      } else {
        *error = QStringLiteral("partial-length packets aren't supported");
        return false;
      }
    } else {
      packet.tag = (header >> 2) & 0x0F;
      const int lengthType = header & 0x03;
      if (lengthType == 3) {
        *error = QStringLiteral("packets of unknown length aren't supported");
        return false;
      }
      const int bytes = 1 << lengthType;
      if (pos + bytes > data.size()) {
        *error = QStringLiteral("a packet is cut short");
        return false;
      }
      length = ReadBig(data, pos, bytes);
      pos += bytes;
    }
    if (length > data.size() - pos) {
      *error = QStringLiteral("a packet is cut short");
      return false;
    }
    packet.body = data.mid(pos, length);
    pos += length;
    packets->append(packet);
  }
  return true;
}

bool ReadMpi(const QByteArray &body, qsizetype *pos, BigUint *value) {
  if (*pos + 2 > body.size()) {
    return false;
  }
  const int bits = static_cast<int>(ReadBig(body, *pos, 2));
  const int bytes = (bits + 7) / 8;
  if (*pos + 2 + bytes > body.size()) {
    return false;
  }
  *value = BigUint::FromBytes(body.mid(*pos + 2, bytes));
  *pos += 2 + bytes;
  return true;
}

bool ParseKey(const QByteArray &armored, DsaKey *key, QString *error) {
  const QList<QByteArray> lines = Lines(armored);
  int index = 0;
  while (index < lines.size() && IsBlank(lines.at(index))) {
    ++index;
  }
  QByteArray data;
  if (!Dearmor(lines, &index, kKeyBlock, &data, error)) {
    return false;
  }
  QList<Packet> packets;
  if (!ReadPackets(data, &packets, error)) {
    return false;
  }
  if (packets.isEmpty() || packets.first().tag != kTagPublicKey) {
    *error = QStringLiteral("the key block doesn't start with a public key");
    return false;
  }
  const QByteArray body = packets.first().body;
  if (body.size() < 6 || body.at(0) != 4) {
    *error = QStringLiteral("only version 4 keys are supported");
    return false;
  }
  if (static_cast<unsigned char>(body.at(5)) != kAlgoDsa) {
    *error = QStringLiteral("only DSA keys are supported");
    return false;
  }
  qsizetype pos = 6;
  if (!ReadMpi(body, &pos, &key->p) || !ReadMpi(body, &pos, &key->q) ||
      !ReadMpi(body, &pos, &key->g) || !ReadMpi(body, &pos, &key->y)) {
    *error = QStringLiteral("the key's numbers are cut short");
    return false;
  }
  const int qBits = key->q.bitLength();
  const BigUint one = BigUint::FromUint(1);
  if ((qBits != 160 && qBits != 224 && qBits != 256) ||
      key->p.bitLength() < 1024 || BigUint::Compare(key->g, one) <= 0 ||
      BigUint::Compare(key->g, key->p) >= 0 ||
      BigUint::Compare(key->y, one) <= 0 ||
      BigUint::Compare(key->y, key->p) >= 0) {
    *error = QStringLiteral("the key's DSA parameters are out of range");
    return false;
  }

  QByteArray framed;
  framed.append(static_cast<char>(0x99));
  framed.append(static_cast<char>((body.size() >> 8) & 0xFF));
  framed.append(static_cast<char>(body.size() & 0xFF));
  framed.append(body);
  key->fingerprint =
      QCryptographicHash::hash(framed, QCryptographicHash::Sha1);
  return true;
}

// Walks one subpacket area. Only the hashed area is trusted for anything but
// picking which key to try.
bool ReadSubpackets(const QByteArray &area, bool hashed, Signature *sig,
                    QString *error) {
  qsizetype pos = 0;
  while (pos < area.size()) {
    const auto first = static_cast<unsigned char>(area.at(pos++));
    qsizetype length = 0;
    if (first < 192) {
      length = first;
    } else if (first < 255) {
      if (pos >= area.size()) {
        *error = QStringLiteral("a signature subpacket is cut short");
        return false;
      }
      length = ((first - 192) << 8) +
               static_cast<unsigned char>(area.at(pos++)) + 192;
    } else {
      if (pos + 4 > area.size()) {
        *error = QStringLiteral("a signature subpacket is cut short");
        return false;
      }
      length = ReadBig(area, pos, 4);
      pos += 4;
    }
    if (length < 1 || length > area.size() - pos) {
      *error = QStringLiteral("a signature subpacket is cut short");
      return false;
    }
    const auto typeByte = static_cast<unsigned char>(area.at(pos));
    const bool critical = (typeByte & 0x80) != 0;
    const int type = typeByte & 0x7F;
    const QByteArray data = area.mid(pos + 1, length - 1);
    pos += length;

    switch (type) {
    case 2: // signature creation time
      if (hashed && data.size() == 4) {
        sig->created = ReadBig(data, 0, 4);
      }
      break;
    case 3: // signature expiration time
      if (hashed && data.size() == 4) {
        sig->expiresAfter = ReadBig(data, 0, 4);
      }
      break;
    case 16: // issuer key ID
      if (data.size() == 8) {
        sig->issuerKeyId = data;
      }
      break;
    case 33: // issuer fingerprint
      if (data.size() == 21 && data.at(0) == 4) {
        sig->issuerFingerprint = data.mid(1);
      }
      break;
    default:
      if (hashed && critical) {
        *error = QStringLiteral("the signature has a critical subpacket "
                                "(type %1) this check doesn't understand")
                     .arg(type);
        return false;
      }
      break;
    }
  }
  return true;
}

bool ParseSignature(const QByteArray &body, Signature *sig, QString *error) {
  if (body.size() < 6 || body.at(0) != 4) {
    *error = QStringLiteral("only version 4 signatures are supported");
    return false;
  }
  if (body.at(1) != kSigCanonicalText) {
    *error = QStringLiteral("the signature isn't a text signature");
    return false;
  }
  if (static_cast<unsigned char>(body.at(2)) != kAlgoDsa) {
    *error = QStringLiteral("only DSA signatures are supported");
    return false;
  }
  sig->hashAlgorithm = static_cast<unsigned char>(body.at(3));
  const int hashedLength = static_cast<int>(ReadBig(body, 4, 2));
  qsizetype pos = 6 + hashedLength;
  if (pos + 2 > body.size()) {
    *error = QStringLiteral("the signature is cut short");
    return false;
  }
  sig->hashedPart = body.left(pos);
  if (!ReadSubpackets(body.mid(6, hashedLength), true, sig, error)) {
    return false;
  }
  const int unhashedLength = static_cast<int>(ReadBig(body, pos, 2));
  pos += 2;
  if (pos + unhashedLength + 2 > body.size()) {
    *error = QStringLiteral("the signature is cut short");
    return false;
  }
  if (!ReadSubpackets(body.mid(pos, unhashedLength), false, sig, error)) {
    return false;
  }
  pos += unhashedLength;
  sig->quickCheck = body.mid(pos, 2);
  pos += 2;
  if (!ReadMpi(body, &pos, &sig->r) || !ReadMpi(body, &pos, &sig->s) ||
      pos != body.size()) {
    *error = QStringLiteral("the signature's numbers are malformed");
    return false;
  }
  if (sig->created == 0) {
    *error = QStringLiteral("the signature has no creation time");
    return false;
  }
  return true;
}

bool HashFor(int algorithm, QCryptographicHash::Algorithm *out) {
  switch (algorithm) {
  case 2:
    *out = QCryptographicHash::Sha1;
    return true;
  case 8:
    *out = QCryptographicHash::Sha256;
    return true;
  case 9:
    *out = QCryptographicHash::Sha384;
    return true;
  case 10:
    *out = QCryptographicHash::Sha512;
    return true;
  case 11:
    *out = QCryptographicHash::Sha224;
    return true;
  default:
    return false;
  }
}

// FIPS 186-4 section 4.7.
bool DsaVerify(const DsaKey &key, const QByteArray &digest, const BigUint &r,
               const BigUint &s) {
  if (r.isZero() || s.isZero() || BigUint::Compare(r, key.q) >= 0 ||
      BigUint::Compare(s, key.q) >= 0) {
    return false;
  }
  // The leftmost bits of the hash, as many as q has. q is 160, 224 or 256
  // bits, so whole bytes always do.
  const BigUint z = BigUint::FromBytes(digest.left(key.q.bitLength() / 8));
  // q is prime, so s^(q-2) is the inverse of s.
  const BigUint w =
      BigUint::PowMod(s, BigUint::Sub(key.q, BigUint::FromUint(2)), key.q);
  const BigUint u1 = BigUint::MulMod(z, w, key.q);
  const BigUint u2 = BigUint::MulMod(r, w, key.q);
  const BigUint v = BigUint::Mod(
      BigUint::MulMod(BigUint::PowMod(key.g, u1, key.p),
                      BigUint::PowMod(key.y, u2, key.p), key.p),
      key.q);
  return v == r;
}

QString Hex(const QByteArray &bytes) {
  return QString::fromLatin1(bytes.toHex().toUpper());
}

} // namespace

Verified Verify(const QByteArray &message,
                const QList<QByteArray> &armoredKeys) {
  Verified result;
  const QList<QByteArray> lines = Lines(message);
  int i = 0;
  while (i < lines.size() && IsBlank(lines.at(i))) {
    ++i;
  }
  if (i >= lines.size() || TrimTrailing(lines.at(i)) != kSignedBegin) {
    result.error = QStringLiteral("it isn't signed");
    return result;
  }
  ++i;
  // "Hash: SHA1" and friends, then a blank line. The signature itself says
  // which hash it used, so these are only checked for shape.
  while (i < lines.size() && !IsBlank(lines.at(i))) {
    if (!lines.at(i).startsWith("Hash:")) {
      result.error = QStringLiteral("its signed block has an unexpected "
                                    "header");
      return result;
    }
    ++i;
  }
  ++i;

  const QByteArray signatureBegin = "-----BEGIN " + kSignatureBlock + "-----";
  QList<QByteArray> signedLines;
  while (i < lines.size() && TrimTrailing(lines.at(i)) != signatureBegin) {
    QByteArray line = lines.at(i);
    if (line.startsWith("- ")) {
      line.remove(0, 2);
    }
    signedLines.append(TrimTrailing(line));
    ++i;
  }
  if (i >= lines.size()) {
    result.error = QStringLiteral("it has no signature");
    return result;
  }

  QByteArray packetData;
  if (!Dearmor(lines, &i, kSignatureBlock, &packetData, &result.error)) {
    return result;
  }
  for (; i < lines.size(); ++i) {
    if (!IsBlank(lines.at(i))) {
      result.error = QStringLiteral("it has unsigned text after the "
                                    "signature");
      return result;
    }
  }

  QList<DsaKey> keys;
  for (const QByteArray &armored : armoredKeys) {
    DsaKey key;
    QString keyError;
    if (!ParseKey(armored, &key, &keyError)) {
      result.error = QStringLiteral("a trusted key couldn't be read: %1")
                         .arg(keyError);
      return result;
    }
    keys.append(key);
  }

  QList<Packet> packets;
  if (!ReadPackets(packetData, &packets, &result.error)) {
    return result;
  }

  const QByteArray canonical = signedLines.join("\r\n");
  QString lastProblem = QStringLiteral("it has no signature");
  for (const Packet &packet : packets) {
    if (packet.tag != kTagSignature) {
      continue;
    }
    Signature sig;
    QString problem;
    if (!ParseSignature(packet.body, &sig, &problem)) {
      lastProblem = problem;
      continue;
    }
    QCryptographicHash::Algorithm algorithm;
    if (!HashFor(sig.hashAlgorithm, &algorithm)) {
      lastProblem = QStringLiteral("the signature uses hash algorithm %1, "
                                   "which isn't supported")
                        .arg(sig.hashAlgorithm);
      continue;
    }
    if (sig.expiresAfter > 0 &&
        sig.created + sig.expiresAfter <
            QDateTime::currentSecsSinceEpoch()) {
      lastProblem = QStringLiteral("the signature has expired");
      continue;
    }

    QCryptographicHash hash(algorithm);
    hash.addData(canonical);
    hash.addData(sig.hashedPart);
    QByteArray trailer;
    trailer.append(static_cast<char>(0x04));
    trailer.append(static_cast<char>(0xFF));
    const int hashedSize = static_cast<int>(sig.hashedPart.size());
    for (int shift = 24; shift >= 0; shift -= 8) {
      trailer.append(static_cast<char>((hashedSize >> shift) & 0xFF));
    }
    hash.addData(trailer);
    const QByteArray digest = hash.result();

    bool fromKnownKey = false;
    for (const DsaKey &key : keys) {
      if (!sig.issuerFingerprint.isEmpty() &&
          sig.issuerFingerprint != key.fingerprint) {
        continue;
      }
      if (sig.issuerFingerprint.isEmpty() && !sig.issuerKeyId.isEmpty() &&
          sig.issuerKeyId != key.fingerprint.right(8)) {
        continue;
      }
      fromKnownKey = true;
      if (digest.left(2) == sig.quickCheck &&
          DsaVerify(key, digest, sig.r, sig.s)) {
        result.ok = true;
        result.text = signedLines.join('\n');
        return result;
      }
    }
    if (fromKnownKey) {
      lastProblem = QStringLiteral("the signature doesn't match the text");
    } else {
      const QByteArray who = sig.issuerFingerprint.isEmpty()
                                 ? sig.issuerKeyId
                                 : sig.issuerFingerprint;
      lastProblem =
          who.isEmpty()
              ? QStringLiteral("it isn't signed by a trusted key")
              : QStringLiteral("it's signed by key %1, which isn't trusted")
                    .arg(Hex(who));
    }
  }
  result.error = lastProblem;
  return result;
}

QString Fingerprint(const QByteArray &armoredKey) {
  DsaKey key;
  QString error;
  if (!ParseKey(armoredKey, &key, &error)) {
    return QString();
  }
  return Hex(key.fingerprint);
}

} // namespace PgpClearsign
