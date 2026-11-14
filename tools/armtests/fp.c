// Pilote du test des instructions flottantes : voir gen_fp.py.
// Opérandes : tous les cas limites (zéros, dénormalisés, infinis, NaN, bornes des
// conversions) croisés entre eux, dans tous les modes d'arrondi et avec les options
// flush-to-zero / NaN par défaut / demi-précision alternative, puis des tirages
// au hasard.
#include <stdint.h>
void emit(uint32_t v);
typedef uint32_t (*fp_fn)(uint32_t, uint32_t, uint32_t, uint32_t*);
extern const int fp_test_count;
extern const fp_fn fp_tests[];

static const uint32_t edge[] = {
  0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007fffff, 0x807fffff, 0x00800000, 0x80800000,
  0x00800001, 0x00ffffff, 0x3f800000, 0xbf800000, 0x3f000000, 0x3fc00000, 0x40200000, 0xc0200000,
  0x3f7fffff, 0x3f800001, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001,
  0x7f800001, 0xff923456, 0x7fc12345, 0x4f000000, 0xcf000000, 0x4effffff, 0xcf000001, 0x4f800000,
  0x4f7fffff, 0x5f000000, 0x4b800000, 0x4b7fffff, 0x33800000, 0x0d000000, 0x7e800000, 0x41200000,
  0x3eaaaaab, 0xbeaaaaab, 0x47000000, 0x46fffe00, 0xc7000000, 0x477fff00, 0x00003c00, 0x7bff7c00,
  0x0001fc00, 0x33000000, 0x38800000, 0x387fffff, 0x477fe000, 0x47800000,
};
#define NEDGE (sizeof(edge) / sizeof(edge[0]))
#define FZ (1u << 24)
#define DN (1u << 25)
#define AHP (1u << 26)
#define RM(n) ((uint32_t) (n) << 22)
static const uint32_t modes[] = {
  0, 0x10, 0x9f, RM(1), RM(2), RM(3), FZ, DN, FZ | DN, AHP, AHP | 0x10,
  0x10 | FZ, 0x10 | DN, RM(1) | FZ | 0x10, RM(2) | DN, RM(3) | FZ | DN, RM(2) | FZ, RM(1) | AHP | DN,
};
#define NMODE (sizeof(modes) / sizeof(modes[0]))

// -DDEBUG_TEST=n : n'exécute que le test n et affiche chaque cas (pour trouver un écart).
#ifdef DEBUG_TEST
#define FIRST DEBUG_TEST
#define LAST (DEBUG_TEST + 1)
#define SHOW(a, b, c, m, r, io) do { emit(a); emit(b); emit(c); emit(m); emit(r); emit(io[0]); emit(io[1]); } while (0)
#else
#define FIRST 0
#define LAST fp_test_count
#define SHOW(a, b, c, m, r, io) do {} while (0)
#endif

int main(void) {
  uint32_t total = 2166136261u;
  for (int t = FIRST; t < LAST; ++t) {
    uint32_t h = 2166136261u, seed = 0xf10a7 + (uint32_t) t * 7919u, io[2];
    for (unsigned mode = 0; mode < NMODE; ++mode) {
      for (unsigned i = 0; i < NEDGE; ++i) {
        for (unsigned j = 0; j < NEDGE; ++j) {
          io[0] = modes[mode]; io[1] = 0;
          uint32_t r = fp_tests[t](edge[i], edge[j], edge[(i * 7 + j * 3 + mode) % NEDGE], io);
          SHOW(edge[i], edge[j], edge[(i * 7 + j * 3 + mode) % NEDGE], modes[mode], r, io);
          h = (h ^ r) * 16777619u; h = (h ^ io[0]) * 16777619u; h = (h ^ io[1]) * 16777619u;
        }
      }
    }
    for (unsigned n = 0; n < 6000; ++n) {
      uint32_t a, b, c;
      seed = seed * 1664525u + 1013904223u; a = seed ^ (seed >> 13);
      seed = seed * 1664525u + 1013904223u; b = seed ^ (seed >> 11);
      seed = seed * 1664525u + 1013904223u; c = seed ^ (seed >> 9);
      if (n & 1) {
        // Exposants voisins : additions et soustractions qui se compensent.
        b = (b & 0x807fffff) | (((a >> 23) + (seed >> 29) - 3) & 0xff) << 23;
        c = (c & 0x807fffff) | (((a >> 22) + (seed >> 27 & 7) - 131) & 0xff) << 23;
      }
      if ((n & 6) == 2) { a = (a & 0x80ffffff) | 0x3e000000; }   // valeurs proches de 1
      const uint32_t m = modes[(seed >> 8) % NMODE] | ((seed >> 20) & 0x10);
      io[0] = m; io[1] = 0;
      uint32_t r = fp_tests[t](a, b, c, io);
      SHOW(a, b, c, m, r, io);
      h = (h ^ r) * 16777619u; h = (h ^ io[0]) * 16777619u; h = (h ^ io[1]) * 16777619u;
    }
    emit(h);
    total = (total ^ h) * 16777619u;
  }
  emit(total);
  return 0;
}
