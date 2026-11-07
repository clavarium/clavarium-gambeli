# Gambeli — entraînement du mot de réveil « Mésange » : trace des modifications

Session du 31 octobre 2026 (Colab, GPU T4). Résultat : modele/mesange.tflite + modele/mesange.json (aussi dans mesange.zip).

## Contenu de ce zip
- mesange-entrainement.ipynb : le notebook corrigé, tel qu'il a tourné (sorties effacées). À rouvrir dans Colab (Fichier › Importer un notebook).
- microwakeword-data.patch : la modification faite dans le code de microWakeWord (git diff).
- modele/ : le modèle produit (mesange.tflite, mesange.json, mesange.zip, journaux d'entraînement).
- README.md : ce fichier.

## Problèmes rencontrés et corrections
1. Cellule « Downloads audio data » : TypeError 'torchcodec.decoders.AudioDecoder' object is not subscriptable.
   Les versions récentes de la bibliothèque datasets décodent l'audio avec torchcodec, absent de Colab.
   Correction : la cellule ne passe plus par le décodage de datasets ; elle lit les fichiers avec soundfile
   (ou librosa en secours) et rééchantillonne en 16 kHz avec scipy.
2. Réponses impulsionnelles MIT : les octets fournis en streaming n'étaient pas lisibles.
   Correction : téléchargement du dépôt avec huggingface_hub.snapshot_download, puis lecture fichier par fichier (270 RIR).
3. AudioSet : l'ancien fichier data/bal_train09.tar n'existe plus (erreur 404) ; le dépôt agkphysics/AudioSet est passé en parquet.
   Correction : téléchargement de data/bal_train/09.parquet (≈ 700 Mo) et extraction des 500 extraits audio en wav 16 kHz.
4. Entraînement : arrêt brutal (« ^C ») à l'étape 500, au premier passage de validation. Cause : manque de mémoire vive
   (le noyau Linux a tué le processus, ≈ 11,4 Go utilisés sur 12,7 Go). L'ensemble « validation_ambient » (découpé en fenêtres) est trop gros.
   Correction : dans microWakeWord/microwakeword/data.py, le pas de découpage des enregistrements ambiants est multiplié par 3
   (voir microwakeword-data.patch). La cellule 1 du notebook applique désormais ce correctif automatiquement après le git clone.
   Il est aussi utile de redémarrer la session avant l'entraînement pour libérer la mémoire du noyau.
5. Cellule 1 : protégée par un test sur /content/.installed, pour que « Tout exécuter » ne redémarre pas la session à chaque fois.
6. Cellule 2 : ne régénère pas les 3000 exemples s'ils existent déjà.
7. Dernière cellule : reprend MOT/NOM par défaut si la session a redémarré entre-temps.

## Résultats (test en streaming, modèle quantifié)
| seuil | mot manqué | fausses détections / heure |
|---|---|---|
| 0,99 | 26,8 % | 0,19 |
| 0,95 | 22,5 % | 0,38 |
| 0,92 | 22,2 % | 0,56 |
| 0,86 | 19,9 % | 0,75 |
Le fichier mesange.json utilise probability_cutoff = 0,9.

## Refaire le tout
1. Ouvrir le notebook dans Colab, Exécution › Modifier le type d'exécution › GPU T4.
2. Lancer la cellule 1 (la session redémarre).
3. Exécution › Tout exécuter. Compter ≈ 1 h 30. Ne pas taper dans l'onglet pendant que ça tourne.
4. À la fin, mesange.zip se télécharge (sinon : panneau Fichiers à gauche, clic droit › Télécharger).

Pistes d'amélioration : plus d'étapes (training_steps), d'autres voix ou de vrais enregistrements du mot, plus de bruits de fond.

## v3 (07-11) et v4
v3 (mesange-entrainement-v3.ipynb, gardé tel qu'il a tourné) : 0,16 réveil/h sur la parole française, mais seulement
3 % des « Mésange » propres réveillent au seuil 0,97 (26 % avec bruit). Cause : deux des cinq voix Piper ne disent pas
le mot. fr_FR-gilles-low n'a pas le signe de la nasale ɑ̃ (« Missing phoneme from id map ») et babille 4 à 11 s ;
fr_FR-mls-medium dit autre chose, quel que soit le locuteur (Whisper : « Merci, merci », « S'il vous plaît »…).
upmc ne réussit le mot qu'environ 1 fois sur 8. Les « Mésange » de la note finale venaient justement de mls et du
locuteur 0 d'upmc. Et « mes anges », mêmes sons que le mot, était appris comme contre-exemple.

v4 (construire_v4.py -> mesange-entrainement-v4.ipynb ; mesange_v4.py ; test_v4.py) : voix siwis, tom, upmc ;
chaque exemple du mot écouté par Whisper et gardé seulement s'il dit le mot ; plus de « mes anges » en contre-exemple ;
le mot seul (plus de « Mésange, dis-moi ») ; silences rognés ; hauteur de voix variée 1 fois sur 2 ; poids du mot 5
(au lieu de 2). Notation et seuils inchangés. Dossier Drive neuf (gambeli-mesange-v4) : rien n'est repris de la v3.
Limite : trois voix de synthèse seulement ; de vrais « Mésange » enregistrés sur le boîtier restent le meilleur ajout.
