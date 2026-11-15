#!/bin/bash
# Recompile le firmware officiel d'Elements depuis ses sources publiées, avec la
# chaîne de compilation ARM, et donne une image .bin chargeable dans LMenTS.
# Sert aux tests automatiques (GitHub) sans dépendre d'un fichier de mise à jour,
# et de point de départ pour un firmware personnel.
#
#   bash tools/build_firmware.sh <dossier de sortie> [nombre de modes]
#
# Nombre de modes : nombre de résonances que le résonateur calcule (52 dans le
# firmware officiel, 64 au maximum). L'auteur note dans le code que 56 tourne
# « extrêmement juste » : c'est un moyen simple de fabriquer un firmware plus
# lourd, pour essayer la jauge et la surcharge. Fichier obtenu :
# elements-source.bin (52) ou elements-<N>modes.bin.
#
# Il faut : git, make, python3, arm-none-eabi-gcc (avec newlib).
# Deux retouches, nécessaires avec les outils récents, sont faites sur la copie :
#   - resources.cc est déjà fourni : on évite de le régénérer (script en Python 2) ;
#   - le script d'édition de liens place les tables .ARM.exidx avec le code
#     (sinon l'éditeur de liens récent refuse de les superposer aux données).
# L'image obtenue n'est pas identique au fichier officiel v1.1 (autre compilateur),
# mais c'est le même programme.
set -euo pipefail
MODES="${2:-52}"
OUT="$(mkdir -p "${1:?dossier de sortie}" && cd "$1" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
EURORACK=f4cfa7697095a28d292bff758beb51deb49b5a86
git clone -q https://github.com/pichenettes/eurorack "$WORK/eurorack"
cd "$WORK/eurorack"
git checkout -q "$EURORACK"
git submodule update -q --init stmlib
touch elements/resources.cc elements/resources.h
if [ "$MODES" != 52 ]; then
  sed -i.orig "s/resonator_.set_resolution(52);/resonator_.set_resolution($MODES);/" elements/dsp/voice.cc
  grep -q "set_resolution($MODES)" elements/dsp/voice.cc || { echo "nombre de modes : remplacement introuvable"; exit 1; }
fi
sed -i.orig 's/-Werror //' stmlib/makefile.inc
python3 - <<'PY'
p = 'stmlib/linker_scripts/stm32f4xx_flash_application_large.ld'
s = open(p).read()
s = s.replace("    *(.rodata*)\n", "    *(.rodata*)\n    *(.ARM.exidx*) *(.ARM.extab*)\n", 1)
open(p, 'w').write(s)
PY
TOOLCHAIN="$(dirname "$(dirname "$(command -v arm-none-eabi-gcc)")")/"
make -s -f elements/makefile TOOLCHAIN_PATH="$TOOLCHAIN" RESOURCES= > "$OUT/build.log" 2>&1 || { tail -20 "$OUT/build.log"; exit 1; }
NAME=elements-source.bin
[ "$MODES" != 52 ] && NAME="elements-${MODES}modes.bin"
arm-none-eabi-objcopy -O binary build/elements/elements.elf "$OUT/$NAME"
echo "Firmware recompilé ($MODES modes) : $OUT/$NAME ($(wc -c < "$OUT/$NAME") octets)"
