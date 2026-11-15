// LMenTS — processeur audio

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include "FirmwareFile.h"
#include "PluginEditor.h"

using namespace lments;

// --- La façade ---------------------------------------------------------------------------

// Avec COARSE sur cette position, la note jouée au clavier est la note entendue
// (le firmware ajoute 19 demi-tons à la position du potard, graduée de 0 à 60).
static constexpr float kCoarseInTune = 17.0f / 60.0f;

const std::array<PotInfo, hw::kNumPots> kPots = { {
  { hw::kPotContour, "contour", "CONTOUR", false, 0.5f },
  { hw::kPotBow, "bow", "BOW", false, 0.0f },
  { hw::kPotBlow, "blow", "BLOW", false, 0.0f },
  { hw::kPotStrike, "strike", "STRIKE", false, 0.7f },
  { hw::kPotCoarse, "coarse", "COARSE", false, kCoarseInTune },
  { hw::kPotFine, "fine", "FINE", true, 0.0f },
  { hw::kPotFm, "fm", "FM", true, 0.0f },
  { hw::kPotFlow, "flow", "FLOW", false, 0.5f },
  { hw::kPotMallet, "mallet", "MALLET", false, 0.5f },
  { hw::kPotGeometry, "geometry", "GEOMETRY", false, 0.25f },
  { hw::kPotBrightness, "brightness", "BRIGHTNESS", false, 0.5f },
  { hw::kPotBowTimbre, "bowTimbre", "TIMBRE (bow)", false, 0.5f },
  { hw::kPotBlowTimbre, "blowTimbre", "TIMBRE (blow)", false, 0.5f },
  { hw::kPotStrikeTimbre, "strikeTimbre", "TIMBRE (strike)", false, 0.5f },
  { hw::kPotDamping, "damping", "DAMPING", false, 0.5f },
  { hw::kPotPosition, "position", "POSITION", false, 0.3f },
  { hw::kPotSpace, "space", "SPACE", false, 0.3f },
  { hw::kPotBowTimbreAtt, "bowTimbreAtt", "CV > TIMBRE (bow)", true, 0.0f },
  { hw::kPotFlowAtt, "flowAtt", "CV > FLOW", true, 0.0f },
  { hw::kPotBlowTimbreAtt, "blowTimbreAtt", "CV > TIMBRE (blow)", true, 0.0f },
  { hw::kPotMalletAtt, "malletAtt", "CV > MALLET", true, 0.0f },
  { hw::kPotStrikeTimbreAtt, "strikeTimbreAtt", "CV > TIMBRE (strike)", true, 0.0f },
  { hw::kPotDampingAtt, "dampingAtt", "CV > DAMPING", true, 0.0f },
  { hw::kPotGeometryAtt, "geometryAtt", "CV > GEOMETRY", true, 0.0f },
  { hw::kPotPositionAtt, "positionAtt", "CV > POSITION", true, 0.0f },
  { hw::kPotBrightnessAtt, "brightnessAtt", "CV > BRIGHTNESS", true, 0.0f },
  { hw::kPotSpaceAtt, "spaceAtt", "CV > SPACE", true, 0.0f },
} };

const std::array<CvInfo, hw::kNumCvs - 1> kCvs = { {
  { hw::kCvFm, "fm", "FM" },
  { hw::kCvStrength, "strength", "STRENGTH" },
  { hw::kCvBowTimbre, "bowTimbre", "TIMBRE (bow)" },
  { hw::kCvFlow, "flow", "FLOW" },
  { hw::kCvBlowTimbre, "blowTimbre", "TIMBRE (blow)" },
  { hw::kCvMallet, "mallet", "MALLET" },
  { hw::kCvStrikeTimbre, "strikeTimbre", "TIMBRE (strike)" },
  { hw::kCvDamping, "damping", "DAMPING" },
  { hw::kCvGeometry, "geometry", "GEOMETRY" },
  { hw::kCvPosition, "position", "POSITION" },
  { hw::kCvBrightness, "brightness", "BRIGHTNESS" },
  { hw::kCvSpace, "space", "SPACE" },
} };

namespace {

using APF = juce::AudioParameterFloat;
using API = juce::AudioParameterInt;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;

// Fréquence d'échantillonnage du firmware officiel (32 kHz nominal).
constexpr double kNominalModuleRate = 32001.201923;
constexpr int kBlock = StereoResampler::kBlock;

juce::ParameterID pid(const juce::String& id) { return juce::ParameterID { id, 1 }; }

std::unique_ptr<APF> floatParam(const juce::String& id, const juce::String& name, float lo, float hi,
                                float def, std::function<juce::String(float)> toText) {
  auto attributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
      [toText](float v, int) { return toText(v); });
  return std::make_unique<APF>(pid(id), name, juce::NormalisableRange<float> { lo, hi }, def, attributes);
}

juce::String percent(float v) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; }
juce::String signedPercent(float v) {
  const int p = juce::roundToInt(v * 100.0f);
  return (p > 0 ? "+" : "") + juce::String(p) + " %";
}
juce::String volts(float v) { return (v > 0.0049f ? "+" : "") + juce::String(v, 2) + " V"; }

const juce::StringArray kCvSources {
  juce::String::fromUTF8("Rien"),
  juce::String::fromUTF8("Molette"),
  juce::String::fromUTF8("Vélocité"),
  juce::String::fromUTF8("Aftertouch"),
  juce::String::fromUTF8("Automation")
};

int16_t toCodec(float x) {
  return static_cast<int16_t>(std::lround(juce::jlimit(-1.0f, 1.0f, x) * 32767.0f));
}

}  // namespace

juce::String cvSourceId(const CvInfo& cv) { return juce::String("cv_") + cv.id + "_source"; }
juce::String cvValueId(const CvInfo& cv) { return juce::String("cv_") + cv.id; }

juce::AudioProcessorValueTreeState::ParameterLayout LmentsAudioProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  // --- Les 27 potards du module ----------------------------------------------------
  for (const auto& pot : kPots) {
    if (pot.pot == hw::kPotCoarse) {
      layout.add(floatParam(pot.id, pot.label, 0.0f, 1.0f, pot.defaultValue, [](float v) {
        const int st = juce::roundToInt(v * 60.0f) - 17;
        return (st > 0 ? "+" : "") + juce::String(st) + " demi-tons";
      }));
    } else if (pot.bipolar) {
      layout.add(floatParam(pot.id, pot.label, -1.0f, 1.0f, pot.defaultValue, signedPercent));
    } else {
      layout.add(floatParam(pot.id, pot.label, 0.0f, 1.0f, pot.defaultValue, percent));
    }
  }
  layout.add(std::make_unique<APB>(pid(ids::play), "PLAY", false));

  // --- Interface MIDI -> CV : ce qui est branché dans les prises ------------------------
  layout.add(std::make_unique<APC>(pid(ids::gateSource), "GATE source", juce::StringArray { "Notes MIDI", "Rien" }, 0));
  for (const auto& cv : kCvs) {
    layout.add(std::make_unique<APC>(pid(cvSourceId(cv)), juce::String(cv.label) + " source", kCvSources, 0));
    layout.add(floatParam(cvValueId(cv), juce::String(cv.label) + " CV", -5.0f, 5.0f, 0.0f, volts));
  }
  layout.add(std::make_unique<API>(pid(ids::bendRange), "Bend range", 0, 12, 2,
                                   juce::AudioParameterIntAttributes().withStringFromValueFunction(
                                       [](int v, int) { return juce::String::fromUTF8("\xc2\xb1") + juce::String(v) + " demi-tons"; })));
  return layout;
}

LmentsAudioProcessor::LmentsAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("EXT IN", juce::AudioChannelSet::stereo(), true)
                         .withOutput("OUT", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "LMENTS", createParameterLayout()) {
  for (size_t i = 0; i < kPots.size(); ++i) potParams_[i] = apvts.getRawParameterValue(kPots[i].id);
  for (size_t i = 0; i < kCvs.size(); ++i) {
    cvSourceParams_[i] = apvts.getRawParameterValue(cvSourceId(kCvs[i]));
    cvValueParams_[i] = apvts.getRawParameterValue(cvValueId(kCvs[i]));
  }
  playParam_ = apvts.getRawParameterValue(ids::play);
  gateSourceParam_ = apvts.getRawParameterValue(ids::gateSource);
  bendRangeParam_ = apvts.getRawParameterValue(ids::bendRange);
  for (auto& m : cvMeters_) m.store(0.0f);

  // Chaque module a son propre numéro de série (le firmware s'en sert pour
  // donner à chaque exemplaire un caractère légèrement différent).
  auto& random = juce::Random::getSystemRandom();
  for (auto& word : uid_) word = static_cast<uint32_t>(random.nextInt());

  moduleSampleRate_ = kNominalModuleRate;
  prepareResamplers();
}

// --- Firmware ------------------------------------------------------------------------------

juce::String LmentsAudioProcessor::firmwareName() const {
  const juce::SpinLock::ScopedLockType lock(const_cast<juce::SpinLock&>(moduleLock_));
  return firmware_.name;
}

juce::String LmentsAudioProcessor::loadFirmwareFile(const juce::File& file) {
  juce::MemoryBlock bytes;
  if (!file.loadFileAsData(bytes)) return juce::String::fromUTF8("Impossible de lire ce fichier.");
  emu::FirmwareImage image;
  std::string error;
  if (!emu::readFirmwareFile(static_cast<const uint8_t*>(bytes.getData()), bytes.getSize(), image, error)) {
    return juce::String::fromUTF8(error.c_str());
  }
  // Comme une mise à jour du module : les réglages enregistrés restent en place.
  std::vector<uint8_t> settings;
  const bool keep = hasFirmware_.load();
  if (keep) settings = currentSettings();
  {
    const juce::SpinLock::ScopedLockType lock(moduleLock_);
    firmware_.name = file.getFileNameWithoutExtension();
    firmware_.image = std::move(image.data);
  }
  boot(keep ? &settings : nullptr);
  return {};
}

std::vector<uint8_t> LmentsAudioProcessor::currentSettings() {
  const juce::SpinLock::ScopedLockType lock(moduleLock_);
  const uint8_t* p = machine_.flashImage() + (emu::ElementsMachine::kSettingsBase - emu::CortexM4::kFlashBase);
  return std::vector<uint8_t>(p, p + emu::ElementsMachine::kSettingsSize);
}

// Met le module sous tension avec le firmware courant. `settings` : contenu de la
// zone des réglages (nul = module neuf, le firmware écrit ses réglages d'usine).
void LmentsAudioProcessor::boot(const std::vector<uint8_t>* settings) {
  const juce::SpinLock::ScopedLockType lock(moduleLock_);
  if (firmware_.image.empty() || !machine_.loadApplication(firmware_.image.data(), firmware_.image.size())) {
    hasFirmware_.store(false);
    return;
  }
  if (settings != nullptr && settings->size() == emu::ElementsMachine::kSettingsSize) {
    std::vector<uint8_t> flash(machine_.flashImage(), machine_.flashImage() + emu::ElementsMachine::kFlashSize);
    std::copy(settings->begin(), settings->end(),
              flash.begin() + (emu::ElementsMachine::kSettingsBase - emu::CortexM4::kFlashBase));
    machine_.restoreFlash(flash.data(), flash.size());
  } else {
    machine_.eraseSettings();
  }
  machine_.setUniqueId(uid_);
  updateControls();
  machine_.powerOn();
  loadCycles_ = machine_.cycles();
  loadIdle_ = machine_.cpu().handlerCycles();
  moduleCrashed_.store(false);
  hasFirmware_.store(true);
}

// --- Audio ---------------------------------------------------------------------------------

void LmentsAudioProcessor::prepareResamplers() {
  toHost_.prepare(moduleSampleRate_, hostSampleRate_);
  toModule_.prepare(hostSampleRate_, moduleSampleRate_);
  // Les entrées audio sont retardées juste assez pour que le module ne demande
  // jamais un échantillon que l'hôte n'a pas encore fourni.
  const double ratio = hostSampleRate_ / moduleSampleRate_;
  const int lag = static_cast<int>(std::ceil((toHost_.lookaheadInInputSamples() + kBlock) * ratio)) +
                  toModule_.lookaheadInInputSamples() + 8;
  inputRingL_.assign(1 << 16, 0.0f);
  inputRingR_.assign(1 << 16, 0.0f);
  inputWritten_ = lag;
  inputRead_ = 0;
}

void LmentsAudioProcessor::prepareToPlay(double sampleRate, int) {
  const juce::SpinLock::ScopedLockType lock(moduleLock_);
  hostSampleRate_ = sampleRate;
  prepareResamplers();
  setLatencySamples(toHost_.latencyInOutputSamples());
  hostSeconds_ = audioSeconds_ = 0.0;
}

bool LmentsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
  const auto in = layouts.getMainInputChannelSet();
  return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

float LmentsAudioProcessor::sourceVolts(CvSource source, float automationVolts) const {
  switch (source) {
    case CvSource::kModWheel: return 5.0f * modWheel_;
    case CvSource::kVelocity: return 5.0f * velocity_;
    case CvSource::kAftertouch: return 5.0f * aftertouch_;
    case CvSource::kAutomation: return automationVolts;
    case CvSource::kNone:
    default: return 0.0f;
  }
}

// Position des potards et tensions des prises -> ce que lisent les convertisseurs.
void LmentsAudioProcessor::updateControls() {
  auto& c = machine_.controls;
  for (size_t i = 0; i < kPots.size(); ++i) {
    const float v = potParams_[i]->load();
    c.pot[hw::kPotAddress[kPots[i].pot]] = hw::potCode(kPots[i].bipolar ? 0.5f * (v + 1.0f) : v);
  }
  // Interface MIDI -> CV : 0 V = note 36, 1 V par octave.
  const float voct = static_cast<float>(currentNote_ - 36) / 12.0f + bend_ * bendRangeParam_->load() / 12.0f;
  c.cv[hw::kCvChannel[hw::kCvVOct]] = hw::cvCode(hw::kCvVOct, voct);
  cvMeters_[hw::kCvVOct].store(voct);
  for (size_t i = 0; i < kCvs.size(); ++i) {
    const auto source = static_cast<CvSource>(juce::roundToInt(cvSourceParams_[i]->load()));
    const float v = sourceVolts(source, cvValueParams_[i]->load());
    c.cv[hw::kCvChannel[kCvs[i].cv]] = hw::cvCode(kCvs[i].cv, v);
    cvMeters_[static_cast<size_t>(kCvs[i].cv)].store(v);
  }
  const bool gateFromNotes = juce::roundToInt(gateSourceParam_->load()) == 0;
  bool gate = gateFromNotes && numHeld_ > 0;
  if (gateLowBlocks_ > 0) { gate = false; --gateLowBlocks_; }
  c.gate = gate;
  c.button = playParam_->load() > 0.5f;
  gateMeter_.store(gate);
}

void LmentsAudioProcessor::handleMidi(const juce::MidiMessage& m) {
  auto remove = [this](int note) {
    int j = 0;
    for (int i = 0; i < numHeld_; ++i) {
      if (heldNotes_[static_cast<size_t>(i)] != note) heldNotes_[static_cast<size_t>(j++)] = heldNotes_[static_cast<size_t>(i)];
    }
    numHeld_ = j;
  };
  if (m.isNoteOn()) {
    const int note = m.getNoteNumber();
    const bool gateWasHigh = numHeld_ > 0;
    remove(note);
    if (numHeld_ == static_cast<int>(heldNotes_.size())) {
      std::rotate(heldNotes_.begin(), heldNotes_.begin() + 1, heldNotes_.end());
      --numHeld_;
    }
    heldNotes_[static_cast<size_t>(numHeld_++)] = note;
    currentNote_ = note;
    velocity_ = m.getFloatVelocity();
    // Note liée : GATE retombe un instant (le firmware la lit une fois par bloc
    // de 16 échantillons) pour que la nouvelle note soit attaquée.
    if (gateWasHigh) gateLowBlocks_ = 3;
  } else if (m.isNoteOff()) {
    remove(m.getNoteNumber());
    if (numHeld_ > 0) currentNote_ = heldNotes_[static_cast<size_t>(numHeld_ - 1)];
  } else if (m.isPitchWheel()) {
    bend_ = static_cast<float>(m.getPitchWheelValue() - 8192) / 8192.0f;
  } else if (m.isControllerOfType(1)) {
    modWheel_ = static_cast<float>(m.getControllerValue()) / 127.0f;
  } else if (m.isChannelPressure()) {
    aftertouch_ = static_cast<float>(m.getChannelPressureValue()) / 127.0f;
  } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
    numHeld_ = 0;
  }
}

// Fournit au convertisseur d'entrée kBlock trames à la fréquence de l'hôte.
void LmentsAudioProcessor::pullHostInput(float* left, float* right) {
  const int64_t mask = static_cast<int64_t>(inputRingL_.size()) - 1;
  for (int i = 0; i < kBlock; ++i) {
    if (inputRead_ < inputWritten_) {
      left[i] = inputRingL_[static_cast<size_t>(inputRead_ & mask)];
      right[i] = inputRingR_[static_cast<size_t>(inputRead_ & mask)];
      ++inputRead_;
    } else {
      left[i] = right[i] = 0.0f;
    }
  }
}

// Fait tourner le module pendant un bloc de 16 échantillons (la taille des blocs
// du firmware) : potards et prises sont relus au même rythme que sur le module.
void LmentsAudioProcessor::pullModuleBlock(float* left, float* right) {
  if (!hasFirmware_.load()) {
    std::fill(left, left + kBlock, 0.0f);
    std::fill(right, right + kBlock, 0.0f);
    return;
  }
  updateControls();
  int16_t inL[kBlock] = {}, inR[kBlock] = {}, outL[kBlock], outR[kBlock];
  if (inputConnected_) {
    for (int i = 0; i < kBlock; ++i) {
      float red, turquoise;
      toModule_.process([this](float* l, float* r) { pullHostInput(l, r); }, red, turquoise);
      // Entrée 1 de l'hôte = EXT IN rouge (côté BLOW) = voie droite du codec ;
      // entrée 2 = EXT IN turquoise (côté STRIKE) = voie gauche du codec.
      inR[i] = toCodec(red);
      inL[i] = toCodec(turquoise);
    }
  }
  machine_.run(inL, inR, outL, outR, kBlock);
  for (int i = 0; i < kBlock; ++i) {
    left[i] = static_cast<float>(outL[i]) * (1.0f / 32768.0f);
    right[i] = static_cast<float>(outR[i]) * (1.0f / 32768.0f);
  }
}

void LmentsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  const double startMs = juce::Time::getMillisecondCounterHiRes();
  const int numSamples = buffer.getNumSamples();
  const int numInputs = getTotalNumInputChannels(), numOutputs = getTotalNumOutputChannels();
  if (numSamples == 0 || numOutputs == 0) return;

  const juce::SpinLock::ScopedLockType lock(moduleLock_);

  // Entrées audio : mises en attente avant que le tampon serve aux sorties.
  inputConnected_ = numInputs > 0;
  float inPeak = 0.0f;
  if (inputConnected_) {
    const float* l = buffer.getReadPointer(0);
    const float* r = buffer.getReadPointer(numInputs > 1 ? 1 : 0);
    const int64_t mask = static_cast<int64_t>(inputRingL_.size()) - 1;
    for (int n = 0; n < numSamples; ++n) {
      inputRingL_[static_cast<size_t>((inputWritten_ + n) & mask)] = l[n];
      inputRingR_[static_cast<size_t>((inputWritten_ + n) & mask)] = r[n];
      inPeak = std::max(inPeak, std::max(std::abs(l[n]), std::abs(r[n])));
    }
    inputWritten_ += numSamples;
  }
  inPeak_.store(std::max(inPeak, inPeak_.load() * 0.8f));

  // Un firmware peut régler une autre fréquence d'échantillonnage que 32 kHz.
  const double rate = machine_.sampleRate();
  if (rate >= 4000.0 && rate <= 400000.0 && std::abs(rate - moduleSampleRate_) > 0.5) {
    moduleSampleRate_ = rate;
    prepareResamplers();
  }

  float* outL = buffer.getWritePointer(0);
  float* outR = buffer.getWritePointer(numOutputs > 1 ? 1 : 0);
  auto render = [&](int start, int end) {
    for (int n = start; n < end; ++n) {
      float l, r;
      toHost_.process([this](float* a, float* b) { pullModuleBlock(a, b); }, l, r);
      outL[n] = l;
      outR[n] = r;
    }
  };
  int position = 0;
  for (const auto metadata : midi) {
    const int eventPosition = juce::jlimit(0, numSamples, metadata.samplePosition);
    render(position, eventPosition);
    position = eventPosition;
    handleMidi(metadata.getMessage());
  }
  render(position, numSamples);
  for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, numSamples);

  // Voyants et mesures pour l'interface.
  if (hasFirmware_.load()) {
    const auto leds = machine_.leds();
    ledPlay_.store(leds.gate);
    ledExciter_.store(leds.exciter);
    ledResonator_.store(leds.resonator);
    moduleCrashed_.store(machine_.crashed());
    moduleOverruns_.store(machine_.overruns());
    const uint64_t cycles = machine_.cycles();
    if (cycles - loadCycles_ > 42000000) {   // quatre fois par seconde
      // Le firmware calcule le son dans ses interruptions ; sa boucle principale ne
      // fait que surveiller le bouton. La charge est donc la part du temps passée
      // en interruption. Estimation : les durées des instructions viennent de la
      // documentation du processeur (voir CortexM4.cpp), pas d'une mesure.
      const uint64_t busy = machine_.cpu().handlerCycles();
      moduleLoad_.store(static_cast<float>(busy - loadIdle_) / static_cast<float>(cycles - loadCycles_));
      loadCycles_ = cycles;
      loadIdle_ = busy;
    }
  } else {
    ledPlay_.store(false); ledExciter_.store(0.0f); ledResonator_.store(0.0f);
    moduleLoad_.store(0.0f);
  }
  outPeak_.store(std::max(buffer.getMagnitude(0, numSamples), outPeak_.load() * 0.8f));

  // Part d'un cœur de l'ordinateur utilisée par l'émulation.
  hostSeconds_ += (juce::Time::getMillisecondCounterHiRes() - startMs) * 0.001;
  audioSeconds_ += static_cast<double>(numSamples) / hostSampleRate_;
  if (audioSeconds_ >= 0.25) {
    hostLoad_.store(static_cast<float>(hostSeconds_ / audioSeconds_));
    hostSeconds_ = audioSeconds_ = 0.0;
  }
}

LmentsAudioProcessor::Leds LmentsAudioProcessor::leds() const {
  Leds l;
  l.play = ledPlay_.load();
  l.exciter = ledExciter_.load();
  l.resonator = ledResonator_.load();
  return l;
}

juce::AudioProcessorEditor* LmentsAudioProcessor::createEditor() {
  return new LmentsAudioProcessorEditor(*this);
}

// --- État : paramètres + firmware + mémoire du module ---------------------------------------
//
// Le firmware chargé voyage avec le projet. Comme le vrai module, LMenTS garde ses
// réglages (calibration, modèle de résonateur, mode caché) dans sa flash : cette
// zone est enregistrée aussi. À la réouverture, le module est remis sous tension
// avec ce contenu, exactement comme un module qu'on rallume.

void LmentsAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
  auto state = apvts.copyState();
  auto base64 = [](const std::vector<uint8_t>& bytes) {
    return juce::MemoryBlock(bytes.data(), bytes.size()).toBase64Encoding();
  };
  juce::ValueTree moduleTree("MODULE");
  moduleTree.setProperty("uid", juce::String::toHexString(static_cast<juce::int64>(uid_[0])) + ":" +
                                    juce::String::toHexString(static_cast<juce::int64>(uid_[1])) + ":" +
                                    juce::String::toHexString(static_cast<juce::int64>(uid_[2])),
                         nullptr);
  if (hasFirmware_.load()) {
    std::vector<uint8_t> image;
    {
      const juce::SpinLock::ScopedLockType lock(moduleLock_);
      moduleTree.setProperty("firmwareName", firmware_.name, nullptr);
      image = firmware_.image;
    }
    moduleTree.setProperty("firmwareImage", base64(image), nullptr);
    moduleTree.setProperty("settings", base64(currentSettings()), nullptr);
  }
  state.appendChild(moduleTree, nullptr);
  if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void LmentsAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
  auto xml = getXmlFromBinary(data, sizeInBytes);
  if (xml == nullptr || !xml->hasTagName(apvts.state.getType())) return;

  auto tree = juce::ValueTree::fromXml(*xml);
  auto moduleTree = tree.getChildWithName("MODULE");
  if (moduleTree.isValid()) tree.removeChild(moduleTree, nullptr);
  apvts.replaceState(tree);
  if (!moduleTree.isValid()) return;

  auto bytes = [](const juce::var& v) {
    juce::MemoryBlock block;
    block.fromBase64Encoding(v.toString());
    const auto* p = static_cast<const uint8_t*>(block.getData());
    return std::vector<uint8_t>(p, p + block.getSize());
  };
  const auto uidParts = juce::StringArray::fromTokens(moduleTree["uid"].toString(), ":", "");
  if (uidParts.size() == 3) {
    for (int i = 0; i < 3; ++i) uid_[i] = static_cast<uint32_t>(uidParts[i].getHexValue64());
  }
  if (!moduleTree.hasProperty("firmwareImage")) return;
  emu::FirmwareImage image;
  std::string error;
  const auto raw = bytes(moduleTree["firmwareImage"]);
  if (!emu::readFirmwareFile(raw.data(), raw.size(), image, error)) return;
  {
    const juce::SpinLock::ScopedLockType lock(moduleLock_);
    firmware_.name = moduleTree["firmwareName"].toString();
    firmware_.image = std::move(image.data);
  }
  const std::vector<uint8_t> settings = bytes(moduleTree["settings"]);
  boot(&settings);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new LmentsAudioProcessor();
}
