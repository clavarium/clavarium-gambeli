Tu vas entraîner, seul et jusqu'au bout, le modèle de mot de réveil « Mésange » (version 4) de mon boîtier vocal, dans Google Colab, sur mon compte Google déjà connecté dans ce Chrome. Je ne serai pas là : ne me pose pas de question sauf dans les cas « STOP » plus bas. Le travail n'est fini que quand le fichier `mesange-v4.zip` est téléchargé sur cet ordinateur.

## Ce qu'il te faut

- Le carnet (notebook) : https://colab.research.google.com/github/clavarium/clavarium-gambeli/blob/next/training/mesange/mesange-entrainement-v4.ipynb
- L'export des faux réveils du boîtier (zip, facultatif) : LIEN_NEGATIFS

## Mise en route

1. Ouvre le carnet dans Colab. Si le lien du carnet commence par `https://raw.githubusercontent.com/UTILISATEUR/DEPOT/BRANCHE/chemin.ipynb`, ouvre `https://colab.research.google.com/github/UTILISATEUR/DEPOT/blob/BRANCHE/chemin.ipynb`. Si ça ne marche pas, télécharge le fichier `.ipynb` avec Chrome, puis dans Colab : *Fichier › Importer un notebook › Importer* et choisis-le. Ensuite, *Fichier › Enregistrer une copie dans Drive*, et travaille dans cette copie.
2. *Exécution › Modifier le type d'exécution* : choisis **GPU T4**, puis *Enregistrer*.
3. Cellule 3 : remplace `NEGATIFS = ''` par `NEGATIFS = 'LIEN_NEGATIFS'` (le lien de l'export ci-dessus). S'il n'y a pas de lien, laisse `''`. Si la cellule 3 n'arrive pas à télécharger ce lien (page de connexion, erreur 403 ou 404) : télécharge le zip avec Chrome, glisse-le dans le panneau *Fichiers* de Colab (icône dossier à gauche, dans `/content`), renomme-le `negatifs.zip`, remets `NEGATIFS = ''` et relance la cellule 3.
4. *Exécution › Tout exécuter*. Quand Colab demande l'accès à Google Drive (cellule 3), accepte : c'est mon propre Drive, et ça sert à reprendre après une coupure. C'est la seule autorisation que tu peux donner.
5. La première fois, la cellule 2 installe les bibliothèques puis **redémarre la session d'elle-même** (« Votre session a planté » ou « redémarrée ») : c'est prévu. Relance alors *Exécution › Tout exécuter* : la cellule 2 affiche « Déjà installé » et la suite démarre.

Il y a 13 cellules numérotées (« # 1. » à « # 13. »). Compte environ 4 heures : les voix de synthèse ≈ 1 h (cellule 4 : chaque exemple est écouté par Whisper ; beaucoup de « refusés », surtout pour upmc, c'est prévu), les données ≈ 30 min, l'entraînement ≈ 2 h 40 (la cellule 11 affiche « ETAPE n sur 15000 gardee » toutes les 500 étapes), l'évaluation ≈ 10 min.

## La boucle (applique-la jusqu'au bout)

Répète jusqu'à ce que la cellule 13 ait fini et que `mesange-v4.zip` soit téléchargé :

1. Regarde la cellule en cours toutes les 5 à 10 minutes. Garde l'onglet Colab ouvert et visible. Ne ferme pas Chrome.
2. Quand une cellule se termine, lis sa sortie. Une coche verte sans message « Error », « Traceback » ou « ATTENTION » : c'est bon, laisse continuer.
3. Si une cellule est en erreur :
   a. Lis le traceback en entier, en partant de la dernière ligne.
   b. Fais la correction la plus petite possible dans **cette** cellule. Par exemple : changer une version épinglée dans `PAQUETS` (cellule 2) ; ajouter un `pip install` manquant ; corriger un chemin ; mettre une autre adresse miroir pour un téléchargement ; attendre 2 minutes puis relancer après une erreur réseau, 429 ou 5xx.
   c. Relance cette cellule, puis *Exécution › Exécuter les cellules suivantes* (ou Ctrl+F10).
   d. Au plus **5 essais par cellule**. Note chaque correction (cellule, erreur, ce que tu as changé) pour le rapport final.
4. Si l'erreur dit « Killed », « ^C » ou parle de mémoire (RAM), ou si la session plante pendant la cellule 11 : *Exécution › Redémarrer la session*, puis *Tout exécuter*. Tout reprend : les étapes déjà faites sont sautées, et l'entraînement repart de sa dernière étape gardée. Si ça plante encore au même endroit, dans la cellule 10, passe `"batch_size": 128` à `64` et note-le.
5. Si Colab se déconnecte (« Connexion perdue », « Reconnecter », session expirée, machine réinitialisée) : clique sur *Reconnecter*, vérifie que c'est toujours **GPU T4**, puis *Exécution › Tout exécuter*. S'il faut tout réinstaller, la cellule 2 redémarre de nouveau la session : relance *Tout exécuter* encore une fois. Les voix, les bruits et l'entraînement sont gardés sur Drive (dossier `gambeli-mesange-v4`) et repris tout seuls.
6. Si la cellule 2 dit « pas de GPU » : mets **GPU T4** (point 2 de la mise en route) et relance tout.

## Règles fermes

- **Ne saute jamais une étape en silence.** Ne mets pas en commentaire une cellule ou un bout de code pour la faire passer. Ne réduis pas les données, le nombre d'étapes (`ETAPES = 15000`) ou les sources sans le noter dans le rapport.
- **Ne modifie jamais l'évaluation.** La cellule 12 ne se change pas. Dans la cellule 1, les fonctions `caracteristiques`, `ModeleBoitier`, `scores_flux`, `sommes_fenetre`, `meilleur_score`, `note_clip`, `reveille`, `reveils`, `tableau_metriques` et `seuil_conseille` ne se changent pas non plus. Garde les seuils 0,90 à 0,99. Si l'erreur vient de la cellule 12, tu peux corriger seulement un chemin ou une bibliothèque manquante, et tu le dis dans le rapport.
- Ne change ni le mot (`MOT`), ni les voix (`VOIX`), ni `DITES` ou `VOISINS`, ni la cellule 6 (les exemples du mot). Dans la cellule 1, ne change pas non plus `synthese`, `Ecoute`, `dit_le_mot` ni `rogne` : ce sont eux qui vérifient que les voix disent bien le mot.
- Si la cellule 4 s'arrête sur « ne sait pas dire » ou « trop de refus » : n'ajoute pas de voix et ne relâche pas le contrôle ; c'est un cas STOP.
- Tu peux corriger les cellules 2 à 11 et 13 pour qu'elles tournent : versions, chemins, paquets, adresses de téléchargement.

## Ce que tu ne fais PAS

- Pas de changement sur mon compte Google ni dans ses réglages. Pas d'autre autorisation que l'accès de Colab à Google Drive.
- Rien de payant : pas de Colab Pro, pas d'unités de calcul, pas de carte bancaire, pas d'essai gratuit à activer.
- Pas de partage : ni le carnet, ni le Drive, ni les fichiers. Rien de publié, aucun courriel envoyé.
- Ne supprime rien dans mon Drive en dehors du dossier `gambeli-mesange-v4`.
- Ne saisis aucun mot de passe et aucun code.

## STOP : arrête-toi et préviens-moi si…

- Google demande de se reconnecter, un mot de passe ou une validation en deux étapes.
- Colab refuse le GPU (quota atteint, « Impossible de se connecter à un GPU ») : réessaie une fois après 30 minutes. Si c'est encore refusé, arrête et préviens-moi (n'achète rien).
- La cellule 4 s'arrête sur « ne sait pas dire » ou « trop de refus » (donne-moi sa sortie).
- Une même cellule a échoué 5 fois malgré tes corrections. Dans ce cas, donne-moi la dernière erreur en entier et ce que tu as essayé.
- Il s'est écoulé plus de 8 heures depuis le début.

## À la fin

1. La cellule 13 télécharge `mesange-v4.zip`. Il contient mesange.tflite, mesange.json, metriques-v4.json et training_parameters.yaml. Ouvre `chrome://downloads` et relève le chemin complet du fichier sur l'ordinateur. Si le téléchargement n'est pas parti, va dans le panneau *Fichiers* de Colab, fais un clic droit sur `mesange-v4.zip`, puis *Télécharger*. Une copie est aussi sur Drive : `Mon Drive/gambeli-mesange-v4/mesange-v4.zip`.
2. Écris-moi un rapport court :
   - où est le zip : le chemin sur l'ordinateur, et Drive ;
   - le tableau de la cellule 13, recopié tel quel. Il donne, pour chaque seuil de 0,90 à 0,99, les réveils par heure sur la parole française et sur le boîtier, et le rappel (propre, bruit, vrais appels) ;
   - le seuil conseillé, et les heures de parole évaluées (dans `metriques-v4.json`, champ `heures`) ;
   - la durée totale, et le nombre de déconnexions ou de redémarrages ;
   - les lignes « gardés sur … ; refusés par voix » de la cellule 4 ;
   - chaque correction faite (cellule, erreur, changement), ou « aucune » ; si tu as dû toucher la cellule 1, dis-le aussi ;
   - tout écart aux règles ci-dessus, ou « aucun ».
