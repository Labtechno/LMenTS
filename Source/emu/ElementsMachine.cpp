// LMenTS — le module Elements émulé (voir ElementsMachine.h). Licence MIT.
//
// Références : STM32F405 Reference Manual (RM0090) pour les registres, fiche
// technique du STM32F405 pour les durées d'écriture en flash, fiche technique du
// codec WM8731 pour sa liaison de réglage, et les pilotes du firmware
// (elements/drivers) pour le câblage des broches.

#include "ElementsMachine.h"

#include <algorithm>
#include <cstring>

namespace lments {
namespace emu {

namespace {

constexpr uint32_t kGpio = 0x40020000;              // ports A, B, C... tous les 0x400
constexpr uint32_t kRcc = 0x40023800, kFlashReg = 0x40023c00, kPwr = 0x40007000;
constexpr uint32_t kAdc1 = 0x40012000, kAdc2 = 0x40012100, kAdcCommon = 0x40012300;
constexpr uint32_t kDma1 = 0x40026000, kDma2 = 0x40026400;
constexpr uint32_t kSpi2 = 0x40003800, kI2s2Ext = 0x40003400;
constexpr uint32_t kI2c2 = 0x40005800, kTim1 = 0x40010000, kUsart1 = 0x40011000;

constexpr uint64_t kNever = ~uint64_t { 0 };
constexpr uint64_t kFallbackSamplePeriod = 5250;    // 32 kHz à 168 MHz, tant que le codec est arrêté

// Registres d'un flux DMA
enum { kDmaCr = 0, kDmaNdtr = 1, kDmaPar = 2, kDmaM0ar = 3, kDmaM1ar = 4, kDmaFcr = 5 };
// Registres d'un ADC (décalage / 4)
enum { kAdcSr = 0, kAdcCr1 = 1, kAdcCr2 = 2, kAdcSmpr1 = 3, kAdcSmpr2 = 4, kAdcSqr1 = 11, kAdcSqr2 = 12, kAdcSqr3 = 13, kAdcDr = 19 };
// Registres SPI / I2S (décalage / 4)
enum { kSpiCr1 = 0, kSpiCr2 = 1, kSpiSr = 2, kSpiDr = 3, kSpiI2sCfgr = 7, kSpiI2sPr = 8 };

// Secteurs de la flash du STM32F405 : 4 x 16 Ko, 1 x 64 Ko, 7 x 128 Ko.
void sectorRange(uint32_t sector, uint32_t& offset, uint32_t& size) {
  if (sector < 4) { offset = sector * 0x4000; size = 0x4000; }
  else if (sector == 4) { offset = 0x10000; size = 0x10000; }
  else { offset = 0x20000 * (sector - 4); size = 0x20000; }
}

// Position des drapeaux d'un flux dans les registres LISR / HISR.
constexpr uint32_t kDmaFlagShift[4] = { 0, 6, 16, 22 };
constexpr uint32_t kDmaHtif = 1u << 4, kDmaTcif = 1u << 5;

int dmaIrq(int controller, int stream) {
  if (controller == 0) return stream < 7 ? 11 + stream : 47;
  return stream < 5 ? 56 + stream : 68 + (stream - 5);
}

}  // namespace

ElementsMachine::ElementsMachine() : cpu_(kFlashSize, kRamSize, this, kCcmSize) {
  for (auto& p : controls.pot) p = 2048;
  // Rien de branché : 0 V sur toutes les entrées (voir hw/Panel.h pour les étages d'entrée).
  for (auto& c : controls.cv) c = 2068;
  controls.cv[12] = 3240;
}

bool ElementsMachine::loadApplication(const uint8_t* data, size_t size) {
  const uint32_t offset = kApplicationBase - CortexM4::kFlashBase;
  if (data == nullptr || size < 8 || size > kFlashSize - offset) return false;
  uint32_t sp, pc;
  std::memcpy(&sp, data, 4);
  std::memcpy(&pc, data + 4, 4);
  const bool spInRam = sp > CortexM4::kRamBase && sp <= CortexM4::kRamBase + kRamSize;
  const bool spInCcm = sp > CortexM4::kCcmBase && sp <= CortexM4::kCcmBase + kCcmSize;
  if (!spInRam && !spInCcm) return false;
  if (pc < kApplicationBase || pc >= kApplicationBase + size || !(pc & 1)) return false;

  uint8_t* flash = cpu_.flash();
  // Comme le bootloader du module : seule la zone de l'application est effacée
  // puis écrite. Les réglages (secteur 1) restent en place.
  std::memset(flash, 0xff, kSettingsBase - CortexM4::kFlashBase);
  std::memset(flash + offset, 0xff, kFlashSize - offset);
  std::memcpy(flash + offset, data, size);
  // À la place du bootloader : la remise à zéro saute directement à l'application.
  std::memcpy(flash, data, 8);
  return true;
}

void ElementsMachine::restoreFlash(const uint8_t* data, size_t size) {
  std::memcpy(cpu_.flash(), data, std::min(size, kFlashSize));
}

void ElementsMachine::eraseSettings() {
  std::memset(cpu_.flash() + (kSettingsBase - CortexM4::kFlashBase), 0xff, kSettingsSize);
}

void ElementsMachine::powerOn() {
  std::memset(cpu_.ram(), 0, kRamSize);
  otherRegisters_.clear();
  rccCr_ = 0x83; rccPllCfgr_ = 0x24003010; rccCfgr_ = 0; rccPllI2sCfgr_ = 0x20003000;
  std::memset(gpioOdr_, 0, sizeof(gpioOdr_));
  std::memset(adc_, 0, sizeof(adc_));
  adcCcr_ = 0; adc1Done_ = kNever;
  std::memset(dmaIsr_, 0, sizeof(dmaIsr_));
  std::memset(dma_, 0, sizeof(dma_));
  std::memset(dmaReload_, 0, sizeof(dmaReload_));
  std::memset(spi_, 0, sizeof(spi_));
  spi_[0][kSpiI2sPr] = spi_[1][kSpiI2sPr] = 2;
  audioRunning_ = false; sampleFraction_ = 0; samplePeriodFixed_ = 0;
  dacL_ = dacR_ = 0;
  i2cCr1_ = i2cSr1_ = i2cSr2_ = 0; i2cAddrPendingClear_ = false; i2cByteIndex_ = 0;
  std::memset(codecRegs_, 0, sizeof(codecRegs_));
  std::memset(tim1_, 0, sizeof(tim1_));
  tim1_[11] = 0xffff;
  flashAcr_ = 0; flashCr_ = 0x80000000u; flashKeyStep_ = 0;
  cpu_.setFlashTiming(0, true);
  cpu_.reset();
  sampleNext_ = cpu_.cycles() + kFallbackSamplePeriod;
  reschedule();
}

// --- Horloges --------------------------------------------------------------------------

double ElementsMachine::cpuHz() const {
  const uint32_t sws = rccCfgr_ & 3;
  if (sws == 0) return 16e6;   // oscillateur interne
  if (sws == 1) return 8e6;    // quartz
  const double m = rccPllCfgr_ & 0x3f, n = (rccPllCfgr_ >> 6) & 0x1ff;
  const double p = 2.0 * (((rccPllCfgr_ >> 16) & 3) + 1);
  return m > 0 ? 8e6 / m * n / p : 16e6;
}

double ElementsMachine::sampleRate() const {
  if (!audioRunning_ || samplePeriodFixed_ == 0) return 0.0;
  return cpuHz() / (static_cast<double>(samplePeriodFixed_) / 4294967296.0);
}

// Le codec reçoit son horloge de la puce : un échantillon toutes les
// (256 ou 32 ou 64) x diviseur périodes de l'horloge I2S, elle-même tirée du
// quartz par un multiplicateur (PLLI2S). On en déduit la durée d'un échantillon
// en cycles du processeur, à partir des registres écrits par le firmware.
void ElementsMachine::updateAudioClock() {
  const uint32_t cfgr = spi_[0][kSpiI2sCfgr], pr = spi_[0][kSpiI2sPr];
  const bool running = (cfgr & 0x0c00) == 0x0c00;   // mode I2S, activé
  uint64_t period = 0;
  if (running) {
    const uint64_t div = 2 * (pr & 0xff) + ((pr >> 8) & 1);
    const uint64_t frame = (pr & 0x200) ? 256 : ((cfgr & 1) ? 64 : 32);
    const uint64_t m = rccPllCfgr_ & 0x3f;
    const uint64_t i2sN = (rccPllI2sCfgr_ >> 6) & 0x1ff, i2sR = (rccPllI2sCfgr_ >> 28) & 7;
    if (div && m && i2sN && i2sR) {
      // Horloge I2S = 8 MHz / m x i2sN / i2sR ; échantillons par seconde = horloge / (frame x div).
      const unsigned __int128 num = static_cast<unsigned __int128>(cpuHz() + 0.5) * frame * div * i2sR * m;
      const unsigned __int128 den = static_cast<unsigned __int128>(8000000) * i2sN;
      period = static_cast<uint64_t>((num << 32) / den);
    }
  }
  if (period == 0 && running) period = kFallbackSamplePeriod << 32;
  const bool changed = running != audioRunning_ || period != samplePeriodFixed_;
  audioRunning_ = running;
  samplePeriodFixed_ = period;
  if (changed && running) {
    sampleFraction_ = 0;
    sampleNext_ = cpu_.cycles() + (period >> 32);
    reschedule();
  }
}

void ElementsMachine::reschedule() {
  cpu_.setExternalDeadline(std::min(sampleNext_, adc1Done_));
}

// --- Exécution -------------------------------------------------------------------------

void ElementsMachine::run(const int16_t* inL, const int16_t* inR, int16_t* outL, int16_t* outR, int n) {
  if (n <= 0) return;
  runInL_ = inL; runInR_ = inR; runOutL_ = outL; runOutR_ = outR;
  runCount_ = n; runIndex_ = 0;
  while (runIndex_ < runCount_ && !cpu_.halted()) {
    cpu_.run(cpu_.cycles() + 8 * kFallbackSamplePeriod * static_cast<uint64_t>(runCount_ - runIndex_));
  }
  for (; runIndex_ < runCount_; ++runIndex_) {   // processeur arrêté : la sortie reste figée
    if (outL) outL[runIndex_] = dacL_;
    if (outR) outR[runIndex_] = dacR_;
  }
  runInL_ = runInR_ = nullptr; runOutL_ = runOutR_ = nullptr;
  runCount_ = runIndex_ = 0;
}

void ElementsMachine::onDeadline(uint64_t now) {
  if (now >= adc1Done_) finishAdc1();
  while (now >= sampleNext_) {
    if (runIndex_ >= runCount_) {
      // Le bloc demandé est complet : les échantillons en retard (processeur bloqué
      // par une écriture en flash, par exemple) seront rendus au prochain appel.
      cpu_.stop();
      break;
    }
    sampleTick();
    if (audioRunning_) {
      sampleFraction_ += samplePeriodFixed_ & 0xffffffffu;
      sampleNext_ += (samplePeriodFixed_ >> 32) + (sampleFraction_ >> 32);
      sampleFraction_ &= 0xffffffffu;
    } else {
      sampleNext_ += kFallbackSamplePeriod;
    }
  }
  reschedule();
}

void ElementsMachine::dmaSetFlags(int controller, int stream, uint32_t flags) {
  // Drapeau encore levé alors que l'interruption est demandée : le firmware n'a
  // pas fini de traiter le bloc précédent. Sur le module, c'est un craquement.
  const uint32_t cr0 = dma_[controller][stream][kDmaCr];
  if ((cr0 & 0x18) && (dmaIsr_[controller][stream >> 2] & (flags << kDmaFlagShift[stream & 3]))) ++overruns_;
  dmaIsr_[controller][stream >> 2] |= flags << kDmaFlagShift[stream & 3];
  const uint32_t cr = dma_[controller][stream][kDmaCr];
  if (((flags & kDmaHtif) && (cr & 0x08)) || ((flags & kDmaTcif) && (cr & 0x10))) {
    cpu_.setIrqPending(dmaIrq(controller, stream), true);
  }
}

// Une période d'échantillonnage : le codec reçoit une trame (gauche, droite) et
// en rend une. Les deux transferts passent par le DMA, sans le processeur.
void ElementsMachine::sampleTick() {
  const int16_t inL = runInL_ ? runInL_[runIndex_] : 0;
  const int16_t inR = runInR_ ? runInR_[runIndex_] : 0;
  if (audioRunning_) {
    // Deux demi-mots par trame : gauche puis droite.
    auto transfer = [this](int stream, bool toMemory, int16_t& left, int16_t& right) {
      uint32_t* s = dma_[0][stream];
      if (!(s[kDmaCr] & 1) || dmaReload_[0][stream] == 0) return;
      for (int channel = 0; channel < 2; ++channel) {
        const uint32_t position = dmaReload_[0][stream] - s[kDmaNdtr];
        const uint32_t address = s[kDmaM0ar] + 2 * position;
        int16_t& sample = channel == 0 ? left : right;
        if (toMemory) cpu_.write16(address, static_cast<uint16_t>(sample));
        else sample = static_cast<int16_t>(cpu_.read16(address));
        --s[kDmaNdtr];
        if (s[kDmaNdtr] == dmaReload_[0][stream] / 2) dmaSetFlags(0, stream, kDmaHtif);
        if (s[kDmaNdtr] == 0) {
          dmaSetFlags(0, stream, kDmaTcif);
          if (s[kDmaCr] & 0x100) s[kDmaNdtr] = dmaReload_[0][stream];   // circulaire
          else { s[kDmaCr] &= ~1u; return; }
        }
      }
    };
    if (spi_[0][kSpiCr2] & 2) transfer(4, false, dacL_, dacR_);          // émission vers le codec
    int16_t l = inL, r = inR;
    if (spi_[1][kSpiCr2] & 1) transfer(3, true, l, r);                   // réception du codec
  }
  if (runOutL_) runOutL_[runIndex_] = dacL_;
  if (runOutR_) runOutR_[runIndex_] = dacR_;
  ++runIndex_;
  if (runIndex_ >= runCount_) cpu_.stop();
}

// --- Convertisseurs (potards et CV) ------------------------------------------------------

// Durée d'une séquence de conversions, en cycles du processeur.
uint64_t ElementsMachine::adcSequenceCycles(const uint32_t* adc) const {
  static const uint32_t sampleTime[8] = { 3, 15, 28, 56, 84, 112, 144, 480 };
  static const uint32_t resolutionBits[4] = { 12, 10, 8, 6 };
  const uint32_t count = ((adc[kAdcSqr1] >> 20) & 15) + 1;
  const uint32_t bits = resolutionBits[(adc[kAdcCr1] >> 24) & 3];
  uint64_t adcCycles = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t channel = i < 6 ? (adc[kAdcSqr3] >> (5 * i)) & 31
                           : i < 12 ? (adc[kAdcSqr2] >> (5 * (i - 6))) & 31
                                    : (adc[kAdcSqr1] >> (5 * (i - 12))) & 31;
    const uint32_t smp = channel < 10 ? (adc[kAdcSmpr2] >> (3 * channel)) & 7
                                      : (adc[kAdcSmpr1] >> (3 * (channel - 10))) & 7;
    adcCycles += sampleTime[smp] + bits;
  }
  // Horloge des ADC : horloge du bus APB2 divisée par 2, 4, 6 ou 8.
  const uint32_t ppre2 = (rccCfgr_ >> 13) & 7;
  const uint64_t busDivider = ppre2 < 4 ? 1 : (uint64_t { 2 } << (ppre2 - 4));
  const uint64_t adcDivider = 2 * (((adcCcr_ >> 16) & 3) + 1);
  return adcCycles * busDivider * adcDivider;
}

void ElementsMachine::startAdc1() {
  if (!(adc_[0][kAdcCr2] & 1)) return;
  adc1Done_ = cpu_.cycles() + adcSequenceCycles(adc_[0]);
  reschedule();
}

// Fin de la séquence de l'ADC1 : chaque valeur part en mémoire par le DMA.
void ElementsMachine::finishAdc1() {
  const uint32_t* adc = adc_[0];
  const uint32_t count = ((adc[kAdcSqr1] >> 20) & 15) + 1;
  const bool leftAligned = adc[kAdcCr2] & 0x800;
  // Flux DMA branché sur le registre de données de l'ADC1.
  uint32_t* s = nullptr;
  int stream = 0;
  for (int i = 0; i < 8; ++i) {
    if ((dma_[1][i][kDmaCr] & 1) && dma_[1][i][kDmaPar] == kAdc1 + 0x4c) { s = dma_[1][i]; stream = i; break; }
  }
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t channel = i < 6 ? (adc[kAdcSqr3] >> (5 * i)) & 31
                           : i < 12 ? (adc[kAdcSqr2] >> (5 * (i - 6))) & 31
                                    : (adc[kAdcSqr1] >> (5 * (i - 12))) & 31;
    const uint32_t code = channel < 16 ? (controls.cv[channel] & 0xfff) : 0;
    const uint32_t value = leftAligned ? code << 4 : code;
    adc_[0][kAdcDr] = value;
    if (s && (adc[kAdcCr2] & 0x100) && dmaReload_[1][stream] != 0 && (s[kDmaCr] & 1)) {
      const uint32_t position = dmaReload_[1][stream] - s[kDmaNdtr];
      const uint32_t size = (s[kDmaCr] >> 13) & 3;
      const uint32_t address = s[kDmaM0ar] + (position << size);
      if (size == 0) cpu_.write8(address, value);
      else if (size == 1) cpu_.write16(address, value);
      else cpu_.write32(address, value);
      --s[kDmaNdtr];
      if (s[kDmaNdtr] == dmaReload_[1][stream] / 2) dmaSetFlags(1, stream, kDmaHtif);
      if (s[kDmaNdtr] == 0) {
        dmaSetFlags(1, stream, kDmaTcif);
        if (s[kDmaCr] & 0x100) s[kDmaNdtr] = dmaReload_[1][stream];
        else s[kDmaCr] &= ~1u;
      }
    }
  }
  adc_[0][kAdcSr] |= 0x12;   // conversion commencée et terminée
  adc1Done_ = (adc[kAdcCr2] & 2) ? cpu_.cycles() + adcSequenceCycles(adc_[0]) : kNever;   // mode continu
}

// --- Broches ---------------------------------------------------------------------------

uint32_t ElementsMachine::gpioInput(int port) const {
  uint32_t v = gpioOdr_[port];
  if (port == 2) {
    // Bouton et GATE : à la masse quand ils sont actifs (résistances de rappel).
    v = (v & ~0x12u) | (controls.button ? 0 : 0x02u) | (controls.gate ? 0 : 0x10u);
  }
  return v & 0xffff;
}

ElementsMachine::Leds ElementsMachine::leds() const {
  Leds l;
  l.gate = gpioOdr_[2] & 0x80;
  const bool pwm = (tim1_[0] & 1) && (tim1_[17] & 0x8000);
  const float top = static_cast<float>(tim1_[11] & 0xffff) + 1.0f;
  l.exciter = pwm && (tim1_[8] & 0x0001) ? std::min(1.0f, static_cast<float>(tim1_[13] & 0xffff) / top) : 0.0f;
  l.resonator = pwm && (tim1_[8] & 0x1000) ? std::min(1.0f, static_cast<float>(tim1_[16] & 0xffff) / top) : 0.0f;
  return l;
}

// --- Flash -----------------------------------------------------------------------------

void ElementsMachine::flashProgram(uint32_t address, uint32_t value, int size) {
  if (!(flashCr_ & 1) || (flashCr_ & 0x80000000u)) return;   // PG absent ou flash verrouillée
  uint8_t* p = cpu_.flash() + (address - CortexM4::kFlashBase);
  for (int i = 0; i < size; ++i) p[i] &= static_cast<uint8_t>(value >> (8 * i));  // 1 -> 0 seulement
  ++flashWrites_;
  // 16 µs par écriture (valeur typique de la fiche technique). Le programme, qui
  // s'exécute depuis la flash, est bloqué pendant ce temps.
  cpu_.stall(static_cast<uint64_t>(cpuHz() * 16e-6));
}

void ElementsMachine::flashControl(uint32_t value) {
  if (flashCr_ & 0x80000000u) return;   // verrouillée : il faut d'abord écrire les deux clés
  if (value & 0x80000000u) { flashCr_ = 0x80000000u; flashKeyStep_ = 0; return; }
  flashCr_ = value & ~0x10000u;
  if (!(value & 0x10000u)) return;      // pas de démarrage
  // Durées typiques d'effacement (fiche technique), selon la taille des mots écrits.
  static const double erase16[3] = { 0.40, 0.30, 0.25 }, erase64[3] = { 1.20, 0.70, 0.55 }, erase128[3] = { 2.00, 1.30, 1.00 };
  const uint32_t psize = std::min<uint32_t>((value >> 8) & 3, 2);
  uint8_t* flash = cpu_.flash();
  double seconds = 0.0;
  if (value & 2) {          // un secteur
    const uint32_t sector = (value >> 3) & 15;
    if (sector < 12) {
      uint32_t offset, size;
      sectorRange(sector, offset, size);
      std::memset(flash + offset, 0xff, size);
      seconds = sector < 4 ? erase16[psize] : (sector == 4 ? erase64[psize] : erase128[psize]);
    }
  } else if (value & 4) {   // tout
    std::memset(flash, 0xff, kFlashSize);
    seconds = 4 * erase16[psize] + erase64[psize] + 7 * erase128[psize];
  }
  ++flashWrites_;
  cpu_.stall(static_cast<uint64_t>(cpuHz() * seconds));
}

// --- Liaison I2C vers le codec ---------------------------------------------------------------

void ElementsMachine::i2cWriteCr1(uint32_t value) {
  if (value & 0x8000) {    // remise à zéro logicielle
    i2cCr1_ = value & 0x8000; i2cSr1_ = i2cSr2_ = 0; i2cByteIndex_ = 0;
    return;
  }
  if (value & 0x100) {     // START : le bus est pris
    i2cSr1_ |= 1;
    i2cSr2_ |= 3;
  }
  if (value & 0x200) {     // STOP
    i2cSr1_ = 0; i2cSr2_ = 0; i2cByteIndex_ = 0;
  }
  i2cCr1_ = value & ~0x300u;
}

void ElementsMachine::i2cWriteData(uint32_t value) {
  if (i2cSr1_ & 1) {       // premier octet après START : l'adresse du codec
    i2cSr1_ = (i2cSr1_ & ~1u) | 0x82;                 // ADDR, TXE
    if (!(value & 1)) i2cSr2_ |= 4;                   // émission
    i2cByteIndex_ = 0;
    return;
  }
  if (i2cByteIndex_ == 0) {
    i2cFirst_ = static_cast<uint8_t>(value);
  } else if (i2cByteIndex_ == 1) {
    // Deux octets : 7 bits de numéro de registre, 9 bits de valeur.
    codecRegs_[(i2cFirst_ >> 1) & 15] = static_cast<uint16_t>(((i2cFirst_ & 1) << 8) | (value & 0xff));
  }
  ++i2cByteIndex_;
  i2cSr1_ |= 0x84;         // TXE, BTF : l'octet est parti
}

// --- Bus -------------------------------------------------------------------------------

uint32_t ElementsMachine::read(uint32_t a, int size) {
  if (a >= 0x1fff7a10u && a < 0x1fff7a1cu) {           // numéro de série de la puce
    uint32_t v = 0;
    std::memcpy(&v, reinterpret_cast<const uint8_t*>(uid_) + (a - 0x1fff7a10u), static_cast<size_t>(std::min<uint32_t>(size, 0x1fff7a1cu - a)));
    return v;
  }
  if (a == 0x1fff7a22u) return 0x400;                  // taille de la flash, en Ko
  const uint32_t word = readRegister(a & ~3u);
  if (size == 4) return word;
  const uint32_t shifted = word >> (8 * (a & 3));
  return size == 2 ? shifted & 0xffff : shifted & 0xff;
}

void ElementsMachine::write(uint32_t a, uint32_t v, int size) {
  if (a >= CortexM4::kFlashBase && a < CortexM4::kFlashBase + kFlashSize) {
    flashProgram(a, v, size);
    return;
  }
  if (size == 4) { writeRegister(a, v); return; }
  const uint32_t reg = a & ~3u, shift = 8 * (a & 3);
  // Mise à 1 / à 0 des broches : registre en écriture seule, par moitiés de 16 bits.
  if (reg >= kGpio && reg < kGpio + 0x2400 && (reg & 0x3ff) == 0x18) { writeRegister(reg, v << shift); return; }
  const uint32_t mask = (size == 2 ? 0xffffu : 0xffu) << shift;
  uint32_t old;
  // Registres dont la lecture a un effet : on ne les relit pas pour une écriture partielle.
  if (reg == kI2c2 + 0x14 || reg == kI2c2 + 0x18 || reg == kI2c2 + 0x10 || reg == kI2c2 ||
      reg == kAdc1 + 0x4c || reg == kAdc2 + 0x4c) old = 0;
  else old = readRegister(reg);
  writeRegister(reg, (old & ~mask) | ((v << shift) & mask));
}

uint32_t ElementsMachine::readRegister(uint32_t a) {
  if (a >= kGpio && a < kGpio + 0x0c00) {
    const int port = static_cast<int>((a - kGpio) >> 10);
    switch (a & 0x3ff) {
      case 0x10: return gpioInput(port);
      case 0x14: return gpioOdr_[port];
      default: break;
    }
  } else if (a >= kAdc1 && a < kAdc2 + 0x50) {
    const int unit = a >= kAdc2 ? 1 : 0;
    const uint32_t index = (a & 0xff) >> 2;
    if (index < 20) {
      if (index == kAdcDr) adc_[unit][kAdcSr] &= ~2u;   // lire la donnée efface « terminé »
      return adc_[unit][index];
    }
  } else if (a == kAdcCommon + 4) {
    return adcCcr_;
  } else if (a >= kDma1 && a < kDma2 + 0xd0) {
    const int controller = a >= kDma2 ? 1 : 0;
    const uint32_t offset = a & 0x3ff;
    if (offset == 0x00) return dmaIsr_[controller][0];
    if (offset == 0x04) return dmaIsr_[controller][1];
    if (offset >= 0x10 && offset < 0xd0) return dma_[controller][(offset - 0x10) / 0x18][((offset - 0x10) % 0x18) >> 2];
    return 0;
  } else if ((a >= kSpi2 && a < kSpi2 + 0x24) || (a >= kI2s2Ext && a < kI2s2Ext + 0x24)) {
    const int unit = a >= kSpi2 ? 0 : 1;
    const uint32_t index = (a & 0xff) >> 2;
    if (index == kSpiSr) return 0x0002;                 // prêt à émettre
    return spi_[unit][index];
  } else if (a >= kTim1 && a < kTim1 + 0x80) {
    return tim1_[(a - kTim1) >> 2];
  }
  switch (a) {
    case kRcc: {
      // Les oscillateurs et multiplicateurs sont prêts dès qu'ils sont allumés.
      uint32_t v = rccCr_ & ~0x0a020002u;
      if (v & 0x00000001u) v |= 0x00000002u;
      if (v & 0x00010000u) v |= 0x00020000u;
      if (v & 0x01000000u) v |= 0x02000000u;
      if (v & 0x04000000u) v |= 0x08000000u;
      return v;
    }
    case kRcc + 0x04: return rccPllCfgr_;
    case kRcc + 0x08: return (rccCfgr_ & ~0xcu) | ((rccCfgr_ & 3) << 2);   // horloge choisie = horloge active
    case kRcc + 0x84: return rccPllI2sCfgr_;
    case kPwr + 0x04: return otherRegisters_[a] | 0x4000;                  // régulateur prêt
    case kFlashReg + 0x00: return flashAcr_;
    case kFlashReg + 0x0c: return 0;                                       // ni occupée, ni en erreur
    case kFlashReg + 0x10: return flashCr_;
    case kI2c2 + 0x00: return i2cCr1_;
    case kI2c2 + 0x14: i2cAddrPendingClear_ = i2cSr1_ & 2; return i2cSr1_;
    case kI2c2 + 0x18: {
      const uint32_t v = i2cSr2_;
      if (i2cAddrPendingClear_) { i2cSr1_ &= ~2u; i2cAddrPendingClear_ = false; }   // lire SR1 puis SR2 efface ADDR
      return v;
    }
    case kUsart1 + 0x00: return 0xc0;                                      // rien reçu, prêt à émettre
    default: break;
  }
  const auto it = otherRegisters_.find(a);
  return it == otherRegisters_.end() ? 0 : it->second;
}

void ElementsMachine::writeRegister(uint32_t a, uint32_t v) {
  if (a >= kGpio && a < kGpio + 0x0c00) {
    const int port = static_cast<int>((a - kGpio) >> 10);
    switch (a & 0x3ff) {
      case 0x14: gpioOdr_[port] = v & 0xffff; return;
      case 0x18: gpioOdr_[port] = (gpioOdr_[port] | (v & 0xffff)) & ~(v >> 16); return;
      default: break;
    }
  } else if (a >= kAdc1 && a < kAdc2 + 0x50) {
    const int unit = a >= kAdc2 ? 1 : 0;
    const uint32_t index = (a & 0xff) >> 2;
    if (index >= 20) return;
    if (index == kAdcSr) { adc_[unit][kAdcSr] &= v; return; }
    if (index == kAdcDr) return;
    if (index != kAdcCr2) { adc_[unit][index] = v; return; }
    adc_[unit][kAdcCr2] = v & ~0x40000000u;
    if (!(v & 0x40000000u) || !(v & 1)) return;        // pas de démarrage de conversion
    if (unit == 0) { startAdc1(); return; }
    // ADC2 : une seule conversion, celle du potard choisi par les aiguilleurs.
    // Un seul des quatre aiguilleurs doit être activé (broche à 0).
    const uint32_t enabled = (~gpioOdr_[1] >> 5) & 15;
    uint32_t code = 0;
    if (((adc_[1][kAdcSqr3] & 31) == 10) && enabled && !(enabled & (enabled - 1))) {
      const uint32_t chip = enabled == 1 ? 0 : (enabled == 2 ? 1 : (enabled == 4 ? 2 : 3));
      const uint32_t c = gpioOdr_[2];
      const uint32_t address = chip * 8 + (((c >> 15) & 1) | (((c >> 14) & 1) << 1) | (((c >> 13) & 1) << 2));
      code = controls.pot[address] & 0xfff;
    }
    adc_[1][kAdcDr] = (v & 0x800) ? code << 4 : code;
    adc_[1][kAdcSr] |= 0x12;
    return;
  } else if (a == kAdcCommon + 4) {
    adcCcr_ = v;
    return;
  } else if (a >= kDma1 && a < kDma2 + 0xd0) {
    const int controller = a >= kDma2 ? 1 : 0;
    const uint32_t offset = a & 0x3ff;
    if (offset == 0x08) { dmaIsr_[controller][0] &= ~v; return; }
    if (offset == 0x0c) { dmaIsr_[controller][1] &= ~v; return; }
    if (offset >= 0x10 && offset < 0xd0) {
      const int stream = static_cast<int>((offset - 0x10) / 0x18);
      const uint32_t index = ((offset - 0x10) % 0x18) >> 2;
      uint32_t* s = dma_[controller][stream];
      if (index == kDmaCr) {
        const bool starting = (v & 1) && !(s[kDmaCr] & 1);
        s[kDmaCr] = v;
        if (starting) dmaReload_[controller][stream] = s[kDmaNdtr];
      } else if (index == kDmaNdtr) {
        s[kDmaNdtr] = v & 0xffff;
      } else {
        s[index] = v;
      }
    }
    return;
  } else if ((a >= kSpi2 && a < kSpi2 + 0x24) || (a >= kI2s2Ext && a < kI2s2Ext + 0x24)) {
    const int unit = a >= kSpi2 ? 0 : 1;
    spi_[unit][(a & 0xff) >> 2] = v & 0xffff;
    updateAudioClock();
    return;
  } else if (a >= kTim1 && a < kTim1 + 0x80) {
    tim1_[(a - kTim1) >> 2] = v;
    return;
  }
  switch (a) {
    case kRcc: rccCr_ = v; return;
    case kRcc + 0x04: rccPllCfgr_ = v; updateAudioClock(); return;
    case kRcc + 0x08: rccCfgr_ = v; updateAudioClock(); return;
    case kRcc + 0x84: rccPllI2sCfgr_ = v; updateAudioClock(); return;
    case kFlashReg + 0x00: flashAcr_ = v; return;
    case kFlashReg + 0x04:
      // Déverrouillage : deux clés à la suite.
      if (v == 0x45670123u) flashKeyStep_ = 1;
      else if (v == 0xcdef89abu && flashKeyStep_ == 1) { flashCr_ &= ~0x80000000u; flashKeyStep_ = 0; }
      else flashKeyStep_ = 0;
      return;
    case kFlashReg + 0x0c: return;
    case kFlashReg + 0x10: flashControl(v); return;
    case kI2c2 + 0x00: i2cWriteCr1(v & 0xffff); return;
    case kI2c2 + 0x10: i2cWriteData(v & 0xff); return;
    case kI2c2 + 0x14: i2cSr1_ &= v | 0xff; return;
    case kI2c2 + 0x18: return;
    default: break;
  }
  otherRegisters_[a] = v;
}

}  // namespace emu
}  // namespace lments
