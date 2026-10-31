# Sources audio de référence (boîtier Gambeli)

Ce que le boîtier peut jouer à la demande (`voicebox-listen` : un flux ; `voicebox-play` : un son d'une minute au plus),
et où le trouver. Licences : on ne prend que du **non contaminant** — domaine public, CC0, CC-BY (citer l'auteur),
ou une licence d'usage libre sans obligation de partager le reste. **Jamais** CC-BY-SA, CC-NC (non commercial), GPL pour du contenu.
Les licences sont à revérifier au moment d'intégrer un élément.

## Radios : l'annuaire

**radio-browser.info** — annuaire ouvert (données en domaine public), ~50 000 stations du monde, avec l'adresse
directe du flux, le format et le débit, et un drapeau « cassé ». C'est la référence à interroger :

    https://de1.api.radio-browser.info/json/stations/byname/<nom>?hidebroken=true
    https://de1.api.radio-browser.info/json/stations/search?countrycode=FR&order=clickcount&reverse=true&limit=25&hidebroken=true

Prendre `url_resolved`. Formats lus par le serveur (ffmpeg) : MP3, AAC/AAC+, Ogg, HLS (.m3u8), listes .m3u/.pls.

## Radios : repères vérifiés (31-10-2026, via l'annuaire)

| Station | Pays | Format | Adresse |
|---|---|---|---|
| France Inter | FR | AAC 192 | http://direct.franceinter.fr/live/franceinter-hifi.aac |
| France Info | FR | AAC 192 | http://icecast.radiofrance.fr/franceinfo-hifi.aac |
| France Culture | FR | AAC 192 | http://icecast.radiofrance.fr/franceculture-hifi.aac |
| FIP | FR | AAC 192 | http://icecast.radiofrance.fr/fip-hifi.aac |
| Radio Classique | FR | MP3 | https://radioclassique.ice.infomaniak.ch/radioclassique-high.mp3 |
| RTL | FR | AAC 64 | (adresse longue : chercher « RTL » dans l'annuaire) |
| Europe 1 | FR | AAC 128 | https://stream.europe1.fr/europe1.aac |
| RMC | FR | MP3 128 | https://audio.bfmtv.com/rmcradio_128.mp3 |
| Nostalgie | FR | AAC 320 | (chercher « Nostalgie » : adresse à jeton) |
| Jazz Radio | FR | MP3 192 | http://jazzradio.ice.infomaniak.ch/jazzradio-high.mp3 |
| Radio Nova | FR | MP3 128 | http://novazz.ice.infomaniak.ch/novazz-128.mp3 |
| OUI FM | FR | MP3 128 | https://ouifm.ice.infomaniak.ch/ouifm-high.mp3 |
| RFI Afrique | FR | MP3 64 | http://live02.rfi.fr/rfiafrique-64.mp3 |
| Rai Radio 1 | IT | MP3 256 | http://icestreaming.rai.it/1.mp3 |
| Cadena SER | ES | MP3 128 | http://playerservices.streamtheworld.com/api/livestream-redirect/CADENASER.mp3 |
| VRT Radio 1 | BE | MP3 | http://icecast.vrtcdn.be/radio1-high.mp3 |
| Radio Swiss Jazz | CH | MP3 128 | http://stream.srg-ssr.ch/m/rsj/mp3_128 |
| Radio Paradise | US | AAC 320 | http://stream-uk1.radioparadise.com/aac-320 |
| NHK World Radio | JP | HLS | https://nhkworld-radio.nhkworld.jp/hls/live/nhkworld-radio-media2/index.m3u8 |

Les grandes radios changent parfois d'adresse : en cas d'échec, reprendre l'adresse dans l'annuaire.

## Bruitages (alertes, aboiement, sonnette…)

| Source | Licence | Remarque |
|---|---|---|
| Kenney.nl (packs audio) | CC0 | petits sons d'interface, clics, alertes |
| Freesound.org | filtrer **CC0** (ou CC-BY) | des centaines de milliers de sons ; API avec clé gratuite |
| OpenGameArt.org | filtrer CC0 / CC-BY | bruitages variés |
| Wikimedia Commons | domaine public / CC-BY | cris d'animaux, sons historiques |
| Pixabay Sounds | licence Pixabay | usage libre, pas de revente du son seul |
| Mixkit | licence Mixkit | usage libre ; ne pas redistribuer en banque |
| À éviter | BBC Sound Effects (non commercial) | |

## Musique libre

| Source | Licence | Remarque |
|---|---|---|
| Musopen | domaine public / CC0 | musique classique enregistrée |
| Internet Archive (78 tours, etc.) | domaine public | anciens enregistrements |
| Free Music Archive | filtrer CC0 / CC-BY | tous genres |
| Incompetech (Kevin MacLeod) | CC-BY 4.0 | citer l'auteur |
| ccMixter | surtout CC-BY | vérifier chaque morceau (certains NC) |
| À éviter | Jamendo (souvent NC), CC-BY-SA | |

## Faire des sons soi-même (bibliothèques)

| Outil | Licence | Usage |
|---|---|---|
| ffmpeg (programme à part) | LGPL/GPL, appelé comme programme : non contaminant | lire, convertir, mixer, générer des tonalités |
| Tone.js | MIT | synthèse dans le navigateur |
| jsfxr / sfxr | libre (Unlicense / MIT selon le portage) | bips et effets de jeu |
