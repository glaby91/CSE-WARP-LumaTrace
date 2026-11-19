# Porsche 911 (930) Turbo, 1975 — modèle de test

Deux variantes GLB du même modèle ont été analysées pour tester l'impact des textures. Leurs métadonnées sont consignées ici ; les binaires ne sont pas encore présents dans la branche distante :

| Fichier | Taille approximative | Géométrie | Textures |
|---|---:|---:|---|
| `porsche_930_textures_1k.glb` | 22 MB | 241 260 triangles | 27 images, principalement 1024 × 1024 |
| `porsche_930_textures_2k_4k.glb` | 74 MB | 241 260 triangles | 27 images, jusqu'à 4096 × 4096 |

Le nombre de triangles est identique ; la différence de qualité provient principalement des textures. Les fichiers binaires sont des pièces jointes de travail de cette session ; seul leur descriptif et leurs métriques ont été publiés dans le dépôt distant pour l'instant.

## Source et attribution

Karol Miklas / **Lionsharp Studios**, *1975 Porsche 911 (930) Turbo* : [page du modèle](https://www.lionsharpstudios.com/portfolio/1975-porsche-911-930-turbo/) · [fiche Sketchfab](https://sketchfab.com/3d-models/free-1975-porsche-911-930-turbo-8568d9d14a994b9cae59499f0dbed21e).

La fiche du modèle indique une licence **Creative Commons Attribution**. Conserver cette attribution avec le modèle et vérifier la licence sur la fiche source avant toute redistribution ou publication du projet.

## Usage prévu

Asset de test pour le prototype LumaTrace : import, lecture des matériaux et textures, comparaison des variantes et benchmark. Ce dépôt ne prétend pas être le créateur du modèle.

## Préparation pour une scène LumaTrace

Le paquet [`Porsche_930_LumaTrace_Scene.zip`](Porsche_930_LumaTrace_Scene.zip) contient une scène avec un seul objet voiture. Le GLB de travail est la variante 1K : elle garde les 241 260 triangles, les 14 matériaux et les 27 images du modèle tout en restant plus légère que la variante 4K.

Le préparateur déplace les images embarquées vers `textures/` et réécrit leurs URI dans le GLB. Les accessors, indices, UV, normales, hiérarchie de nœuds, matériaux, modes alpha et extensions glTF restent dans les données du modèle. La scène n'ajoute ni route ni décor.

Le manifeste `scene.json` est le contrat d'import LumaTrace. Le chargeur de ce dépôt reconnaît `gltf2-model`, charge le GLB et ses images externes, et conserve les matériaux PBR et alpha pour le renderer D3D11.
