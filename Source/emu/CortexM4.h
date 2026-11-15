// LMenTS — émulateur de processeur ARM Cortex-M4F (ARMv7E-M, Thumb / Thumb-2,
// instructions DSP, calcul en virgule flottante simple précision VFPv4-SP).
//
// C'est le processeur du STM32F405 d'Elements. L'émulateur exécute le fichier du
// firmware tel quel, instruction par instruction, et compte les cycles d'horloge
// consommés (valeurs de la documentation ARM, voir cycles dans CortexM4.cpp).
//
// Point de départ : l'émulateur Cortex-M3 du projet Tresses (même auteur, MIT).
// Écrit pour le projet, sans code tiers. Licence MIT.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lments {
namespace emu {

// Tout ce qui n'est ni la flash ni la RAM : périphériques du microcontrôleur.
class Bus {
 public:
  virtual ~Bus() = default;
  // size = 1, 2 ou 4 octets.
  virtual uint32_t read(uint32_t address, int size) = 0;
  virtual void write(uint32_t address, uint32_t value, int size) = 0;
  // Appelé quand le compteur de cycles atteint l'échéance fixée par
  // setExternalDeadline() : c'est là que les timers lèvent leurs interruptions.
  virtual void onDeadline(uint64_t cycles) = 0;
};

class CortexM4 {
 public:
  static constexpr uint32_t kFlashBase = 0x08000000;
  static constexpr uint32_t kRamBase = 0x20000000;
  static constexpr uint32_t kCcmBase = 0x10000000;   // mémoire « CCM » du STM32F4
  static constexpr int kNumIrqs = 82;           // STM32F405
  static constexpr int kNumExceptions = 16 + kNumIrqs;

  CortexM4(size_t flashSize, size_t ramSize, Bus* bus, size_t ccmSize = 0);

  uint8_t* flash() { return flash_.data(); }
  size_t flashSize() const { return flashSize_; }
  uint8_t* ram() { return ram_.data(); }
  size_t ramSize() const { return ramSize_; }

  // Remise à zéro : charge SP et PC depuis la table des vecteurs (début de flash).
  void reset();

  // Exécute jusqu'à ce que le compteur de cycles atteigne `untilCycle`.
  void run(uint64_t untilCycle);

  uint64_t cycles() const { return cycles_; }
  uint64_t instructions() const { return instructions_; }

  // Cycles passés dans une boucle d'attente (le programme tourne en rond sans rien
  // modifier, en attendant une interruption). L'émulateur les saute d'un bloc :
  // le résultat est strictement le même, mais le calcul est bien plus rapide.
  // Le rapport idleCycles / cycles donne aussi le temps libre du processeur.
  uint64_t idleCycles() const { return idleCycles_; }

  // Nombre d'entrées dans chaque exception (16 + n = interruption n), et cycles
  // passés hors du programme principal. Pour les mesures de charge.
  uint64_t exceptionCount(int number) const { return exceptionCount_[number]; }
  uint64_t handlerCycles() const { return handlerCycles_ + (ipsr_ != 0 ? cycles_ - handlerSince_ : 0); }

  // Demande à run() de rendre la main après l'instruction en cours.
  void stop() { stopRequested_ = true; irqDirty_ = true; }

  // Le processeur est bloqué pendant `cycles` (écriture en flash, par exemple).
  void stall(uint64_t cycles) { cycles_ += cycles; }

  // Mémoire flash du STM32F405 et son « accélérateur ART », tels que le firmware
  // les règle dans le registre FLASH_ACR (voir le modèle dans CortexM4.cpp) :
  // cycles d'attente par lecture (5 à 168 MHz), lecture anticipée, cache
  // d'instructions (64 lignes de 16 octets), cache de données (8 lignes).
  // 0 cycle d'attente = flash idéale.
  void setFlashTiming(uint32_t waitStates, bool prefetch, bool instructionCache, bool dataCache);
  void resetFlashCaches();
  // Lectures de la flash qui n'ont pas trouvé leur ligne en cache (pour les mesures).
  uint64_t flashInstructionMisses() const { return iMisses_; }
  uint64_t flashDataMisses() const { return dMisses_; }

  // Interruptions externes (numéro d'IRQ du STM32, 0..81).
  void setIrqPending(int irq, bool pending);

  // Prochaine échéance des périphériques (en cycles). ~0 = aucune.
  void setExternalDeadline(uint64_t cycle);

  // Arrêt sur erreur fatale (instruction inconnue en HardFault, lockup...).
  bool halted() const { return halted_; }
  const char* haltReason() const { return haltReason_; }
  uint32_t faultPc() const { return faultPc_; }
  void halt(const char* reason);

  // Registres, pour les tests et le débogage.
  uint32_t reg(int n) const { return r_[n]; }
  void setReg(int n, uint32_t v) { r_[n] = v; }
  uint32_t pc() const { return pc_; }
  uint32_t xpsr() const;
  uint32_t fpReg(int n) const { return s_[n]; }
  void setFpReg(int n, uint32_t v) { s_[n] = v; }
  uint32_t fpscr() const { return fpscr_; }

  // Accès mémoire (utilisé aussi par les périphériques, ex. DMA).
  uint32_t read8(uint32_t a);
  uint32_t read16(uint32_t a);
  uint32_t read32(uint32_t a);
  void write8(uint32_t a, uint32_t v);
  void write16(uint32_t a, uint32_t v);
  void write32(uint32_t a, uint32_t v);

 private:
  // --- Exécution -----------------------------------------------------------------
  void step();
  void exec16(uint32_t insn);
  void exec32(uint32_t insn);
  void execDataProcessing(uint32_t op, bool setFlags, uint32_t rd, uint32_t rn,
                          uint32_t operand2, bool shifterCarry);
  void execLoadStoreSingle(uint32_t insn);
  void execLoadStoreMultiple(uint32_t insn);
  void execDualExclusiveTable(uint32_t insn);
  void execBranchMisc(uint32_t insn);
  void execParallel(uint32_t insn);
  void execCoprocessor(uint32_t insn);
  void execFpData(uint32_t insn);
  void fpOtherTiming(uint32_t hw1, uint32_t hw2, uint32_t d, uint32_t m, uint32_t last);
  void fpConsume(uint32_t reg);
  bool fpCheck();
  void preserveFpState();
  void setFpscr(uint32_t v) {
    fpscr_ = v & 0xf7c0009fu;
    fpFast_ = (fpscr_ & 0x00c00010u) == 0x10u;
    fpAnyFinite_ = (fpscr_ & 0x01c00018u) == 0x18u;
  }
  enum { kSlowAdd, kSlowSub, kSlowMul, kSlowDiv, kSlowSqrt, kSlowMulAdd };
  uint32_t fpSlow(int op, uint32_t a, uint32_t b, uint32_t c);
  uint32_t fpAdd(uint32_t a, uint32_t b);
  uint32_t fpSub(uint32_t a, uint32_t b);
  uint32_t fpMul(uint32_t a, uint32_t b);
  uint32_t fpDiv(uint32_t a, uint32_t b);
  uint32_t fpSqrt(uint32_t a);
  uint32_t fpMulAdd(uint32_t addend, uint32_t a, uint32_t b);
  void undefined(uint32_t insn);

  uint32_t slowRead(uint32_t a, int size);
  void slowWrite(uint32_t a, uint32_t v, int size);
  uint32_t scsRead(uint32_t a);
  void scsWrite(uint32_t a, uint32_t v);

  uint32_t readReg(uint32_t n) const { return n == 15 ? pc_ + 4 : r_[n]; }
  void branchTo(uint32_t address);       // BranchWritePC
  void branchDirect(uint32_t address);   // B, B<cond> : détecte les boucles d'attente
  void bxWritePc(uint32_t address);      // BXWritePC / LoadWritePC
  void aluWritePc(uint32_t address) { branchTo(address); }

  bool conditionPassed(uint32_t cond) const;
  void setNZ(uint32_t result) { n_ = result >> 31; z_ = result == 0; }
  uint32_t addWithCarry(uint32_t x, uint32_t y, uint32_t carryIn, bool setFlags);

  // --- Exceptions ----------------------------------------------------------------
  void pendException(int number) { pending_[number] = true; irqDirty_ = true; }
  void checkInterrupts();
  void exceptionEntry(int number, uint32_t returnAddress);
  void exceptionReturn(uint32_t excReturn);
  int groupPriority(int number) const;
  void updateExecutionPriority();
  void raiseFault(const char* reason);
  uint32_t& currentSp() { return r_[13]; }
  void switchStack(bool usePsp);

  // --- SysTick ---------------------------------------------------------------------
  uint32_t systickCurrent() const;
  void systickReschedule();
  void serviceDeadlines();
  void updateNextEvent();

  // --- État ----------------------------------------------------------------------
  uint32_t r_[16] = {};
  uint32_t pc_ = 0;        // adresse de l'instruction en cours
  uint32_t nextPc_ = 0;
  bool n_ = false, z_ = false, c_ = false, v_ = false, q_ = false;
  uint32_t ge_ = 0;        // drapeaux GE (instructions DSP sur octets / demi-mots)

  // Unité de calcul en virgule flottante : 32 registres simple précision
  // (S0-S31 ; D0-D15 = deux registres S accolés).
  uint32_t s_[32] = {};
  uint32_t fpscr_ = 0;
  bool fpFast_ = false;    // arrondi au plus près et drapeau « inexact » déjà levé
  // En plus : drapeau « soupassement » déjà levé et pas de flush-to-zero. Les très
  // petits nombres (dénormalisés) peuvent alors eux aussi être calculés par l'ordinateur.
  bool fpAnyFinite_ = false;
  bool controlFpca_ = false;
  bool fpReady_ = false;
  uint32_t fpLastDest_ = 0xffu;   // registre écrit par la dernière opération de calcul flottante
  uint64_t fpLastAt_ = 0;         // ... et son numéro d'instruction (voir execFpData)
  bool fpLastMac_ = false;   // les vérifications d'accès aux flottants sont déjà faites
  uint32_t cpacr_ = 0;
  uint32_t fpccr_ = 0xc0000000u, fpcar_ = 0, fpdscr_ = 0;
  uint32_t itstate_ = 0;
  bool inIt_ = false;      // instruction 16 bits en cours dans un bloc IT
  uint32_t ipsr_ = 0;
  uint32_t otherSp_ = 0;   // la pile non sélectionnée (PSP ou MSP)
  bool usingPsp_ = false;
  bool controlNpriv_ = false;
  uint32_t primask_ = 0, faultmask_ = 0, basepri_ = 0;

  bool pending_[kNumExceptions] = {};
  bool active_[kNumExceptions] = {};
  bool enabled_[kNumExceptions] = {};
  uint8_t priority_[kNumExceptions] = {};
  int execPriority_ = 256;
  bool irqDirty_ = false;
  bool sleeping_ = false;

  uint32_t vtor_ = 0;
  uint32_t aircrPrigroup_ = 0;
  uint32_t ccr_ = 0;
  uint32_t shcsr_ = 0;
  uint32_t cfsr_ = 0, hfsr_ = 0;

  uint32_t systickCtrl_ = 0, systickLoad_ = 0;
  uint32_t systickValueAtRef_ = 0;
  uint64_t systickRefCycle_ = 0;
  uint64_t systickNextFire_ = ~uint64_t { 0 };

  uint64_t cycles_ = 0;
  uint64_t instructions_ = 0;
  uint64_t exceptionCount_[kNumExceptions] = {};
  uint64_t handlerCycles_ = 0, handlerSince_ = 0;
  uint64_t externalDeadline_ = ~uint64_t { 0 };
  uint64_t nextEvent_ = ~uint64_t { 0 };

  bool exclusive_ = false;

  // Modèle de la flash (voir step(), flashFetchLine() et flashDataRead()).
  struct LineCache {
    static constexpr int kMaxLines = 64;
    static constexpr uint32_t kFlashLines = 1u << 16;   // 1 Mo de flash / 16 octets
    int size = 0;                  // nombre de lignes (64 instructions, 8 données)
    uint32_t tag[kMaxLines];       // numéro de ligne (adresse / 16), ~0 = vide
    uint64_t used[kMaxLines];      // dernière utilisation, pour remplacer la plus ancienne
    uint64_t clock = 0;
    std::vector<int8_t> slot;      // pour chaque ligne de la flash : sa place en cache, ou -1
    void reset(int lines) {
      size = lines;
      for (int i = 0; i < kMaxLines; ++i) { tag[i] = ~0u; used[i] = 0; }
      slot.assign(kFlashLines, -1);
    }
    // Renvoie vrai si la ligne était présente ; sinon la place (en chassant la
    // moins récemment utilisée).
    bool access(uint32_t line) {
      line &= kFlashLines - 1;
      ++clock;
      const int s = slot[line];
      if (s >= 0) { used[s] = clock; return true; }
      int oldest = 0;
      for (int i = 1; i < size; ++i) if (used[i] < used[oldest]) oldest = i;
      if (tag[oldest] != ~0u) slot[tag[oldest]] = -1;
      tag[oldest] = line; used[oldest] = clock; slot[line] = static_cast<int8_t>(oldest);
      return false;
    }
  };
  uint32_t flashWait_ = 0;
  bool flashPrefetch_ = false, icacheOn_ = false, dcacheOn_ = false;
  bool prefetchFlush_ = true;       // un branchement a eu lieu (ligne courante à revérifier)
  uint32_t currentLine_ = ~0u;      // ligne de 16 octets d'où viennent les instructions en cours
  LineCache icache_, dcache_;
  uint64_t iMisses_ = 0, dMisses_ = 0;
  bool pipelinedAccess_ = false;    // l'instruction précédente était un chargement/rangement simple
  bool previousAccess_ = false;
  void flashDataRead(uint32_t address);
  void flashFetchLine(uint32_t line);
  // Les instructions sont prises dans la ligne `line` : cas fréquents traités ici
  // (même ligne, ou ligne présente dans le cache), les autres par flashFetchLine.
  void touchLine(uint32_t line) {
    if (line == currentLine_) { prefetchFlush_ = false; return; }
    if (icacheOn_) {
      const int8_t s = icache_.slot[line & (LineCache::kFlashLines - 1)];
      if (s >= 0) { icache_.used[s] = ++icache_.clock; currentLine_ = line; prefetchFlush_ = false; return; }
    }
    flashFetchLine(line);
  }
  // Cycle de donnée d'un chargement/rangement simple, absorbé si l'instruction
  // précédente en était un aussi.
  void accessCycle() {
    if (!previousAccess_) ++cycles_;
    pipelinedAccess_ = true;
  }

  // Détection des boucles d'attente (voir idleCycles()).
  // Une photo de l'état à chaque cible de branchement arrière. Une boucle d'attente
  // peut contenir plusieurs boucles internes (main → DoEvents → ...) : on garde
  // donc une photo par cible, pour quelques cibles.
  struct LoopSnapshot {
    uint32_t pc = 0xffffffffu;
    uint32_t r[15] = {};
    uint32_t status = 0;
    uint64_t changes = 0;          // valeur de changes_ au moment de la photo
    uint64_t cycles = 0, instructions = 0, idle = 0;
  };
  static constexpr int kSnapshots = 8;
  LoopSnapshot snapshots_[kSnapshots];
  int nextSnapshot_ = 0;
  uint64_t changes_ = 1;           // compte les écritures qui modifient la mémoire, lectures de périphériques, interruptions
  uint64_t idleCycles_ = 0;
  uint64_t runLimit_ = 0;
  bool stopRequested_ = false;
  bool halted_ = false;
  const char* haltReason_ = "";
  uint32_t faultPc_ = 0;

  std::vector<uint8_t> flash_, ram_, ccm_;
  size_t flashSize_, ramSize_, ccmSize_;
  Bus* bus_;
};

}  // namespace emu
}  // namespace lments
