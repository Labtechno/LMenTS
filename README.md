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
| 2 | Périphériques de la puce, carte du module, plugin : le firmware démarre, les potards, les CV, les LED et le son marchent | fait (version d'essai) |
| 3 | Mémoire séparée par firmware, firmwares personnels, finitions du chargement | à faire |
| 4 | Temps d'accès mémoire, jauge d'occupation du processeur fiable, craquements en cas de surcharge | à faire |
| 5 | Optimisation, installateur définitif | à faire |

## Installer la version d'essai

1. Page **Releases** du dépôt : télécharge **LMenTS.dmg** et ouvre-le.
2. Double-clique sur **Installer LMenTS**. La première fois, macOS refuse (LMenTS
   n'est pas signé par Apple) : **Réglages Système > Confidentialité et sécurité >
   Ouvrir quand même**.
3. Relance Reason : LMenTS est dans **Plugins**, fabricant **Labtechno**.
4. Dans le plugin : **Charger un firmware…**, puis le fichier de mise à jour
   officiel du module (`elements_1.1.wav`), ou un `.bin`. On peut aussi déposer le
   fichier sur le plugin. Le firmware est ensuite gardé dans le projet Reason.

Aucun firmware n'est fourni avec le plugin.

## Le plugin dans Reason

- **Façade** : les 27 potards du module, le bouton PLAY et ses trois voyants. Tous
  les potards et le bouton PLAY sont des paramètres automatisables.
- **Sorties** : OUT L et OUT R, en stéréo. Avec SPACE au minimum, comme sur le
  module, OUT L porte le son de l'excitateur seul et OUT R celui du résonateur seul.
- **Entrées audio** : l'entrée 1 du plugin est l'EXT IN rouge (côté BLOW),
  l'entrée 2 l'EXT IN turquoise (côté STRIKE). Dans Reason, elles sont à l'arrière
  du boîtier du plugin (touche Tab).
- **Interface MIDI → CV** (panneau de droite) : les notes jouent sur V/OCT
  (0 V = note 36) et sur GATE. Chacune des 12 autres prises CV peut recevoir
  la molette, la vélocité, l'aftertouch ou une valeur d'automation ; chaque prise a
  un paramètre automatisable, donc pilotable par le CV Programmer de Reason.
- **Accord** : avec COARSE à sa position par défaut, la note jouée au clavier est
  la note entendue (vérifié : la 3 = 220,0 Hz).
- **Jauges** : occupation de la puce du module (estimation) et part d'un cœur de
  ton Mac utilisée par l'émulation.

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

## Ce qui est vérifié à l'étape 2

| Essai | Résultat |
|---|---|
| Fichier officiel `elements_1.1.wav` : décodage | 1 664 paquets, toutes les sommes de contrôle justes |
| Démarrage du firmware officiel v1.1 | horloge réglée à 168 MHz, son à 32 001,2 Hz, codec réglé, réglages d'usine écrits en mémoire puis relus |
| Accord par V/OCT | une octave par volt (écart < 0,2 %), note attendue au réglage d'usine (277,4 Hz pour 277,2 Hz) |
| Bouton PLAY, entrée GATE, voyants | une note, les voyants suivent le son |
| EXT IN | un son envoyé fait sonner le résonateur |
| Plugin : notes MIDI, stéréo, CV, enregistrement et relecture du projet, hôte à 44,1 / 48 / 96 kHz | tout marche |
| Firmware recompilé depuis ses sources (outil `tools/build_firmware.sh`) | démarre et joue juste, comme l'officiel |

### Ce qui n'est pas encore vérifié

- **Rien n'a été essayé dans Reason** ni sur ton Mac : les essais du plugin
  tournent sur GitHub, sans logiciel hôte.
- **Rien n'a été comparé à un vrai module.**
- **La jauge de la puce du module** affiche environ 98 % : d'après les durées
  d'instructions de la documentation, le firmware occupe presque toute la puce.
  L'auteur du firmware note lui-même que le réglage choisi tourne « très juste ».
  Le temps d'accès à la mémoire flash n'est pas encore compté : la jauge reste une
  estimation jusqu'à l'étape 4.
- **La vitesse sur un M4 Max** : environ 85 à 100 % d'un cœur du Mac M1 de GitHub
  par instance (voir l'onglet Actions). Un M4 Max est nettement plus rapide par
  cœur, mais ce n'est pas mesuré.
- **L'électronique analogique** (étages d'entrée, convertisseur audio, sortie) est
  idéale : les valeurs viennent des réglages d'usine du firmware, pas des schémas.

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

GitHub lance tout cela à chaque envoi, puis fabrique et essaie le plugin et
son installateur sur un Mac (onglet **Actions**).

## Organisation

| Dossier | Contenu |
|---|---|
| `Source/emu` | la puce virtuelle : processeur (`CortexM4.*`), calcul à virgule (`CortexM4Fpu.inc`, `SoftFloat.h`), carte du module (`ElementsMachine.*`), lecture des firmwares (`FirmwareFile.*`) |
| `Source/hw` | la façade : potards, prises et étages d'entrée (`Panel.h`) |
| `Source` | le plugin (`PluginProcessor.*`, `PluginEditor.*`) et la conversion de fréquence (`engine/Resampler.h`) |
| `tools` | essais du processeur (`armtests`), du module (`machine_test.cpp`), du plugin (`plugin_test.cpp`), recompilation du firmware (`build_firmware.sh`) |
| `installer` | fabrication de l'installateur Mac |
| `firmware` | code source du firmware d'Elements, pour les tests et comme référence (voir `firmware/README.md`) |

## Licences

- Code du projet : licence MIT (voir `LICENSE`). Aucun code GPL.
- Firmware d'Elements et bibliothèque stmlib : Emilie Gillet, licence MIT.
- L'émulateur du processeur vient du projet Tresses (même auteur, licence MIT).
