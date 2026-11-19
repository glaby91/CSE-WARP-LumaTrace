# Porsche 930 — Ruby / Turquoise / Orange

Texture de carrosserie autonome pour le modèle Porsche 930 livré avec LumaTrace. Elle n'est pas encore reliée au GLB ni activée dans l'application.

## Cible et format

- GLB glTF 2.0 d'origine, matériau d'index `9` nommé `paint`.
- Coordonnées UV `TEXCOORD_0` de ce modèle précis.
- PNG RGB 8 bits, 2048 × 2048, couleurs sRGB.
- Couleur principale rouge rubis `#9E001E`, rubans turquoise `#00B7C5`, orange `#FF8A24`, filet or `#F7C253`.

## Application ultérieure

Ajouter cette image comme `pbrMetallicRoughness.baseColorTexture` du matériau `paint`, avec `texCoord: 0`. Mettre alors `baseColorFactor` à `[1, 1, 1, 1]` afin que le facteur gris foncé d'origine ne ternisse pas les couleurs. Conserver ses facteurs métalliques, de rugosité et extensions.

Le rendu raster et le calcul des reflets par rayons lisent maintenant la texture de couleur de base lorsqu'elle est assignée. Le calcul des rayons utilise les UV interpolées au point d'impact et accepte jusqu'à 64 matériaux. Le traitement de l'alpha des surfaces touchées par les rayons reste à étudier.

## Régénération

Avec le GLB d'origine extrait du paquet Windows :

```sh
python generate_texture.py /chemin/vers/porsche_930.glb basecolor.png
```

Dépendances du script : Python 3, NumPy et Pillow. Le script génère uniquement le PNG ; il ne modifie ni le GLB, ni le code, ni la scène LumaTrace.
