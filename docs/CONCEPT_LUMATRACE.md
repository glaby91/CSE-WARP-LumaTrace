# LumaTrace — note de conception

**Statut :** idée à prototyper ; aucune intégration au moteur CSE WARP n'est réalisée.

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

## HLSL, DirectX 11 et CPU

- Utiliser des passes raster HLSL pour la pré-passe Z et la composition.
- Prototyper le calcul par triangle avec un compute shader HLSL et des buffers structurés pour les triangles visibles, les matériaux et les résultats lumineux.
- Construire d'abord le BVH côté CPU pour valider les données et disposer d'une référence. Un parcours CPU complet peut servir de mode de comparaison ou de secours, mais ses performances doivent être mesurées séparément.
- Ne pas supposer que le compute shader sera plus rapide : mesurer le coût de construction/mise à jour du BVH, le parcours, les transferts, le filtrage et la composition.

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

## Modèle de test inclus

Deux GLB du même véhicule sont conservés pour isoler l'effet de la résolution des textures. Leur géométrie est identique : **241 260 triangles chacun**. La variante nommée `textures_1k` fait environ 22 MB avec des textures majoritairement 1024 px ; `textures_2k_4k` fait environ 74 MB et comprend des textures jusqu'à 4096 px. Ce ne sont donc pas deux niveaux de détail géométrique.

Voir [la fiche et l'attribution de la Porsche 930](../assets/vehicles/porsche_930/README.md).
