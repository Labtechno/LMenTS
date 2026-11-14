# Code source du firmware d'Elements

`eurorack/elements` et `eurorack/stmlib` sont des copies du code publié par
Mutable Instruments (auteur : Emilie Gillet), sous licence MIT :

- https://github.com/pichenettes/eurorack, commit `f4cfa7697095a28d292bff758beb51deb49b5a86`
- https://github.com/pichenettes/stmlib, commit `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec`

Le texte de la licence est en tête de chaque fichier, et dans `eurorack/stmlib/LICENSE`.

Ce code ne fait pas partie de l'émulateur. Il sert :

- aux tests (`tools/armtests`) : la synthèse d'Elements est compilée pour la puce
  et pour l'ordinateur, et les résultats sont comparés ;
- de référence pour émuler la carte du module (pilotes dans `elements/drivers`).

## Ce qui a été retiré, et pourquoi

Le projet ne contient aucun fichier sous licence GPL. Quatre éléments du dépôt
d'origine l'étaient, ou étaient inutiles ici :

| Retiré | Raison |
|---|---|
| `elements/test/elements_test.cc` | licence GPL |
| `elements/resources/*.py` (dont `samples.py`) | licence GPL ; ce sont les scripts qui fabriquent `resources.cc`, déjà présent |
| `stmlib/ui/event_queue.h` | licence GPL |
| `elements/hardware_design`, `elements/samples` | schémas et sons bruts, inutiles pour le plugin |

À savoir : `elements/ui.h` (interface du module, licence MIT) inclut
`stmlib/ui/event_queue.h` (GPL). Le firmware officiel contient donc ce petit
morceau de code GPL. Cela ne concerne pas l'émulateur, qui ne contient aucun code
du firmware, mais comptera au moment de décider si le plugin est livré avec un
fichier de firmware ou non (étape 3).
