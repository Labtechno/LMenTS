# LMenTS

Plugin VST3 pour Reason (Mac) : le **vrai firmware** du module Eurorack **Elements**
tourne dans une **émulation de sa puce** (STM32F405, processeur Cortex-M4F).
Ce n'est pas une imitation du son : c'est le programme du module, exécuté
instruction par instruction.

Projet indépendant, sans lien avec le fabricant du module.

## Où en est le projet

| Étape | Contenu | État |
|---|---|---|
| 1 | Processeur Cortex-M4F émulé, avec ses tests | fait |
| 2 | Périphériques de la puce, carte du module, plugin : le firmware démarre, les potards, les CV, les LED et le son marchent | à faire |
| 3 | Chargement de fichiers de firmware (`.wav`, `.bin`) depuis le plugin, réglages sauvés dans le projet Reason | à faire |
| 4 | Temps d'accès mémoire, jauge d'occupation du processeur, craquements en cas de surcharge | à faire |
| 5 | Fichier officiel v1.1, optimisation, installateur `.dmg` | à faire |

Il n'y a donc **pas encore de plugin à installer**.

## Ce qui est vérifié à l'étape 1

Les tests sont dans `tools/armtests`. Un même programme, compilé pour la puce, est
exécuté par l'émulateur et par une référence ; les résultats doivent être identiques.

| Test | Référence | Résultat |
|---|---|---|
| 677 instructions entières et DSP, environ 5 000 jeux de valeurs chacune | QEMU | identique |
| 105 instructions de calcul à virgule, environ 58 000 jeux de valeurs chacune (4 modes d'arrondi, nombres limites, NaN, infinis) | QEMU | identique |
| Interruptions, y compris la sauvegarde des registres à virgule | QEMU | identique |
| Bibliothèque C et calcul à virgule, à 5 niveaux d'optimisation | le même code sur l'ordinateur | identique |
| Synthèse d'Elements (code du firmware, options de compilation du firmware) : 10 réglages, 20 secondes de son | QEMU | identique, bit pour bit |
| La même synthèse, comparée à l'ordinateur | le même code sur l'ordinateur | identique, bit pour bit (voir la note) |

QEMU ne sert que de référence pendant les tests : il n'y a aucun code de QEMU
dans le projet.

**Note sur la comparaison avec l'ordinateur.** Deux précautions sont nécessaires,
et elles montrent pourquoi émuler la puce vaut mieux que recompiler le code :

- le compilateur du firmware fusionne des multiplications et des additions (un seul
  arrondi au lieu de deux). Un ordinateur ne le fait pas aux mêmes endroits. Pour
  cette comparaison, les deux côtés sont compilés sans fusion. Avec la fusion du
  vrai firmware, l'écart de son reste minuscule (de 51 à 121 dB sous le signal) ;
- le mode caché « Ominous » convertit un nombre négatif en entier non signé. La
  puce donne 0, un ordinateur donne autre chose. La version ordinateur du test
  imite donc la puce sur cette ligne. Sans cela, le son d'« Ominous » recompilé
  pour un ordinateur est nettement différent de celui du module.

### Ce qui n'est pas encore vérifié

- **Rien n'a été comparé à un vrai module.** Les références sont QEMU et le code
  source du firmware.
- **Les durées des instructions** (nombre de cycles d'horloge) viennent de la
  documentation du Cortex-M4. Elles serviront à la jauge de l'étape 4 et restent
  une estimation.
- **Le temps d'accès à la mémoire flash** du STM32F405 n'est pas encore modélisé.
- **La vitesse sur Mac** : voir l'onglet Actions (tests sur un Mac Apple Silicon
  de GitHub). Aucune mesure sur un M4 Max pour l'instant.

## Refaire les tests

Sur Linux, avec `gcc-arm-none-eabi`, `libnewlib-arm-none-eabi`,
`libstdc++-arm-none-eabi-newlib` et `qemu-system-arm` :

```
bash tools/armtests/run_tests.sh
```

Sur Mac, sans rien installer d'autre que les outils de développement d'Apple (les
programmes de test sont fournis déjà compilés, avec leurs résultats de référence) :

```
bash tools/armtests/run_prebuilt.sh
```

GitHub lance les deux à chaque envoi (onglet **Actions**).

## Organisation

| Dossier | Contenu |
|---|---|
| `Source/emu` | la puce virtuelle : processeur (`CortexM4.*`), calcul à virgule (`CortexM4Fpu.cpp`, `SoftFloat.h`) |
| `tools` | banc d'essai du processeur (`emu_run.cpp`) et tests (`armtests`) |
| `firmware` | code source du firmware d'Elements, pour les tests et comme référence (voir `firmware/README.md`) |

## Licences

- Code du projet : licence MIT (voir `LICENSE`). Aucun code GPL.
- Firmware d'Elements et bibliothèque stmlib : Emilie Gillet, licence MIT.
- L'émulateur du processeur vient du projet Tresses (même auteur, licence MIT).
