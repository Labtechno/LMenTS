// LMenTS — essais du module complet émulé (puce + carte), avec un vrai firmware.
//
//   machine_test <firmware.bin> boot            démarrage, horloges, codec, réglages d'usine
//   machine_test <firmware.bin> play <out.wav>  joue quelques notes et écrit le son
//   machine_test <firmware.bin> pitch           vérifie l'accord : 1 V par octave
//   machine_test <firmware.bin> speed           vitesse de l'émulateur et charge du processeur émulé
//
// Licence MIT.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../Source/emu/ElementsMachine.h"
#include "../Source/hw/Panel.h"

using namespace lments;

static int failures = 0;
static void expect(bool ok, const char* what) {
  std::printf("  %-74s %s\n", what, ok ? "OK" : "ECHEC");
  if (!ok) ++failures;
}

struct Module {
  emu::ElementsMachine machine;
  std::vector<int16_t> left, right;     // tout ce qui est sorti depuis le dernier clear()
  float pots[hw::kNumPots];
  float cvs[hw::kNumCvs] = {};

  explicit Module(const std::vector<uint8_t>& firmware) {
    if (!machine.loadApplication(firmware.data(), firmware.size())) { std::printf("image de firmware refusee\n"); std::exit(2); }
    // Réglage neutre : potards au milieu, niveaux des excitateurs à zéro sauf STRIKE.
    for (auto& p : pots) p = 0.5f;
    pots[hw::kPotBow] = 0.0f; pots[hw::kPotBlow] = 0.0f; pots[hw::kPotStrike] = 0.8f;
    pots[hw::kPotSpace] = 0.3f; pots[hw::kPotDamping] = 0.6f;
    apply();
    machine.powerOn();
  }
  void apply() {
    for (int i = 0; i < hw::kNumPots; ++i) machine.controls.pot[hw::kPotAddress[i]] = hw::potCode(pots[i]);
    for (int i = 0; i < hw::kNumCvs; ++i) machine.controls.cv[hw::kCvChannel[i]] = hw::cvCode(static_cast<hw::Cv>(i), cvs[i]);
  }
  void run(double seconds, const int16_t* inL = nullptr, const int16_t* inR = nullptr) {
    apply();
    int n = static_cast<int>(seconds * 32000.0);
    int16_t l[256], r[256];
    while (n > 0) {
      const int chunk = n > 256 ? 256 : n;
      machine.run(inL, inR, l, r, chunk);
      left.insert(left.end(), l, l + chunk);
      right.insert(right.end(), r, r + chunk);
      n -= chunk;
    }
  }
  void clear() { left.clear(); right.clear(); }
  double rms(const std::vector<int16_t>& v) const {
    if (v.empty()) return 0.0;
    double s = 0.0;
    for (int16_t x : v) s += static_cast<double>(x) * x;
    return std::sqrt(s / static_cast<double>(v.size())) / 32768.0;
  }
};

// Fréquence fondamentale par autocorrélation (signal périodique simple).
static double fundamental(const std::vector<int16_t>& v, double sampleRate) {
  const size_t n = v.size();
  if (n < 4096) return 0.0;
  double best = 0.0; size_t bestLag = 0;
  std::vector<double> x(n);
  double mean = 0.0;
  for (int16_t s : v) mean += s;
  mean /= static_cast<double>(n);
  for (size_t i = 0; i < n; ++i) x[i] = v[i] - mean;
  double energy = 0.0;
  for (double s : x) energy += s * s;
  if (energy <= 0.0) return 0.0;
  std::vector<double> ac(2001, 0.0);
  for (size_t lag = 16; lag <= 2000; ++lag) {
    double s = 0.0;
    for (size_t i = 0; i + lag < n; ++i) s += x[i] * x[i + lag];
    ac[lag] = s / energy;
  }
  // Premier maximum local nettement positif : la période.
  for (size_t lag = 17; lag < 2000; ++lag) {
    if (ac[lag] > 0.5 && ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1]) { best = ac[lag]; bestLag = lag; break; }
  }
  if (bestLag == 0) return 0.0;
  // Affinage par parabole.
  const double a = ac[bestLag - 1], b = best, c = ac[bestLag + 1];
  const double shift = (a - c) / (2.0 * (a - 2.0 * b + c));
  return sampleRate / (static_cast<double>(bestLag) + shift);
}

static void writeWav(const char* path, const std::vector<int16_t>& l, const std::vector<int16_t>& r, uint32_t rate) {
  FILE* f = std::fopen(path, "wb");
  if (!f) { std::perror(path); return; }
  const uint32_t bytes = static_cast<uint32_t>(l.size()) * 4;
  auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 4, 1, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 8, 1, f);
  u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
  std::fwrite("data", 4, 1, f); u32(bytes);
  for (size_t i = 0; i < l.size(); ++i) { u16(static_cast<uint16_t>(l[i])); u16(static_cast<uint16_t>(r[i])); }
  std::fclose(f);
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: machine_test firmware.bin boot|play|pitch|speed [fichier]\n"); return 2; }
  std::vector<uint8_t> firmware;
  {
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 2; }
    uint8_t buffer[65536];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) firmware.insert(firmware.end(), buffer, buffer + n);
    std::fclose(f);
  }
  const std::string cmd = argv[2];
  Module m(firmware);

  if (cmd == "boot") {
    m.run(0.5);
    std::printf("Demarrage : processeur a %.1f MHz, son a %.2f Hz, %u ecritures en flash\n",
                m.machine.cpuHz() / 1e6, m.machine.sampleRate(), m.machine.flashWriteCount());
    if (m.machine.crashed()) std::printf("  arret : %s (pc=%08x, faute a %08x)\n", m.machine.crashReason(), m.machine.cpu().pc(), m.machine.cpu().faultPc());
    expect(!m.machine.crashed(), "le processeur tourne");
    expect(std::abs(m.machine.cpuHz() - 168e6) < 1.0, "le firmware a regle l'horloge du processeur sur 168 MHz");
    expect(std::abs(m.machine.sampleRate() - 32001.2) < 0.1, "le firmware a regle le son sur 32 001,2 Hz (32 kHz nominal)");
    expect(m.machine.codecRegister(9) == 1, "le codec audio a ete regle et active par le firmware");
    expect(m.machine.codecRegister(7) == 0x02, "format du codec : I2S 16 bits, esclave");
    expect(m.machine.flashWriteCount() > 0, "premier demarrage : le firmware enregistre ses reglages d'usine");
    // À la mise sous tension, le firmware part de potards « à zéro » et rejoint leur
    // vraie position : le résonateur réagit par un petit son, qui s'éteint.
    std::printf("Mise sous tension : niveau %.4f pendant la premiere demi-seconde\n", m.rms(m.left));
    m.run(2.5); m.clear(); m.run(0.5);
    expect(m.rms(m.left) < 1e-3 && m.rms(m.right) < 1e-3, "silence une fois le module stabilise, tant que rien n'est joue");

    // Deuxième mise sous tension : les réglages sont relus, rien n'est réécrit.
    const uint32_t writes = m.machine.flashWriteCount();
    m.machine.powerOn();
    m.clear();
    m.run(0.3);
    expect(m.machine.flashWriteCount() == writes, "deuxieme demarrage : les reglages sont relus, rien n'est reecrit");

    // Bouton PLAY : une note, la LED du bouton s'allume, les deux autres suivent le son.
    m.clear();
    m.machine.controls.button = true;
    m.run(0.05);
    const bool ledOn = m.machine.leds().gate;
    float exciterLed = 0.0f, resonatorLed = 0.0f;
    for (int i = 0; i < 20; ++i) {
      m.run(0.01);
      exciterLed = std::max(exciterLed, m.machine.leds().exciter);
      resonatorLed = std::max(resonatorLed, m.machine.leds().resonator);
    }
    m.machine.controls.button = false;
    m.run(0.5);
    std::printf("Note au bouton PLAY : niveau gauche %.4f, droite %.4f ; LED excitateur %.2f, resonateur %.2f\n",
                m.rms(m.left), m.rms(m.right), exciterLed, resonatorLed);
    expect(ledOn, "la LED du bouton PLAY s'allume pendant la note");
    expect(m.rms(m.left) > 0.005 && m.rms(m.right) > 0.005, "le bouton PLAY produit du son sur les deux sorties");
    expect(exciterLed > 0.02 && resonatorLed > 0.02, "les LED d'excitateur et de resonateur suivent le son");
    expect(!m.machine.leds().gate, "la LED du bouton s'eteint au relachement");

    // Entrée GATE.
    m.run(1.5); m.clear();
    m.machine.controls.gate = true; m.run(0.2);
    m.machine.controls.gate = false; m.run(0.3);
    expect(m.rms(m.left) > 0.005, "l'entree GATE declenche une note");

    // Entrées audio : un bruit dans EXT IN (côté STRIKE) fait sonner le résonateur.
    m.run(2.0); m.clear();
    m.run(0.3);
    const double quiet = m.rms(m.left);
    std::vector<int16_t> noise(256);
    uint32_t seed = 1;
    m.clear();
    for (int k = 0; k < 40; ++k) {
      for (auto& s : noise) { seed = seed * 1664525u + 1013904223u; s = static_cast<int16_t>(static_cast<int32_t>(seed) >> 19); }
      m.apply();
      int16_t l[256], r[256];
      m.machine.run(noise.data(), nullptr, l, r, 256);
      m.left.insert(m.left.end(), l, l + 256);
    }
    std::printf("Entree audio : niveau de sortie %.5f sans signal, %.5f avec du bruit en entree\n", quiet, m.rms(m.left));
    expect(m.rms(m.left) > 10.0 * quiet + 0.002, "un signal dans EXT IN fait sonner le resonateur");
  } else if (cmd == "play" && argc > 3) {
    m.run(0.3); m.clear();
    const float notes[] = { 0.0f, 0.25f, 0.5833f, 1.0f, 0.5833f, 0.25f, 0.0f, -1.0f };
    for (int section = 0; section < 3; ++section) {
      if (section == 1) { m.pots[hw::kPotStrike] = 0.0f; m.pots[hw::kPotBow] = 0.9f; m.pots[hw::kPotContour] = 0.6f; }
      if (section == 2) { m.pots[hw::kPotBow] = 0.0f; m.pots[hw::kPotBlow] = 0.8f; m.pots[hw::kPotGeometry] = 0.7f; m.pots[hw::kPotSpace] = 0.7f; }
      for (float v : notes) {
        m.cvs[hw::kCvVOct] = 1.0f + v;
        m.machine.controls.gate = true; m.run(section == 0 ? 0.1 : 0.35);
        m.machine.controls.gate = false; m.run(section == 0 ? 0.3 : 0.15);
      }
      m.run(1.0);
    }
    writeWav(argv[3], m.left, m.right, 32000);
    std::printf("%.1f s de son ecrites dans %s (niveau %.3f / %.3f)\n", m.left.size() / 32000.0, argv[3], m.rms(m.left), m.rms(m.right));
    expect(!m.machine.crashed(), "le processeur tourne");
  } else if (cmd == "pitch") {
    // Son le plus pur possible : un coup de maillet sur un résonateur peu amorti.
    m.pots[hw::kPotGeometry] = 0.3f; m.pots[hw::kPotBrightness] = 0.2f; m.pots[hw::kPotDamping] = 0.8f;
    m.pots[hw::kPotSpace] = 0.0f; m.pots[hw::kPotCoarse] = 0.5f; m.pots[hw::kPotFine] = 0.5f;
    m.run(0.5);
    double f[4];
    for (int v = 0; v < 4; ++v) {
      m.cvs[hw::kCvVOct] = static_cast<float>(v);
      m.run(0.3); m.clear();
      m.machine.controls.gate = true; m.run(0.05);
      m.machine.controls.gate = false; m.run(0.1); m.clear();
      m.run(0.6);
      f[v] = fundamental(m.left, m.machine.sampleRate());
      std::printf("  V/OCT = %d V : %.2f Hz\n", v, f[v]);
      m.run(1.0);
    }
    for (int v = 1; v < 4; ++v) {
      char what[96];
      std::snprintf(what, sizeof(what), "de %d V a %d V : une octave (rapport %.4f)", v - 1, v, f[v - 1] > 0 ? f[v] / f[v - 1] : 0.0);
      expect(f[v - 1] > 0 && std::abs(f[v] / f[v - 1] - 2.0) < 0.01, what);
    }
    // Avec COARSE et FINE au milieu, 1 V donne un do : note 12 + 30 + 19 = 61... le
    // firmware compte en demi-tons MIDI, la 440 Hz = note 69.
    const double expected = 440.0 * std::pow(2.0, (12.0 + 30.0 + 19.0 - 69.0) / 12.0);
    char what[96];
    std::snprintf(what, sizeof(what), "a 1 V, COARSE et FINE au milieu : %.1f Hz attendus (reglage d'usine)", expected);
    expect(std::abs(f[1] / expected - 1.0) < 0.03, what);
  } else if (cmd == "speed") {
    m.run(0.5);
    struct Case { const char* name; float bow, blow, strike, space; bool gate; };
    const Case cases[] = {
      { "au repos", 0.0f, 0.0f, 0.8f, 0.3f, false },
      { "maillet", 0.0f, 0.0f, 0.8f, 0.3f, true },
      { "archet + souffle + maillet, reverb", 0.8f, 0.8f, 0.8f, 0.9f, true },
    };
    for (const Case& c : cases) {
      m.pots[hw::kPotBow] = c.bow; m.pots[hw::kPotBlow] = c.blow; m.pots[hw::kPotStrike] = c.strike; m.pots[hw::kPotSpace] = c.space;
      m.machine.controls.gate = c.gate;
      m.run(0.2);
      auto& cpu = m.machine.cpu();
      const uint64_t c0 = m.machine.cycles(), i0 = cpu.idleCycles(), h0 = cpu.handlerCycles(), n0 = cpu.exceptionCount(16 + 14);
      const uint32_t o0 = m.machine.overruns();
      const auto t0 = std::chrono::steady_clock::now();
      m.run(3.0);
      const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      const double total = static_cast<double>(m.machine.cycles() - c0);
      const double busy = 1.0 - static_cast<double>(cpu.idleCycles() - i0) / total;
      const double blocks = static_cast<double>(cpu.exceptionCount(16 + 14) - n0);
      std::printf("  %-36s 3 s de son en %.2f s : %3.0f %% d'un coeur ; puce occupee a %.1f %% (interruptions %.1f %%), %.0f blocs/s, %u blocs manques\n",
                  c.name, s, 100.0 * s / 3.0, 100.0 * busy, 100.0 * static_cast<double>(cpu.handlerCycles() - h0) / total, blocks / 3.0, m.machine.overruns() - o0);
      m.machine.controls.gate = false;
      m.run(0.5);
    }
    expect(!m.machine.crashed(), "le processeur tourne");
  } else {
    std::fprintf(stderr, "commande inconnue\n");
    return 2;
  }
  std::printf(failures ? "DES ECHECS\n" : "OK\n");
  return failures ? 1 : 0;
}
