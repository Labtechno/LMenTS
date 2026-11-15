// LMenTS — interface

#include "PluginEditor.h"

using namespace lments;

namespace {

// La façade est décrite dans un repère de 455 x 345 points (ses proportions),
// puis agrandie.
constexpr float kScale = 1.5f;
constexpr int kPanelWidth = 683, kPanelHeight = 518, kSideWidth = 560;
constexpr int kSideMargin = 14, kRowsTop = 180, kRowHeight = 26;

const juce::Colour kPanelColour(0xffdcdddf), kInk(0xff2c2c2e), kRed(0xffc0305a), kTurquoise(0xff2ba6a2);
const juce::Colour kSideColour(0xff26272b), kSideText(0xffd8d8dc), kSideDim(0xff9a9ba2);
const juce::Colour kTrack(0xff3a3b41), kControl(0xff34353b), kControlLine(0xff55565e), kRule(0xff33343a);
const juce::Colour kJackLit(0xffffc531), kDot(0xff6a6b72);
const char* const kSourceNames[] = { "Rien", "Molette", "V\xc3\xa9locit\xc3\xa9", "Aftertouch", "Automation" };

enum KnobKind { kWhite, kRedKnob, kTurquoiseKnob };
struct KnobPlace { float x, y, diameter; KnobKind kind; const char* label; };

// Position de chaque potard sur la façade (même ordre que hw::Pot).
const KnobPlace kKnobPlaces[hw::kNumPots] = {
  { 42, 47, 34, kWhite, "CONTOUR" }, { 105, 47, 34, kWhite, "BOW" }, { 168, 47, 34, kRedKnob, "BLOW" }, { 232, 47, 34, kTurquoiseKnob, "STRIKE" },
  { 293, 47, 34, kWhite, "COARSE" }, { 355, 47, 34, kWhite, "FINE" }, { 418, 47, 34, kWhite, "FM" },
  { 125, 125, 50, kRedKnob, "FLOW" }, { 210, 125, 50, kTurquoiseKnob, "MALLET" }, { 317, 125, 50, kWhite, "GEOMETRY" }, { 402, 125, 50, kWhite, "BRIGHTNESS" },
  { 105, 196, 34, kWhite, "TIMBRE" }, { 168, 196, 34, kRedKnob, "TIMBRE" }, { 232, 196, 34, kTurquoiseKnob, "TIMBRE" },
  { 293, 196, 34, kWhite, "DAMPING" }, { 355, 196, 34, kWhite, "POSITION" }, { 418, 196, 34, kWhite, "SPACE" },
  { 100, 250, 15, kWhite, "" }, { 134, 250, 15, kWhite, "" }, { 168, 250, 15, kWhite, "" }, { 203, 250, 15, kWhite, "" }, { 238, 250, 15, kWhite, "" },
  { 290, 250, 15, kWhite, "" }, { 324, 250, 15, kWhite, "" }, { 357, 250, 15, kWhite, "" }, { 392, 250, 15, kWhite, "" }, { 427, 250, 15, kWhite, "" },
};

// Prises de la façade : position, nom, couleur de l'étiquette, et ce qu'elles montrent.
struct JackPlace { float x, y; const char* label; int cv; };   // cv : indice hw::Cv, ou -1
const JackPlace kCvJacks[] = {
  { 22, 168, "V/OCT", hw::kCvVOct }, { 60, 168, "FM", hw::kCvFm }, { 60, 208, "STRENGTH", hw::kCvStrength },
  { 100, 291, "", hw::kCvBowTimbre }, { 134, 291, "", hw::kCvFlow }, { 168, 291, "", hw::kCvBlowTimbre },
  { 203, 291, "", hw::kCvMallet }, { 238, 291, "", hw::kCvStrikeTimbre },
  { 290, 291, "", hw::kCvDamping }, { 324, 291, "", hw::kCvGeometry }, { 357, 291, "", hw::kCvPosition },
  { 392, 291, "", hw::kCvBrightness }, { 427, 291, "", hw::kCvSpace },
};
constexpr float kPlayX = 42, kPlayY = 112, kPlayRadius = 13;

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

}  // namespace

void KnobLook::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                                float startAngle, float endAngle, juce::Slider& slider) {
  const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
  const float radius = 0.5f * std::min(bounds.getWidth(), bounds.getHeight()) - 1.0f;
  const auto centre = bounds.getCentre();
  const float angle = startAngle + position * (endAngle - startAngle);
  const int kind = static_cast<int>(slider.getProperties().getWithDefault("kind", 0));
  const juce::Colour cap = kind == kRedKnob ? kRed : (kind == kTurquoiseKnob ? kTurquoise : juce::Colour(0xfff3f3f1));
  const bool small = radius < 20.0f;

  // Jupe sombre, puis capuchon de couleur.
  g.setColour(juce::Colour(0x30000000));
  g.fillEllipse(centre.x - radius, centre.y - radius + 2.0f, 2.0f * radius, 2.0f * radius);
  g.setColour(juce::Colour(0xff222224));
  g.fillEllipse(centre.x - radius, centre.y - radius, 2.0f * radius, 2.0f * radius);
  const float inner = small ? radius * 0.62f : radius * 0.74f;
  g.setGradientFill(juce::ColourGradient(cap.brighter(0.25f), centre.x, centre.y - inner, cap.darker(0.12f), centre.x, centre.y + inner, false));
  g.fillEllipse(centre.x - inner, centre.y - inner, 2.0f * inner, 2.0f * inner);
  // Repère.
  juce::Path mark;
  const float w = std::max(2.0f, radius * 0.1f);
  mark.addRoundedRectangle(-0.5f * w, -radius * 0.98f, w, radius * (small ? 0.6f : 0.5f), 0.5f * w);
  g.setColour(small ? juce::Colour(0xfff3f3f1) : (kind == kWhite ? juce::Colour(0xff222224) : juce::Colour(0xfff3f3f1)));
  g.fillPath(mark, juce::AffineTransform::rotation(angle).translated(centre));
}

// Curseurs horizontaux (pitch bend, tension en mode Automation) : un rail fin,
// rempli depuis le zéro, et un repère clair.
void KnobLook::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float position, float,
                                float, juce::Slider::SliderStyle, juce::Slider& slider) {
  const float cy = static_cast<float>(y) + 0.5f * static_cast<float>(height);
  // Le rail occupe toute la largeur du curseur, comme les barres des autres prises.
  const bool bare = slider.getTextBoxPosition() == juce::Slider::NoTextBox;
  const float x0 = bare ? 0.0f : static_cast<float>(x), x1 = bare ? static_cast<float>(slider.getWidth()) : static_cast<float>(x + width);
  g.setColour(kTrack);
  g.fillRoundedRectangle(x0, cy - 2.0f, x1 - x0, 4.0f, 2.0f);
  const double lo = slider.getMinimum(), hi = slider.getMaximum();
  const float zero = lo < 0.0 && hi > 0.0 ? static_cast<float>(slider.getPositionOfValue(0.0)) : x0;
  g.setColour(slider.isEnabled() ? juce::Colour(0xffc9cad0) : kDot);
  g.fillRoundedRectangle(std::min(zero, position), cy - 2.0f, std::abs(position - zero), 4.0f, 2.0f);
  g.setColour(juce::Colour(0xfff0f0f2));
  g.fillRoundedRectangle(position - 2.0f, cy - 6.0f, 4.0f, 12.0f, 2.0f);
}

// --- Matrice Interface MIDI -> CV -----------------------------------------------------------

CvMatrixRow::CvMatrixRow(LmentsAudioProcessor& p, size_t index)
    : processor_(p), index_(index),
      sourceAttachment_(*p.apvts.getParameter(cvSourceId(kCvs[index])),
                        [this](float value) { source_ = juce::roundToInt(value); value_.setVisible(source_ == kNumSources - 1); repaint(); }) {
  value_.setSliderStyle(juce::Slider::LinearHorizontal);
  value_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  value_.setPopupDisplayEnabled(true, true, nullptr);
  value_.setTooltip(utf8("Tension envoy\xc3\xa9" "e dans la prise (param\xc3\xa8tre automatisable)"));
  addChildComponent(value_);
  valueAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, cvValueId(kCvs[index]), value_);
  sourceAttachment_.sendInitialUpdate();
}

juce::Rectangle<int> CvMatrixRow::meterArea() const {
  const int x = kNameWidth + kNumSources * kColumnWidth + 8;
  return { x, 0, getWidth() - x - 56, getHeight() };
}

void CvMatrixRow::resized() { value_.setBounds(meterArea()); }

int CvMatrixRow::sourceAt(juce::Point<int> position) const {
  const int column = (position.x - kNameWidth) / kColumnWidth;
  return position.x >= kNameWidth && column < kNumSources ? column : -1;
}

void CvMatrixRow::mouseDown(const juce::MouseEvent& e) {
  const int source = sourceAt(e.getPosition());
  if (source >= 0 && source != source_) sourceAttachment_.setValueAsCompleteGesture(static_cast<float>(source));
}

void CvMatrixRow::refresh() {
  const float volts = processor_.cvVolts(kCvs[index_].cv);
  if (std::abs(volts - shownVolts_) > 0.005f) { shownVolts_ = volts; repaint(); }
}

void CvMatrixRow::paint(juce::Graphics& g) {
  const float h = static_cast<float>(getHeight());
  g.setColour(kRule);
  g.fillRect(0, 0, getWidth(), 1);
  // Nom de la prise, avec un petit voyant quand une tension y arrive.
  const bool live = source_ != 0 && std::abs(shownVolts_) > 0.01f;
  g.setColour(live ? kJackLit : kDot);
  g.fillEllipse(0.0f, 0.5f * h - 3.0f, 6.0f, 6.0f);
  g.setColour(kSideText);
  g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
  g.drawText(kCvs[index_].label, 12, 0, kNameWidth - 12, getHeight(), juce::Justification::centredLeft, true);
  // Les 5 sources : un point plein pour celle qui est branchée.
  for (int j = 0; j < kNumSources; ++j) {
    const float cx = static_cast<float>(kNameWidth + j * kColumnWidth) + 0.5f * kColumnWidth, cy = 0.5f * h, r = 7.0f;
    if (j == source_) {
      g.setColour(j == 0 ? kDot : kJackLit);
      g.fillEllipse(cx - r, cy - r, 2 * r, 2 * r);
    } else {
      g.setColour(kDot);
      g.drawEllipse(cx - r + 0.75f, cy - r + 0.75f, 2 * r - 1.5f, 2 * r - 1.5f, 1.5f);
    }
  }
  // Tension : curseur en mode Automation (dessiné par le Slider), sinon barre en direct.
  const auto meter = meterArea().toFloat();
  if (source_ != kNumSources - 1) {
    const float cy = 0.5f * h, zero = meter.getCentreX();
    g.setColour(kTrack);
    g.fillRoundedRectangle(meter.getX(), cy - 2.0f, meter.getWidth(), 4.0f, 2.0f);
    const float end = zero + 0.5f * meter.getWidth() * juce::jlimit(-1.0f, 1.0f, shownVolts_ / 5.0f);
    g.setColour(juce::Colour(0xffc9cad0));
    g.fillRoundedRectangle(std::min(zero, end), cy - 2.0f, std::abs(end - zero), 4.0f, 2.0f);
  }
  g.setColour(source_ == 0 ? kDot : kSideText);
  g.setFont(juce::FontOptions(11.0f));
  g.drawText((shownVolts_ > 0.0049f ? "+" : "") + juce::String(shownVolts_, 2) + " V", getWidth() - 54, 0, 54, getHeight(),
             juce::Justification::centredRight, false);
}

LmentsAudioProcessorEditor::LmentsAudioProcessorEditor(LmentsAudioProcessor& p)
    : AudioProcessorEditor(&p), processor_(p) {
  // Couleurs des menus, boutons et cases du panneau de droite.
  knobLook_.setColour(juce::ComboBox::backgroundColourId, kControl);
  knobLook_.setColour(juce::ComboBox::outlineColourId, kControlLine);
  knobLook_.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe8e8ec));
  knobLook_.setColour(juce::ComboBox::arrowColourId, kSideText);
  knobLook_.setColour(juce::TextButton::buttonColourId, kControl);
  knobLook_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe8e8ec));
  knobLook_.setColour(juce::Slider::textBoxBackgroundColourId, kControl);
  knobLook_.setColour(juce::Slider::textBoxOutlineColourId, kControlLine);
  knobLook_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe8e8ec));
  setLookAndFeel(&knobLook_);
  for (size_t i = 0; i < knobs_.size(); ++i) {
    auto& knob = knobs_[i];
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.getProperties().set("kind", static_cast<int>(kKnobPlaces[i].kind));
    knob.setPopupDisplayEnabled(true, true, this);
    knob.setTooltip(kPots[i].label);
    addAndMakeVisible(knob);
    knobAttachments_[i] = std::make_unique<SliderAttachment>(processor_.apvts, kPots[i].id, knob);
  }

  loadButton_.setButtonText(utf8("Charger un firmware\xe2\x80\xa6"));
  loadButton_.onClick = [this] { chooseFirmware(); };
  addAndMakeVisible(loadButton_);
  recentButton_.setButtonText(utf8("R\xc3\xa9" "cents \xe2\x96\xbe"));
  recentButton_.onClick = [this] { showRecent(); };
  addAndMakeVisible(recentButton_);
  resetButton_.setButtonText(utf8("R\xc3\xa9glages d'usine\xe2\x80\xa6"));
  resetButton_.setTooltip(utf8("Efface la m\xc3\xa9moire de ce firmware (calibration, mod\xc3\xa8le de r\xc3\xa9sonateur, mode cach\xc3\xa9) et le red\xc3\xa9marre comme un module neuf."));
  resetButton_.onClick = [this] { confirmReset(); };
  addAndMakeVisible(resetButton_);

  juce::PropertiesFile::Options options;
  options.applicationName = "LMenTS-recents";
  options.filenameSuffix = "settings";
  options.osxLibrarySubFolder = "Application Support";
  options.folderName = "LMenTS";
  settings_ = std::make_unique<juce::PropertiesFile>(options);
  recent_.setMaxNumberOfItems(10);
  recent_.restoreFromString(settings_->getValue("recentFirmwares"));

  gateSource_.addItemList({ "Notes MIDI", "Rien" }, 1);
  addAndMakeVisible(gateSource_);
  gateSourceAttachment_ = std::make_unique<ComboAttachment>(processor_.apvts, ids::gateSource, gateSource_);
  bendRange_.setSliderStyle(juce::Slider::LinearHorizontal);
  bendRange_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 84, 20);
  addAndMakeVisible(bendRange_);
  bendRangeAttachment_ = std::make_unique<SliderAttachment>(processor_.apvts, ids::bendRange, bendRange_);

  for (size_t i = 0; i < kCvs.size(); ++i) {
    cvRows_[i] = std::make_unique<CvMatrixRow>(processor_, i);
    addAndMakeVisible(*cvRows_[i]);
  }

  setSize(kPanelWidth + kSideWidth, kPanelHeight);
  startTimerHz(30);
}

LmentsAudioProcessorEditor::~LmentsAudioProcessorEditor() {
  setPlay(false);
  setLookAndFeel(nullptr);
}

juce::Point<float> LmentsAudioProcessorEditor::panelPoint(float x, float y) const {
  return { x * kScale, y * kScale };
}

void LmentsAudioProcessorEditor::resized() {
  for (size_t i = 0; i < knobs_.size(); ++i) {
    const auto& place = kKnobPlaces[i];
    const float d = place.diameter * kScale;
    const auto c = panelPoint(place.x, place.y);
    knobs_[i].setBounds(juce::Rectangle<float>(c.x - 0.5f * d, c.y - 0.5f * d, d, d).toNearestInt());
  }
  const int left = kPanelWidth + kSideMargin, width = kSideWidth - 2 * kSideMargin;
  loadButton_.setBounds(left, 10, 150, 26);
  recentButton_.setBounds(left + 156, 10, 86, 26);
  resetButton_.setBounds(left + width - 130, 10, 130, 26);
  gateSource_.setBounds(left + 44, 134, 120, 22);
  bendRange_.setBounds(left + 260, 134, 220, 22);
  for (size_t i = 0; i < kCvs.size(); ++i)
    cvRows_[i]->setBounds(left, kRowsTop + kRowHeight * static_cast<int>(i), width, kRowHeight);
}

// --- Dessin --------------------------------------------------------------------------------

void LmentsAudioProcessorEditor::paintPanel(juce::Graphics& g) {
  const auto panel = juce::Rectangle<float>(0, 0, static_cast<float>(kPanelWidth), static_cast<float>(kPanelHeight));
  g.setGradientFill(juce::ColourGradient(kPanelColour.brighter(0.08f), 0, 0, kPanelColour.darker(0.06f), 0, panel.getBottom(), false));
  g.fillRect(panel);

  auto label = [&](const juce::String& text, float x, float y, float size, juce::Colour background, bool boxed) {
    g.setFont(juce::FontOptions(size * kScale, juce::Font::bold));
    const auto c = panelPoint(x, y);
    const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text) + 8.0f * kScale;
    const juce::Rectangle<float> box(c.x - 0.5f * w, c.y - 4.5f * kScale, w, 9.0f * kScale);
    if (boxed) { g.setColour(background); g.fillRoundedRectangle(box, 2.0f * kScale); }
    g.setColour(boxed ? juce::Colours::white : kInk);
    g.drawText(text, box, juce::Justification::centred, false);
  };

  // Titre (pas de logo ni de marque).
  g.setColour(kInk);
  g.setFont(juce::FontOptions(13.0f * kScale, juce::Font::plain));
  g.drawText("LMenTS", juce::Rectangle<float>(14 * kScale, 4 * kScale, 200 * kScale, 16 * kScale), juce::Justification::centredLeft, false);
  g.setFont(juce::FontOptions(6.5f * kScale, juce::Font::plain));
  g.drawText(utf8("synth\xc3\xa9tiseur modal"), juce::Rectangle<float>(250 * kScale, 6 * kScale, 195 * kScale, 12 * kScale), juce::Justification::centredRight, false);
  // Séparation excitateur / résonateur.
  g.setColour(kInk.withAlpha(0.35f));
  const float dash[] = { 2.0f * kScale, 2.0f * kScale };
  g.drawDashedLine(juce::Line<float>(panelPoint(262, 26), panelPoint(262, 304)), dash, 2, 1.0f);

  // Noms des potards.
  for (size_t i = 0; i < knobs_.size(); ++i) {
    const auto& place = kKnobPlaces[i];
    if (place.label[0] == 0) continue;
    const bool boxed = place.kind != kWhite;
    label(place.label, place.x, place.y + 0.5f * place.diameter + 9.0f, 5.6f, place.kind == kRedKnob ? kRed : kTurquoise, boxed);
  }
  // Graduation - / + des atténuverseurs.
  g.setColour(kInk);
  g.setFont(juce::FontOptions(5.5f * kScale, juce::Font::bold));
  for (size_t i = hw::kPotBowTimbreAtt; i < static_cast<size_t>(hw::kNumPots); ++i) {
    const auto c = panelPoint(kKnobPlaces[i].x, kKnobPlaces[i].y);
    g.drawText("-", juce::Rectangle<float>(c.x - 16 * kScale, c.y - 14 * kScale, 8 * kScale, 8 * kScale), juce::Justification::centred, false);
    g.drawText("+", juce::Rectangle<float>(c.x + 8 * kScale, c.y - 14 * kScale, 8 * kScale, 8 * kScale), juce::Justification::centred, false);
  }

  auto jack = [&](float x, float y, float level, juce::Colour ring) {
    const auto c = panelPoint(x, y);
    const float r = 7.5f * kScale;
    g.setColour(juce::Colour(0xff8f9094));
    g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour(ring);
    g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, 1.5f * kScale);
    g.setColour(juce::Colour(0xff17181a));
    g.fillEllipse(c.x - 0.55f * r, c.y - 0.55f * r, 1.1f * r, 1.1f * r);
    if (level > 0.0f) {   // quelque chose est branché et actif
      g.setColour(juce::Colour(0xffffc531).withAlpha(juce::jlimit(0.0f, 1.0f, level)));
      g.fillEllipse(c.x - 0.3f * r, c.y - 0.3f * r, 0.6f * r, 0.6f * r);
    }
  };
  const juce::Colour steel(0xff5d5e62);
  for (const auto& j : kCvJacks) {
    jack(j.x, j.y, std::abs(processor_.cvVolts(j.cv)) / 5.0f, steel);
    if (j.label[0]) label(j.label, j.x, j.y - 14.0f, 5.0f, kInk, false);
  }
  jack(22, 208, processor_.gateHigh() ? 1.0f : 0.0f, steel);
  label("GATE", 22, 194, 5.0f, kInk, false);
  jack(22, 250, processor_.inputPeak(), kRed);
  jack(60, 250, processor_.inputPeak(), kTurquoise);
  label("EXT IN", 22, 236, 5.0f, kRed, true);
  label("EXT IN", 60, 236, 5.0f, kTurquoise, true);
  g.setColour(juce::Colour(0xff4a4b55));
  g.fillRoundedRectangle(juce::Rectangle<float>(6 * kScale, 268 * kScale, 71 * kScale, 37 * kScale), 2.0f * kScale);
  jack(22, 291, processor_.outputPeak(), steel);
  jack(60, 291, processor_.outputPeak(), steel);
  g.setColour(juce::Colours::white);
  g.setFont(juce::FontOptions(5.0f * kScale, juce::Font::bold));
  g.drawText("OUT L", juce::Rectangle<float>(7 * kScale, 270 * kScale, 30 * kScale, 9 * kScale), juce::Justification::centred, false);
  g.drawText("OUT R", juce::Rectangle<float>(45 * kScale, 270 * kScale, 30 * kScale, 9 * kScale), juce::Justification::centred, false);

  // Bouton PLAY et voyants.
  const auto leds = processor_.leds();
  {
    const auto c = panelPoint(kPlayX, kPlayY);
    const float r = kPlayRadius * kScale;
    g.setColour(juce::Colour(0xff9a9b9f));
    g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    const float inner = r * (playDown_ ? 0.72f : 0.78f);
    g.setColour(leds.play ? juce::Colour(0xffffffff) : juce::Colour(0xffe9e9ea));
    g.fillEllipse(c.x - inner, c.y - inner, 2 * inner, 2 * inner);
    if (leds.play) {
      g.setColour(juce::Colour(0x55ffffff));
      g.fillEllipse(c.x - 1.25f * r, c.y - 1.25f * r, 2.5f * r, 2.5f * r);
    }
    label("PLAY", kPlayX + 30, kPlayY, 5.6f, kInk, false);
  }
  auto led = [&](float x, float y, float level, juce::Colour colour) {
    const auto c = panelPoint(x, y);
    const float r = 4.0f * kScale;
    g.setColour(juce::Colour(0xff55565a));
    g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour(colour.withAlpha(juce::jlimit(0.0f, 1.0f, 0.15f + 0.85f * level)));
    g.fillEllipse(c.x - 0.8f * r, c.y - 0.8f * r, 1.6f * r, 1.6f * r);
  };
  led(168, 150, leds.exciter, juce::Colour(0xff7be04a));
  led(358, 150, leds.resonator, juce::Colour(0xffffa126));

  if (!processor_.hasFirmware()) {
    g.setColour(juce::Colour(0xd0101114));
    g.fillRect(panel);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawFittedText(utf8("Aucun firmware charg\xc3\xa9"), panel.withTrimmedBottom(60).toNearestInt(), juce::Justification::centred, 1);
    g.setFont(juce::FontOptions(15.0f));
    g.drawFittedText(utf8("Clique sur \xc2\xab Charger un firmware\xe2\x80\xa6 \xc2\xbb, ou d\xc3\xa9pose ici le fichier de mise \xc3\xa0 jour\ndu module (par exemple elements_1.1.wav) ou un fichier .bin."),
                     panel.withTrimmedTop(70).toNearestInt(), juce::Justification::centred, 3);
  }
}

void LmentsAudioProcessorEditor::paintSide(juce::Graphics& g) {
  const int left = kPanelWidth + kSideMargin, width = kSideWidth - 2 * kSideMargin;
  g.setColour(kSideColour);
  g.fillRect(kPanelWidth, 0, kSideWidth, kPanelHeight);

  // Firmware chargé, entre les boutons « Récents » et « Réglages d'usine ».
  const int nameX = left + 250, nameWidth = width - 250 - 138;
  g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
  g.setColour(processor_.hasFirmware() ? kSideText : kSideDim);
  g.drawText(processor_.hasFirmware() ? processor_.firmwareName() : utf8("aucun firmware"), nameX, 10, nameWidth, 26,
             juce::Justification::centredLeft, true);

  // Ligne d'information : un problème en rouge, sinon l'empreinte du firmware.
  juce::String warning = message_;
  if (warning.isEmpty() && processor_.hasFirmware()) {
    if (processor_.problem() == LmentsAudioProcessor::Problem::kCrashed)
      warning = utf8("Le processeur du module s'est arr\xc3\xaat\xc3\xa9 : ce firmware a rencontr\xc3\xa9 une erreur.");
    else if (processor_.problem() == LmentsAudioProcessor::Problem::kNoSound)
      warning = utf8("Ce firmware n'a pas d\xc3\xa9marr\xc3\xa9 le son : ce n'est peut-\xc3\xaatre pas un firmware Elements.");
  }
  g.setFont(juce::FontOptions(11.0f));
  if (warning.isNotEmpty()) {
    g.setColour(juce::Colour(0xffff8a7a));
    g.drawText(warning, left, 40, width, 16, juce::Justification::centredLeft, true);
  } else if (processor_.hasFirmware()) {
    g.setColour(kSideDim);
    g.drawText(utf8("empreinte ") + processor_.firmwareId() + utf8(" \xc2\xb7 ") +
                   (processor_.memoryRestored() ? utf8("r\xc3\xa9glages repris du projet") : utf8("module neuf")),
               left, 40, width, 16, juce::Justification::centredLeft, true);
  }

  // Jauges, côte à côte : moyenne en barre, pointe en trait.
  auto gauge = [&](const juce::String& text, float value, float peak, int x, int w) {
    g.setColour(kSideText);
    g.setFont(juce::FontOptions(11.5f));
    g.drawText(text, x, 60, 104, 18, juce::Justification::centredLeft, false);
    const juce::Rectangle<float> bar(static_cast<float>(x + 106), 66.0f, static_cast<float>(w - 106 - 42), 7.0f);
    g.setColour(kTrack);
    g.fillRoundedRectangle(bar, 3.0f);
    const float worst = std::max(value, peak);
    g.setColour(worst > 0.999f ? juce::Colour(0xffff5a4a) : (worst > 0.9f ? juce::Colour(0xffffb23c) : juce::Colour(0xff5fd08a)));
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, value)), 3.0f);
    if (peak > 0.0f) {
      g.setColour(juce::Colour(0xfff0f0f2));
      g.fillRect(bar.getX() + bar.getWidth() * juce::jlimit(0.0f, 1.0f, peak) - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
    }
    g.setColour(kSideText);
    g.drawText(juce::String(juce::roundToInt(value * 100.0f)) + " %", x + w - 40, 60, 40, 18, juce::Justification::centredRight, false);
  };
  const float moduleLoad = processor_.moduleLoad(), modulePeak = processor_.moduleLoadPeak(), hostLoad = processor_.hostLoad();
#if JUCE_MAC
  const char* const hostName = "Ce Mac (1 c\xc5\x93ur)";
#else
  const char* const hostName = "Ce PC (1 c\xc5\x93ur)";
#endif
  const int half = (width - 16) / 2;
  gauge(utf8("Puce du module"), moduleLoad, modulePeak, left, half);
  gauge(utf8(hostName), hostLoad, 0.0f, left + half + 16, half);
  g.setFont(juce::FontOptions(11.0f));
  if (processor_.moduleOverloaded()) {
    g.setColour(juce::Colour(0xffff5a4a));
    g.drawText(utf8("Surcharge : le module craque (") + juce::String(processor_.moduleOverruns()) + utf8(" blocs manqu\xc3\xa9s)"),
               left, 80, width, 16, juce::Justification::centredLeft, true);
  } else if (processor_.hasFirmware()) {
    g.setColour(kSideDim);
    g.drawText(utf8("pointe ") + juce::String(juce::roundToInt(modulePeak * 100.0f)) + utf8(" % (estimation, voir README)"),
               left, 80, width, 16, juce::Justification::centredLeft, true);
  }

  // Interface MIDI -> CV.
  g.setColour(kSideText);
  g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
  g.drawText(utf8("Interface MIDI \xe2\x86\x92 CV"), left, 104, width, 20, juce::Justification::centredLeft, false);
  g.setColour(kSideDim);
  g.setFont(juce::FontOptions(11.0f));
  g.drawText(utf8("V/OCT : notes MIDI (0 V = note 36)"), left, 104, width, 20, juce::Justification::centredRight, false);
  g.setColour(juce::Colour(0xff4a4b52));
  g.fillRect(left, 126, width, 1);
  g.setColour(kSideText);
  g.setFont(juce::FontOptions(11.5f));
  g.drawText("GATE", left, 134, 40, 22, juce::Justification::centredLeft, false);
  g.drawText("Pitch bend", left + 180, 134, 76, 22, juce::Justification::centredLeft, false);

  // En-tête de la matrice.
  g.setColour(kSideDim);
  g.setFont(juce::FontOptions(10.5f));
  g.drawText("prise", left + 12, kRowsTop - 18, 80, 16, juce::Justification::centredLeft, false);
  for (int j = 0; j < CvMatrixRow::kNumSources; ++j)
    g.drawText(utf8(kSourceNames[j]), left + CvMatrixRow::kNameWidth + j * CvMatrixRow::kColumnWidth, kRowsTop - 18,
               CvMatrixRow::kColumnWidth, 16, juce::Justification::centred, false);
  g.drawText("tension", left + CvMatrixRow::kNameWidth + CvMatrixRow::kNumSources * CvMatrixRow::kColumnWidth + 8, kRowsTop - 18,
             80, 16, juce::Justification::centredLeft, false);
  g.setColour(kRule);
  g.fillRect(left, kRowsTop + kRowHeight * static_cast<int>(kCvs.size()), width, 1);
}

void LmentsAudioProcessorEditor::paint(juce::Graphics& g) {
  paintPanel(g);
  paintSide(g);
}

// --- Actions -------------------------------------------------------------------------------

void LmentsAudioProcessorEditor::timerCallback() {
  // « Automation » : la tension vient du curseur ; sinon le curseur ne sert pas.
  for (auto& row : cvRows_) row->refresh();
  resetButton_.setEnabled(processor_.hasFirmware());
  repaint();
}

void LmentsAudioProcessorEditor::setPlay(bool down) {
  if (down == playDown_) return;
  playDown_ = down;
  if (auto* parameter = processor_.apvts.getParameter(ids::play)) {
    if (down) parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(down ? 1.0f : 0.0f);
    if (!down) parameter->endChangeGesture();
  }
}

void LmentsAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) {
  if (e.position.getDistanceFrom(panelPoint(kPlayX, kPlayY)) <= kPlayRadius * kScale) setPlay(true);
}

void LmentsAudioProcessorEditor::mouseUp(const juce::MouseEvent&) { setPlay(false); }

void LmentsAudioProcessorEditor::chooseFirmware() {
  chooser_ = std::make_unique<juce::FileChooser>(utf8("Choisir un firmware Elements (.wav de mise \xc3\xa0 jour ou .bin)"),
                                                 juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.wav;*.bin");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                        [this](const juce::FileChooser& chooser) {
                          const auto file = chooser.getResult();
                          if (file.existsAsFile()) loadFirmware(file);
                        });
}

void LmentsAudioProcessorEditor::loadFirmware(const juce::File& file) {
  message_ = processor_.loadFirmwareFile(file);
  if (message_.isEmpty()) {
    recent_.addFile(file);
    settings_->setValue("recentFirmwares", recent_.toString());
    settings_->saveIfNeeded();
  }
  repaint();
}

void LmentsAudioProcessorEditor::showRecent() {
  recent_.removeNonExistentFiles();
  juce::PopupMenu menu;
  if (recent_.getNumFiles() == 0) menu.addItem(1, utf8("(aucun firmware ouvert r\xc3\xa9" "cemment)"), false);
  for (int i = 0; i < recent_.getNumFiles(); ++i) {
    const auto file = recent_.getFile(i);
    menu.addItem(100 + i, file.getFileName() + "   (" + file.getParentDirectory().getFileName() + ")");
  }
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&recentButton_), [this](int result) {
    if (result >= 100 && result - 100 < recent_.getNumFiles()) loadFirmware(recent_.getFile(result - 100));
  });
}

void LmentsAudioProcessorEditor::confirmReset() {
  if (!processor_.hasFirmware()) return;
  juce::AlertWindow::showOkCancelBox(
      juce::MessageBoxIconType::QuestionIcon, utf8("R\xc3\xa9glages d'usine"),
      utf8("Effacer la m\xc3\xa9moire de \xc2\xab ") + processor_.firmwareName() +
          utf8(" \xc2\xbb et le red\xc3\xa9marrer comme un module neuf ?\n\nLa calibration, le mod\xc3\xa8le de r\xc3\xa9sonateur et le mode cach\xc3\xa9 reviennent \xc3\xa0 leurs r\xc3\xa9glages d'usine. Les potards ne bougent pas."),
      utf8("Effacer"), utf8("Annuler"), this,
      juce::ModalCallbackFunction::create([this](int ok) {
        if (ok) { processor_.resetFirmwareMemory(); message_ = {}; repaint(); }
      }));
}

bool LmentsAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files) {
  return files.size() == 1 && (files[0].endsWithIgnoreCase(".wav") || files[0].endsWithIgnoreCase(".bin"));
}

void LmentsAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int) {
  if (files.size() == 1) loadFirmware(juce::File(files[0]));
}
