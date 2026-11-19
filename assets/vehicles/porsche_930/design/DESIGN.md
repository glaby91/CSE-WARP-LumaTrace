# Porsche 930 — rouge rubis

La carrosserie reprend le rouge rubis brillant de la référence approuvée. Le motif latéral associe une bande diagonale graphite, un filet argenté et un liseré clair fin. Pneus, vitrages, chromes, feux et pièces noires conservent leurs matériaux glTF.

## Intégration dans LumaTrace

- Le traitement vise le matériau glTF nommé `paint`.
- Couleur et motif sont calculés dans le pixel shader comme albédo : ils reçoivent l'éclairage direct et le reflet spéculaire, sans émission lumineuse.
- La finition est métallique (0,74) et lisse (rugosité 0,17).
- Le matériau GPU utilisé par la passe de rayons reprend la même couleur, pour conserver des reflets cohérents.
- Une seule source directe large et chaude éclaire la scène. Aucun rebond d'éclairage indirect n'est ajouté.

Les bandes sont définies dans l'espace monde du véhicule pour éviter de dépendre des îlots UV. Contrôler leur placement sur les captures Windows WARP avant tout ajustement de coordonnées.
