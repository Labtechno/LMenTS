#!/bin/bash
# Montre les premiers cas où l'émulateur et QEMU diffèrent pour un test flottant.
#   bash debug_fp.sh <numéro du test>   (après run_tests.sh)
# Colonnes : a, b, c, FPSCR d'entrée, résultat, FPSCR de sortie, second résultat.
cd "$(dirname "$0")"
OUT=${OUT:-/tmp/lments-armtests}
ARCH="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
arm-none-eabi-gcc $ARCH -O1 -DDEBUG_TEST=$1 fp.c "$OUT/fp.S" "$OUT/fp_table.c" startup.c -nostartfiles -T link.ld \
    --specs=nosys.specs -Wl,--defsym=FLASH_ORIGIN=0 -o "$OUT/dbg.elf" > /dev/null 2>&1 || exit 1
arm-none-eabi-objcopy -O binary "$OUT/dbg.elf" "$OUT/dbg.bin"
"$OUT/emu_run" "$OUT/dbg.bin" 2> /dev/null | paste -d' ' - - - - - - - > "$OUT/dbg.out"
timeout 600 qemu-system-arm -M mps2-an386 -nographic -monitor none -serial stdio -semihosting \
    -kernel "$OUT/dbg.elf" 2> /dev/null < /dev/null | paste -d' ' - - - - - - - > "$OUT/dbg.ref"
echo "ecarts : $(diff "$OUT/dbg.ref" "$OUT/dbg.out" | grep -c '^<') sur $(wc -l < "$OUT/dbg.ref") cas (QEMU en premier)"
diff "$OUT/dbg.ref" "$OUT/dbg.out" | grep '^[<>]' | sort -k2,5 -s | head -${2:-12}
