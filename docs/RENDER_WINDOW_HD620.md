# Fenêtre et pipeline initial — Intel HD 620

La fenêtre de rendu démarre seulement après le chargement transactionnel de la scène. Le profil initial reste volontairement modeste : cible interne 960 × 540, Direct3D 11 matériel, VSync, aucun MSAA, échantillonnage lumineux à un point par triangle. Le choix 1/2/4 points est exposé au rendu ; 4 est un mode qualité, pas le réglage de démarrage pour la HD 620.

## Ordre des passes

1. **Z-prepass et sélection** : les surfaces opaques et masquées écrivent la profondeur et un identifiant global de triangle dans une cible `R32_UINT`. Le mode alpha `BLEND` est écarté de cette passe pour ne pas cacher les surfaces visibles derrière les vitres.
2. **Marquage HLSL** : un compute shader lit la cible d'ID et marque atomiquement chaque triangle visible une seule fois.
3. **Éclairage HLSL** : un thread par triangle visible utilise 1, 2 ou 4 points de surface. Les rayons d'ombre et de réflexion parcourent le BVH construit sur CPU puis envoyé au GPU. Les rayons ne parcourent jamais toute la voiture en force brute.
4. **Raster Direct3D 11** : le pixel shader combine couleur de base, métallicité/rugosité, carte normale, occlusion et émission avec le résultat lumineux par triangle. Les surfaces `BLEND` sont dessinées ensuite avec alpha blending et profondeur sans écriture.

Les réflexions sont un résultat séparé de l'éclairage direct : le compute shader trace une direction réfléchie et récupère le facteur de couleur du matériau touché, ou un dégradé d'environnement. Les textures sont bien appliquées au matériau raster, mais ne sont pas encore échantillonnées sur les rebonds réfléchis. Les extensions glTF de spécularité, clearcoat et transmission sont conservées par le loader, sans modèle d'éclairage dédié dans ce premier pipeline.

## BVH partagé CPU/GPU

`scene_bvh.cpp` applique les transformations glTF, aplatit les primitives dans un ordre de triangles stable, puis construit un BVH médian sur le plus grand axe des centroïdes, avec six triangles au plus par feuille. Les buffers envoyés au GPU conservent le même ID global que la cible de sélection. Le CPU construit la structure ; le parcours des rayons est effectué par le compute shader HLSL.

## Modules reliés

Le backend `d3d11_scene_renderer` crée le périphérique matériel (avec repli WARP), compile les shaders HLSL au démarrage, téléverse la géométrie, les matériaux et le BVH, puis exécute les passes dans cet ordre : pré-passe profondeur/ID, marquage des triangles visibles, éclairage/rayons compute, raster texturé et présentation. Une fenêtre console Qt séparée montre les entrées/sorties chronométrées et indentées, jusqu'aux accès glTF et primitives. Les temps affichés sont des temps mur côté CPU ; les dispatchs D3D11 sont asynchrones et ne représentent pas seuls la durée GPU réelle. Le premier rendu complet est lu depuis le backbuffer et exporté en PNG ; changer le nombre d'échantillons produit un nouvel export. Le journal et l’image sont rangés sous le dossier de données utilisateur de LumaTrace, dans `logs/` et `renders/`; leurs chemins exacts sont affichés dans la fenêtre console. La barre d’outils offre 1, 2 ou 4 échantillons par triangle. Le réglage au démarrage reste à 1 pour la HD 620. Le viewport est statique pour l’instant ; les contrôles de caméra ne sont pas encore ajoutés.

Le code a été écrit sans lancer de compilation ni de test. Il reste donc à valider sur Windows avec Qt 6 et un périphérique D3D11 ; le rendu et les performances HD 620 ne sont pas confirmés à l’exécution.
