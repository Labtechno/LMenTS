#!/bin/bash
# Refait les comparaisons sans compilateur ARM ni QEMU : les programmes de test
# déjà compilés (prebuilt/*.bin) sont exécutés par l'émulateur, et leurs sorties
# comparées aux résultats de référence enregistrés (prebuilt/*.ref : QEMU, ou
# l'ordinateur pour « arith »). Sert à vérifier l'émulateur sur Mac, où le calcul
# flottant de l'ordinateur n'est pas le même que sur un PC.
#
#   bash run_prebuilt.sh [compilateur et options]     ex. : bash run_prebuilt.sh clang++ -arch x86_64
set -u
cd "$(dirname "$0")"
OUT=${OUT:-/tmp/lments-prebuilt}
mkdir -p "$OUT"
CXX=("${@:-c++}")
EMU="../../Source/emu"
"${CXX[@]}" -std=c++17 -O2 -ffp-contract=off -o "$OUT/emu_run" ../emu_run.cpp $EMU/CortexM4.cpp $EMU/CortexM4Fpu.cpp || exit 1
fail=0
for t in arith flags fp irq elements; do
  "$OUT/emu_run" prebuilt/$t.bin > "$OUT/$t.out" 2> "$OUT/$t.stat"
  if [ -s "$OUT/$t.out" ] && cmp -s prebuilt/$t.ref "$OUT/$t.out"; then printf "  %-10s OK (%s lignes)\n" $t "$(wc -l < "$OUT/$t.out" | tr -d ' ')"
  else printf "  %-10s DIFFERENT\n" $t; diff prebuilt/$t.ref "$OUT/$t.out" | head -6; cat "$OUT/$t.stat"; fail=1; fi
done
# Vitesse : le test Elements calcule 20 secondes de son (10 réglages de 2 s).
"$OUT/emu_run" prebuilt/elements_flash.bin 08000000 > "$OUT/bench.out" 2> "$OUT/bench.stat"
if cmp -s prebuilt/elements.ref "$OUT/bench.out"; then
  awk '{ for (i = 1; i <= NF; ++i) if ($i == "s" && $(i+1) ~ /^\(/) { printf "  vitesse    20 s de son calculees en %.1f s : %.0f %% d un coeur de cet ordinateur\n", $(i-1), $(i-1) * 5 } }' "$OUT/bench.stat"
else
  echo "  vitesse    DIFFERENT"; fail=1
fi
[ $fail = 0 ] && echo "TOUT EST CONFORME" || echo "DES ECARTS SUBSISTENT"
exit $fail
