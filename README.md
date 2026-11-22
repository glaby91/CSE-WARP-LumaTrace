# CSE-WARP-LumaTrace

Prototype expérimental de rendu DirectX 11 / HLSL autour de l'éclairage évalué par triangle.

## Piste étudiée

- Pré-passe Z pour identifier la géométrie visible.
- Échantillonnage lumineux par triangle avec un petit nombre de rayons.
- Cache de radiance par triangle visible : les surfaces déjà calculées sont réutilisées tant que les lumières et les paramètres d’échantillonnage ne changent pas ; les nouvelles surfaces visibles sont ajoutées progressivement.
- Les réflexions métalliques, dépendantes de la caméra, restent recalculées à chaque image.
- Lissage de l'éclairage entre triangles voisins.
- Calculs GPU via HLSL, avec BVH préparé côté CPU si nécessaire.

Ce dépôt démarre comme espace de travail de conception ; aucune intégration au moteur CSE WARP n'est encore réalisée.

## Ressource de livrée

Une texture de carrosserie autonome pour la Porsche 930 est préparée dans [designs/porsche_930/ruby_turquoise_orange](designs/porsche_930/ruby_turquoise_orange/). Elle cible le matériau glTF `paint` et son UV `TEXCOORD_0`. Elle n'est pas appliquée au modèle ni au rendu ; voir son README pour les paramètres de raccordement ultérieur.
