// LMenTS — processeur audio.
//
// Le plugin héberge un module Elements émulé (Source/emu) : une puce STM32F405
// virtuelle exécute l'image binaire du firmware. Le plugin ne fait que quatre choses :
//   1. transformer la position des potards et les tensions des prises en codes de
//      convertisseur (étage analogique virtuel, Source/hw/Panel.h) ;
//   2. jouer le rôle d'une interface MIDI -> CV branchée sur les prises ;
//   3. convertir le son entre la fréquence du module (32 kHz) et celle de l'hôte ;
//   4. charger un fichier de firmware et le garder dans le projet.

#pragma once

#include <array>
#include <atomic>
#include <map>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ElementsMachine.h"
#include "Panel.h"
#include "Resampler.h"

// Sources possibles pour une prise CV.
enum class CvSource : int { kNone = 0, kModWheel, kVelocity, kAftertouch, kAutomation };

// Description d'un potard de la façade.
struct PotInfo {
  lments::hw::Pot pot;
  const char* id;        // identifiant du paramètre
  const char* label;     // nom sur la façade
  bool bipolar;          // atténuverseur ou réglage centré : -1..+1, zéro au milieu
  float defaultValue;
};
// Description d'une prise CV pilotable (toutes sauf V/OCT, pilotée par les notes).
struct CvInfo {
  lments::hw::Cv cv;
  const char* id;
  const char* label;
};

extern const std::array<PotInfo, lments::hw::kNumPots> kPots;
extern const std::array<CvInfo, lments::hw::kNumCvs - 1> kCvs;
juce::String cvSourceId(const CvInfo& cv);   // paramètre « source » d'une prise CV
juce::String cvValueId(const CvInfo& cv);    // paramètre « tension » d'une prise CV

namespace ids {
inline constexpr const char* play = "play";
inline constexpr const char* gateSource = "gateSource";
inline constexpr const char* bendRange = "bendRange";
}  // namespace ids

class LmentsAudioProcessor : public juce::AudioProcessor {
 public:
  LmentsAudioProcessor();
  ~LmentsAudioProcessor() override = default;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using AudioProcessor::processBlock;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 10.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

  // --- Pour l'interface ----------------------------------------------------------
  juce::AudioProcessorValueTreeState apvts;

  // Charge un fichier .bin ou .wav (à appeler depuis le fil de l'interface).
  // Renvoie un message d'erreur, ou une chaîne vide.
  //
  // Chaque firmware a sa propre mémoire (ses réglages : calibration, modèle de
  // résonateur, mode caché...), reconnue par l'empreinte du fichier. Revenir à un
  // firmware déjà utilisé dans ce projet lui rend sa mémoire ; un firmware nouveau
  // démarre comme un module neuf. Toutes ces mémoires sont sauvées dans le projet.
  juce::String loadFirmwareFile(const juce::File& file);
  bool hasFirmware() const { return hasFirmware_.load(); }
  juce::String firmwareName() const;
  juce::String firmwareId() const;
  // Efface la mémoire du firmware chargé et le redémarre : réglages d'usine.
  void resetFirmwareMemory();
  // Vrai si la mémoire du firmware chargé vient du projet (sinon : module neuf).
  bool memoryRestored() const { return memoryRestored_.load(); }
  int numMemories() const;

  // Problème constaté au démarrage du firmware.
  enum class Problem { kNone, kNoSound, kCrashed };
  Problem problem() const { return static_cast<Problem>(problem_.load()); }

  // Voyants du module.
  struct Leds { bool play = false; float exciter = 0.0f, resonator = 0.0f; };
  Leds leds() const;

  // Occupation du processeur du module (0..1) : moyenne et pointe sur les blocs de
  // son du dernier quart de seconde ; part d'un cœur de cet ordinateur utilisée
  // par l'émulation (0..1 et plus) ; blocs audio que le firmware n'a pas calculés
  // à temps (surcharge : le module craque), au total et depuis deux secondes ;
  // arrêt éventuel du processeur émulé.
  float moduleLoad() const { return moduleLoad_.load(); }
  float moduleLoadPeak() const { return moduleLoadPeak_.load(); }
  float hostLoad() const { return hostLoad_.load(); }
  uint32_t moduleOverruns() const { return moduleOverruns_.load(); }
  bool moduleOverloaded() const { return moduleOverloaded_.load(); }
  bool moduleCrashed() const { return moduleCrashed_.load(); }

  // Tension présente sur chaque prise CV (pour l'affichage), et état de GATE.
  float cvVolts(int cv) const { return cvMeters_[static_cast<size_t>(cv)].load(); }
  bool gateHigh() const { return gateMeter_.load(); }
  float outputPeak() const { return outPeak_.load(); }
  float inputPeak() const { return inPeak_.load(); }

 private:
  struct Firmware {
    juce::String name;
    std::string id;               // empreinte de l'image (FirmwareFile.h)
    std::vector<uint8_t> image;
  };
  // Mémoire d'un firmware : contenu des secteurs de flash qui ne contiennent pas
  // son programme (seuls les secteurs non vierges sont gardés).
  using Memory = std::map<int, std::vector<uint8_t>>;
  void boot(const Memory* memory);
  Memory captureMemory();               // à appeler avec moduleLock_ tenu
  void handleMidi(const juce::MidiMessage& message);
  void updateControls();
  void pullModuleBlock(float* left, float* right);
  void pullHostInput(float* left, float* right);
  void prepareResamplers();
  float sourceVolts(CvSource source, float automationVolts) const;

  std::array<std::atomic<float>*, lments::hw::kNumPots> potParams_ {};
  std::array<std::atomic<float>*, lments::hw::kNumCvs - 1> cvSourceParams_ {}, cvValueParams_ {};
  std::atomic<float>* playParam_ = nullptr;
  std::atomic<float>* gateSourceParam_ = nullptr;
  std::atomic<float>* bendRangeParam_ = nullptr;

  // Module émulé et son firmware.
  lments::emu::ElementsMachine machine_;
  Firmware firmware_;
  std::map<std::string, Memory> otherMemories_;   // les firmwares utilisés avant dans ce projet
  std::atomic<bool> hasFirmware_ { false }, memoryRestored_ { false };
  std::atomic<int> problem_ { 0 };
  uint64_t bootCycles_ = 0;
  uint32_t uid_[3] = { 0, 0, 0 };
  juce::SpinLock moduleLock_;

  double hostSampleRate_ = 48000.0, moduleSampleRate_ = 0.0;
  lments::StereoResampler toHost_, toModule_;
  // Entrées audio de l'hôte, en attente d'être converties pour le module.
  std::vector<float> inputRingL_, inputRingR_;
  int64_t inputWritten_ = 0, inputRead_ = 0;
  bool inputConnected_ = false;

  // Interface MIDI -> CV.
  std::array<int, 16> heldNotes_ {};
  int numHeld_ = 0;
  int currentNote_ = 36;     // 0 V au repos
  float velocity_ = 0.0f, modWheel_ = 0.0f, aftertouch_ = 0.0f, bend_ = 0.0f;
  int gateLowBlocks_ = 0;    // GATE retombe brièvement entre deux notes liées

  std::atomic<bool> ledPlay_ { false };
  std::atomic<float> ledExciter_ { 0.0f }, ledResonator_ { 0.0f };
  std::atomic<float> moduleLoad_ { 0.0f }, moduleLoadPeak_ { 0.0f }, hostLoad_ { 0.0f };
  std::atomic<uint32_t> moduleOverruns_ { 0 };
  std::atomic<bool> moduleOverloaded_ { false };
  float blockPeak_ = 0.0f;            // pointe depuis le dernier relevé
  uint32_t overrunsSeen_ = 0;
  double overloadHold_ = 0.0;         // secondes pendant lesquelles l'alerte reste affichée
  std::atomic<bool> moduleCrashed_ { false };
  uint64_t loadCycles_ = 0, loadIdle_ = 0;   // loadIdle_ : cycles passés en interruption au dernier relevé
  double hostSeconds_ = 0.0, audioSeconds_ = 0.0;
  std::array<std::atomic<float>, lments::hw::kNumCvs> cvMeters_ {};
  std::atomic<bool> gateMeter_ { false };
  std::atomic<float> outPeak_ { 0.0f }, inPeak_ { 0.0f };

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LmentsAudioProcessor)
};
