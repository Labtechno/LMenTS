// LMenTS — unité de calcul en virgule flottante du Cortex-M4F (VFPv4-SP-D16).
// Voir CortexM4.h. Licence MIT.
//
// Deux chemins pour chaque opération :
//   - rapide : l'ordinateur fait le calcul avec ses propres nombres flottants. La
//     norme IEEE 754 garantit alors le même résultat, bit pour bit, que la puce,
//     à condition que les opérandes et le résultat soient des nombres « ordinaires »
//     (ni NaN, ni infini, ni dénormalisé), que l'arrondi soit « au plus près » et
//     que le drapeau « inexact » soit déjà levé (il ne peut alors plus changer) ;
//   - exact : tous les autres cas passent par SoftFloat.h, qui suit pas à pas le
//     manuel ARM (arrondis, drapeaux, NaN, flush-to-zero...).
// Le chemin rapide ne dépend d'aucun réglage flottant de l'ordinateur : si
// celui-ci écrase les très petits nombres (réglage courant dans les logiciels
// audio), le résultat sort des cas « ordinaires » et repasse par le chemin exact.

#include "CortexM4.h"
#include "SoftFloat.h"

#include <cmath>

#pragma STDC FP_CONTRACT OFF

namespace lments {
namespace emu {

namespace {

inline float asFloat(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
inline uint32_t asBits(float f) { uint32_t bits; std::memcpy(&bits, &f, 4); return bits; }

// Nombre normal : ni zéro, ni dénormalisé, ni infini, ni NaN.
inline bool isNormal(uint32_t a) { return ((a & 0x7f800000u) - 0x00800000u) < 0x7f000000u; }
inline bool isZero(uint32_t a) { return (a << 1) == 0; }
inline bool isPlain(uint32_t a) { return isNormal(a) || isZero(a); }
// Résultat sûr d'une multiplication ou d'une division : normal, mais pas le plus
// petit nombre normal. Celui-ci peut venir de l'arrondi d'une valeur plus petite,
// que la puce signale comme un soupassement (et écrase en mode flush-to-zero).
inline bool isSafeProduct(uint32_t r) { return ((r & 0x7f800000u) - 0x00800000u) < 0x7f000000u && (r & 0x7fffffffu) != 0x00800000u; }

constexpr uint32_t kSign = 0x80000000u;

}  // namespace

// Vérifications avant toute instruction flottante (ExecuteFPCheck du manuel ARM).
bool CortexM4::fpCheck() {
  const uint32_t access = (cpacr_ >> 20) & 3;
  if (access == 0 || access == 2 || (access == 1 && controlNpriv_ && ipsr_ == 0)) {
    cfsr_ |= 1u << 19;
    raiseFault("unite flottante desactivee (CPACR)");
    return false;
  }
  if (fpccr_ & 1) preserveFpState();
  if ((fpccr_ & 0x80000000u) && !controlFpca_) {
    setFpscr((fpscr_ & ~0x07c00000u) | (fpdscr_ & 0x07c00000u));
    controlFpca_ = true;
  }
  ++changes_;   // une boucle d'attente ne calcule pas en flottant
  return true;
}

// Sauvegarde paresseuse : écrit maintenant les registres dans la place réservée
// sur la pile à l'entrée de l'interruption.
void CortexM4::preserveFpState() {
  for (uint32_t i = 0; i < 16; ++i) write32(fpcar_ + 4 * i, s_[i]);
  write32(fpcar_ + 0x40, fpscr_);
  fpccr_ &= ~1u;
  cycles_ += 17;
}

uint32_t CortexM4::fpAdd(uint32_t a, uint32_t b) {
  if (fpFast_ && isPlain(a) && isPlain(b)) {
    const uint32_t r = asBits(asFloat(a) + asFloat(b));
    if (isNormal(r)) return r;
    if (isZero(r) && ((a ^ b) == kSign || (isZero(a) && isZero(b)))) return r;
  }
  uint32_t env = fpscr_;
  const uint32_t r = sf::add(a, b, env);
  if (env != fpscr_) setFpscr(env);
  return r;
}

uint32_t CortexM4::fpSub(uint32_t a, uint32_t b) {
  if (fpFast_ && isPlain(a) && isPlain(b)) {
    const uint32_t r = asBits(asFloat(a) - asFloat(b));
    if (isNormal(r)) return r;
    if (isZero(r) && (a == b || (isZero(a) && isZero(b)))) return r;
  }
  uint32_t env = fpscr_;
  const uint32_t r = sf::add(a, b, env, true);
  if (env != fpscr_) setFpscr(env);
  return r;
}

uint32_t CortexM4::fpMul(uint32_t a, uint32_t b) {
  if (fpFast_ && isPlain(a) && isPlain(b)) {
    const uint32_t r = asBits(asFloat(a) * asFloat(b));
    if (isSafeProduct(r)) return r;
    if (isZero(r) && (isZero(a) || isZero(b))) return r;
  }
  uint32_t env = fpscr_;
  const uint32_t r = sf::mul(a, b, env);
  if (env != fpscr_) setFpscr(env);
  return r;
}

uint32_t CortexM4::fpDiv(uint32_t a, uint32_t b) {
  if (fpFast_ && isPlain(a) && isNormal(b)) {
    const uint32_t r = asBits(asFloat(a) / asFloat(b));
    if (isSafeProduct(r)) return r;
    if (isZero(a)) return (a ^ b) & kSign;
  }
  uint32_t env = fpscr_;
  const uint32_t r = sf::div(a, b, env);
  if (env != fpscr_) setFpscr(env);
  return r;
}

uint32_t CortexM4::fpSqrt(uint32_t a) {
  if (fpFast_) {
    if (isNormal(a) && !(a & kSign)) return asBits(std::sqrt(asFloat(a)));
    if (isZero(a)) return a;
  }
  uint32_t env = fpscr_;
  const uint32_t r = sf::sqrt(a, env);
  if (env != fpscr_) setFpscr(env);
  return r;
}

// addend + a * b avec un seul arrondi.
uint32_t CortexM4::fpMulAdd(uint32_t addend, uint32_t a, uint32_t b) {
  if (fpFast_ && isPlain(addend) && isPlain(a) && isPlain(b)) {
#if defined(__aarch64__) || defined(__FMA__)
    const uint32_t r = asBits(__builtin_fmaf(asFloat(a), asFloat(b), asFloat(addend)));
#else
    // Sans instruction « fused multiply-add » : calcul en double précision. Le
    // produit de deux float y est exact ; la somme est rendue « impaire » quand elle
    // n'est pas exacte, ce qui garantit que l'arrondi final vers float est le bon.
    const double p = static_cast<double>(asFloat(a)) * static_cast<double>(asFloat(b));
    const double c = static_cast<double>(asFloat(addend));
    double s = p + c;
    const double t = s - p;
    const double err = (p - (s - t)) + (c - t);
    if (err != 0.0) {
      uint64_t bits;
      std::memcpy(&bits, &s, 8);
      if (!(bits & 1)) {
        bits += ((err > 0.0) == (s > 0.0)) ? 1 : -1;
        std::memcpy(&s, &bits, 8);
      }
    }
    const uint32_t r = asBits(static_cast<float>(s));
#endif
    if (isSafeProduct(r)) return r;
  }
  uint32_t env = fpscr_;
  const uint32_t r = sf::mulAdd(addend, a, b, env);
  if (env != fpscr_) setFpscr(env);
  return r;
}

// Opérations de calcul (VADD, VMUL, VCVT...), simple précision uniquement.
void CortexM4::execFpData(uint32_t insn) {
  const uint32_t hw1 = insn >> 16, hw2 = insn & 0xffff;
  if (hw2 & 0x100) { undefined(insn); return; }   // double précision : absente de cette puce
  const uint32_t d = ((hw2 >> 12) << 1) | ((hw1 >> 6) & 1);
  const uint32_t n = ((hw1 & 15) << 1) | ((hw2 >> 7) & 1);
  const uint32_t m = ((hw2 & 15) << 1) | ((hw2 >> 5) & 1);
  const bool op = hw2 & 0x40;
  const uint32_t sm = s_[m];
  uint32_t env;

  switch ((hw1 >> 4) & 0xb) {
    case 0x0: {  // VMLA / VMLS
      uint32_t p = fpMul(s_[n], sm);
      if (op) p ^= kSign;
      s_[d] = fpAdd(s_[d], p);
      cycles_ += 2;
      return;
    }
    case 0x1: {  // VNMLS / VNMLA
      uint32_t p = fpMul(s_[n], sm);
      if (op) p ^= kSign;
      s_[d] = fpAdd(s_[d] ^ kSign, p);
      cycles_ += 2;
      return;
    }
    case 0x2: s_[d] = fpMul(s_[n], sm) ^ (op ? kSign : 0); return;        // VMUL / VNMUL
    case 0x3: s_[d] = op ? fpSub(s_[n], sm) : fpAdd(s_[n], sm); return;   // VADD / VSUB
    case 0x8:
      if (op) break;
      s_[d] = fpDiv(s_[n], sm);
      cycles_ += 13;
      return;
    case 0x9:  // VFNMS / VFNMA
      s_[d] = fpMulAdd(s_[d] ^ kSign, op ? (s_[n] ^ kSign) : s_[n], sm);
      cycles_ += 2;
      return;
    case 0xa:  // VFMA / VFMS
      s_[d] = fpMulAdd(s_[d], op ? (s_[n] ^ kSign) : s_[n], sm);
      cycles_ += 2;
      return;
    case 0xb: {
      if (!op) {  // VMOV immédiat
        const uint32_t imm8 = ((hw1 & 15) << 4) | (hw2 & 15);
        const uint32_t b6 = (imm8 >> 6) & 1;
        const uint32_t exp = ((b6 ^ 1) << 7) | (b6 ? 0x7c : 0) | ((imm8 >> 4) & 3);
        s_[d] = ((imm8 >> 7) << 31) | (exp << 23) | ((imm8 & 15) << 19);
        return;
      }
      const bool bit7 = hw2 & 0x80;
      switch (hw1 & 15) {
        case 0: s_[d] = bit7 ? (sm & ~kSign) : sm; return;           // VABS / VMOV
        case 1:                                                      // VSQRT / VNEG
          if (bit7) { s_[d] = fpSqrt(sm); cycles_ += 13; }
          else s_[d] = sm ^ kSign;
          return;
        case 2: case 3: {  // VCVTB / VCVTT : demi-précision
          const uint32_t low = bit7 ? 16 : 0;
          env = fpscr_;
          if (!(hw1 & 1)) s_[d] = sf::halfToSingle((sm >> low) & 0xffff, env);
          else s_[d] = (s_[d] & ~(0xffffu << low)) | (sf::singleToHalf(sm, env) << low);
          if (env != fpscr_) setFpscr(env);
          return;
        }
        case 4: case 5: {  // VCMP / VCMPE (avec un registre ou avec zéro)
          const uint32_t a = s_[d], b = (hw1 & 1) ? 0 : sm;
          uint32_t nzcv;
          if (isPlain(a) && isPlain(b)) {
            const float x = asFloat(a), y = asFloat(b);
            nzcv = x == y ? 0x6 : (x < y ? 0x8 : 0x2);
          } else {
            env = fpscr_;
            nzcv = sf::compare(a, b, bit7, env);
            if (env != fpscr_) fpscr_ = env;
          }
          setFpscr((fpscr_ & 0x0fffffffu) | (nzcv << 28));
          return;
        }
        case 8:  // VCVT : entier vers flottant
          if (fpFast_) {
            s_[d] = asBits(bit7 ? static_cast<float>(static_cast<int32_t>(sm)) : static_cast<float>(sm));
          } else {
            env = fpscr_;
            s_[d] = sf::fromFixed(bit7 ? static_cast<int64_t>(static_cast<int32_t>(sm)) : static_cast<int64_t>(sm), 0, false, env);
            if (env != fpscr_) setFpscr(env);
          }
          return;
        case 12: case 13: {  // VCVT / VCVTR : flottant vers entier
          const bool isSigned = hw1 & 1;
          if (fpFast_ && bit7) {
            if (isZero(sm)) { s_[d] = 0; return; }
            const uint32_t mag = sm & ~kSign;
            if (isNormal(sm) && isSigned && mag < 0x4f000000u) {            // |x| < 2^31
              s_[d] = static_cast<uint32_t>(static_cast<int32_t>(asFloat(sm)));
              return;
            }
            if (isNormal(sm) && !isSigned && !(sm & kSign) && mag < 0x4f800000u) {   // 0 < x < 2^32
              s_[d] = static_cast<uint32_t>(static_cast<int64_t>(asFloat(sm)));
              return;
            }
          }
          env = fpscr_;
          s_[d] = sf::toFixed(sm, 32, 0, !isSigned, bit7, env);
          if (env != fpscr_) setFpscr(env);
          return;
        }
        case 10: case 11: case 14: case 15: {  // VCVT : virgule fixe
          const bool toFixed = hw1 & 4, isUnsigned = hw1 & 1;
          const int size = bit7 ? 32 : 16;
          int frac = size - static_cast<int>(((hw2 & 15) << 1) | ((hw2 >> 5) & 1));
          if (frac < 0) frac = 0;
          env = fpscr_;
          if (toFixed) {
            s_[d] = sf::toFixed(s_[d], size, frac, isUnsigned, true, env);
          } else {
            const uint32_t x = s_[d];
            const int64_t value = size == 16
                ? (isUnsigned ? static_cast<int64_t>(x & 0xffff) : static_cast<int64_t>(static_cast<int16_t>(x)))
                : (isUnsigned ? static_cast<int64_t>(x) : static_cast<int64_t>(static_cast<int32_t>(x)));
            s_[d] = sf::fromFixed(value, frac, true, env);
          }
          if (env != fpscr_) setFpscr(env);
          return;
        }
        default: break;
      }
      break;
    }
    default: break;
  }
  undefined(insn);
}

// Toutes les instructions du « coprocesseur » flottant : calculs, transferts avec
// les registres ordinaires, chargements et rangements en mémoire.
void CortexM4::execCoprocessor(uint32_t insn) {
  const uint32_t hw1 = insn >> 16, hw2 = insn & 0xffff;
  if ((hw1 & 0xf000) != 0xe000 || ((hw2 >> 8) & 0xe) != 0xa) {
    cfsr_ |= 1u << 19;
    raiseFault("coprocesseur absent");
    return;
  }
  if (!fpCheck()) return;
  const bool dbl = hw2 & 0x100;

  if ((hw1 & 0x0f00) == 0x0e00) {
    if (!(hw2 & 0x10)) { execFpData(insn); return; }
    // Transferts 32 bits avec un registre ordinaire.
    const uint32_t rt = hw2 >> 12;
    const bool toCore = hw1 & 0x10;
    const uint32_t a = (hw1 >> 5) & 7;
    if (!dbl) {
      if (a == 0) {  // VMOV Sn <-> Rt
        const uint32_t n = ((hw1 & 15) << 1) | ((hw2 >> 7) & 1);
        if (toCore) r_[rt] = s_[n]; else s_[n] = r_[rt];
        return;
      }
      if (a == 7 && (hw1 & 15) == 1) {  // VMRS / VMSR
        if (!toCore) {
          setFpscr(r_[rt]);
        } else if (rt == 15) {
          n_ = fpscr_ >> 31; z_ = (fpscr_ >> 30) & 1; c_ = (fpscr_ >> 29) & 1; v_ = (fpscr_ >> 28) & 1;
        } else {
          r_[rt] = fpscr_;
        }
        return;
      }
    } else if ((a & 6) == 0 && (hw2 & 0x60) == 0) {  // VMOV Dn[x] <-> Rt
      const uint32_t reg = (((hw2 >> 7) & 1) << 4) | (hw1 & 15);
      if (reg < 16) {
        const uint32_t index = 2 * reg + (a & 1);
        if (toCore) r_[rt] = s_[index]; else s_[index] = r_[rt];
        return;
      }
    }
    undefined(insn);
    return;
  }

  const bool p = hw1 & 0x100, u = hw1 & 0x80, dBit = hw1 & 0x40, w = hw1 & 0x20, load = hw1 & 0x10;
  const uint32_t rn = hw1 & 15, vd = hw2 >> 12, imm8 = hw2 & 0xff;

  if (!p && !u) {
    if (dBit && !w && (hw2 & 0xd0) == 0x10) {  // VMOV : deux registres ordinaires <-> deux S ou un D
      const uint32_t rt2 = rn, rt = vd;
      uint32_t index;
      if (dbl) {
        const uint32_t reg = (((hw2 >> 5) & 1) << 4) | (hw2 & 15);
        if (reg > 15) { undefined(insn); return; }
        index = 2 * reg;
      } else {
        index = ((hw2 & 15) << 1) | ((hw2 >> 5) & 1);
        if (index == 31) { undefined(insn); return; }
      }
      if (load) { r_[rt] = s_[index]; r_[rt2] = s_[index + 1]; }
      else { s_[index] = r_[rt]; s_[index + 1] = r_[rt2]; }
      ++cycles_;
      return;
    }
    undefined(insn);
    return;
  }

  const uint32_t base = rn == 15 ? ((pc_ + 4) & ~3u) : r_[rn];
  if (p && !w) {  // VLDR / VSTR
    const uint32_t address = u ? base + (imm8 << 2) : base - (imm8 << 2);
    if (dbl) {
      const uint32_t reg = (static_cast<uint32_t>(dBit) << 4) | vd;
      if (reg > 15) { undefined(insn); return; }
      if (load) { s_[2 * reg] = read32(address); s_[2 * reg + 1] = read32(address + 4); }
      else { write32(address, s_[2 * reg]); write32(address + 4, s_[2 * reg + 1]); }
      cycles_ += 2;
    } else {
      const uint32_t reg = (vd << 1) | static_cast<uint32_t>(dBit);
      if (load) s_[reg] = read32(address); else write32(address, s_[reg]);
      ++cycles_;
    }
    return;
  }

  if (p && u) { undefined(insn); return; }   // P = U = W = 1
  // VLDM / VSTM / VPUSH / VPOP
  const uint32_t first = dbl ? 2 * ((static_cast<uint32_t>(dBit) << 4) | vd) : ((vd << 1) | static_cast<uint32_t>(dBit));
  const uint32_t count = dbl ? (imm8 & ~1u) : imm8;
  if (count == 0 || first + count > 32) { undefined(insn); return; }
  uint32_t address = u ? base : base - (imm8 << 2);
  if (w) r_[rn] = u ? base + (imm8 << 2) : base - (imm8 << 2);
  for (uint32_t i = 0; i < count; ++i, address += 4) {
    if (load) s_[first + i] = read32(address); else write32(address, s_[first + i]);
  }
  cycles_ += count;
}

}  // namespace emu
}  // namespace lments
