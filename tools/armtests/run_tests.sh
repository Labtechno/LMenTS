#!/bin/bash
# Valide le processeur virtuel : chaque programme est compilé pour Cortex-M4F, puis
# exécuté par l'émulateur LMenTS. Ses sorties sont comparées à celles du même
# code compilé pour l'ordinateur et, si QEMU est installé, à celles de QEMU.
#
#   bash run_tests.sh              tous les tests
#   bash run_tests.sh rapide       sans le test du son d'Elements (le plus long)
#   bash run_tests.sh enregistrer  tous les tests, puis met à jour le dossier prebuilt/
#                                  (programmes compilés + résultats de référence, pour
#                                  refaire les comparaisons sur un Mac sans rien installer)
set -u
cd "$(dirname "$0")"
OUT=${OUT:-/tmp/lments-armtests}
mkdir -p "$OUT"
ARCH="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
CFLAGS="$ARCH -ffunction-sections -fdata-sections -fno-common -Wall"
LDFLAGS="-nostartfiles -T link.ld -Wl,--gc-sections --specs=nosys.specs -Wl,--defsym=FLASH_ORIGIN=0"
fail=0

EMU="../../Source/emu"
${CXX:-g++} -std=c++17 -O2 -ffp-contract=off -o "$OUT/emu_run" ../emu_run.cpp $EMU/CortexM4.cpp $EMU/CortexM4Fpu.cpp || exit 1
(cd "$OUT" && python3 "$OLDPWD/gen_flags.py" > gen.log && python3 "$OLDPWD/gen_fp.py" > genfp.log) || exit 1

check() {  # nom, fichier de référence, fichier obtenu
  if [ -s "$3" ] && cmp -s "$2" "$3"; then printf "  %-44s OK (%s lignes)\n" "$1" "$(wc -l < "$3")"
  else printf "  %-44s DIFFERENT\n" "$1"; diff "$2" "$3" | head -6; fail=1; fi
}

run_qemu() {  # elf, sortie
  timeout 900 qemu-system-arm -M mps2-an386 -nographic -monitor none -serial stdio \
      -semihosting -kernel "$1" > "$2" 2>/dev/null < /dev/null
}

build() {  # nom, optimisation, sources...
  local name=$1 opt=$2; shift 2
  arm-none-eabi-gcc $CFLAGS $opt "$@" startup.c $LDFLAGS -o "$OUT/$name.elf" > "$OUT/$name.build.log" 2>&1 || { echo "  $name : echec de compilation"; head "$OUT/$name.build.log"; fail=1; return 1; }
  arm-none-eabi-objcopy -O binary "$OUT/$name.elf" "$OUT/$name.bin"
}

HAVE_QEMU=0; command -v qemu-system-arm > /dev/null && HAVE_QEMU=1
[ $HAVE_QEMU = 1 ] || echo "QEMU absent : les comparaisons instruction par instruction sont sautees"

echo "== Arithmetique, bibliotheque C, calcul flottant (reference : meme code compile pour l'ordinateur)"
gcc -O1 -w -ffp-contract=off arith.c native_io.c -lm -o "$OUT/arith_native" && "$OUT/arith_native" > "$OUT/arith.ref"
for opt in -O0 -O1 -O2 -Os -O3; do
  build "arith$opt" "$opt -ffp-contract=off" arith.c -lm || continue
  "$OUT/emu_run" "$OUT/arith$opt.bin" > "$OUT/arith$opt.out" 2> "$OUT/arith$opt.stat"
  check "arith $opt" "$OUT/arith.ref" "$OUT/arith$opt.out"
done

if [ $HAVE_QEMU = 1 ]; then
  echo "== Entiers, drapeaux, instructions DSP, une par une (reference : QEMU)"
  build flags -O1 flags.c "$OUT/flags.S" "$OUT/flags_table.c" && {
    "$OUT/emu_run" "$OUT/flags.bin" > "$OUT/flags.out" 2> "$OUT/flags.stat"
    run_qemu "$OUT/flags.elf" "$OUT/flags.ref"
    check "$(cat "$OUT/gen.log")" "$OUT/flags.ref" "$OUT/flags.out"
  }
  echo "== Instructions flottantes, une par une (reference : QEMU)"
  build fp -O1 fp.c "$OUT/fp.S" "$OUT/fp_table.c" && {
    "$OUT/emu_run" "$OUT/fp.bin" > "$OUT/fp.out" 2> "$OUT/fp.stat"
    run_qemu "$OUT/fp.elf" "$OUT/fp.ref"
    check "$(cat "$OUT/genfp.log")" "$OUT/fp.ref" "$OUT/fp.out"
  }
  echo "== Exceptions, interruptions, sauvegarde des registres flottants (reference : QEMU)"
  for opt in -O0 -O2; do
    build "irq$opt" "$opt" irq.c || continue
    "$OUT/emu_run" "$OUT/irq$opt.bin" > "$OUT/irq$opt.out" 2> "$OUT/irq$opt.stat"
    # La SysTick de QEMU suit l'horloge réelle de l'ordinateur : sur une machine
    # chargée, le compte de ticks peut varier d'un essai à l'autre. On réessaie.
    for try in 1 2 3 4 5; do
      run_qemu "$OUT/irq$opt.elf" "$OUT/irq$opt.ref"
      cmp -s "$OUT/irq$opt.ref" "$OUT/irq$opt.out" && break
    done
    check "irq $opt" "$OUT/irq$opt.ref" "$OUT/irq$opt.out"
  done
fi

if [ "${1:-}" != rapide ]; then
  echo "== Synthese Elements (references : QEMU, puis l'ordinateur)"
  FW=../../firmware/eurorack
  DSP="$FW/elements/dsp/exciter.cc $FW/elements/dsp/multistage_envelope.cc $FW/elements/dsp/ominous_voice.cc $FW/elements/dsp/part.cc $FW/elements/dsp/resonator.cc $FW/elements/dsp/string.cc $FW/elements/dsp/tube.cc $FW/elements/dsp/voice.cc $FW/elements/resources.cc $FW/stmlib/dsp/atan.cc $FW/stmlib/dsp/units.cc $FW/stmlib/utils/random.cc"
  # Mêmes options que le makefile du firmware (stmlib/makefile.inc, famille f4xx).
  FWFLAGS="-O2 -fno-exceptions -fno-rtti -fasm -finline -finline-functions-called-once -fdata-sections -ffunction-sections -fshort-enums -fno-move-loop-invariants -fno-unwind-tables -funroll-loops -Wno-all -DSTM32F40_41xxx -DSTM32F4XX -DARM_MATH_CM4 -D__FPU_PRESENT"
  build_elements() {  # nom, options supplémentaires
    arm-none-eabi-g++ $ARCH -fno-common $FWFLAGS $2 -I$FW -Istub elements_dsp.cc $DSP -x c startup.c $LDFLAGS -lm -o "$OUT/$1.elf" > "$OUT/$1.build.log" 2>&1 || { echo "  echec de compilation"; grep -m5 error "$OUT/$1.build.log"; fail=1; return 1; }
    arm-none-eabi-objcopy -O binary "$OUT/$1.elf" "$OUT/$1.bin"
    "$OUT/emu_run" "$OUT/$1.bin" > "$OUT/$1.out" 2> "$OUT/$1.stat"
  }
  # 1. Compilé exactement comme le firmware : l'émulateur doit donner le même
  #    résultat que QEMU, bit pour bit.
  build_elements elements "" && if [ $HAVE_QEMU = 1 ]; then
    run_qemu "$OUT/elements.elf" "$OUT/elements.qemu"
    check "emulateur = QEMU, bit pour bit" "$OUT/elements.qemu" "$OUT/elements.out"
  fi
  # 2. Comparaison avec l'ordinateur. Le compilateur du firmware fusionne des
  #    multiplications et additions (un seul arrondi au lieu de deux), ce que
  #    l'ordinateur ne fait pas aux mêmes endroits. On compile donc les deux côtés
  #    sans fusion : les résultats doivent alors être identiques, bit pour bit.
  #    Une exception connue : le mode cache « Ominous » convertit un nombre négatif en
  #    entier non signé. La puce donne 0, un processeur d'ordinateur donne autre chose
  #    (le langage C++ ne fixe pas le résultat). Pour la comparaison, la version
  #    ordinateur reçoit donc une copie du fichier où cette conversion imite la puce.
  mkdir -p "$OUT/patched/elements/dsp"
  sed 's|static_cast<uint32_t>(fm \* 2147483648.0f)|arm_u32(fm * 2147483648.0f)|; s|^namespace elements {|static inline uint32_t arm_u32(float x) { return x >= 4294967296.0f ? 0xffffffffu : (x > 0.0f ? static_cast<uint32_t>(x) : 0u); }\nnamespace elements {|' \
      $FW/elements/dsp/ominous_voice.h > "$OUT/patched/elements/dsp/ominous_voice.h"
  grep -q arm_u32 "$OUT/patched/elements/dsp/ominous_voice.h" || { echo "  copie du mode Ominous : remplacement introuvable"; fail=1; }
  g++ -O2 -w -ffp-contract=off -DTEST -I"$OUT/patched" -I$FW elements_dsp.cc $DSP -x c native_io.c -lm -o "$OUT/elements_native" && "$OUT/elements_native" > "$OUT/elements.native"
  build_elements elements_nofma "-ffp-contract=off" && check "emulateur = ordinateur, bit pour bit" "$OUT/elements.native" "$OUT/elements_nofma.out"
  # 3. Pour information : écart de son entre le firmware (avec fusion) et l'ordinateur.
  python3 compare_audio.py "$OUT/elements.native" "$OUT/elements.out"
  grep -o "[0-9.]* M instr/s.*168 MHz" "$OUT/elements.stat" | sed 's/^/  vitesse de cet ordinateur, sans optimisation : /'
fi
if [ "${1:-}" = enregistrer ] && [ $fail = 0 ] && [ $HAVE_QEMU = 1 ]; then
  mkdir -p prebuilt
  cp "$OUT/arith-O2.bin" prebuilt/arith.bin;     cp "$OUT/arith.ref" prebuilt/arith.ref
  cp "$OUT/flags.bin" prebuilt/flags.bin;        cp "$OUT/flags.ref" prebuilt/flags.ref
  cp "$OUT/fp.bin" prebuilt/fp.bin;              cp "$OUT/fp.ref" prebuilt/fp.ref
  cp "$OUT/irq-O2.bin" prebuilt/irq.bin;         cp "$OUT/irq-O2.ref" prebuilt/irq.ref
  cp "$OUT/elements.bin" prebuilt/elements.bin;  cp "$OUT/elements.qemu" prebuilt/elements.ref
  # Le même programme placé à l'adresse de la flash d'un STM32, comme le vrai firmware :
  # c'est lui qui sert à mesurer la vitesse de l'émulateur.
  arm-none-eabi-g++ $ARCH -fno-common $FWFLAGS -I$FW -Istub elements_dsp.cc $DSP -x c startup.c ${LDFLAGS/FLASH_ORIGIN=0/FLASH_ORIGIN=0x08000000} -lm -o "$OUT/elements_flash.elf" > /dev/null 2>&1 &&
    arm-none-eabi-objcopy -O binary "$OUT/elements_flash.elf" prebuilt/elements_flash.bin
  echo "prebuilt/ mis a jour"
fi
[ $fail = 0 ] && echo "TOUT EST CONFORME" || echo "DES ECARTS SUBSISTENT"
exit $fail
