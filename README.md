# CSE-WARP-LumaTrace

Prototype expérimental de rendu DirectX 11 / HLSL autour de l'éclairage évalué par triangle.

## Piste étudiée

- Pré-passe Z pour identifier la géométrie visible.
- Échantillonnage lumineux par triangle avec un petit nombre de rayons.
- Lissage de l'éclairage entre triangles voisins.
- Calculs GPU via HLSL, avec BVH préparé côté CPU si nécessaire.

Ce dépôt démarre comme espace de travail de conception ; aucune intégration au moteur CSE WARP n'est encore réalisée.
