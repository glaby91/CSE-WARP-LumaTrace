# LumaTrace — note de conception

**Statut :** prototype intégré à `CSE-WARP-LumaTrace` ; le parcours BVH4 est implémenté en HLSL compute et doit être mesuré sur matériel Windows réel.

**Socle retenu :** Qt 6 pour l'application et le chargement/contrôle de scène ; Direct3D 11 pour le Z-prepass, les buffers et les compute shaders HLSL (`cs_5_0`). La compilation et les vérifications de conformité se feront dans GitHub Actions sous Windows, comme pour le moteur d'origine. Les tests WARP valident la compilation et le comportement reproductible, mais ne prédisent pas les performances d'une carte graphique réelle.

**Périmètre Git :** le build V17.551 est le run 551 du commit de la branche `codex/v17.523-full-vulkan-shipping` dans `moteur3d`; ce code reste une référence en lecture seule. Les seules copies et adaptations seront faites dans `CSE-WARP-LumaTrace`, branche `DX11LightTracing`, puis compilées depuis ce dépôt.

## Objectif

Explorer un éclairage hybride qui conserve le raster classique pour la visibilité et réserve le lancer de rayons à l'estimation de la lumière. L'idée est de calculer une valeur d'éclairage par triangle visible, avec très peu de rayons par triangle, puis de lisser ces valeurs entre triangles voisins.

Le gain attendu est une baisse du nombre de rayons par rapport à un lancer par pixel. C'est une hypothèse à mesurer : le nombre de triangles visibles peut lui-même être élevé, et un petit nombre de rayons ne garantit pas un résultat stable ou sans bruit.

## Chaîne de rendu proposée

1. **Préparer la scène.** Conserver les positions, normales, UV, indices et matériaux des objets. Les textures ne sont pas remplacées par un rendu « non fini » : elles fournissent les propriétés de surface nécessaires au calcul.
2. **Faire une pré-passe Z.** Rasteriser la géométrie pour connaître la profondeur visible et les triangles qui contribuent effectivement à l'image.
3. **Construire les données d'intersection.** Commencer par un BVH CPU pour la scène statique ou peu mobile. La traversal peut ensuite être étudiée en HLSL compute. DirectX 11 ne fournit pas l'API matérielle de ray tracing de DXR ; le parcours des rayons doit donc être implémenté et mesuré.
4. **Choisir des points d'échantillonnage.** Pour chaque triangle visible, utiliser d'abord son centroïde et sa normale. Évaluer ensuite plusieurs points ou échantillons stratifiés pour les triangles grands ou fortement éclairés.
5. **Lancer quelques rayons.** Tester 1 à 4 rayons par triangle comme point de départ, vers des directions d'éclairage ou un hémisphère d'échantillonnage. Les rayons servent à déterminer visibilité, occlusion et contribution lumineuse, pas à reconstruire la géométrie.
6. **Évaluer le matériau.** Lire les textures par les UV au point d'intersection et utiliser au minimum la couleur diffuse. Ajouter progressivement rugosité, métallicité et normales si la stabilité le permet. Il n'est donc pas nécessaire de précalculer une image texturée avant le lancer des rayons, mais les données de matériau et les UV doivent être disponibles.
7. **Filtrer l'éclairage.** Interpoler entre triangles voisins pour réduire les variations visibles. Le filtrage doit respecter les arêtes, les normales, la profondeur et les ruptures de matériau afin d'éviter les fuites de lumière.
8. **Composer l'image.** Combiner l'éclairage estimé avec les couleurs de surface et le résultat de visibilité de la pré-passe, puis comparer au rendu raster de référence.

## Algorithme détaillé

### Entrées et ressources

- Géométrie en espace monde : sommets, triangles, normales, UV, identifiant de matériau et identifiant d'objet.
- Matériaux : textures de base-color/albédo, paramètres PBR disponibles, émission éventuelle et règles de transparence.
- Caméra, profondeur de la pré-passe, lumières directes, environnement et graine d'échantillonnage déterministe.
- BVH CPU pour l'intersection de référence ; buffers D3D11 du BVH et des triangles pour le compute shader.

### Pseudocode par image

```text
si sceneRevision a changé :
    charger/valider la scène avec Qt
    convertir les meshes en triangles monde + matériaux + UV
    construire BVH CPU ; convertir les nœuds, triangles et indices en buffers D3D11

rasteriser le Z-prepass dans depth
rasteriser les identifiants de primitive dans primitiveId, avec le même test de profondeur
marquer les triangles visibles puis compacter leur liste (append/scan GPU)

pour chaque triangle visible en compute :
    choisir 1, 2 ou 4 points barycentriques déterministes
    pour chaque point :
        échantillonner la visibilité des lumières directes par shadow rays
        échantillonner l'hémisphère pour la contribution indirecte si activée
        traverser le BVH ; à l'impact, interpoler normale et UV
        évaluer albédo et paramètres du matériau, puis accumuler la radiance
    écrire l'irradiance et la confiance/bruit dans triangleLighting

filtrer triangleLighting entre triangles adjacents compatibles
rasteriser la scène finale : texture PBR par pixel × éclairage du triangle
appliquer transparence, post-traitement et sortie Qt selon le chemin existant
```

Le Z-prepass ne donne pas à lui seul la liste des primitives visibles. Le prototype doit attacher une cible d'identifiant de primitive à la passe de profondeur (ou utiliser une passe d'identification équivalente), puis dédupliquer les identifiants. Un buffer de profondeur seul ne permet pas d'associer sans ambiguïté chaque échantillon au triangle à éclairer.

### Estimation par échantillon

Pour un point de surface `x`, une normale `n` et une direction tirée `w`, le prototype accumule une estimation Monte-Carlo de forme :

```text
L(x,n) = L_direct(x,n) + (1/N) * somme_i[ visible_i * f_r(x,wi,wo) * L_hit_i * cos(theta_i) / pdf(wi) ]
```

`N` vaut d'abord 1, 2 ou 4. Le terme direct est comparé en deux variantes : ombrage analytique raster existant conservé + shadow rays, ou calcul ray-based contrôlé. L'indirect commence en un seul rebond et reste une option ; son coût et son bruit seront mesurés séparément. Échantillonner la texture au point d'impact utilise les UV interpolées du triangle touché. Pour la couleur visible, la texture reste évaluée par pixel dans la passe de composition afin de conserver les détails fins même si l'éclairage est constant par triangle.

### Détection des triangles visibles

- La passe d'identification écrit l'index de primitive gagnant dans la profondeur.
- Un kernel compute lit les identifiants à l'écran, ignore le fond, et marque `visible[primitiveId]` avec un `InterlockedOr` ou un append buffer.
- Un compactage produit les seuls triangles à traiter. Le coût du marquage/compactage est chronométré ; le prototype peut aussi comparer une variante simple « un thread par triangle » qui teste un indicateur de visibilité.
- MSAA doit être traité explicitement : premier jalon sans MSAA, puis règle de résolution multi-échantillon documentée.

### Filtrage sans fuite

Construire l'adjacence triangle-triangle à la création du BVH/scène. Le poids d'interpolation combine proximité spatiale, normale, profondeur, matériau, objet et visibilité. Les arêtes disjointes ou les ruptures de matériau coupent le filtrage. Ajouter ensuite accumulation temporelle, avec invalidation au changement de caméra, scène, lumière ou matériau, et reprojection validée séparément.

## HLSL, DirectX 11 et CPU

- Le constructeur CPU produit d'abord un BVH binaire par SAH à 16 groupes, avec des feuilles de 6 triangles, puis aplatit les branches en nœuds BVH4. La construction reste une opération de chargement de scène.
- Chaque nœud BVH4 regroupe jusqu'à quatre boîtes enfant dans un seul buffer structuré. Le shader teste les enfants, visite les plus proches d'abord, et conserve la recherche de l'intersection la plus proche pour les réflexions.
- Les rayons d'ombre utilisent une recherche au premier obstacle. Si la pile GPU fixe est pleine, le shader bascule sur un parcours exact de secours de tous les triangles afin de ne perdre aucune branche.
- Le BVH4 reste un parcours logiciel sur compute shader Direct3D 11 ; il n'utilise pas les unités matérielles DXR. La simulation CPU de la Porsche a réduit le nombre de lectures de nœuds d'environ 55 % face au BVH binaire SAH testé, avec davantage de tests de boîtes ; cette mesure n'est pas un gain de temps GPU.
- Utiliser des passes raster HLSL pour la pré-passe Z et la composition.
- Prototyper le calcul par triangle avec un compute shader HLSL et des buffers structurés pour les triangles visibles, les matériaux et les résultats lumineux.
- Construire d'abord le BVH côté CPU pour valider les données et disposer d'une référence. Un parcours CPU complet peut servir de mode de comparaison ou de secours, mais ses performances doivent être mesurées séparément.
- Ne pas supposer que le compute shader sera plus rapide : mesurer le coût de construction/mise à jour du BVH, le parcours, les transferts, le filtrage et la composition.
- Garder Qt comme couche d'interface, de chargement de manifeste et de gestion de cycle de vie ; ne pas créer un second moteur de fenêtre ou un second backend graphique.
- Garder le rendu raster de référence et le mode expérimental derrière une option Qt réversible. En cas de shader, de format UAV ou de feature level non pris en charge, revenir au rendu raster et journaliser la cause.
- GitHub Actions Windows/Qt/MSVC compile le programme et les shaders, exécute les tests déterministes et publie les journaux. Le temps GPU sous WARP n'est pas un benchmark matériel ; les performances HD 620 et GPU de bureau se mesurent sur machines réelles.

## Réutilisation depuis le moteur d'origine

L'audit initial des sources est consigné dans [`REUSE_AUDIT_MOTEUR3D.md`](REUSE_AUDIT_MOTEUR3D.md). La direction est de réutiliser les contrats et les modules isolés, pas de recopier tout le rendu V14/V17 dans ce dépôt.

## Tessellation et niveau de détail

Une tessellation plus fine près de la caméra peut améliorer la résolution spatiale de l'éclairage par triangle. Elle augmente aussi le nombre de triangles, les données du BVH et potentiellement le nombre total de rayons. Il faudra comparer une géométrie fixe à une tessellation adaptative, avec seuils et hystérésis pour éviter les changements visibles de niveau de détail.

La tessellation ne crée pas à elle seule de détail géométrique pertinent : les tests devront distinguer une meilleure distribution d'échantillons d'un véritable ajout de forme ou de déplacement.

## Risques et questions à vérifier

- Le nombre de rayons peut rester important si beaucoup de triangles sont visibles.
- Un rayon par centroïde peut manquer des ombres, des petites sources, des détails de matériau et les variations sur un grand triangle.
- La moyenne entre triangles peut lisser les détails mais aussi brouiller les silhouettes et les ombres.
- Les rayons secondaires nécessitent une origine décalée pour éviter l'auto-intersection.
- Les objets dynamiques imposent une stratégie de mise à jour du BVH.
- Transparence, matériaux spéculaires, éclairage indirect et animation sont hors du premier prototype.
- Il faudra comparer le coût et la qualité à un éclairage raster seul, sur la même caméra et à résolution égale.

## Plan de prototype

1. Construire une scène contrôlée avec une voiture, un sol, une lumière et quelques objets d'occlusion.
2. Ajouter un rendu de référence raster et vérifier la pré-passe Z.
3. Implémenter une intersection rayon-triangle CPU et valider le BVH sur quelques rayons connus.
4. Calculer l'éclairage par triangle en modes 1, 2 et 4 rayons, sans filtrage puis avec filtrage.
5. Porter le parcours validé vers HLSL compute et comparer CPU/GPU.
6. Mesurer image par seconde, temps par passe, nombre de triangles visibles, nombre de rayons, bruit et erreurs visuelles.
7. Tester ensuite plusieurs distances, niveaux de tessellation et matériaux.

## Modèle de test de la session

Deux GLB du même véhicule sont fournis dans cette session pour isoler l'effet de la résolution des textures. Leur géométrie est identique : **241 260 triangles chacun**. La variante nommée `textures_1k` fait environ 22 MB avec des textures majoritairement 1024 px ; `textures_2k_4k` fait environ 74 MB et comprend des textures jusqu'à 4096 px. Ce ne sont donc pas deux niveaux de détail géométrique.

Voir [la fiche et l'attribution de la Porsche 930](../assets/vehicles/porsche_930/README.md).

