# Tests sonores du boîtier : par personas

But : chercher dans toutes les directions ce qui peut rater dans le son — se réveiller, entendre, couper,
comprendre, s'entendre soi-même, jouer, se taire — avec des gens et des lieux variés, pas seulement nos réglages.

Chaque scénario dit **ce qu'on vérifie** et **comment** : `sim` (simulateur tools/sim, pistes synthétiques),
`dictée` (envoi réel à la dictée), `boîtier` (sur le vrai boîtier), `serveur` (tests unitaires).

## Les personas

| # | Persona | Lieu, situation | Ce qui peut casser |
|---|---|---|---|
| P1 | Jeanne, 84 ans, voix faible, appareil auditif | salon, télé forte | réveil manqué, phrase coupée, réponse trop rapide ou trop faible |
| P2 | Léo, 7 ans, voix aiguë, parle vite | chambre | mot d'éveil mal reconnu (voix d'enfant), dictée fantaisiste |
| P3 | Karim, accent marqué, français seconde langue | cuisine, hotte allumée | dictée en mauvaise langue, bruit continu |
| P4 | Sophie, cherche ses mots, longues pauses « euh… » | bureau | coupée en pleine réflexion |
| P5 | Marc, parle fort, dans une grande pièce qui résonne | entrée carrelée | écho, fin de phrase mal vue |
| P6 | Deux personnes qui parlent en même temps | repas de famille | mauvaise voix gardée, transcription mêlée |
| P7 | Une réunion de 6 personnes, 1 h 30 | salle de réunion | enregistrement long : coupures, qui parle, fin propre |
| P8 | Paul en voiture, partage de connexion du téléphone | voiture, moteur, vitres ouvertes | Wi-Fi qui coupe, bruit de roulement |
| P9 | Une musicienne qui joue du piano | salon | réveil sur des notes, écoute qui ne finit pas |
| P10 | Un chien qui aboie, un bébé qui pleure | maison animée | faux réveils, bruits pris pour de la voix |
| P11 | Quelqu'un qui chuchote la nuit | chambre, 2 h | réveil manqué, réponse trop forte |
| P12 | Un farceur : « mésanges », « mais sans », « mes anges » | partout | faux réveils sur des mots voisins |
| P13 | Le boîtier lui-même : radio, réponse, sonnerie | partout | se réveille sur sa propre voix, se coupe lui-même |
| P14 | Deux boîtiers dans la même maison | salon + cuisine | les deux se réveillent, se répondent |
| P15 | Coupure de courant, réseau qui tombe | n'importe quand | grand bruit au redémarrage, phrase perdue sans le dire |

## Les scénarios

### Se réveiller
1. P1, P2, P11 : le mot dit doucement, fort, vite, chuchoté, avec un accent — taux de réveil sur 50 essais chacun. `boîtier` (enregistrements réels), puis `sim` une fois les enregistrements gardés.
2. P12 : 100 mots voisins du mot d'éveil, et 1 h de télé, radio, conversation — faux réveils par heure. `boîtier`.
3. P9, P10 : piano, aboiements, vaisselle, rire — faux réveils. `boîtier`.
4. P13 : le boîtier joue sa voix, la radio, ses sons — il ne doit jamais se réveiller seul ; une personne qui dit le mot par-dessus doit passer (après l'annulation d'écho). `boîtier`.
5. P14 : deux boîtiers entendent le mot — un seul répond (le plus proche). `serveur` + `boîtier`.

### Entendre jusqu'au bout
6. P4 : pauses de 0,5 à 3 s au milieu d'une phrase — jamais coupée sous 2,5 s. `sim` (déjà : 240 pistes).
7. P1 + télé, P3 + hotte, P8 + moteur : la fin de phrase trouvée malgré le bruit continu. `sim` (ajouter hotte, moteur).
8. P5 : réverbération forte (pièce carrelée). `sim` (ajouter une réponse impulsionnelle).
9. P11 : voix chuchotée à 1 m, puis à 3 m. `sim`.
10. Phrase de 4 min 59 et de 5 min 01 : la première entière, la seconde coupée proprement et dite. `serveur`.

### Comprendre
11. P2, P3 : dictée sur voix d'enfant et accent ; langue détectée juste. `dictée`.
12. P6 : deux voix — la personne qui a parlé au boîtier est gardée. `dictée` (pistes mêlées, déjà essayé).
13. Silence, souffle, bruit seul : la dictée n'invente rien (« Sous-titres par… »). `dictée` + `serveur`.
14. La dictée ne répond pas, ou lentement : réessai tout seul, puis la personne prévenue en mots. `serveur` (fait).

### Enregistrer longtemps
15. P7 : 1 h 30 avec 6 voix — tout transcrit, les voix séparées, rien perdu aux coupures de 5 min. `dictée` (piste synthétique longue).
16. P7 : le réseau coupe 30 s au milieu — l'enregistrement reprend, la transcription le dit. `boîtier`.
17. Arrêt par le bouton, par le mot, par la limite de 4 h. `boîtier` + `serveur`.

### Jouer et se taire
18. P13 : la voix par-dessus la radio, la radio baisse en fondu et remonte. `serveur` (fait) + `boîtier` (à l'oreille).
19. P1, P11 : volume adapté à l'heure (plus bas la nuit) et à la personne. `boîtier`.
20. P15 : débrancher et rebrancher pendant une phrase, la radio, un enregistrement — aucun grand bruit, rien de brouillé. `boîtier` (garde anti-bruit faite).
21. Deux messages qui arrivent ensemble — le second attend la fin du premier. `boîtier` (fait).

### Signes (sons et lumières)
22. Chaque état — écoute, compris, fin d'écoute, message, enregistrement, hors ligne, mise à jour — reconnu par P1 et P2 sans explication. `boîtier` (à l'œil et à l'oreille).

## Ordre proposé
1. Garder de vrais enregistrements (avec accord) : ils nourrissent le simulateur et l'entraînement du mot d'éveil.
2. Annulation d'écho (scénarios 4, 18).
3. Ajouter au simulateur : hotte, moteur, réverbération, chuchotement (7 à 9).
4. Mesurer les faux réveils sur 1 h de télé et de mots voisins (2, 3, 12).
