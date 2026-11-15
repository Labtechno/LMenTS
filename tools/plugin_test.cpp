// LMenTS — essai du processeur du plugin, sans logiciel hôte : chargement d'un
// firmware, notes MIDI, accord, entrées audio, enregistrement et relecture de
// l'état d'un projet.
//
//   plugin_test <elements.wav|.bin> [fichier_invalide]
//
// Licence MIT.

#include <cmath>
#include <cstdio>
#include <vector>

#include "../Source/PluginProcessor.h"

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
  std::printf("  %-72s %s\n", what, ok ? "OK" : "ECHEC");
  if (!ok) ++failures;
}

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

// Fait tourner le plugin ; enregistre la voie gauche (et la droite) de la sortie.
struct Run {
  std::vector<float> left, right;
};
Run render(LmentsAudioProcessor& p, double seconds, juce::MidiBuffer* firstMidi = nullptr,
           const std::function<float(int64_t)>& input = nullptr, int64_t* clock = nullptr) {
  Run r;
  juce::AudioBuffer<float> buffer(2, kBlock);
  const int total = static_cast<int>(seconds * kRate);
  int64_t t = clock ? *clock : 0;
  for (int pos = 0; pos < total; pos += kBlock) {
    juce::MidiBuffer empty;
    juce::MidiBuffer& midi = (pos == 0 && firstMidi) ? *firstMidi : empty;
    buffer.clear();
    if (input) {
      for (int i = 0; i < kBlock; ++i) buffer.setSample(1, i, input(t + i));   // entrée 2 = EXT IN turquoise
    }
    p.processBlock(buffer, midi);
    t += kBlock;
    for (int i = 0; i < kBlock; ++i) { r.left.push_back(buffer.getSample(0, i)); r.right.push_back(buffer.getSample(1, i)); }
  }
  if (clock) *clock = t;
  return r;
}

double rms(const std::vector<float>& v, size_t from = 0) {
  double s = 0.0;
  for (size_t i = from; i < v.size(); ++i) s += static_cast<double>(v[i]) * v[i];
  return v.size() > from ? std::sqrt(s / static_cast<double>(v.size() - from)) : 0.0;
}

// Fréquence fondamentale par autocorrélation.
double fundamental(const std::vector<float>& v) {
  const size_t n = v.size();
  double mean = 0.0;
  for (float s : v) mean += s;
  mean /= static_cast<double>(n);
  std::vector<double> x(n);
  double energy = 0.0;
  for (size_t i = 0; i < n; ++i) { x[i] = v[i] - mean; energy += x[i] * x[i]; }
  if (energy <= 0.0) return 0.0;
  const size_t maxLag = 3000;
  std::vector<double> ac(maxLag + 2, 0.0);
  for (size_t lag = 20; lag <= maxLag + 1; ++lag) {
    double s = 0.0;
    for (size_t i = 0; i + lag < n; ++i) s += x[i] * x[i + lag];
    ac[lag] = s / energy;
  }
  for (size_t lag = 21; lag <= maxLag; ++lag) {
    if (ac[lag] > 0.5 && ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1]) {
      const double a = ac[lag - 1], b = ac[lag], c = ac[lag + 1];
      return kRate / (static_cast<double>(lag) + (a - c) / (2.0 * (a - 2.0 * b + c)));
    }
  }
  return 0.0;
}

void setParam(LmentsAudioProcessor& p, const char* id, float value) {
  auto* parameter = p.apvts.getParameter(id);
  parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

// Un coup de maillet à la note donnée, puis mesure de la hauteur.
double pitchOf(LmentsAudioProcessor& p, int note) {
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
  midi.addEvent(juce::MidiMessage::noteOff(1, note), 2400);
  render(p, 0.15, &midi);
  const Run r = render(p, 0.5);
  render(p, 1.5);
  // SPACE à zéro : OUT L porte l'excitateur seul, OUT R le résonateur seul.
  return fundamental(r.right);
}

}  // namespace

int main(int argc, char** argv) {
  juce::ScopedJuceInitialiser_GUI init;
  if (argc < 2) { std::printf("usage: plugin_test firmware.wav [invalide]\n"); return 2; }

  LmentsAudioProcessor p;
  p.setPlayConfigDetails(2, 2, kRate, kBlock);
  p.prepareToPlay(kRate, kBlock);

  // --- Sans firmware : silence, et le dit -----------------------------------------
  expect(!p.hasFirmware(), "au premier lancement, aucun firmware n'est charge");
  expect(rms(render(p, 0.2).left) == 0.0, "sans firmware : silence");

  if (argc > 2) {
    const auto error = p.loadFirmwareFile(juce::File(juce::String(argv[2])));
    std::printf("Fichier invalide : %s\n", error.toRawUTF8());
    expect(error.isNotEmpty() && !p.hasFirmware(), "un fichier qui n'est pas un firmware Elements est refuse");
  }

  // --- Chargement ----------------------------------------------------------------
  const auto error = p.loadFirmwareFile(juce::File(juce::String(argv[1])));
  expect(error.isEmpty() && p.hasFirmware(), "le firmware se charge");
  if (!p.hasFirmware()) { std::printf("%s\n", error.toRawUTF8()); return 1; }
  std::printf("Firmware : %s\n", p.firmwareName().toRawUTF8());
  render(p, 3.0);   // démarrage et stabilisation
  expect(!p.moduleCrashed(), "le processeur du module tourne");
  expect(rms(render(p, 0.5).left) < 1e-3, "au repos : silence");

  // --- Notes MIDI et accord -------------------------------------------------------
  setParam(p, "damping", 0.8f);
  setParam(p, "brightness", 0.2f);
  setParam(p, "geometry", 0.3f);
  setParam(p, "space", 0.0f);
  render(p, 1.0);
  const double a3 = pitchOf(p, 57), a4 = pitchOf(p, 69), a2 = pitchOf(p, 45);
  std::printf("La 2 : %.2f Hz, la 3 : %.2f Hz, la 4 : %.2f Hz\n", a2, a3, a4);
  expect(std::abs(a3 / 220.0 - 1.0) < 0.01, "note 57 au clavier = 220 Hz (a 1 % pres), COARSE en position par defaut");
  expect(std::abs(a4 / a3 - 2.0) < 0.01 && std::abs(a3 / a2 - 2.0) < 0.01, "une octave au clavier = une octave entendue");

  // --- Stéréo, voyants, charge -------------------------------------------------------
  {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    setParam(p, "space", 0.6f);
    const Run r = render(p, 0.1, &midi);
    const auto leds = p.leds();
    const Run r2 = render(p, 0.6);
    juce::MidiBuffer off;
    off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    render(p, 0.1, &off);
    double diff = 0.0;
    for (size_t i = 0; i < r2.left.size(); ++i) diff += std::abs(r2.left[i] - r2.right[i]);
    std::printf("Note tenue : niveau %.3f / %.3f, LED %d %.2f %.2f, puce du module %.0f %%\n",
                rms(r2.left), rms(r2.right), leds.play, leds.exciter, leds.resonator, 100.0f * p.moduleLoad());
    expect(rms(r2.left) > 0.01 && rms(r2.right) > 0.01 && diff > 1.0, "OUT L et OUT R sortent un son stereo");
    expect(leds.play && leds.resonator > 0.0f, "les voyants suivent la note");
    expect(p.moduleLoad() > 0.5f && p.moduleLoad() <= 1.0f, "la jauge de la puce du module donne une valeur plausible");
    (void) r;
  }
  render(p, 3.0);

  // --- Bouton PLAY -------------------------------------------------------------------
  setParam(p, "play", 1.0f);
  const Run played = render(p, 0.3);
  setParam(p, "play", 0.0f);
  render(p, 2.0);
  expect(rms(played.left) > 0.01, "le bouton PLAY (parametre automatisable) joue une note");

  // --- Entrée audio : EXT IN turquoise (côté STRIKE) ----------------------------------
  setParam(p, "strike", 0.0f);
  render(p, 3.0);
  const double quiet = rms(render(p, 0.5).left);
  int64_t clock = 0;
  uint32_t seed = 1;
  const Run excited = render(p, 1.0, nullptr, [&seed](int64_t t) {
    seed = seed * 1664525u + 1013904223u;
    return (t % 24000) < 400 ? static_cast<float>(static_cast<int32_t>(seed) >> 8) / 8388608.0f * 0.8f : 0.0f;
  }, &clock);
  std::printf("Entree audio : %.5f sans signal, %.5f avec des coups de bruit dans EXT IN\n", quiet, rms(excited.left));
  expect(rms(excited.left) > 10.0 * quiet + 0.003, "un son envoye dans EXT IN fait sonner le resonateur");
  setParam(p, "strike", 0.7f);

  // --- CV pilotées par MIDI ou par automation ------------------------------------------
  {
    auto* source = p.apvts.getParameter("cv_space_source");
    source->setValueNotifyingHost(source->convertTo0to1(4.0f));   // Automation
    setParam(p, "cv_space", 3.0f);
    render(p, 0.05);
    const float v = p.cvVolts(lments::hw::kCvSpace);
    std::printf("Prise SPACE : %.2f V\n", v);
    expect(std::abs(v - 3.0f) < 0.01f, "une prise CV en mode Automation suit son parametre");
    source->setValueNotifyingHost(source->convertTo0to1(0.0f));
  }

  // --- Enregistrement du projet et relecture -------------------------------------------
  setParam(p, "geometry", 0.77f);
  render(p, 0.3);
  juce::MemoryBlock state;
  p.getStateInformation(state);
  std::printf("Etat du projet : %zu octets\n", state.getSize());

  LmentsAudioProcessor q;
  q.setPlayConfigDetails(2, 2, kRate, kBlock);
  q.prepareToPlay(kRate, kBlock);
  q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  expect(q.hasFirmware() && q.firmwareName() == p.firmwareName(), "le firmware est relu depuis le projet");
  expect(std::abs(q.apvts.getRawParameterValue("geometry")->load() - 0.77f) < 1e-4f, "les potards sont relus depuis le projet");
  render(q, 3.0);
  expect(!q.moduleCrashed(), "le module relu demarre");
  setParam(q, "damping", 0.8f); setParam(q, "brightness", 0.2f); setParam(q, "geometry", 0.3f); setParam(q, "space", 0.0f);
  render(q, 1.0);
  const double qa3 = pitchOf(q, 57);
  expect(std::abs(qa3 / 220.0 - 1.0) < 0.01, "le module relu est toujours accorde");

  // --- Autres fréquences de l'hôte -------------------------------------------------------
  for (double rate : { 44100.0, 96000.0 }) {
    LmentsAudioProcessor r;
    r.setPlayConfigDetails(2, 2, rate, kBlock);
    r.prepareToPlay(rate, kBlock);
    r.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    double sum = 0.0;
    for (int pos = 0; pos < static_cast<int>(rate * 3.5); pos += kBlock) {
      buffer.clear();
      juce::MidiBuffer empty;
      r.processBlock(buffer, pos >= static_cast<int>(rate * 3.0) && pos < static_cast<int>(rate * 3.0) + kBlock ? midi : empty);
      if (pos >= static_cast<int>(rate * 3.0)) sum += buffer.getRMSLevel(0, 0, kBlock);
    }
    char what[96];
    std::snprintf(what, sizeof(what), "l'hote a %.1f kHz : le module joue", rate / 1000.0);
    expect(sum > 0.1 && !r.moduleCrashed(), what);
  }

  std::printf(failures ? "DES ECHECS\n" : "OK\n");
  return failures ? 1 : 0;
}
