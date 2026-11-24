# Expérience — reflet texturé déformé sur l’eau

**Branche :** `experiment/reflet-texture-eau`  
**Statut :** approche validée pour un futur prototype, après la décomposition du moteur. Aucun code moteur n’est modifié et la branche `main` reste intacte.

## Décision retenue

Pour le reflet visible d’une fresque sur un bassin, commencer par une passe raster depuis une caméra virtuelle miroir, puis appliquer une légère déformation de texture dans le shader de l’eau. Cette méthode évite de rechercher exhaustivement les objets et leurs trajets avec des rayons : le raster et son test de profondeur déterminent déjà les objets visibles dans le reflet.

Des rayons BVH clairsemés pourront être ajoutés en appoint pour contrôler ou raffiner les bords, les occultations et les zones où la projection est incertaine. Ils ne seront pas la méthode principale.

Le prototype sera réalisé après la décomposition des modules du moteur, dans un espace de test séparé. Aucun résultat de performance n’est encore mesuré ; le temps réel reste un objectif à vérifier sur la machine cible.

## But du test

Évaluer le coût et l’apparence d’un reflet texturé sur un plan d’eau ondulé, et le comparer à un rendu de référence par rayons si le backend existant le permet.

## Scène minimale

- Une fresque plane, texturée et visible depuis la caméra.
- Un bassin rectangulaire avec un maillage d’eau.
- Deux réglages de surface : eau plane, puis ondulations animées.
- Une caméra fixe, puis une caméra en mouvement.
- Un objet occultant entre la fresque et une partie du bassin pour tester les limites de la projection.

## Méthode candidate

### 1. Passe raster miroir

Créer une caméra virtuelle, symétrique par rapport au plan du bassin, puis rasteriser dans une texture les objets visibles depuis cette caméra. Utiliser un test de profondeur pour gérer les objets qui se cachent les uns les autres.

La texture doit rester ancrée dans l’espace du bassin, et non dans l’écran, afin d’éviter qu’elle glisse lorsque la caméra bouge. La passe peut être limitée aux objets susceptibles d’apparaître dans le reflet et calculée à résolution réduite si nécessaire.

### 2. Déformation sur l’eau ondulée

Dans le shader de l’eau, calculer les UV du reflet à partir de la projection de base, puis appliquer un décalage UV lié aux normales ou à une fonction d’ondes. Le maillage d’eau peut onduler séparément : sa géométrie donne le relief et les normales, tandis que le décalage UV donne le mouvement du reflet.

Éviter de cumuler une forte déformation géométrique et un fort décalage UV. La déformation doit être réglable et rester stable dans l’espace de la scène.

### 3. Rayons BVH en appoint

Si nécessaire, lancer une grille clairsemée de rayons dans les zones visibles du bassin pour :

- contrôler les limites de la fresque reflétée ;
- détecter les occultations et écarts de projection ;
- signaler les tuiles d’image où un échantillonnage plus dense est utile.

Subdiviser localement les tuiles qui montrent une forte variation. Les zones uniformes gardent une faible densité. Ce mécanisme demeure optionnel tant que le rendu raster suffit.

### 4. Budget temps réel

Mesurer séparément le coût de la passe miroir, du shader d’eau et du raffinement BVH. Si nécessaire, réduire la résolution de la texture, ne rasteriser que les objets visibles dans le bassin ou actualiser la texture moins souvent lorsque la scène change peu.

## Comparaisons à effectuer

Comparer, sur la même caméra et la même scène :

- texture miroir plane sans déformation ;
- texture miroir avec déformation UV ;
- texture miroir avec raffinement BVH clairsemé ;
- rendu de référence par rayons, si le backend existant le permet.

Mesurer séparément :

- temps de génération/actualisation de la texture ;
- temps de la passe de repérage BVH, si elle est activée ;
- temps shader et temps total par image ;
- stabilité lors du mouvement caméra et de l’animation de l’eau ;
- erreurs visibles aux bords, sur les occultations et sur les hautes fréquences.

## Critères de réussite

- Le reflet reste attaché au bassin pendant le mouvement caméra.
- Les ondulations déforment le reflet sans glissement ni scintillement manifeste.
- Les occultations restent cohérentes ou sont identifiées par le raffinement.
- Le temps total est inférieur au rendu de référence dans la scène de test, avec une qualité visuelle jugée acceptable.
- Les mesures et captures sont conservées avec les paramètres utilisés.

## Limites connues à vérifier

Une texture seule ne reconstitue pas les parties de la scène absentes depuis le point de vue réfléchi. La caméra miroir doit donc rasteriser la scène depuis ce point de vue. Les occultations complexes, les bords et les surfaces très irrégulières peuvent nécessiter un raffinement. Le test doit vérifier si la passe miroir coûte moins cher que la méthode par rayons qu’elle remplace.

## Suite de travail

1. Terminer la décomposition des modules du moteur.
2. Ajouter une petite scène autonome de test, sans intégrer le prototype au moteur principal.
3. Implémenter la passe miroir rasterisée et les paramètres d’ondulation.
4. Ajouter le raffinement BVH clairsemé uniquement si le test le justifie.
5. Capturer et comparer les modes ci-dessus, puis décider d’une intégration sur la base des mesures.
