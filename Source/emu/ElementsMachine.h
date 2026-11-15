// LMenTS — le module Elements émulé : un STM32F405 (CortexM4.h) entouré des
// composants de la carte, tels que le firmware les voit à travers les registres.
//
//   I2S2 + DMA1 ... codec audio WM8731 : 2 entrées (EXT IN), 2 sorties (OUT L, OUT R),
//                   16 bits ; le firmware calcule le son par blocs de 16 échantillons
//   I2C2 .......... réglage du codec au démarrage
//   ADC1 + DMA2 ... les 13 entrées CV (V/OCT, FM, STRENGTH et 10 CV de paramètres)
//   ADC2 .......... les 27 potards, lus un par un à travers 4 aiguilleurs (multiplexeurs)
//   PB5..8, PC13..15  choix de l'aiguilleur et du potard
//   PC1 ........... bouton PLAY          PC4 ... entrée GATE
//   PC7 ........... LED du bouton PLAY   TIM1 .. les deux autres LED (luminosité variable)
//   FLASH ......... programmation et effacement (réglages du firmware)
//
// Le firmware est une image binaire chargée telle quelle. Licence MIT.

#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "CortexM4.h"

namespace lments {
namespace emu {

class ElementsMachine : public Bus {
 public:
  static constexpr size_t kFlashSize = 1024 * 1024;   // STM32F405RG
  static constexpr size_t kRamSize = 128 * 1024;
  static constexpr size_t kCcmSize = 64 * 1024;
  static constexpr uint32_t kApplicationBase = 0x08008000;  // après le bootloader et les réglages
  static constexpr uint32_t kSettingsBase = 0x08004000;     // secteur 1 : réglages du firmware
  static constexpr size_t kSettingsSize = 16 * 1024;

  // Ce qui est branché sur le module et la position de ses commandes. Lu par le
  // firmware au moment où il interroge ses convertisseurs.
  struct Controls {
    // Potards, dans l'ordre du câblage des aiguilleurs (adresse 0 à 31) :
    // code 12 bits, 0 = à fond à gauche, 4095 = à fond à droite.
    uint16_t pot[32] = {};
    // Entrées CV, par numéro de canal de l'ADC1 : code 12 bits après l'étage d'entrée.
    uint16_t cv[16] = {};
    bool gate = false;     // tension présente sur l'entrée GATE
    bool button = false;   // bouton PLAY enfoncé
  };
  Controls controls;

  // État des trois voyants.
  struct Leds {
    bool gate;          // LED du bouton PLAY
    float exciter;      // 0..1
    float resonator;    // 0..1
  };

  ElementsMachine();

  // Charge une image d'application (.bin, prévue pour l'adresse 0x08008000).
  // Renvoie false si l'image n'est pas plausible. Les réglages enregistrés par le
  // firmware (secteur 1) sont conservés, comme lors d'une mise à jour du module.
  bool loadApplication(const uint8_t* data, size_t size);

  // Flash complète (firmware + réglages).
  const uint8_t* flashImage() { return cpu_.flash(); }
  void restoreFlash(const uint8_t* data, size_t size);
  void eraseSettings();

  // La flash est découpée en 12 secteurs (4 x 16 Ko, 1 x 64 Ko, 7 x 128 Ko), la
  // plus petite zone qu'on peut effacer. Le secteur 0 est le bootloader ; le
  // firmware officiel range ses réglages dans le secteur 1, mais un autre
  // firmware peut utiliser n'importe quel secteur libre. La « mémoire » d'un
  // firmware, ce sont donc tous les secteurs qui ne contiennent pas son programme.
  static constexpr int kNumSectors = 12;
  static void sectorRange(int sector, uint32_t& offset, uint32_t& size);
  // Vrai si le secteur contient une partie du programme chargé (ou le bootloader).
  bool sectorHoldsProgram(int sector) const;
  void readSector(int sector, uint8_t* data) const;
  void writeSector(int sector, const uint8_t* data);   // nul = effacé (0xFF)
  uint32_t flashWriteCount() const { return flashWrites_; }
  // Nombre de blocs audio que le firmware n'a pas eu le temps de calculer (surcharge).
  uint32_t overruns() const { return overruns_; }

  void setUniqueId(const uint32_t uid[3]) { for (int i = 0; i < 3; ++i) uid_[i] = uid[i]; }

  // Mise sous tension : remet le processeur et les périphériques à zéro.
  void powerOn();

  // Fait tourner le module pendant n périodes d'échantillonnage du codec.
  // inL / inR : ce qui entre dans le codec (peuvent être nuls = silence) ;
  // outL / outR : ce qui en sort. Valeurs 16 bits.
  void run(const int16_t* inL, const int16_t* inR, int16_t* outL, int16_t* outR, int n);

  // Fréquence d'échantillonnage réglée par le firmware (0 tant que le codec est arrêté).
  double sampleRate() const;
  // Fréquence d'horloge du processeur réglée par le firmware.
  double cpuHz() const;
  bool audioRunning() const { return audioRunning_; }

  Leds leds() const;
  // Registres du codec écrits par le firmware (9 bits chacun), pour les tests.
  uint16_t codecRegister(int index) const { return codecRegs_[index & 15]; }

  uint64_t cycles() const { return cpu_.cycles(); }
  bool crashed() const { return cpu_.halted(); }
  const char* crashReason() const { return cpu_.haltReason(); }
  CortexM4& cpu() { return cpu_; }

  // --- Bus ---------------------------------------------------------------------
  uint32_t read(uint32_t address, int size) override;
  void write(uint32_t address, uint32_t value, int size) override;
  void onDeadline(uint64_t cycles) override;

 private:
  uint32_t readRegister(uint32_t address);
  void writeRegister(uint32_t address, uint32_t value);
  uint32_t gpioInput(int port) const;
  void flashProgram(uint32_t address, uint32_t value, int size);
  void flashControl(uint32_t value);
  void i2cWriteCr1(uint32_t value);
  void i2cWriteData(uint32_t value);
  void sampleTick();
  void updateAudioClock();
  void startAdc1();
  void finishAdc1();
  uint64_t adcSequenceCycles(const uint32_t* adc) const;
  void reschedule();
  uint32_t* dmaStream(int controller, int stream) { return dma_[controller][stream]; }
  void dmaSetFlags(int controller, int stream, uint32_t flags);

  CortexM4 cpu_;
  size_t applicationSize_ = 0;
  uint32_t uid_[3] = { 0x004c004d, 0x654e5453, 0x20313131 };
  std::unordered_map<uint32_t, uint32_t> otherRegisters_;

  // RCC
  uint32_t rccCr_ = 0x83, rccPllCfgr_ = 0x24003010, rccCfgr_ = 0, rccPllI2sCfgr_ = 0x20003000;

  // GPIO A, B, C : registre de sortie
  uint32_t gpioOdr_[3] = {};

  // ADC1, ADC2 (20 registres chacun), registre commun CCR
  uint32_t adc_[2][20] = {};
  uint32_t adcCcr_ = 0;
  uint64_t adc1Done_ = ~uint64_t { 0 };

  // DMA1, DMA2 : registres d'état et 8 flux de 6 registres (CR, NDTR, PAR, M0AR, M1AR, FCR)
  uint32_t dmaIsr_[2][2] = {};
  uint32_t dma_[2][8][6] = {};
  uint32_t dmaReload_[2][8] = {};

  // SPI2 / I2S2 et I2S2ext
  uint32_t spi_[2][9] = {};
  bool audioRunning_ = false;
  uint64_t sampleNext_ = ~uint64_t { 0 };
  uint64_t sampleFraction_ = 0;       // partie fractionnaire, en 2^-32 cycle
  uint64_t samplePeriodFixed_ = 0;    // période en cycles, virgule fixe 32.32
  int16_t dacL_ = 0, dacR_ = 0;

  // I2C2 + codec
  uint32_t i2cCr1_ = 0, i2cSr1_ = 0, i2cSr2_ = 0;
  bool i2cAddrPendingClear_ = false;
  int i2cByteIndex_ = 0;
  uint8_t i2cFirst_ = 0;
  uint16_t codecRegs_[16] = {};

  // TIM1
  uint32_t tim1_[32] = {};

  // FLASH
  uint32_t flashAcr_ = 0, flashCr_ = 0x80000000u, flashKeyStep_ = 0;
  uint32_t flashWrites_ = 0;
  uint32_t overruns_ = 0;

  // Entrées / sorties de run()
  const int16_t* runInL_ = nullptr;
  const int16_t* runInR_ = nullptr;
  int16_t* runOutL_ = nullptr;
  int16_t* runOutR_ = nullptr;
  int runCount_ = 0, runIndex_ = 0;
};

}  // namespace emu
}  // namespace lments
