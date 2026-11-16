#include "big_uint.h"

#include <algorithm>

namespace {

constexpr std::uint64_t kBase = std::uint64_t(1) << 32;

int LeadingZeros(std::uint32_t value) {
  int count = 0;
  while (count < 32 && !(value & (std::uint32_t(1) << (31 - count)))) {
    ++count;
  }
  return count;
}

} // namespace

BigUint BigUint::FromBytes(const QByteArray &bigEndian) {
  BigUint result;
  const qsizetype size = bigEndian.size();
  result.mLimbs.assign(static_cast<size_t>((size + 3) / 4), 0);
  for (qsizetype i = 0; i < size; ++i) {
    const qsizetype fromEnd = size - 1 - i;
    const auto byte = static_cast<std::uint32_t>(
        static_cast<unsigned char>(bigEndian.at(i)));
    result.mLimbs[static_cast<size_t>(fromEnd / 4)] |= byte
                                                       << (8 * (fromEnd % 4));
  }
  result.trim();
  return result;
}

BigUint BigUint::FromUint(std::uint32_t value) {
  BigUint result;
  if (value) {
    result.mLimbs.push_back(value);
  }
  return result;
}

QByteArray BigUint::toBytes() const {
  QByteArray out;
  for (size_t i = mLimbs.size(); i-- > 0;) {
    for (int shift = 24; shift >= 0; shift -= 8) {
      const char byte = static_cast<char>((mLimbs[i] >> shift) & 0xFF);
      if (out.isEmpty() && byte == 0) {
        continue;
      }
      out.append(byte);
    }
  }
  return out;
}

int BigUint::bitLength() const {
  if (mLimbs.empty()) {
    return 0;
  }
  return static_cast<int>(mLimbs.size()) * 32 - LeadingZeros(mLimbs.back());
}

int BigUint::Compare(const BigUint &a, const BigUint &b) {
  if (a.mLimbs.size() != b.mLimbs.size()) {
    return a.mLimbs.size() < b.mLimbs.size() ? -1 : 1;
  }
  for (size_t i = a.mLimbs.size(); i-- > 0;) {
    if (a.mLimbs[i] != b.mLimbs[i]) {
      return a.mLimbs[i] < b.mLimbs[i] ? -1 : 1;
    }
  }
  return 0;
}

BigUint BigUint::Add(const BigUint &a, const BigUint &b) {
  BigUint result;
  const size_t size = std::max(a.mLimbs.size(), b.mLimbs.size());
  result.mLimbs.resize(size + 1, 0);
  std::uint64_t carry = 0;
  for (size_t i = 0; i < size; ++i) {
    const std::uint64_t sum =
        std::uint64_t(i < a.mLimbs.size() ? a.mLimbs[i] : 0) +
        (i < b.mLimbs.size() ? b.mLimbs[i] : 0) + carry;
    result.mLimbs[i] = static_cast<std::uint32_t>(sum);
    carry = sum >> 32;
  }
  result.mLimbs[size] = static_cast<std::uint32_t>(carry);
  result.trim();
  return result;
}

BigUint BigUint::Sub(const BigUint &a, const BigUint &b) {
  Q_ASSERT(Compare(a, b) >= 0);
  BigUint result = a;
  std::int64_t borrow = 0;
  for (size_t i = 0; i < result.mLimbs.size(); ++i) {
    std::int64_t diff = std::int64_t(result.mLimbs[i]) -
                        (i < b.mLimbs.size() ? b.mLimbs[i] : 0) - borrow;
    borrow = diff < 0 ? 1 : 0;
    if (diff < 0) {
      diff += std::int64_t(kBase);
    }
    result.mLimbs[i] = static_cast<std::uint32_t>(diff);
  }
  result.trim();
  return result;
}

BigUint BigUint::Mul(const BigUint &a, const BigUint &b) {
  BigUint result;
  if (a.isZero() || b.isZero()) {
    return result;
  }
  result.mLimbs.assign(a.mLimbs.size() + b.mLimbs.size(), 0);
  for (size_t i = 0; i < a.mLimbs.size(); ++i) {
    std::uint64_t carry = 0;
    for (size_t j = 0; j < b.mLimbs.size(); ++j) {
      const std::uint64_t cur = std::uint64_t(a.mLimbs[i]) * b.mLimbs[j] +
                                result.mLimbs[i + j] + carry;
      result.mLimbs[i + j] = static_cast<std::uint32_t>(cur);
      carry = cur >> 32;
    }
    result.mLimbs[i + b.mLimbs.size()] = static_cast<std::uint32_t>(carry);
  }
  result.trim();
  return result;
}

// Knuth's algorithm D (TAOCP vol. 2, 4.3.1), following the divmnu
// formulation in Hacker's Delight 9-2, keeping only the remainder.
BigUint BigUint::Mod(const BigUint &a, const BigUint &m) {
  Q_ASSERT(!m.isZero());
  if (Compare(a, m) < 0) {
    return a;
  }
  const size_t n = m.mLimbs.size();
  if (n == 1) {
    std::uint64_t rem = 0;
    for (size_t i = a.mLimbs.size(); i-- > 0;) {
      rem = ((rem << 32) | a.mLimbs[i]) % m.mLimbs[0];
    }
    return FromUint(static_cast<std::uint32_t>(rem));
  }

  // Shift both so the divisor's top limb has its high bit set.
  const int s = LeadingZeros(m.mLimbs.back());
  const size_t len = a.mLimbs.size();
  std::vector<std::uint32_t> vn(n);
  for (size_t i = n - 1; i > 0; --i) {
    vn[i] = s ? (m.mLimbs[i] << s) | (m.mLimbs[i - 1] >> (32 - s))
              : m.mLimbs[i];
  }
  vn[0] = m.mLimbs[0] << s;
  std::vector<std::uint32_t> un(len + 1);
  un[len] = s ? a.mLimbs[len - 1] >> (32 - s) : 0;
  for (size_t i = len - 1; i > 0; --i) {
    un[i] = s ? (a.mLimbs[i] << s) | (a.mLimbs[i - 1] >> (32 - s))
              : a.mLimbs[i];
  }
  un[0] = a.mLimbs[0] << s;

  for (size_t j = len - n + 1; j-- > 0;) {
    const std::uint64_t top = (std::uint64_t(un[j + n]) << 32) | un[j + n - 1];
    std::uint64_t qhat = top / vn[n - 1];
    std::uint64_t rhat = top % vn[n - 1];
    while (qhat >= kBase ||
           qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
      --qhat;
      rhat += vn[n - 1];
      if (rhat >= kBase) {
        break;
      }
    }

    // un[j..j+n] -= qhat * vn
    std::int64_t k = 0;
    std::int64_t t = 0;
    for (size_t i = 0; i < n; ++i) {
      const std::uint64_t p = qhat * vn[i];
      t = std::int64_t(un[i + j]) - k - std::int64_t(p & 0xFFFFFFFFu);
      un[i + j] = static_cast<std::uint32_t>(t);
      k = std::int64_t(p >> 32) - (t >> 32);
    }
    t = std::int64_t(un[j + n]) - k;
    un[j + n] = static_cast<std::uint32_t>(t);

    // qhat was one too big: add the divisor back once.
    if (t < 0) {
      std::uint64_t carry = 0;
      for (size_t i = 0; i < n; ++i) {
        const std::uint64_t sum = std::uint64_t(un[i + j]) + vn[i] + carry;
        un[i + j] = static_cast<std::uint32_t>(sum);
        carry = sum >> 32;
      }
      un[j + n] = static_cast<std::uint32_t>(un[j + n] + carry);
    }
  }

  BigUint result;
  result.mLimbs.resize(n);
  for (size_t i = 0; i < n; ++i) {
    result.mLimbs[i] =
        s ? (un[i] >> s) | (un[i + 1] << (32 - s)) : un[i];
  }
  result.trim();
  return result;
}

BigUint BigUint::MulMod(const BigUint &a, const BigUint &b, const BigUint &m) {
  return Mod(Mul(a, b), m);
}

BigUint BigUint::PowMod(const BigUint &base, const BigUint &exponent,
                        const BigUint &m) {
  BigUint result = Mod(FromUint(1), m);
  const BigUint b = Mod(base, m);
  for (int i = exponent.bitLength(); i-- > 0;) {
    result = MulMod(result, result, m);
    if (exponent.bit(i)) {
      result = MulMod(result, b, m);
    }
  }
  return result;
}

void BigUint::trim() {
  while (!mLimbs.empty() && mLimbs.back() == 0) {
    mLimbs.pop_back();
  }
}

bool BigUint::bit(int index) const {
  const auto limb = static_cast<size_t>(index / 32);
  if (limb >= mLimbs.size()) {
    return false;
  }
  return ((mLimbs[limb] >> (index % 32)) & 1u) != 0;
}
