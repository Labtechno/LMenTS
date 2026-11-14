// Test des exceptions : SysTick, interruptions imbriquées, PendSV, SVC, masquage,
// et sauvegarde des registres flottants pendant les interruptions.
#include <stdint.h>
void emit(uint32_t v);

#define REG(a) (*(volatile uint32_t*) (a))
#define SYST_CSR REG(0xe000e010)
#define SYST_RVR REG(0xe000e014)
#define SYST_CVR REG(0xe000e018)
#define NVIC_ISER REG(0xe000e100)
#define NVIC_ISPR REG(0xe000e200)
#define NVIC_IPR(n) (*(volatile uint8_t*) (0xe000e400 + (n)))
#define ICSR REG(0xe000ed04)
#define SHPR(n) (*(volatile uint8_t*) (0xe000ed18 + (n) - 4))

#define FPCCR REG(0xe000ef34)
#define FPCAR REG(0xe000ef38)
#define FPDSCR REG(0xe000ef3c)
#define CPACR REG(0xe000ed88)

static volatile uint32_t ticks, depth, sum;
static volatile uint32_t fpMode;    // ce que fait la SysTick avec les flottants
static volatile float fsink;

static uint32_t control(void) { uint32_t v; __asm__ volatile("mrs %0, control" : "=r"(v)); return v; }
static uint32_t getFpscr(void) { uint32_t v; __asm__ volatile("vmrs %0, fpscr" : "=r"(v)); return v; }
static void setFpscr(uint32_t v) { __asm__ volatile("vmsr fpscr, %0" : : "r"(v)); }

// Abîme volontairement tous les registres flottants sauvés par le processeur.
__attribute__((noinline)) static void trashFp(void) {
  __asm__ volatile(
      "vmov.f32 s0, #1.0\n vmov.f32 s1, #2.0\n vmov.f32 s2, #3.0\n vmov.f32 s3, #4.0\n"
      "vmov.f32 s4, #5.0\n vmov.f32 s5, #6.0\n vmov.f32 s6, #7.0\n vmov.f32 s7, #8.0\n"
      "vmov.f32 s8, #9.0\n vmov.f32 s9, #10.0\n vmov.f32 s10, #11.0\n vmov.f32 s11, #12.0\n"
      "vmov.f32 s12, #13.0\n vmov.f32 s13, #14.0\n vmov.f32 s14, #15.0\n vmov.f32 s15, #16.0\n"
      "vdiv.f32 s0, s1, s2\n"
      : : : "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12", "s13", "s14", "s15");
  setFpscr(0x03c0009f);
}

void SysTick_Handler(void) {
  ++ticks;
  if (fpMode == 1) trashFp();
  if (fpMode == 2 && ticks % 3 == 0) trashFp();
}

// Rend compte de l'état du processeur vu depuis une interruption.
void IRQ3_Handler(void) {
  uint32_t lr; __asm__ volatile("mov %0, lr" : "=r"(lr));
  uint32_t sp; __asm__ volatile("mov %0, sp" : "=r"(sp));
  emit(lr); emit(control()); emit(FPCCR);
  uint32_t car = FPCAR;
  trashFp();
  emit(FPCCR); emit(control()); emit(car - sp < 0x100);
}

// Calcul flottant long : aucun résultat ne doit dépendre des interruptions.
__attribute__((noinline)) static uint32_t fwork(uint32_t n) {
  float x = 0.5f, y = 0.25f, acc = 0.0f;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; ++i) {
    x = x * 1.0001f + y * 0.001f;
    y = y * 0.9999f - x * 0.0005f;
    acc += x * y;
    if (x > 2.0f) x -= 1.5f;
    union { float f; uint32_t u; } b = { .f = acc };
    h = (h ^ b.u) * 16777619u;
  }
  fsink = acc;
  return h;
}
void SVC_Handler(void) { emit(0x5c000000 | depth); }
void PendSV_Handler(void) { emit(0x9e000000 | depth); }
void IRQ2_Handler(void) { ++depth; emit(0x22000000 | depth); --depth; }
void IRQ1_Handler(void) {
  ++depth; emit(0x11000000 | depth);
  NVIC_ISPR = 4;            // IRQ2, plus prioritaire : préemption immédiate
  emit(0x11100000 | depth);
  ICSR = 1u << 28;          // PendSV, moins prioritaire : après la sortie
  emit(0x11200000 | depth); --depth;
}
void IRQ0_Handler(void) {
  ++depth; emit(0x00a00000 | depth);
  NVIC_ISPR = 2;            // IRQ1, plus prioritaire
  emit(0x00b00000 | depth);
  __asm__ volatile("svc 5");
  emit(0x00c00000 | depth); --depth;
}

__attribute__((noinline)) static uint32_t work(uint32_t n) {
  uint32_t a = 1;
  for (uint32_t i = 0; i < n; ++i) { a = a * 1664525u + 1013904223u; sum += a >> 7; }
  return a;
}

int main(void) {
  NVIC_IPR(0) = 0xc0; NVIC_IPR(1) = 0x80; NVIC_IPR(2) = 0x40;
  SHPR(11) = 0x40; SHPR(14) = 0xe0; SHPR(15) = 0x20;
  NVIC_ISER = 7;

  emit(1);
  NVIC_ISPR = 1;                              // lance la cascade IRQ0 > IRQ1 > IRQ2 > PendSV
  emit(2);

  __asm__ volatile("cpsid i");                // PRIMASK : rien ne passe
  NVIC_ISPR = 4; emit(3);
  __asm__ volatile("cpsie i"); emit(4);

  uint32_t base = 0x80;                       // BASEPRI = 0x80 : seul IRQ2 (0x40) passe
  __asm__ volatile("msr basepri, %0" : : "r"(base));
  NVIC_ISPR = 2; NVIC_ISPR = 4; emit(5);
  base = 0; __asm__ volatile("msr basepri, %0" : : "r"(base)); emit(6);

  // SysTick : les calculs en cours ne doivent pas être perturbés par l'interruption.
  uint32_t before = work(2000);
  SYST_RVR = 999; SYST_CVR = 0; SYST_CSR = 7;
  uint32_t during = work(2000);
  while (ticks < 50) {}
  uint32_t t1 = ticks;
  for (int i = 0; i < 10; ++i) __asm__ volatile("wfi");
  SYST_CSR = 0;
  emit(before == during); emit(t1 >= 50); emit(ticks - t1 >= 9 && ticks - t1 <= 11);

  // Pile : les registres r4-r11 et les drapeaux survivent aux interruptions.
  SYST_CSR = 7;
  uint32_t ok = 1;
  for (uint32_t i = 0; i < 20000; ++i) {
    uint64_t x = (uint64_t) i * 0x9e3779b97f4a7c15ull;
    uint64_t y = x + 0xffffffffull;            // retenue qui traverse une interruption
    if (y - 0xffffffffull != x) ok = 0;
  }
  SYST_CSR = 0;
  emit(ok); emit(ticks >= 60);

  // Flottants. État au démarrage, puis après un premier calcul.
  emit(CPACR); emit(FPCCR); emit(control());
  uint32_t ref = fwork(60000);
  emit(ref); emit(control());
  NVIC_IPR(3) = 0x40; NVIC_ISER = 8;
  setFpscr(0x00400000);
  NVIC_ISPR = 8;                              // interruption qui se sert des flottants
  emit(getFpscr()); emit(control()); emit(FPCCR);
  setFpscr(0);

  // Trois réglages : sauvegarde paresseuse (défaut), immédiate, puis aucune.
  for (int mode = 0; mode < 3; ++mode) {
    FPCCR = mode == 0 ? 0xc0000000 : (mode == 1 ? 0x80000000 : 0);
    for (uint32_t m = 0; m < 3; ++m) {
      if (mode == 2 && m != 0) continue;      // sans sauvegarde, l'interruption ne doit pas toucher aux flottants
      fpMode = m;
      ticks = 0;
      SYST_RVR = 777; SYST_CVR = 0; SYST_CSR = 7;
      uint32_t got = fwork(60000);
      SYST_CSR = 0;
      emit(got == ref); emit(ticks > 5);
    }
  }
  FPCCR = 0xc0000000;
  fpMode = 0;

  // Nouveau contexte : FPDSCR donne le mode d'arrondi par défaut d'une interruption.
  FPDSCR = 0x00800000;
  NVIC_ISPR = 8;
  emit(getFpscr());
  FPDSCR = 0;

  // Sans contexte flottant actif, le cadre de pile redevient court.
  { uint32_t c = control() & ~4u; __asm__ volatile("msr control, %0\n isb" : : "r"(c)); }
  NVIC_ISPR = 8;
  emit(control());
  return 0;
}
