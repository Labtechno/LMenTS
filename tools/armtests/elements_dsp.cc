// Test du son d'Elements : le code de synthèse du firmware (elements/dsp) est
// compilé tel quel, avec les options du firmware, et joue une série de réglages.
// Le même programme tourne sur le processeur émulé, sous QEMU et sur l'ordinateur.
//
// Sortie : pour chaque réglage, une somme de contrôle de tous les échantillons
// (comparaison bit pour bit avec QEMU), puis un échantillon sur 16 en clair
// (mesure de l'écart avec la version compilée pour l'ordinateur).
#include <stdint.h>
#include <string.h>

#include "elements/dsp/part.h"

extern "C" void emit(uint32_t v);
extern "C" void put(char c);

using namespace elements;

static uint16_t reverb_buffer[32768];
static Part part;

struct Preset {
  const char* name;
  int model;          // 0 modal, 1 cordes sympathiques, 2 cordes modulées ; 3 = mode « Ominous »
  float note;
  float bow, blow, strike;
  float contour, bow_timbre, blow_meta, blow_timbre, strike_meta, strike_timbre;
  float geometry, brightness, damping, position, space;
  bool external;      // envoie un signal dans les deux entrées audio
};

static const Preset presets[] = {
  { "maillet",   0, 48.0f, 0.0f, 0.0f, 0.8f, 0.5f, 0.5f, 0.5f, 0.5f, 0.50f, 0.5f, 0.20f, 0.50f, 0.25f, 0.30f, 0.30f, false },
  { "archet",    0, 36.0f, 0.9f, 0.0f, 0.0f, 0.4f, 0.6f, 0.5f, 0.5f, 0.50f, 0.5f, 0.35f, 0.40f, 0.70f, 0.20f, 0.50f, false },
  { "souffle",   0, 60.0f, 0.0f, 0.9f, 0.0f, 0.7f, 0.5f, 0.3f, 0.7f, 0.50f, 0.5f, 0.60f, 0.60f, 0.60f, 0.40f, 0.60f, false },
  { "tout",      0, 43.0f, 0.5f, 0.5f, 0.5f, 0.2f, 0.3f, 0.8f, 0.4f, 0.90f, 0.8f, 0.80f, 0.70f, 0.50f, 0.70f, 0.90f, false },
  { "granules",  0, 72.0f, 0.0f, 0.0f, 0.9f, 0.9f, 0.5f, 0.5f, 0.5f, 0.99f, 0.3f, 0.05f, 0.90f, 0.90f, 0.10f, 0.10f, false },
  { "plectre",   0, 55.0f, 0.0f, 0.0f, 0.9f, 0.5f, 0.5f, 0.5f, 0.5f, 0.75f, 0.9f, 0.50f, 0.20f, 0.40f, 0.50f, 0.00f, false },
  { "externe",   0, 50.0f, 0.0f, 0.3f, 0.3f, 0.5f, 0.5f, 0.5f, 0.5f, 0.20f, 0.5f, 0.40f, 0.50f, 0.50f, 0.30f, 0.40f, true },
  { "cordes",    1, 45.0f, 0.3f, 0.0f, 0.7f, 0.5f, 0.5f, 0.5f, 0.5f, 0.40f, 0.6f, 0.30f, 0.60f, 0.60f, 0.40f, 0.50f, false },
  { "cordes 2",  2, 52.0f, 0.0f, 0.4f, 0.7f, 0.6f, 0.5f, 0.6f, 0.5f, 0.60f, 0.4f, 0.70f, 0.50f, 0.50f, 0.60f, 0.70f, false },
  { "ominous",   3, 40.0f, 0.6f, 0.5f, 0.6f, 0.5f, 0.4f, 0.6f, 0.5f, 0.50f, 0.5f, 0.50f, 0.60f, 0.60f, 0.50f, 0.60f, false },
};

static uint32_t noise_state = 12345;
static float noise() {
  noise_state = noise_state * 1664525u + 1013904223u;
  return static_cast<float>(static_cast<int32_t>(noise_state) >> 8) * (1.0f / 8388608.0f);
}

int main() {
  const size_t kBlock = 16;
  const size_t kBlocks = 2 * 32000 / kBlock;    // 2 secondes par réglage
  for (size_t p = 0; p < sizeof(presets) / sizeof(presets[0]); ++p) {
    const Preset& s = presets[p];
    memset(reverb_buffer, 0, sizeof(reverb_buffer));
    memset(static_cast<void*>(&part), 0, sizeof(part));
    part.Init(reverb_buffer);
    uint32_t seed[3] = { 0x1234567, 0x89abcdef, 0x42424242 };
    part.Seed(seed, 3);
    part.set_easter_egg(s.model == 3);
    part.set_resonator_model(static_cast<ResonatorModel>(s.model == 3 ? 0 : s.model));
    Patch* patch = part.mutable_patch();
    patch->exciter_envelope_shape = s.contour;
    patch->exciter_bow_level = s.bow;
    patch->exciter_bow_timbre = s.bow_timbre;
    patch->exciter_blow_level = s.blow;
    patch->exciter_blow_meta = s.blow_meta;
    patch->exciter_blow_timbre = s.blow_timbre;
    patch->exciter_strike_level = s.strike;
    patch->exciter_strike_meta = s.strike_meta;
    patch->exciter_strike_timbre = s.strike_timbre;
    patch->resonator_geometry = s.geometry;
    patch->resonator_brightness = s.brightness;
    patch->resonator_damping = s.damping;
    patch->resonator_position = s.position;
    patch->space = s.space;

    for (const char* c = s.name; *c; ++c) put(*c);
    put('\n');
    uint32_t h = 2166136261u;
    for (size_t b = 0; b < kBlocks; ++b) {
      float blow_in[kBlock], strike_in[kBlock], main_out[kBlock], aux_out[kBlock];
      for (size_t i = 0; i < kBlock; ++i) {
        blow_in[i] = s.external ? 0.3f * noise() : 0.0f;
        strike_in[i] = s.external && ((b * kBlock + i) % 4000) < 40 ? 0.8f : 0.0f;
      }
      PerformanceState state;
      const size_t t = b % 1000;                  // une note toutes les demi-secondes
      state.gate = t < 600;
      state.note = s.note + static_cast<float>((b / 1000) % 4) * 3.0f;
      state.modulation = 0.0f;
      state.strength = 0.6f + 0.1f * static_cast<float>((b / 1000) % 3);
      // Les potards bougent pendant la note, comme sous les doigts.
      patch->resonator_position = s.position + 0.2f * static_cast<float>(t) / 1000.0f;
      patch->resonator_geometry = s.geometry + 0.1f * static_cast<float>(b) / static_cast<float>(kBlocks);
      part.Process(state, blow_in, strike_in, main_out, aux_out, kBlock);
      for (size_t i = 0; i < kBlock; ++i) {
        uint32_t m, a;
        memcpy(&m, &main_out[i], 4);
        memcpy(&a, &aux_out[i], 4);
        h = (h ^ m) * 16777619u;
        h = (h ^ a) * 16777619u;
      }
      if (b % 16 == 0) {
        uint32_t m, a;
        memcpy(&m, &main_out[0], 4);
        memcpy(&a, &aux_out[0], 4);
        emit(m); emit(a);
      }
    }
    put('=');
    emit(h);
  }
  return 0;
}
