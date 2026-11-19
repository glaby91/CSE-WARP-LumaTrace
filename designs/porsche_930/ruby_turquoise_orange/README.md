# Porsche 930 — Ruby / Turquoise / Orange

Texture de carrosserie du modèle Porsche 930 livré avec LumaTrace. Elle est activée sur le matériau `paint` par le manifeste de scène, sans modifier le GLB source.

## Cible et format

- GLB glTF 2.0 d'origine, matériau d'index `9` nommé `paint`.
- Coordonnées UV `TEXCOORD_0` de ce modèle précis.
- PNG RGB 8 bits, 2048 × 2048, couleurs sRGB.
- Couleur principale rouge rubis `#9E001E`, rubans turquoise `#00B7C5`, orange `#FF8A24`, filet or `#F7C253`.

## Affectation au matériau

Le manifeste utilise une entrée `materialOverrides` qui nomme le matériau `paint`, charge `textures/porsche_930_ruby_turquoise_orange.png` et met `baseColorFactor` à `[1, 1, 1, 1]`. Les facteurs métalliques, la rugosité et les extensions du matériau GLB restent inchangés. L'override conserve les UV0 de la carrosserie.

Le rendu raster et le calcul des reflets par rayons lisent maintenant cette texture. Le calcul des rayons utilise les UV interpolées au point d'impact et accepte jusqu'à 64 matériaux. Le traitement de l'alpha des surfaces touchées par les rayons reste à étudier.

## Régénération

Avec le GLB d'origine extrait du paquet Windows :

```sh
python generate_texture.py /chemin/vers/porsche_930.glb basecolor.png
```

Dépendances du script : Python 3, NumPy et Pillow. Le script génère uniquement le PNG ; il ne modifie ni le GLB, ni le code, ni la scène LumaTrace.
