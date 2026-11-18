# CSE-WARP-LumaTrace

Prototype expérimental de rendu DirectX 11 / HLSL autour de l'éclairage évalué par triangle.

## État

Ce dépôt est un espace de conception et de prototypage. La piste décrite ici n'est pas encore intégrée au moteur CSE WARP.

## Documents

- [Note de conception : éclairage par triangle avec lancer de rayons](docs/CONCEPT_LUMATRACE.md)
- [Audit de réutilisation du moteur CSE WARP](docs/REUSE_AUDIT_MOTEUR3D.md)
- [Modèle de test Porsche 911 (930) Turbo](assets/vehicles/porsche_930/README.md)

## Intention

Étudier un rendu hybride DirectX 11 : une pré-passe Z classique identifie la visibilité, puis un petit nombre de rayons estime l'éclairage de triangles visibles. Les propositions, hypothèses, limites et essais sont consignés dans la note de conception avant toute intégration au moteur.
