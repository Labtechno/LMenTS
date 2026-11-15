// LMenTS — interface.
//
// À gauche : la façade du module, avec la disposition et les couleurs de
// l'original (sans logo ni marque). À droite : le firmware chargé, les jauges, et
// le panneau « Interface MIDI -> CV », qui dit ce qui est branché dans les prises.
//
// Interface provisoire de l'étape 2 : elle sera retravaillée sur maquettes.

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
  void loadFirmware(const juce::File& file);
  juce::Point<float> panelPoint(float x, float y) const;
  void setPlay(bool down);

  LmentsAudioProcessor& processor_;
  KnobLook knobLook_;

  std::array<juce::Slider, lments::hw::kNumPots> knobs_;
  std::array<std::unique_ptr<SliderAttachment>, lments::hw::kNumPots> knobAttachments_;

  juce::TextButton loadButton_;
  std::unique_ptr<juce::FileChooser> chooser_;
  juce::String message_;

  juce::ComboBox gateSource_;
  std::unique_ptr<ComboAttachment> gateSourceAttachment_;
  juce::Slider bendRange_;
  std::unique_ptr<SliderAttachment> bendRangeAttachment_;
  std::array<juce::ComboBox, lments::hw::kNumCvs - 1> cvSources_;
  std::array<std::unique_ptr<ComboAttachment>, lments::hw::kNumCvs - 1> cvSourceAttachments_;
  std::array<juce::Slider, lments::hw::kNumCvs - 1> cvValues_;
  std::array<std::unique_ptr<SliderAttachment>, lments::hw::kNumCvs - 1> cvValueAttachments_;

  bool playDown_ = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LmentsAudioProcessorEditor)
};
