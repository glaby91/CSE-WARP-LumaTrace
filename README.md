# CSE-WARP-LumaTrace

Prototype expérimental de rendu DirectX 11 / HLSL autour de l'éclairage évalué par triangle.

## État

Ce dépôt est un espace de conception et de prototypage. La piste décrite ici n'est pas encore intégrée au moteur CSE WARP.

## Documents

- [Note de conception : éclairage par triangle avec lancer de rayons](docs/CONCEPT_LUMATRACE.md)
- [Audit de réutilisation du moteur CSE WARP](docs/REUSE_AUDIT_MOTEUR3D.md)
- [Chargeur de scènes et contrat glTF](docs/SCENE_LOADER.md)
- [Fenêtre de rendu et profil HD 620](docs/RENDER_WINDOW_HD620.md)
- [Modèle de test Porsche 911 (930) Turbo](assets/vehicles/porsche_930/README.md)

## Chargeur

Le premier module C++/Qt du dépôt est `LumaTraceSceneLoader`. Il reprend le chargement transactionnel de V17.551 et charge le manifeste de la scène Porsche, le GLB 2.0, les transformations glTF, les buffers triangulaires, les attributs de sommet, les matériaux et les textures.

Au lancement, une petite fenêtre de diagnostic façon console détaille et chronomètre le chargement, la construction du BVH et chaque passe D3D11/HLSL. Elle écrit le même journal sur disque. Une seconde fenêtre Qt affiche le rendu 960 × 540 ; le premier rendu complet est exporté en PNG à côté des journaux dans le dossier de données utilisateur. Le viewport utilise la pré-passe profondeur/ID, le calcul d'ombres et de reflets par rayons HLSL parcourant le BVH, puis le raster texturé avec cartes normales. Le profil démarre à un échantillon lumineux par triangle ; la barre d'outils permet d'en choisir un, deux ou quatre. Les modules et limites du prototype sont décrits dans [la note du rendu HD 620](docs/RENDER_WINDOW_HD620.md).

## Intention

Étudier un rendu hybride DirectX 11 : une pré-passe Z classique identifie la visibilité, puis un petit nombre de rayons estime l'éclairage de triangles visibles. Les propositions, hypothèses, limites et essais sont consignés dans la note de conception avant toute intégration au moteur.
