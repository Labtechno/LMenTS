#!/usr/bin/env python3
"""Extrait le firmware contenu dans un fichier .wav de mise à jour d'Elements.

    python3 firmware_wav.py elements_1.1.wav elements_1.1.bin

Le .wav est le programme du module transformé en son, pour être joué dans son
entrée audio. Codage (celui du « bootloader audio » des modules à puce STM32F4) :
porteuse à 6 kHz dont la phase change à chaque symbole (QPSK, 2 bits par symbole,
6 000 symboles par seconde), paquets de 256 octets précédés d'une amorce et
suivis d'une somme de contrôle CRC-32.

Le fichier obtenu est l'image du programme telle qu'elle est écrite dans la puce
à partir de l'adresse 0x08008000.

Outil de vérification (étape 2). Le chargement des .wav depuis le plugin arrive
à l'étape 3. Écrit pour LMenTS, licence MIT.
"""
import math, struct, sys, wave, zlib

PACKET = 256
PREAMBLE = [2, 1, 2, 1] * 4 + [3, 0, 3, 0] * 4      # 0x99 x 4 puis 0xcc x 4, en symboles

def decode(path):
    w = wave.open(path, "rb")
    if w.getnchannels() != 1 or w.getsampwidth() != 2:
        raise SystemExit("fichier inattendu : il faut un .wav mono 16 bits")
    rate = w.getframerate()
    carrier, symbols_per_second = 6000, 6000
    n = rate // symbols_per_second                   # échantillons par symbole
    if rate % carrier or rate % symbols_per_second:
        raise SystemExit("fréquence d'échantillonnage inattendue : %d Hz" % rate)
    raw = w.readframes(w.getnframes())
    samples = struct.unpack("<%dh" % (len(raw) // 2), raw)
    # Début du signal : la porteuse démarre après un silence, phase nulle.
    start = next((i for i, s in enumerate(samples) if abs(s) > 1000), None)
    if start is None:
        raise SystemExit("le fichier est silencieux")
    start -= start % n
    sin = [math.sin(2 * math.pi * carrier * k / rate) for k in range(n)]
    cos = [math.cos(2 * math.pi * carrier * k / rate) for k in range(n)]
    symbols = bytearray()
    for i in range(start, len(samples) - n + 1, n):
        q = sum(samples[i + k] * sin[k] for k in range(n))
        c = sum(samples[i + k] * cos[k] for k in range(n))
        symbols.append((1 if q > 0 else 0) | (2 if c > 0 else 0))
    pattern = bytes(PREAMBLE)
    data, packets, bad, pos = bytearray(), 0, 0, 0
    while True:
        pos = symbols.find(pattern, pos)
        if pos < 0 or pos + len(pattern) + 4 * (PACKET + 4) > len(symbols):
            break
        pos += len(pattern)
        body = bytearray()
        for k in range(PACKET + 4):
            s = symbols[pos + 4 * k: pos + 4 * k + 4]
            body.append((s[0] << 6) | (s[1] << 4) | (s[2] << 2) | s[3])
        crc = struct.unpack(">I", bytes(body[PACKET:]))[0]
        if zlib.crc32(bytes(body[:PACKET])) & 0xffffffff == crc:
            data += body[:PACKET]
            packets += 1
            pos += 4 * (PACKET + 4)
        else:
            bad += 1
    return bytes(data), packets, bad

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    image, packets, bad = decode(sys.argv[1])
    if not packets or bad:
        raise SystemExit("échec : %d paquets lus, %d sommes de contrôle fausses" % (packets, bad))
    image = image.rstrip(b"\xff")
    image += b"\xff" * (-len(image) % 4)
    open(sys.argv[2], "wb").write(image)
    print("%d paquets, toutes les sommes de contrôle sont justes ; %d octets écrits dans %s" % (packets, len(image), sys.argv[2]))
