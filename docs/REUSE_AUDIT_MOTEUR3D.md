# Audit de réutilisation du moteur CSE WARP

**Référentiel consulté :** `glaby91/moteur3d`, branche `codex/v17.600-mega` (18 novembre 2026), avec vérification des documents de la branche `master`. La branche `v17.551` n'a pas été retrouvée parmi les branches publiées ; cet audit n'affirme donc pas avoir inspecté ce commit précis.

## Règle de travail et dépôt cible

- `moteur3d` et sa branche `v17.551` sont une **source de référence en lecture seule**. Aucun commit, correctif ou changement ne sera poussé dans ce dépôt.
- Les seuls fichiers adaptés ou développés pour l'expérience iront dans [`CSE-WARP-LumaTrace`, branche `DX11LightTracing`](https://github.com/glaby91/CSE-WARP-LumaTrace/tree/DX11LightTracing).
- Le prototype gardera Qt 6 et Direct3D 11. Sa compilation et ses validations seront rattachées à GitHub Actions dans le dépôt LumaTrace, dès qu'il contiendra le code applicatif.
- Les constats techniques ci-dessous viennent des sources accessibles sur `codex/v17.600-mega` et `master`. Ils servent à préparer la copie/adaptation ; ils ne remplacent pas la comparaison finale avec le contenu exact de `v17.551` avant le portage.

## Résumé

Le moteur d'origine fournit déjà les briques importantes : Qt 6, chargement de scènes modulaires, conversion vers une scène de triangles, BVH CPU, backend raster D3D11, et compute shader HLSL qui parcourt un BVH. **Le concept LumaTrace n'est donc pas à reconstruire depuis zéro.** Le nouveau travail porte surtout sur l'identification des triangles visibles, l'éclairage échantillonné par triangle, le filtrage entre triangles et une intégration propre au chemin D3D11 existant.

| Brique | État dans le moteur d'origine | Décision pour LumaTrace |
|---|---|---|
| Fenêtre et interface Qt 6 | Utilisées par l'application V17 | Garder Qt 6 ; ajouter un mode expérimental réversible, sans seconde fenêtre moteur. |
| Chargement modulaire de scènes | `v16::loadScene` lit et valide les manifests JSON de scène. | Réutiliser le schéma et le chargement transactionnel. |
| Construction runtime | `v16::buildRuntime` convertit les ressources en scène, triangles, matériaux et BVH. | Réutiliser/adopter l'interface `Scene`; ajouter l'import GLB ou sa conversion explicite. |
| Z-prepass D3D11 | Essai depth-only optionnel, borné aux principaux occludeurs opaques. | Réutiliser états et parcours de dessin, puis ajouter la cible d'identifiant de primitive. |
| BVH CPU | Construction médiane sur l'axe de plus grande étendue, feuilles de 6 objets ; parcours CPU proche-en-premier. | Réutiliser d'abord pour la référence CPU et la scène statique ; mesurer la qualité et le coût sur la voiture. |
| Compute D3D11/HLSL | Parcourt les buffers de nœuds, triangles, ordre, matériaux, textures et lumières ; compilation `cs_5_0`. | Réutiliser structures, validation et conventions de buffers ; remplacer/adopter le noyau pour le calcul par triangle. |
| GitHub Actions | Build Windows 2022, Qt 6.8.3/MSVC 2022 et CMake existent dans le moteur. | Reprendre la recette de build pour la cible LumaTrace ; ajouter ses tests dédiés quand le code applicatif est présent. |

## Sources et constats vérifiés

### Qt et chargement de scènes

- [`src/m3d_scene_v16.cpp`](https://github.com/glaby91/moteur3d/blob/codex/v17.600-mega/experiments/2026-10-30-cote-ete-gt/native_v14/src/m3d_scene_v16.cpp) utilise `QFile`, `QJsonDocument`, `QDir` et `QFileInfo`. Le chargement valide le format `m3d-scene-v16`, résout les ressources sous la racine de scène et rend un `LoadResult` explicite ; l'appelant ne remplace sa scène active qu'en cas de réussite.
- [`src/m3d_scene_v16.h`](https://github.com/glaby91/moteur3d/blob/codex/v17.600-mega/experiments/2026-10-30-cote-ete-gt/native_v14/src/m3d_scene_v16.h) expose `SceneDatabase`, `loadScene` et la compilation en scène de rendu.
- [`src/m3d_v16_bridge.cpp`](https://github.com/glaby91/moteur3d/blob/codex/v17.600-mega/experiments/2026-10-30-cote-ete-gt/native_v14/src/m3d_v16_bridge.cpp) transforme les ressources runtime en triangles avec attributs, matériaux et UV, puis construit un BVH.
- Le chemin `triangle_mesh` est décrit comme conservant coordonnées monde et UV et alimentant CPU/BVH/DX11. L'audit n'a pas confirmé que ce chemin importe directement un GLB binaire. Il faut donc vérifier/ajouter un importeur glTF/GLB, ou convertir le GLB Porsche en ressources de scène prises en charge.
- La documentation [`V16-NATIVE-LOADER.md`](https://github.com/glaby91/moteur3d/blob/master/docs/V16-NATIVE-LOADER.md) limite explicitement ce chargeur à son périmètre V16 ; ne pas le traiter comme un importeur universel déjà complet.

### Z-prepass

- [`src/m3d_directx11.cpp`](https://github.com/glaby91/moteur3d/blob/codex/v17.600-mega/experiments/2026-10-30-cote-ete-gt/native_v14/src/m3d_directx11.cpp) contient un essai activable par `M3D_V17_EXPERIMENT_Z_PREPASS=1`.
- L'essai dessine les principaux murs/planchers opaques en profondeur seule, sans pixel shader ni évaluation de matériau. Il est explicitement optionnel et isolé du chemin de production.
- Ce chemin donne un patron pour la gestion D3D11 du depth-stencil et les draw calls. Il **ne** construit pas une liste de triangles visibles et n'écrit pas d'identifiant de primitive. LumaTrace doit ajouter une passe/cible d'identification (avec le même test de profondeur), puis dédupliquer/compacter les ID avant le compute.
- L'optimisation du Z-prepass n'est pas le même objectif que LumaTrace : ici, le Z-prepass sert de masque de visibilité et de source d'ID pour le traitement par triangle.

### BVH et intersections

- Le constructeur `buildBvh` de [`src/m3d_v16_bridge.cpp`](https://github.com/glaby91/moteur3d/blob/codex/v17.600-mega/experiments/2026-10-30-cote-ete-gt/native_v14/src/m3d_v16_bridge.cpp) calcule les bornes des objets, choisit l'axe ayant la plus grande étendue des centroïdes, partage à la médiane avec `nth_element`, et arrête à 6 objets par feuille.
- [`src/m3d_intersection.cpp`](https://github.com/glaby91/moteur3d/blob/codex/v17.600-mega/experiments/2026-10-30-cote-ete-gt/native_v14/src/m3d_intersection.cpp) parcourt les enfants proches en premier, teste les boîtes puis les triangles et conserve des compteurs AABB/triangles. Il a aussi une voie AVX2 pour certains groupes de triangles.
- [`OPTIMISATION-BVH-261102-0006.md`](https://github.com/glaby91/moteur3d/blob/master/experiments/2026-10-30-cote-ete-gt/native_v14/docs/OPTIMISATION-BVH-261102-0006.md) décrit la préparation des arêtes/constantes barycentriques au chargement et l'usage par le parcours.
- Réutilisation recommandée : garder un même résultat d'intersection (`t`, primitive, coordonnées barycentriques), l'utiliser pour vérifier les rayons CPU, puis comparer au parcours GPU. Enregistrer le nombre de nœuds et de triangles testés avec le temps CPU/GPU.
- La construction actuelle est un bon point de départ, pas une preuve qu'elle sera optimale à 241k triangles. Mesurer la profondeur, le taux de recouvrement des boîtes, le nombre de nœuds visités et comparer éventuellement à SAH avant d'optimiser.

### D3D11 et HLSL

- [`src/m3d_compute11.cpp`](https://github.com/glaby91/moteur3d/blob/codex/v17.600-mega/experiments/2026-10-30-cote-ete-gt/native_v14/src/m3d_compute11.cpp) valide que la scène est triangulaire et possède un BVH, crée les buffers GPU des nœuds/triangles/indices/matériaux/lumières et compile le shader `cs_5_0`.
- [`m3d_raytrace_compute.hlsl`](https://github.com/glaby91/moteur3d/blob/codex/v17.600-mega/experiments/2026-10-30-cote-ete-gt/native_v14/src/shaders/m3d_raytrace_compute.hlsl) contient déjà les structures HLSL du BVH, des triangles avec normales/UV, des matériaux et des impacts barycentriques. Le shader actuel est lancé par pixel (avec chemin GI séparé) ; il n'implémente pas la liste compacte de triangles visibles suivie d'un éclairage constant par primitive.
- Le module compute existant crée son propre device/contexte et fait un readback staging vers le CPU dans ce chemin de preview. Il est donc une référence de code et de données, pas encore un plug-in direct au renderer raster : l'intégration finale devra partager le device/context D3D11 ou définir un transfert GPU explicite, et éviter un readback CPU inutile pour le rendu de chaque image.
- DXR n'est pas retenu. La cible reste D3D11 + Compute Shader 5.0, y compris le repli CPU ; pas d'exigence de matériel DXR.

### Compilation CI

- [`build-moteur3d-v17.yml`](https://github.com/glaby91/moteur3d/blob/codex/v17.600-mega/.github/workflows/build-moteur3d-v17.yml) configure CMake sous Windows 2022, installe Qt 6.8.3 pour MSVC 2022 x64 et compile les cibles V17/V16.
- [`v17/CMakeLists.txt`](https://github.com/glaby91/moteur3d/blob/codex/v17.600-mega/v17/CMakeLists.txt) requiert CMake 3.24 et Qt 6.2+ (Core, Gui, Widgets, Concurrent) ; le workflow CI fournit Qt 6.8.3.
- LumaTrace gardera CMake/Qt 6/Windows/MSVC/D3D11. Le workflow propre à ce dépôt compilera l'application et les shaders HLSL, exécutera tests CPU déterministes et tests D3D11 WARP de conformité, et archivera diagnostics. Il ne présentera pas WARP comme mesure de FPS matériel.

## Ce qu'il ne faut pas reprendre tel quel

1. Le renderer V14 complet, ses scènes codées en dur et son format binaire historique : trop couplés à des démonstrations existantes.
2. Le shader de preview par pixel comme résultat final : la nouvelle expérimentation travaille sur une liste de primitives visibles et un buffer d'éclairage par triangle.
3. Le readback staging vers une image Qt à chaque frame : garder la sortie sur GPU tant que possible.
4. Les hypothèses de performance CPU/GPU anciennes sans mesure avec la voiture, la caméra et les paramètres LumaTrace.

## Première séquence d'intégration proposée

1. Relier le chargeur Qt existant et construire une scène triangle-only contenant le GLB converti/importé.
2. Ajouter une image de référence raster DirectX 11 et vérifier la pré-passe Z.
3. Ajouter `primitiveId` au Z-prepass et produire la liste visible par compute.
4. Réutiliser le BVH et le test rayon-triangle CPU pour valider un échantillon de rayons.
5. Ajouter le compute shader par triangle, en modes 1/2/4 rayons, puis filtrer sans franchir les discontinuités.
6. Brancher le buffer d'éclairage au pixel shader de composition sans retirer le chemin raster.
7. Activer la compilation Actions Windows/Qt et les validations WARP ; réserver les benchmarks de performance à des GPU physiques.
