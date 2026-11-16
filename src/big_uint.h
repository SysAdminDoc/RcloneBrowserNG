#pragma once

#include "pch.h"

#include <cstdint>
#include <vector>

// Just enough unsigned big-integer arithmetic to check a DSA signature:
// compare, add, subtract, multiply, remainder and modular powers. Inputs are
// public values (keys, signatures, hashes), so nothing here tries to run in
// constant time. Not a general-purpose library.
class BigUint {
public:
  BigUint() = default;

  // Big-endian bytes, as OpenPGP stores its numbers. Leading zeros are fine.
  static BigUint FromBytes(const QByteArray &bigEndian);
  static BigUint FromUint(std::uint32_t value);
  // Big-endian with no leading zero bytes; empty for zero.
  QByteArray toBytes() const;

  bool isZero() const { return mLimbs.empty(); }
  int bitLength() const;

  // -1, 0 or 1.
  static int Compare(const BigUint &a, const BigUint &b);
  static BigUint Add(const BigUint &a, const BigUint &b);
  // a must not be smaller than b.
  static BigUint Sub(const BigUint &a, const BigUint &b);
  static BigUint Mul(const BigUint &a, const BigUint &b);
  // m must not be zero.
  static BigUint Mod(const BigUint &a, const BigUint &m);
  static BigUint MulMod(const BigUint &a, const BigUint &b, const BigUint &m);
  static BigUint PowMod(const BigUint &base, const BigUint &exponent,
                        const BigUint &m);

  friend bool operator==(const BigUint &a, const BigUint &b) {
    return a.mLimbs == b.mLimbs;
  }
  friend bool operator!=(const BigUint &a, const BigUint &b) {
    return !(a == b);
  }

private:
  void trim();
  bool bit(int index) const;

  // Least significant limb first, with no zero limbs at the top.
  std::vector<std::uint32_t> mLimbs;
};
