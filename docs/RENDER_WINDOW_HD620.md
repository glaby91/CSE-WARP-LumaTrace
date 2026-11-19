# Fenêtre et pipeline initial — Intel HD 620

La fenêtre de rendu démarre seulement après le chargement transactionnel de la scène. Le profil initial reste volontairement modeste : cible interne 960 × 540, Direct3D 11 matériel, VSync, aucun MSAA, échantillonnage lumineux à un point par triangle. Le choix 1/2/4 points est exposé au rendu ; 4 est un mode qualité, pas le réglage de démarrage pour la HD 620. La caméra est rapprochée pour donner davantage de place à la voiture.

## Ordre des passes

1. **Z-prepass et sélection** : les surfaces opaques et masquées écrivent la profondeur et un identifiant global de triangle dans une cible `R32_UINT`. Le mode alpha `BLEND` est écarté de cette passe pour ne pas cacher les surfaces visibles derrière les vitres.
2. **Marquage HLSL** : un compute shader lit la cible d'ID et marque atomiquement chaque triangle visible une seule fois.
3. **Éclairage HLSL** : un thread par triangle visible utilise 1, 2 ou 4 points de surface. Chaque point teste une direction légèrement différente vers une source lumineuse externe étendue ; l'accumulation des ombres adoucit les contours d'ombre quand on augmente le nombre de rayons. Les rayons d'ombre et de réflexion parcourent le BVH construit sur CPU puis envoyé au GPU. Les rayons ne parcourent jamais toute la voiture en force brute.
4. **Raster Direct3D 11** : le pixel shader combine couleur de base, métallicité/rugosité, carte normale, occlusion et émission avec le résultat lumineux par triangle. Les surfaces `BLEND` sont dessinées ensuite avec alpha blending et profondeur sans écriture.

Les réflexions sont un résultat séparé de l'éclairage direct : le compute shader trace une direction réfléchie et récupère le facteur de couleur du matériau touché, ou un dégradé d'environnement. Les textures sont bien appliquées au matériau raster et leur couleur reçoit l'éclairage direct. Les cartes de texture des objets touchés ne sont pas encore échantillonnées sur les rebonds réfléchis. Les extensions glTF de spécularité, clearcoat et transmission sont conservées par le loader, sans modèle d'éclairage dédié dans ce premier pipeline.

## BVH partagé CPU/GPU

`scene_bvh.cpp` applique les transformations glTF, aplatit les primitives dans un ordre de triangles stable, puis construit un BVH médian sur le plus grand axe des centroïdes, avec six triangles au plus par feuille. Les buffers envoyés au GPU conservent le même ID global que la cible de sélection. Le CPU construit la structure ; le parcours des rayons est effectué par le compute shader HLSL.

## Modules reliés

Le backend `d3d11_scene_renderer` crée le périphérique matériel (avec repli WARP), compile les shaders HLSL au démarrage, téléverse la géométrie, les matériaux et le BVH, puis exécute les passes dans cet ordre : pré-passe profondeur/ID, marquage des triangles visibles, éclairage/rayons compute, raster texturé et présentation. Une fenêtre console Qt séparée montre les entrées/sorties chronométrées et indentées, jusqu'aux accès glTF et primitives. Les temps affichés sont des temps mur côté CPU ; les dispatchs D3D11 sont asynchrones et ne représentent pas seuls la durée GPU réelle. Le premier rendu complet est exporté sous `LumaTrace_<horodatage>_1_rayon.png`. Passer à 2 puis à 4 dans la barre d’outils déclenche les fichiers `_2_rayons.png` et `_4_rayons.png` sans écraser les versions précédentes. Le journal et les images sont rangés sous le dossier de données utilisateur de LumaTrace, dans `logs/` et `renders/`; leurs chemins exacts sont affichés dans la fenêtre console. Le réglage au démarrage reste à 1 pour la HD 620. Le viewport est statique pour l’instant ; les contrôles de caméra ne sont pas encore ajoutés.

La cible Windows a été compilée avec Qt 6.8.3, MSVC 2022 et le SDK Direct3D 11 via GitHub Actions. Le packaging Qt a également réussi. Les shaders HLSL sont compilés au démarrage de l’application : cette étape, le chargement effectif du modèle, le rendu visuel et les performances sur HD 620 restent à valider à l’exécution. Les charges utiles GLB/textures Git LFS ne sont pas présentes sur la branche distante ; l’artefact actuel confirme la compilation, mais ne constitue pas encore un paquet de scène exécutable.
