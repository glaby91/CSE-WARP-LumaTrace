# Expérience — reflet texturé déformé sur l’eau

**Branche :** `experiment/reflet-texture-eau`  
**Statut :** protocole expérimental à implémenter ; aucune modification du moteur ni de la branche `main`.

## But

Évaluer une méthode hybride peu coûteuse pour rendre le reflet d’une fresque sur un plan d’eau ondulé :

1. Quelques rayons de repérage identifient les correspondances entre la fresque, la surface réfléchissante et l’image.
2. Une texture de reflet est produite par projection/rasterisation.
3. Le shader de l’eau déforme légèrement les coordonnées de cette texture en fonction des ondulations.
4. Des rayons BVH supplémentaires restent disponibles pour vérifier les occultations et échantillonner les zones difficiles.

L’objectif est de comparer le coût et l’apparence à un reflet calculé par rayons, sans supposer d’avance que l’approximation sera plus rapide ou suffisamment fidèle.

## Scène minimale

- Une fresque plane, texturée et visible depuis la caméra.
- Un bassin rectangulaire avec un maillage d’eau.
- Deux réglages de surface : eau plane, puis ondulations animées.
- Une caméra fixe, puis une caméra en mouvement.
- Un objet occultant entre la fresque et une partie du bassin pour tester les limites de la projection.

## Méthode candidate

### 1. Texture de reflet de référence

Produire une texture de la fresque dans l’espace de réflexion du bassin. Pour un bassin plan, la projection de base peut être obtenue par la transformation miroir de la caméra par rapport au plan d’eau, puis une passe raster classique avec test de profondeur.

La texture doit rester ancrée dans l’espace du bassin, et non dans l’écran, afin d’éviter qu’elle glisse lorsque la caméra bouge.

### 2. Repérage par rayons clairsemés

Lancer une grille clairsemée de rayons depuis la caméra vers les zones visibles du bassin. Les rayons permettent de :

- confirmer les surfaces d’eau visibles ;
- repérer les limites de la fresque reflétée ;
- détecter les occultations et les écarts de projection ;
- signaler les tuiles d’image où un échantillonnage plus dense est utile.

Le nombre de rayons est contrôlé par la résolution des tuiles et par les changements de scène. Il ne s’agit pas d’un rayon unique par objet, ni d’un rayon par pixel.

### 3. Déformation sur l’eau ondulée

Dans le shader de l’eau, calculer les UV du reflet à partir de la projection de base, puis appliquer un décalage UV lié aux normales ou à une fonction d’ondes. Le maillage d’eau peut onduler séparément : sa géométrie donne le relief et les normales, tandis que le décalage UV donne le mouvement du reflet.

Éviter de cumuler une forte déformation géométrique et un fort décalage UV. La déformation doit être réglable et rester stable dans l’espace de la scène.

### 4. Raffinement local

Quand les rayons clairsemés détectent un bord, une occultation ou une variation forte, subdiviser localement la tuile et densifier les échantillons. Les zones uniformes gardent une faible densité. Les résultats voisins peuvent être interpolés, avec une limite aux discontinuités de profondeur et aux silhouettes.

## Comparaisons à effectuer

Comparer, sur la même caméra et la même scène :

- texture réfléchie plane sans déformation ;
- texture réfléchie avec déformation UV ;
- méthode candidate avec repérage clairsemé et raffinement local ;
- rendu de référence par rayons, si le backend existant le permet.

Mesurer séparément :

- temps de génération/actualisation de la texture ;
- temps de la passe de repérage BVH ;
- temps shader et temps total par image ;
- stabilité lors du mouvement caméra et de l’animation de l’eau ;
- erreurs visibles aux bords, sur les occultations et sur les hautes fréquences.

## Critères de réussite

- Le reflet reste attaché au bassin pendant le mouvement caméra.
- Les ondulations déforment le reflet sans glissement ni scintillement manifeste.
- Les occultations et limites importantes sont détectées par le raffinement.
- Le temps total est inférieur au rendu de référence dans la scène de test, avec une qualité visuelle jugée acceptable.
- Les mesures et captures sont conservées avec les paramètres utilisés.

## Limites connues à vérifier

Une texture seule ne reconstitue pas les parties de la fresque cachées à la caméra principale. La projection miroir rasterisée doit donc regarder la scène depuis le point de vue réfléchi. Une méthode à rayons peut encore être nécessaire aux occultations complexes, aux bords et aux surfaces très irrégulières. Le test doit aussi vérifier si la mise à jour de la texture coûte moins cher que les rayons qu’elle remplace.

## Suite de travail

1. Ajouter une petite scène autonome de test.
2. Implémenter la projection miroir rasterisée et les paramètres d’ondulation.
3. Ajouter l’échantillonnage BVH clairsemé puis adaptatif.
4. Capturer et comparer les quatre modes ci-dessus.
5. Ne proposer une intégration à LumaTrace qu’après mesure et revue du résultat.
