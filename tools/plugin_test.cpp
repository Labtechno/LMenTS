// LMenTS — essai du processeur du plugin, sans logiciel hôte : chargement d'un
// firmware, notes MIDI, accord, entrées audio, mémoire de chaque firmware,
// enregistrement et relecture de l'état d'un projet.
//
//   plugin_test <elements.wav|.bin> [fichier_invalide]
//
// Pour l'essai de la mémoire par firmware, un deuxième firmware est fabriqué à
// partir du premier : le même programme, suivi de quatre octets nuls. Son
// empreinte est différente, le plugin le traite donc comme un autre firmware.
//
// Licence MIT.

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// Niveau de OUT R pendant une note de maillet : il dépend nettement du modèle de résonateur.
double noteLevel(LmentsAudioProcessor& p) {
  render(p, 1.0);
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 52, static_cast<juce::uint8>(100)), 0);
  midi.addEvent(juce::MidiMessage::noteOff(1, 52), 2400);
  const Run r = render(p, 0.8, &midi);
  render(p, 2.0);
  return rms(r.right);
}

// Change de modèle de résonateur comme sur le module : atténuverseurs du
// résonateur à fond à droite, DAMPING, POSITION et SPACE presque au maximum, puis
// PLAY tenu trois secondes. Le firmware enregistre le nouveau modèle en mémoire.
void nextResonatorModel(LmentsAudioProcessor& p) {
  const char* ids[] = { "geometryAtt", "brightnessAtt", "dampingAtt", "positionAtt", "spaceAtt", "damping", "position", "space" };
  float saved[8];
  for (int i = 0; i < 8; ++i) saved[i] = p.apvts.getRawParameterValue(ids[i])->load();
  for (int i = 0; i < 5; ++i) setParam(p, ids[i], 1.0f);
  for (int i = 5; i < 8; ++i) setParam(p, ids[i], 0.9f);
  render(p, 1.0);
  setParam(p, "play", 1.0f);
  render(p, 3.4);
  setParam(p, "play", 0.0f);
  render(p, 0.5);
  for (int i = 0; i < 8; ++i) setParam(p, ids[i], saved[i]);
  render(p, 5.0);   // les potards rejoignent leur position, le module se calme
}

bool close(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance * std::max(a, b); }

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

  // --- Mémoire de chaque firmware ------------------------------------------------------
  std::printf("Memoire par firmware :\n");
  const juce::File firmwareA { juce::String(argv[1]) };
  const juce::File firmwareB = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("lments-test-B.bin");
  {
    // Le deuxième firmware : l'image du premier (décodée si c'est un .wav), plus 4 octets nuls.
    LmentsAudioProcessor tmp;
    tmp.setPlayConfigDetails(2, 2, kRate, kBlock);
    tmp.prepareToPlay(kRate, kBlock);
    tmp.loadFirmwareFile(firmwareA);
    juce::MemoryBlock st;
    tmp.getStateInformation(st);
    auto xml = juce::AudioProcessor::getXmlFromBinary(st.getData(), static_cast<int>(st.getSize()));
    juce::MemoryBlock image;
    image.fromBase64Encoding(xml->getChildByName("MODULE")->getStringAttribute("firmwareImage"));
    const uint8_t zeros[4] = {};
    image.append(zeros, 4);
    firmwareB.replaceWithData(image.getData(), image.getSize());
  }
  LmentsAudioProcessor m;
  m.setPlayConfigDetails(2, 2, kRate, kBlock);
  m.prepareToPlay(kRate, kBlock);
  m.loadFirmwareFile(firmwareA);
  render(m, 3.0);
  const juce::String idA = m.firmwareId();
  const double model0 = noteLevel(m);
  nextResonatorModel(m);
  const double model1 = noteLevel(m);
  std::printf("  niveau d'une note : %.4f avec le modele d'origine, %.4f apres l'appui long\n", model0, model1);
  expect(!close(model0, model1, 0.2), "l'appui long sur PLAY change le modele de resonateur");

  expect(m.loadFirmwareFile(firmwareB).isEmpty() && m.firmwareId() != idA, "un deuxieme firmware (autre empreinte) se charge");
  render(m, 3.0);
  expect(!m.memoryRestored(), "un firmware jamais utilise demarre comme un module neuf");
  const double levelB = noteLevel(m);
  std::printf("  deuxieme firmware : %.4f\n", levelB);
  expect(close(levelB, model0, 0.15), "le deuxieme firmware joue avec le modele d'origine");

  m.loadFirmwareFile(firmwareA);
  render(m, 3.0);
  const double backA = noteLevel(m);
  std::printf("  retour au premier : %.4f\n", backA);
  expect(m.memoryRestored() && close(backA, model1, 0.15), "revenir au premier firmware lui rend sa memoire (modele change)");
  expect(m.numMemories() == 2, "le projet garde la memoire des deux firmwares");

  juce::MemoryBlock projectState;
  m.getStateInformation(projectState);
  {
    LmentsAudioProcessor r;
    r.setPlayConfigDetails(2, 2, kRate, kBlock);
    r.prepareToPlay(kRate, kBlock);
    r.setStateInformation(projectState.getData(), static_cast<int>(projectState.getSize()));
    render(r, 3.0);
    const double reloaded = noteLevel(r);
    expect(r.firmwareId() == idA && r.numMemories() == 2, "projet relu : firmware et memoires des deux firmwares");
    expect(close(reloaded, model1, 0.15), "projet relu : le modele de resonateur choisi est garde");
    r.loadFirmwareFile(firmwareB);
    render(r, 3.0);
    expect(r.memoryRestored(), "projet relu : l'autre firmware retrouve sa memoire");
    r.loadFirmwareFile(firmwareA);
    render(r, 3.0);
    r.resetFirmwareMemory();
    render(r, 3.0);
    const double reset = noteLevel(r);
    std::printf("  apres reglages d'usine : %.4f\n", reset);
    expect(!r.memoryRestored() && close(reset, model0, 0.15), "reglages d'usine : retour au modele d'origine");
  }

  // Projet enregistré par la version de l'étape 2 : la zone des réglages (secteur 1)
  // dans la propriété « settings ».
  {
    auto xml = juce::AudioProcessor::getXmlFromBinary(projectState.getData(), static_cast<int>(projectState.getSize()));
    auto* module = xml->getChildByName("MODULE");
    juce::String sector1;
    for (auto* memory : module->getChildWithTagNameIterator("MEMORY")) {
      if (!memory->getBoolAttribute("current")) continue;
      for (auto* sector : memory->getChildWithTagNameIterator("SECTOR"))
        if (sector->getIntAttribute("index") == 1) sector1 = sector->getStringAttribute("data");
    }
    module->deleteAllChildElementsWithTagName("MEMORY");
    module->setAttribute("settings", sector1);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*xml, old);
    LmentsAudioProcessor r;
    r.setPlayConfigDetails(2, 2, kRate, kBlock);
    r.prepareToPlay(kRate, kBlock);
    r.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    render(r, 3.0);
    expect(sector1.isNotEmpty() && close(noteLevel(r), model1, 0.15), "projet de l'etape 2 : ses reglages sont repris");
  }

  // --- Faux firmware : le plugin le signale -----------------------------------------------
  {
    // Table de vecteurs valable, puis une boucle sans fin : le son ne démarre jamais.
    const uint8_t image[16] = { 0x00, 0x00, 0x02, 0x20, 0x09, 0x80, 0x00, 0x08, 0xfe, 0xe7, 0, 0, 0, 0, 0, 0 };
    const juce::File fake = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("lments-test-boucle.bin");
    fake.replaceWithData(image, sizeof(image));
    LmentsAudioProcessor r;
    r.setPlayConfigDetails(2, 2, kRate, kBlock);
    r.prepareToPlay(kRate, kBlock);
    expect(r.loadFirmwareFile(fake).isEmpty(), "un firmware qui ne fait rien se charge quand meme");
    render(r, 1.0);
    expect(r.problem() == LmentsAudioProcessor::Problem::kNone, "pendant 2 secondes, pas d'alerte (temps de demarrage)");
    render(r, 1.5);
    expect(r.problem() == LmentsAudioProcessor::Problem::kNoSound, "au-dela : le plugin signale que le son n'a pas demarre");
    fake.deleteFile();
  }
  firmwareB.deleteFile();

  // --- Jauge et surcharge (facultatif : firmware plus lourd, voir tools/build_firmware.sh) ---
  if (const char* heavy = std::getenv("LMENTS_HEAVY_FIRMWARE")) {
    std::printf("Surcharge :\n");
    for (const juce::String path : { juce::String(argv[1]), juce::String(heavy) }) {
      LmentsAudioProcessor r;
      r.setPlayConfigDetails(2, 2, kRate, kBlock);
      r.prepareToPlay(kRate, kBlock);
      r.loadFirmwareFile(juce::File(path));
      // Réglage le plus lourd : note tenue très grave, trois excitateurs, réverbération.
      setParam(r, "bow", 0.8f); setParam(r, "blow", 0.8f); setParam(r, "strike", 0.8f);
      setParam(r, "space", 0.9f); setParam(r, "coarse", 0.0f); setParam(r, "geometry", 0.2f);
      render(r, 3.0);
      juce::MidiBuffer midi;
      midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(100)), 0);
      render(r, 2.0, &midi);
      std::printf("  %s : charge %.1f %%, pointe %.1f %%, blocs manques %u, alerte %d\n", juce::File(path).getFileName().toRawUTF8(),
                  100.0f * r.moduleLoad(), 100.0f * r.moduleLoadPeak(), r.moduleOverruns(), r.moduleOverloaded());
      if (path == juce::String(argv[1])) {
        expect(r.moduleOverruns() == 0 && !r.moduleOverloaded() && r.moduleLoadPeak() < 0.99f, "firmware normal, reglage le plus lourd : pas de surcharge");
        expect(r.moduleLoadPeak() >= r.moduleLoad() && r.moduleLoad() > 0.8f, "jauge : moyenne et pointe plausibles");
      } else {
        expect(r.moduleOverruns() > 0 && r.moduleOverloaded() && r.moduleLoadPeak() > 0.99f, "firmware trop lourd : le plugin signale la surcharge");
      }
    }
  }

  // Image de l'interface (pour la documentation et pour vérifier la mise en page) :
  //   LMENTS_SNAPSHOT=interface.png plugin_test firmware.wav
  if (const char* snapshot = std::getenv("LMENTS_SNAPSHOT")) {
    LmentsAudioProcessor s;
    s.setPlayConfigDetails(2, 2, kRate, kBlock);
    s.prepareToPlay(kRate, kBlock);
    s.loadFirmwareFile(firmwareA);
    setParam(s, "cv_strength_source", 2.0f);      // vélocité
    setParam(s, "cv_flow_source", 1.0f);          // molette
    setParam(s, "cv_damping_source", 4.0f);       // automation
    setParam(s, "cv_damping", 3.1f);
    setParam(s, "cv_brightness_source", 4.0f);
    setParam(s, "cv_brightness", -1.75f);
    render(s, 1.0);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 80), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(110)), 0);
    render(s, 0.6, &midi);
    std::unique_ptr<juce::AudioProcessorEditor> editor(s.createEditor());
    editor->setVisible(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);   // l'interface lit les tensions
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(snapshot);
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat png;
    expect(stream.openedOk() && png.writeImageToStream(image, stream), "image de l'interface enregistree");
  }

  std::printf(failures ? "DES ECHECS\n" : "OK\n");
  return failures ? 1 : 0;
}
