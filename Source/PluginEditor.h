// LMenTS — interface (maquette D, choisie à l'étape 6).
//
// À gauche : la façade du module, avec la disposition et les couleurs de
// l'original (sans logo ni marque). À droite : le firmware chargé, les jauges, et
// le panneau « Interface MIDI -> CV » sous forme de matrice : une ligne par prise
// CV, une colonne par source ; on clique sur un point pour brancher la source.

#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

// Dessin des potards : capuchon blanc, rouge ou turquoise, en trois tailles.
class KnobLook : public juce::LookAndFeel_V4 {
 public:
  void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position,
                        float startAngle, float endAngle, juce::Slider&) override;
  void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float position, float minPos,
                        float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
  juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(12.5f); }
  juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::FontOptions(12.5f); }
  juce::Font getLabelFont(juce::Label&) override { return juce::FontOptions(12.0f); }
  juce::Font getPopupMenuFont() override { return juce::FontOptions(13.0f); }
};

// Une ligne de la matrice : la prise, ses 5 sources (Rien, Molette, Vélocité,
// Aftertouch, Automation), la tension envoyée. En mode Automation, la tension se
// règle à la souris ; sinon, la barre montre la tension reçue, en direct.
class CvMatrixRow : public juce::Component {
 public:
  static constexpr int kNameWidth = 104, kColumnWidth = 58, kNumSources = 5;
  CvMatrixRow(LmentsAudioProcessor&, size_t index);
  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent&) override;
  void refresh();   // appelé 30 fois par seconde

 private:
  int sourceAt(juce::Point<int> position) const;
  juce::Rectangle<int> meterArea() const;

  LmentsAudioProcessor& processor_;
  size_t index_;
  int source_ = 0;
  float shownVolts_ = 0.0f;
  juce::ParameterAttachment sourceAttachment_;
  juce::Slider value_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> valueAttachment_;
};

class LmentsAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public juce::FileDragAndDropTarget,
                                   private juce::Timer {
 public:
  explicit LmentsAudioProcessorEditor(LmentsAudioProcessor&);
  ~LmentsAudioProcessorEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;

  bool isInterestedInFileDrag(const juce::StringArray& files) override;
  void filesDropped(const juce::StringArray& files, int x, int y) override;

 private:
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

  void timerCallback() override;
  void paintPanel(juce::Graphics&);
  void paintSide(juce::Graphics&);
  void chooseFirmware();
  void showRecent();
  void confirmReset();
  void loadFirmware(const juce::File& file);
  juce::Point<float> panelPoint(float x, float y) const;
  void setPlay(bool down);

  LmentsAudioProcessor& processor_;
  KnobLook knobLook_;

  std::array<juce::Slider, lments::hw::kNumPots> knobs_;
  std::array<std::unique_ptr<SliderAttachment>, lments::hw::kNumPots> knobAttachments_;

  juce::TextButton loadButton_, recentButton_, resetButton_;
  std::unique_ptr<juce::FileChooser> chooser_;
  juce::String message_;
  // Firmwares ouverts récemment (sur cet ordinateur, tous projets confondus).
  std::unique_ptr<juce::PropertiesFile> settings_;
  juce::RecentlyOpenedFilesList recent_;

  juce::ComboBox gateSource_;
  std::unique_ptr<ComboAttachment> gateSourceAttachment_;
  juce::Slider bendRange_;
  std::unique_ptr<SliderAttachment> bendRangeAttachment_;
  std::array<std::unique_ptr<CvMatrixRow>, lments::hw::kNumCvs - 1> cvRows_;

  bool playDown_ = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LmentsAudioProcessorEditor)
};
