#!/usr/bin/env python3
"""Compare le son d'Elements calculé par la puce émulée et par l'ordinateur.

Les deux fichiers viennent de elements_dsp.cc. Le compilateur n'arrondit pas aux
mêmes endroits sur la puce et sur l'ordinateur : on n'attend pas l'identité, mais
un écart très faible. Affiche, par réglage, l'écart en décibels sous le signal.
"""
import math, struct, sys

def load(path):
    presets, name = {}, None
    for line in open(path):
        line = line.strip()
        if line.startswith("="):
            continue
        if len(line) == 8 and all(c in "0123456789abcdef" for c in line) and name:
            presets[name].append(struct.unpack("<f", struct.pack("<I", int(line, 16)))[0])
        elif line:
            name = line
            presets[name] = []
    return presets

ref, got = load(sys.argv[1]), load(sys.argv[2])
ok = True
if ref.keys() != got.keys() or not ref:
    print("  reglages differents ou absents"); sys.exit(1)
for name in ref:
    a, b = ref[name], got[name]
    if len(a) != len(b) or not a:
        print(f"  {name:10s} longueurs differentes"); ok = False; continue
    if any(math.isnan(x) or math.isinf(x) for x in a + b):
        print(f"  {name:10s} valeurs invalides"); ok = False; continue
    signal = math.sqrt(sum(x * x for x in a) / len(a))
    error = math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)) / len(a))
    if error == 0:
        print(f"  {name:10s} identique a l'ordinateur ({len(a)} echantillons)")
        continue
    db = 20 * math.log10(error / signal) if signal > 0 else 0
    print(f"  {name:10s} arrondis du firmware : ecart de {db:6.1f} dB sous le signal (pour information)")
sys.exit(0 if ok else 1)
