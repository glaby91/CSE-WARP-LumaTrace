# Chargeur de scènes LumaTrace

Le chargeur reprend le principe transactionnel vérifié dans V17.551 : une scène en cours n'est remplacée que si le manifeste, le modèle, la géométrie, les matériaux et toutes les textures ont été chargés et validés.

## Manifeste `lumatrace-scene-v1`

Le chargeur ouvre les objets `gltf2-model` déclarés dans `objects`. Les chemins du modèle et des textures doivent rester sous le dossier de la scène. Le modèle peut être au format GLB 2.0 ou glTF 2.0 avec buffers externes.

Il lit la scène glTF active et conserve les transformations hiérarchiques des nœuds, les sommets, indices, normales, UV0, tangentes, couleurs, affectations de matériaux, images et échantillonneurs. Les facteurs PBR glTF, les modes alpha et les extensions de matériaux sont stockés dans la scène chargée. Les extensions KHR non encore interprétées par le shader restent disponibles sous forme JSON dans chaque matériau.

## Contrat de rendu

- Chaque primitive est un triangle-list indexé, associée à un matériau.
- Les positions restent dans l'espace local glTF ; `worldTransform` porte la transformation cumulative du nœud.
- Les matériaux exposent couleur RGBA, métallicité, rugosité, émission, normal scale, occlusion strength, textures et mode alpha.
- Les textures sont décodées en `QImage`, avec leur chemin source et leur sampler glTF.
- Les extensions glTF `KHR_materials_specular`, `KHR_materials_clearcoat` et `KHR_materials_transmission` sont conservées intégralement dans `Material::extensions`; le shader devra les interpréter pour reproduire ces effets.
- Les limites et erreurs sont retournées dans `LoadResult`; aucun résultat partiel ne devient la scène active.

## Limites actuelles

Le chargeur rejette les accessors clairsemés, les primitives compressées, les morph targets et les modes glTF autres que les triangles. Le GLB Porsche préparé n'utilise pas ces fonctions. La scène d'essai contient 75 primitives triangulaires, 14 matériaux et 27 textures. Le chargeur prépare les données ; la fenêtre Qt, le rendu D3D11 et le traitement alpha/transmission seront branchés dans les étapes suivantes.

## Compilation

La cible `LumaTraceSceneLoader` requiert CMake 3.24+ et Qt 6.2+ (`Core`, `Gui`). Le code est isolé dans LumaTrace ; le dépôt `moteur3d` reste inchangé.
