// LMenTS — conversion de fréquence d'échantillonnage, en stéréo.
//
// Le module calcule son son à 32 kHz (32 001,2 Hz exactement). Ce convertisseur
// fait le passage entre cette fréquence et celle du logiciel hôte (44,1, 48,
// 96 kHz...), dans les deux sens : la sortie du module vers l'hôte, et les
// entrées audio de l'hôte vers le module. Filtre sinc fenêtré (Kaiser) qui coupe
// juste sous la plus basse des deux demi-fréquences.
//
// Repris du projet Tresses (même auteur), passé en stéréo. Licence MIT.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lments {

class StereoResampler {
 public:
  static constexpr int kPhases = 256;       // résolution de la table du noyau
  static constexpr double kZeroCrossings = 32.0;
  static constexpr double kKaiserBeta = 8.0;
  static constexpr int kBlock = 16;         // taille des blocs fournis par la source
  static constexpr double kPi = 3.14159265358979323846;

  void prepare(double inputRate, double outputRate) {
    step_ = inputRate / outputRate;
    const double cutoffHz = 0.5 * std::min(inputRate, outputRate) * 0.93;
    const double wc = cutoffHz / inputRate;  // cycles par échantillon d'entrée
    halfWidth_ = static_cast<int>(std::ceil(kZeroCrossings / (2.0 * wc)));

    int size = 1;
    while (size < 2 * halfWidth_ + 4 * kBlock + 8) size <<= 1;
    left_.assign(static_cast<size_t>(size), 0.0f);
    right_.assign(static_cast<size_t>(size), 0.0f);
    mask_ = size - 1;

    const int taps = 2 * halfWidth_;
    table_.assign(static_cast<size_t>(taps * kPhases + 2), 0.0f);
    const double i0Beta = besselI0(kKaiserBeta);
    for (int k = 0; k <= taps * kPhases + 1; ++k) {
      // d = distance (en échantillons d'entrée) au point d'interpolation.
      const double d = static_cast<double>(k) / kPhases - halfWidth_;
      const double x = d / halfWidth_;
      double w = 0.0;
      if (std::abs(x) < 1.0) w = besselI0(kKaiserBeta * std::sqrt(1.0 - x * x)) / i0Beta;
      const double arg = 2.0 * wc * d;
      const double sinc = std::abs(arg) < 1e-12 ? 1.0 : std::sin(kPi * arg) / (kPi * arg);
      table_[static_cast<size_t>(k)] = static_cast<float>(2.0 * wc * sinc * w);
    }
    reset();
  }

  void reset() {
    std::fill(left_.begin(), left_.end(), 0.0f);
    std::fill(right_.begin(), right_.end(), 0.0f);
    written_ = 0;
    ipos_ = 0;
    frac_ = 0.0;
  }

  // Retard introduit, en échantillons de sortie.
  int latencyInOutputSamples() const { return static_cast<int>(std::lround(halfWidth_ / step_)); }
  // Avance maximale prise sur la source, en échantillons d'entrée.
  int lookaheadInInputSamples() const { return halfWidth_ + kBlock + 1; }

  // Produit une trame de sortie. `pull(float* l, float* r)` doit écrire kBlock
  // trames à la fréquence d'entrée.
  template <typename Source>
  void process(Source&& pull, float& outL, float& outR) {
    // Position de sortie P = ipos_ + frac_ (en échantillons d'entrée).
    // On somme x[n] * h(n - P) pour n dans [n0, n0 + 2 * halfWidth_).
    const bool hasFrac = frac_ > 0.0;
    const int64_t n0 = ipos_ - halfWidth_ + (hasFrac ? 1 : 0);
    const double offset = hasFrac ? 1.0 - frac_ : 0.0;
    const int taps = 2 * halfWidth_;
    while (written_ < n0 + taps) {
      float l[kBlock], r[kBlock];
      pull(l, r);
      for (int i = 0; i < kBlock; ++i) {
        left_[static_cast<size_t>((written_ + i) & mask_)] = l[i];
        right_[static_cast<size_t>((written_ + i) & mask_)] = r[i];
      }
      written_ += kBlock;
    }

    const double t = offset * kPhases;
    const int base = static_cast<int>(t);
    const float mu = static_cast<float>(t - base);
    const float* h = table_.data() + base;
    float accL = 0.0f, accR = 0.0f;
    int64_t n = n0;
    for (int j = 0; j < taps; ++j, ++n, h += kPhases) {
      const float coeff = h[0] + mu * (h[1] - h[0]);
      const size_t index = static_cast<size_t>(n & mask_);
      accL += left_[index] * coeff;
      accR += right_[index] * coeff;
    }
    outL = accL;
    outR = accR;

    frac_ += step_;
    const double whole = std::floor(frac_);
    ipos_ += static_cast<int64_t>(whole);
    frac_ -= whole;
  }

 private:
  static double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double y = x * x / 4.0;
    for (int k = 1; k < 64; ++k) {
      term *= y / (static_cast<double>(k) * k);
      sum += term;
      if (term < sum * 1e-17) break;
    }
    return sum;
  }

  double step_ = 1.0;
  int halfWidth_ = 0;
  std::vector<float> table_;
  std::vector<float> left_, right_;
  int64_t mask_ = 0;
  int64_t written_ = 0;
  int64_t ipos_ = 0;
  double frac_ = 0.0;
};

}  // namespace lments
