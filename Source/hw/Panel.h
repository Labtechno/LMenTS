// LMenTS — la façade d'Elements : ses potards, ses prises, et l'électronique
// analogique qui les relie à la puce.
//
// Les numéros (adresse d'aiguilleur pour les potards, canal de convertisseur pour
// les CV) décrivent le câblage de la carte. Ils viennent des pilotes du firmware
// officiel (elements/drivers/pots_adc.cc et cv_adc.cc).
//
// Étages d'entrée. Les schémas du module ne sont pas lisibles ici ; les valeurs
// sont déduites des constantes du firmware, qui décrivent une carte « idéale » :
//   - CV de paramètre, FM, STRENGTH : tension à la puce = 1,667 V - 0,2 x CV
//     (réglage par défaut du firmware : 10 x 20 / 120 / 3,3 ; plage de +/- 8 V,
//     comme l'indique la documentation du module) ;
//   - V/OCT : tension à la puce = 2,611 V - 0,47 x CV (pente et décalage par
//     défaut du firmware ; un commentaire de l'auteur donne les valeurs mesurées
//     sur la carte : 0,6488 à 1 V et 0,3640 à 3 V, soit la même droite).
// Avec ces étages, les réglages d'usine par défaut du firmware sont exactement
// justes : le module émulé n'a pas besoin d'être calibré.
//
// Licence MIT.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lments {
namespace hw {

// Potards, dans l'ordre de la façade (de gauche à droite, de haut en bas).
enum Pot {
  kPotContour, kPotBow, kPotBlow, kPotStrike, kPotCoarse, kPotFine, kPotFm,
  kPotFlow, kPotMallet, kPotGeometry, kPotBrightness,
  kPotBowTimbre, kPotBlowTimbre, kPotStrikeTimbre, kPotDamping, kPotPosition, kPotSpace,
  kPotBowTimbreAtt, kPotFlowAtt, kPotBlowTimbreAtt, kPotMalletAtt, kPotStrikeTimbreAtt,
  kPotDampingAtt, kPotGeometryAtt, kPotPositionAtt, kPotBrightnessAtt, kPotSpaceAtt,
  kNumPots
};

// Adresse de chaque potard sur les aiguilleurs (0 à 31).
constexpr uint8_t kPotAddress[kNumPots] = {
  28, 30, 29, 22, 23, 26, 25,
  31, 20, 21, 24,
  5, 7, 6, 4, 0, 27,
  1, 2, 11, 8, 9,
  10, 19, 16, 17, 18,
};

// Entrées CV, dans l'ordre de la façade.
enum Cv {
  kCvVOct, kCvFm, kCvStrength,
  kCvBowTimbre, kCvFlow, kCvBlowTimbre, kCvMallet, kCvStrikeTimbre,
  kCvDamping, kCvGeometry, kCvPosition, kCvBrightness, kCvSpace,
  kNumCvs
};

// Canal du convertisseur (ADC1) de chaque entrée CV.
constexpr uint8_t kCvChannel[kNumCvs] = { 12, 13, 0, 1, 2, 3, 4, 5, 6, 7, 15, 8, 9 };

// Position d'un potard (0 = à fond à gauche, 1 = à fond à droite) -> code 12 bits.
inline uint16_t potCode(float position) {
  return static_cast<uint16_t>(std::lround(std::clamp(position, 0.0f, 1.0f) * 4095.0f));
}

// Tension sur une prise CV -> code 12 bits lu par la puce.
inline uint16_t cvCode(Cv input, float volts) {
  const float pin = input == kCvVOct ? 10.0f * 47.0f / 180.0f - 0.47f * volts
                                     : 10.0f * 20.0f / 120.0f - 0.2f * volts;
  return static_cast<uint16_t>(std::lround(std::clamp(pin / 3.3f, 0.0f, 1.0f) * 4095.0f));
}

// Seuil de l'entrée GATE (un transistor) : estimation.
constexpr float kGateThresholdVolts = 0.7f;

}  // namespace hw
}  // namespace lments
