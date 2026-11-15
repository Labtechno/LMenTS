#!/bin/bash
# Recompile le firmware officiel d'Elements depuis ses sources publiées, avec la
# chaîne de compilation ARM, et donne une image .bin chargeable dans LMenTS.
# Sert aux tests automatiques (GitHub) sans dépendre d'un fichier de mise à jour,
# et de point de départ pour un firmware personnel.
#
#   bash tools/build_firmware.sh <dossier de sortie>
#
# Il faut : git, make, python3, arm-none-eabi-gcc (avec newlib).
# Deux retouches, nécessaires avec les outils récents, sont faites sur la copie :
#   - resources.cc est déjà fourni : on évite de le régénérer (script en Python 2) ;
#   - le script d'édition de liens place les tables .ARM.exidx avec le code
#     (sinon l'éditeur de liens récent refuse de les superposer aux données).
# L'image obtenue n'est pas identique au fichier officiel v1.1 (autre compilateur),
# mais c'est le même programme.
set -euo pipefail
OUT="$(mkdir -p "${1:?dossier de sortie}" && cd "$1" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
EURORACK=f4cfa7697095a28d292bff758beb51deb49b5a86
git clone -q https://github.com/pichenettes/eurorack "$WORK/eurorack"
cd "$WORK/eurorack"
git checkout -q "$EURORACK"
git submodule update -q --init stmlib
touch elements/resources.cc elements/resources.h
sed -i.orig 's/-Werror //' stmlib/makefile.inc
python3 - <<'PY'
p = 'stmlib/linker_scripts/stm32f4xx_flash_application_large.ld'
s = open(p).read()
s = s.replace("    *(.rodata*)\n", "    *(.rodata*)\n    *(.ARM.exidx*) *(.ARM.extab*)\n", 1)
open(p, 'w').write(s)
PY
TOOLCHAIN="$(dirname "$(dirname "$(command -v arm-none-eabi-gcc)")")/"
make -s -f elements/makefile TOOLCHAIN_PATH="$TOOLCHAIN" RESOURCES= > "$OUT/build.log" 2>&1 || { tail -20 "$OUT/build.log"; exit 1; }
arm-none-eabi-objcopy -O binary build/elements/elements.elf "$OUT/elements-source.bin"
echo "Firmware recompilé : $OUT/elements-source.bin ($(wc -c < "$OUT/elements-source.bin") octets)"
