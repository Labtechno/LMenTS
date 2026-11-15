// LMenTS — calcul en virgule flottante simple précision, version exacte.
//
// Reproduit le comportement de l'unité de calcul du Cortex-M4F (VFPv4-SP) dans tous
// les cas : quatre modes d'arrondi, nombres dénormalisés, « flush-to-zero », NaN,
// infinis, et drapeaux d'exception du registre FPSCR. Suit le pseudo-code du
// manuel ARMv7-M (FPAdd, FPMul, FPRound...).
//
// Ce code est lent : le processeur émulé ne l'appelle que pour les cas rares. Les
// cas courants passent par le calcul flottant de l'ordinateur (voir CortexM4Fpu.inc),
// dont le résultat est alors garanti identique par la norme IEEE 754.
//
// Écrit pour LMenTS, sans code tiers. Licence MIT.

#pragma once

#include <cstdint>

namespace lments {
namespace emu {
namespace sf {

enum : uint32_t {
  kIOC = 1u << 0,   // opération invalide
  kDZC = 1u << 1,   // division par zéro
  kOFC = 1u << 2,   // dépassement
  kUFC = 1u << 3,   // soupassement
  kIXC = 1u << 4,   // résultat inexact
  kIDC = 1u << 7,   // entrée dénormalisée (remplacée par zéro)
  kRModeShift = 22,
  kFZ = 1u << 24,
  kDN = 1u << 25,
  kAHP = 1u << 26,
};
enum : uint32_t { kRoundNearest = 0, kRoundPlus = 1, kRoundMinus = 2, kRoundZero = 3 };

constexpr uint32_t kDefaultNaN = 0x7fc00000u;

typedef unsigned __int128 u128;

enum Type { kZero, kNonzero, kInf, kQNaN, kSNaN };

// valeur = mant * 2^exp
struct Num {
  Type type;
  bool sign;
  uint32_t mant;
  int exp;
};

inline Num unpack(uint32_t x, uint32_t& fpscr) {
  Num n;
  n.sign = x >> 31;
  n.mant = 0;
  n.exp = 0;
  const uint32_t e = (x >> 23) & 0xff, f = x & 0x7fffffu;
  if (e == 0) {
    if (f == 0) {
      n.type = kZero;
    } else if (fpscr & kFZ) {
      n.type = kZero;
      fpscr |= kIDC;
    } else {
      n.type = kNonzero; n.mant = f; n.exp = -149;
    }
  } else if (e == 255) {
    n.type = f == 0 ? kInf : ((f & 0x400000u) ? kQNaN : kSNaN);
  } else {
    n.type = kNonzero; n.mant = f | 0x800000u; n.exp = static_cast<int>(e) - 150;
  }
  return n;
}

inline bool isNaN(const Num& n) { return n.type == kQNaN || n.type == kSNaN; }

inline uint32_t processNaN(Type type, uint32_t op, uint32_t& fpscr) {
  if (type == kSNaN) { fpscr |= kIOC; op |= 0x400000u; }
  return (fpscr & kDN) ? kDefaultNaN : op;
}

inline uint32_t invalid(uint32_t& fpscr) { fpscr |= kIOC; return kDefaultNaN; }

// Division d'un nombre de 128 bits par un nombre de 64 bits, bit par bit. Le
// compilateur ne la fournit pas sous Windows (il faudrait une bibliothèque en
// plus) ; elle ne sert que dans les cas rares, la lenteur n'a pas d'importance.
inline u128 divide128(u128 numerator, uint64_t denominator, uint64_t& remainder) {
  u128 quotient = 0, rest = 0;
  for (int i = 127; i >= 0; --i) {
    rest = (rest << 1) | ((numerator >> i) & 1);
    if (rest >= denominator) { rest -= denominator; quotient |= u128 { 1 } << i; }
  }
  remainder = static_cast<uint64_t>(rest);
  return quotient;
}

inline int msb128(u128 m) {
  const uint64_t hi = static_cast<uint64_t>(m >> 64);
  if (hi) return 127 - __builtin_clzll(hi);
  return 63 - __builtin_clzll(static_cast<uint64_t>(m));
}

// Arrondit m * 2^e (m non nul ; `sticky` = il reste un résidu plus petit que le
// dernier bit de m) au format demandé : F bits de mantisse, exposant sur `expBits`.
// half = demi-précision (pas de flush-to-zero, et mode « alternatif » possible).
inline uint32_t roundPack(bool sign, u128 m, int e, bool sticky, uint32_t& fpscr,
                          int F = 23, int expBits = 8, bool half = false) {
  const int bias = (1 << (expBits - 1)) - 1;
  const uint32_t signBit = static_cast<uint32_t>(sign) << (F + expBits);
  const int p = msb128(m);
  const int E = e + p;
  const int minE = 1 - bias;
  int biased, shift;
  if (E >= minE) {
    biased = E + bias;
    shift = p - F;
  } else {
    if ((fpscr & kFZ) && !half) { fpscr |= kUFC; return signBit; }
    biased = 0;
    shift = (minE - F) - e;
  }
  uint64_t mant;
  bool guard = false, rest = sticky;
  if (shift <= 0) {
    mant = static_cast<uint64_t>(m << -shift);
  } else if (shift > 128) {
    mant = 0; rest = true;
  } else if (shift == 128) {
    mant = 0;
    guard = (m >> 127) & 1;
    rest = rest || (m & ((u128 { 1 } << 127) - 1)) != 0;
  } else {
    mant = static_cast<uint64_t>(m >> shift);
    guard = (m >> (shift - 1)) & 1;
    rest = rest || (m & ((u128 { 1 } << (shift - 1)) - 1)) != 0;
  }
  bool inexact = guard || rest;
  bool up;
  switch ((fpscr >> kRModeShift) & 3) {
    case kRoundNearest: up = guard && (rest || (mant & 1)); break;
    case kRoundPlus: up = inexact && !sign; break;
    case kRoundMinus: up = inexact && sign; break;
    default: up = false; break;
  }
  if (biased == 0 && inexact) fpscr |= kUFC;   // « minuscule » jugé avant l'arrondi
  if (up) {
    ++mant;
    if (biased != 0 && mant == (uint64_t { 2 } << F)) { mant >>= 1; ++biased; }
  }
  const int maxBiased = (1 << expBits) - 1;
  if (half && (fpscr & kAHP)) {
    if (biased > maxBiased) {
      fpscr |= kIOC;
      return signBit | ((1u << (F + expBits)) - 1);
    }
  } else if (biased >= maxBiased) {
    fpscr |= kOFC | kIXC;
    bool toInf;
    switch ((fpscr >> kRModeShift) & 3) {
      case kRoundNearest: toInf = true; break;
      case kRoundPlus: toInf = !sign; break;
      case kRoundMinus: toInf = sign; break;
      default: toInf = false; break;
    }
    const uint32_t inf = static_cast<uint32_t>(maxBiased) << F;
    return signBit | (toInf ? inf : inf - 1);
  }
  if (inexact) fpscr |= kIXC;
  // Un dénormalisé arrondi vers le haut jusqu'à 2^F devient le plus petit normal :
  // l'addition ci-dessous le code naturellement.
  return signBit | ((static_cast<uint32_t>(biased) << F) +
                    static_cast<uint32_t>(mant) - (biased ? (1u << F) : 0));
}

// Somme exacte de deux valeurs signées (ma * 2^ea) + (mb * 2^eb), puis arrondi.
// Les mantisses tiennent sur 48 bits au plus.
inline uint32_t addRound(bool sa, u128 ma, int ea, bool sb, u128 mb, int eb, uint32_t& fpscr) {
  const bool zeroSign = ((fpscr >> kRModeShift) & 3) == kRoundMinus;
  if (ma == 0 && mb == 0) return static_cast<uint32_t>(zeroSign) << 31;
  if (ma == 0) return roundPack(sb, mb, eb, false, fpscr);
  if (mb == 0) return roundPack(sa, ma, ea, false, fpscr);
  if (ea < eb) {
    const bool ts = sa; sa = sb; sb = ts;
    const u128 tm = ma; ma = mb; mb = tm;
    const int te = ea; ea = eb; eb = te;
  }
  // a a le plus grand exposant : on le décale à gauche (64 bits au plus), et b à
  // droite pour le reste, en notant s'il perd des bits.
  int d = ea - eb;
  bool sticky = false;
  const int left = d > 64 ? 64 : d;
  ma <<= left;
  int e = ea - left;
  d -= left;
  if (d > 0) {
    if (d >= 64) { sticky = true; mb = 0; }
    else { sticky = (mb & ((u128 { 1 } << d) - 1)) != 0; mb >>= d; }
  }
  if (sa == sb) return roundPack(sa, ma + mb, e, sticky, fpscr);
  bool sign = sa;
  u128 m;
  if (ma >= mb) {
    m = ma - mb;
    if (sticky) --m;          // a - (b + résidu) = (a - b - 1) + (1 - résidu)
  } else {
    m = mb - ma;              // d = 0 ici : pas de résidu
    sign = sb;
  }
  if (m == 0) return static_cast<uint32_t>(zeroSign) << 31;   // (jamais avec un résidu : a >= 2^64 alors)
  return roundPack(sign, m, e, sticky, fpscr);
}

inline uint32_t add(uint32_t a, uint32_t b, uint32_t& fpscr, bool subtract = false) {
  const Num x = unpack(a, fpscr), y = unpack(b, fpscr);
  if (x.type == kSNaN) return processNaN(x.type, a, fpscr);
  if (y.type == kSNaN) return processNaN(y.type, b, fpscr);
  if (x.type == kQNaN) return processNaN(x.type, a, fpscr);
  if (y.type == kQNaN) return processNaN(y.type, b, fpscr);
  const bool sy = y.sign != subtract;
  if (x.type == kInf && y.type == kInf && x.sign != sy) return invalid(fpscr);
  if (x.type == kInf) return (static_cast<uint32_t>(x.sign) << 31) | 0x7f800000u;
  if (y.type == kInf) return (static_cast<uint32_t>(sy) << 31) | 0x7f800000u;
  if (x.type == kZero && y.type == kZero && x.sign == sy) return static_cast<uint32_t>(x.sign) << 31;
  return addRound(x.sign, x.mant, x.exp, sy, y.mant, y.exp, fpscr);
}

inline uint32_t mul(uint32_t a, uint32_t b, uint32_t& fpscr) {
  const Num x = unpack(a, fpscr), y = unpack(b, fpscr);
  if (x.type == kSNaN) return processNaN(x.type, a, fpscr);
  if (y.type == kSNaN) return processNaN(y.type, b, fpscr);
  if (x.type == kQNaN) return processNaN(x.type, a, fpscr);
  if (y.type == kQNaN) return processNaN(y.type, b, fpscr);
  const uint32_t sign = static_cast<uint32_t>(x.sign != y.sign) << 31;
  if ((x.type == kInf && y.type == kZero) || (x.type == kZero && y.type == kInf)) return invalid(fpscr);
  if (x.type == kInf || y.type == kInf) return sign | 0x7f800000u;
  if (x.type == kZero || y.type == kZero) return sign;
  return roundPack(sign != 0, static_cast<u128>(static_cast<uint64_t>(x.mant) * y.mant), x.exp + y.exp, false, fpscr);
}

inline uint32_t div(uint32_t a, uint32_t b, uint32_t& fpscr) {
  const Num x = unpack(a, fpscr), y = unpack(b, fpscr);
  if (x.type == kSNaN) return processNaN(x.type, a, fpscr);
  if (y.type == kSNaN) return processNaN(y.type, b, fpscr);
  if (x.type == kQNaN) return processNaN(x.type, a, fpscr);
  if (y.type == kQNaN) return processNaN(y.type, b, fpscr);
  const uint32_t sign = static_cast<uint32_t>(x.sign != y.sign) << 31;
  if ((x.type == kInf && y.type == kInf) || (x.type == kZero && y.type == kZero)) return invalid(fpscr);
  if (x.type == kInf || y.type == kZero) {
    if (x.type != kInf) fpscr |= kDZC;
    return sign | 0x7f800000u;
  }
  if (x.type == kZero || y.type == kInf) return sign;
  const u128 num = static_cast<u128>(x.mant) << 64;
  uint64_t remainder;
  const u128 q = divide128(num, y.mant, remainder);
  const bool sticky = remainder != 0;
  return roundPack(sign != 0, q, x.exp - y.exp - 64, sticky, fpscr);
}

inline uint32_t sqrt(uint32_t a, uint32_t& fpscr) {
  const Num x = unpack(a, fpscr);
  if (isNaN(x)) return processNaN(x.type, a, fpscr);
  if (x.type == kZero) return static_cast<uint32_t>(x.sign) << 31;
  if (x.type == kInf && !x.sign) return a;
  if (x.sign) return invalid(fpscr);
  u128 m = x.mant;
  int e = x.exp;
  if (e & 1) { m <<= 1; e -= 1; }
  m <<= 64;
  e -= 64;
  // Racine entière, bit par bit.
  u128 root = 0;
  for (int bit = 63; bit >= 0; --bit) {
    const u128 trial = root | (u128 { 1 } << bit);
    if (trial * trial <= m) root = trial;
  }
  return roundPack(false, root, e / 2, root * root != m, fpscr);
}

// addend + a * b, avec un seul arrondi (FPMulAdd).
inline uint32_t mulAdd(uint32_t addend, uint32_t a, uint32_t b, uint32_t& fpscr) {
  const Num z = unpack(addend, fpscr), x = unpack(a, fpscr), y = unpack(b, fpscr);
  const bool infTimesZero = (x.type == kInf && y.type == kZero) || (x.type == kZero && y.type == kInf);
  if (z.type == kQNaN && infTimesZero) return invalid(fpscr);
  if (z.type == kSNaN) return processNaN(z.type, addend, fpscr);
  if (x.type == kSNaN) return processNaN(x.type, a, fpscr);
  if (y.type == kSNaN) return processNaN(y.type, b, fpscr);
  if (z.type == kQNaN) return processNaN(z.type, addend, fpscr);
  if (x.type == kQNaN) return processNaN(x.type, a, fpscr);
  if (y.type == kQNaN) return processNaN(y.type, b, fpscr);
  const bool signP = x.sign != y.sign;
  const bool infP = x.type == kInf || y.type == kInf;
  const bool zeroP = x.type == kZero || y.type == kZero;
  const bool infA = z.type == kInf, zeroA = z.type == kZero;
  if (infTimesZero || (infA && infP && z.sign != signP)) return invalid(fpscr);
  if ((infA && !z.sign) || (infP && !signP)) return 0x7f800000u;
  if ((infA && z.sign) || (infP && signP)) return 0xff800000u;
  if (zeroA && zeroP && z.sign == signP) return static_cast<uint32_t>(z.sign) << 31;
  const u128 mp = zeroP ? u128 { 0 } : static_cast<u128>(static_cast<uint64_t>(x.mant) * y.mant);
  return addRound(z.sign, zeroA ? 0 : z.mant, z.exp, signP, mp, x.exp + y.exp, fpscr);
}

// Comparaison : rend les drapeaux N Z C V (bits 3..0).
inline uint32_t compare(uint32_t a, uint32_t b, bool quietNaNException, uint32_t& fpscr) {
  const Num x = unpack(a, fpscr), y = unpack(b, fpscr);
  if (isNaN(x) || isNaN(y)) {
    if (x.type == kSNaN || y.type == kSNaN || quietNaNException) fpscr |= kIOC;
    return 0x3;
  }
  // Ordre total sur (signe, grandeur), zéros confondus.
  auto key = [](const Num& n, uint32_t bits) -> int64_t {
    if (n.type == kZero) return 0;
    const int64_t mag = bits & 0x7fffffffu;
    return n.sign ? -mag : mag;
  };
  const int64_t kx = key(x, a), ky = key(y, b);
  return kx == ky ? 0x6 : (kx < ky ? 0x8 : 0x2);
}

// Flottant vers entier ou virgule fixe (FPToFixed). size = 16 ou 32 bits.
inline uint32_t toFixed(uint32_t a, int size, int fracBits, bool isUnsigned, bool roundZero, uint32_t& fpscr) {
  const Num x = unpack(a, fpscr);
  const int64_t maxV = isUnsigned ? (int64_t { 1 } << size) - 1 : (int64_t { 1 } << (size - 1)) - 1;
  const int64_t minV = isUnsigned ? 0 : -(int64_t { 1 } << (size - 1));
  if (isNaN(x)) { fpscr |= kIOC; return 0; }
  if (x.type == kZero) return 0;
  if (x.type == kInf) { fpscr |= kIOC; return static_cast<uint32_t>(x.sign ? minV : maxV); }
  // |valeur| * 2^frac = mant * 2^e
  const int e = x.exp + fracBits;
  u128 whole;
  bool guard = false, rest = false;
  if (e >= 0) {
    if (e > 40) { fpscr |= kIOC; return static_cast<uint32_t>(x.sign ? minV : maxV); }
    whole = static_cast<u128>(x.mant) << e;
  } else if (-e > 64) {
    whole = 0; rest = true;
  } else {
    const int s = -e;
    whole = static_cast<u128>(x.mant) >> s;
    guard = (static_cast<u128>(x.mant) >> (s - 1)) & 1;
    rest = (static_cast<u128>(x.mant) & ((u128 { 1 } << (s - 1)) - 1)) != 0;
  }
  const bool inexact = guard || rest;
  bool up;
  switch (roundZero ? static_cast<uint32_t>(kRoundZero) : ((fpscr >> kRModeShift) & 3)) {
    case kRoundNearest: up = guard && (rest || (whole & 1)); break;
    case kRoundPlus: up = inexact && !x.sign; break;
    case kRoundMinus: up = inexact && x.sign; break;
    default: up = false; break;
  }
  if (up) ++whole;
  int64_t value;
  bool overflow = false;
  if (whole > (u128 { 1 } << 40)) {
    overflow = true;
    value = x.sign ? minV : maxV;
  } else {
    value = x.sign ? -static_cast<int64_t>(whole) : static_cast<int64_t>(whole);
    if (value > maxV) { value = maxV; overflow = true; }
    else if (value < minV) { value = minV; overflow = true; }
  }
  if (overflow) fpscr |= kIOC;
  else if (inexact) fpscr |= kIXC;
  return static_cast<uint32_t>(value);
}

// Entier ou virgule fixe vers flottant (FixedToFP). `value` est déjà étendu sur 64 bits.
inline uint32_t fromFixed(int64_t value, int fracBits, bool roundNearest, uint32_t& fpscr) {
  if (value == 0) return 0;
  const bool sign = value < 0;
  const u128 m = sign ? static_cast<u128>(-static_cast<__int128>(value)) : static_cast<u128>(value);
  if (!roundNearest) return roundPack(sign, m, -fracBits, false, fpscr);
  uint32_t env = fpscr & ~(3u << kRModeShift);
  const uint32_t result = roundPack(sign, m, -fracBits, false, env);
  fpscr |= env & 0x9f;
  return result;
}

inline uint32_t halfToSingle(uint32_t h, uint32_t& fpscr) {
  const uint32_t sign = (h >> 15) & 1, e = (h >> 10) & 0x1f, f = h & 0x3ff;
  if (e == 0x1f && !(fpscr & kAHP)) {
    if (f == 0) return (sign << 31) | 0x7f800000u;
    if (!(f & 0x200)) fpscr |= kIOC;
    if (fpscr & kDN) return kDefaultNaN;
    return (sign << 31) | 0x7fc00000u | ((f & 0x1ff) << 13);
  }
  if (e == 0) {
    if (f == 0) return sign << 31;
    return roundPack(sign, f, -24, false, fpscr);
  }
  return roundPack(sign, f | 0x400, static_cast<int>(e) - 25, false, fpscr);
}

inline uint32_t singleToHalf(uint32_t a, uint32_t& fpscr) {
  const Num x = unpack(a, fpscr);
  const uint32_t sign = static_cast<uint32_t>(x.sign) << 15;
  if (isNaN(x)) {
    if (x.type == kSNaN || (fpscr & kAHP)) fpscr |= kIOC;
    if (fpscr & kAHP) return sign;
    if (fpscr & kDN) return 0x7e00;
    return sign | 0x7e00 | ((a >> 13) & 0x1ff);
  }
  if (x.type == kInf) {
    if (fpscr & kAHP) { fpscr |= kIOC; return sign | 0x7fff; }
    return sign | 0x7c00;
  }
  if (x.type == kZero) return sign;
  return roundPack(x.sign, x.mant, x.exp, false, fpscr, 10, 5, true);
}

}  // namespace sf
}  // namespace emu
}  // namespace lments
