// LMenTS — banc d'essai du processeur virtuel.
//
//   emu_run <image.bin> [adresse de chargement en hexa] [limite en millions de cycles]
//
// Charge une image brute en flash, exécute, et recopie sur la sortie standard ce
// que le programme écrit sur le port série de test. Les adresses sont celles de la
// carte « mps2-an386 » de QEMU, pour que le même programme tourne des deux côtés :
//   0x40004000  port série (un caractère par écriture)
//   0x40005010  fin du programme (la valeur écrite est le code de retour)
//   0x40005004  repères de mesure de cycles
// Les statistiques vont sur la sortie d'erreur.

#include <chrono>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../Source/emu/CortexM4.h"

using namespace lments::emu;

struct TestBus : Bus {
  CortexM4* cpu = nullptr;
  int exitCode = -1;
  uint32_t mark = 0;
  uint64_t markCycles = 0;
  uint32_t read(uint32_t, int) override { return 0; }
  void write(uint32_t a, uint32_t v, int) override {
    if (a == 0x40004000u) std::fputc(static_cast<int>(v & 0xff), stdout);
    else if (a == 0x40005010u) { exitCode = static_cast<int>(v); cpu->halt("exit"); }
    else if (a == 0x40005004u) {
      // Repères de mesure : cycles consommés entre un repère non nul et le repère 0.
      if (v) { mark = v; markCycles = cpu->cycles(); }
      else std::fprintf(stderr, "[mesure] %u %llu\n", mark, static_cast<unsigned long long>(cpu->cycles() - markCycles));
    }
  }
  void onDeadline(uint64_t) override {}
};

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: emu_run image.bin [base] [Mcycles]\n"); return 2; }
  const uint32_t base = argc > 2 ? static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 16)) : 0;
  const uint64_t limit = (argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 20000) * 1000000ull;

#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);   // sorties identiques à celles de Linux et du Mac
#endif
  TestBus bus;
  CortexM4 cpu(2 * 1024 * 1024, 1024 * 1024, &bus, 64 * 1024);
  bus.cpu = &cpu;
  FILE* f = std::fopen(argv[1], "rb");
  if (!f) { std::perror(argv[1]); return 2; }
  const uint32_t offset = base >= CortexM4::kFlashBase ? base - CortexM4::kFlashBase : base;
  const size_t n = std::fread(cpu.flash() + offset, 1, cpu.flashSize() - offset, f);
  std::fclose(f);

  cpu.reset();
  const auto t0 = std::chrono::steady_clock::now();
  cpu.run(limit);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::fflush(stdout);
  std::fprintf(stderr,
               "[emu] %zu octets, %llu instructions, %llu cycles, %.2f s (%.0f M instr/s, %.1fx un STM32F405 a 168 MHz), fin: %s",
               n, static_cast<unsigned long long>(cpu.instructions()),
               static_cast<unsigned long long>(cpu.cycles()), seconds,
               cpu.instructions() / seconds / 1e6, cpu.cycles() / seconds / 168e6, cpu.haltReason());
  if (bus.exitCode < 0) std::fprintf(stderr, " (pc=%08x, faute a %08x)", cpu.pc(), cpu.faultPc());
  std::fprintf(stderr, "\n");
  return bus.exitCode < 0 ? 3 : bus.exitCode;
}
