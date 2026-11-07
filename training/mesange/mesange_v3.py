"""Mésange v3 : les outils du carnet d'entraînement (Colab), en Python simple, testables hors Colab.

Le carnet mesange-entrainement-v3.ipynb écrit ce fichier dans /content/mesange_v3.py (cellule 1) et l'importe.
Rien ici n'a besoin du GPU ; les parties « boîtier » (score comme le firmware) ont besoin de pymicro_features
et d'un interpréteur TFLite (ai_edge_litert ou tensorflow), importés seulement quand on s'en sert.

  - patch_microwakeword(dossier) : les trois corrections du code de microWakeWord (idempotent) ;
  - charge16k(chemin) : n'importe quel fichier audio -> float32 mono 16 kHz ;
  - tenu_a_part(nom) : 1 fichier sur 5, choisi par le SHA-1 de son nom, comme le serveur (wakelearn.mjs heldOut) ;
  - lire_export(archive, dossier) : l'export des clips du boîtier (zip, tar ou dossier), trié en positifs/négatifs ;
  - Cache : les étapes coûteuses gardées sur Google Drive, reprises après une coupure ;
  - ModeleBoitier, scores_flux, reveils, meilleur_score : le modèle noté exactement comme le boîtier (main/mww.cc) ;
  - tableau_metriques, seuil_conseille : la table finale 0,90-0,99.
"""
import csv, hashlib, io, json, os, shutil, subprocess, tarfile, time, urllib.request, wave, zipfile

import numpy as np

RATE = 16000
SEUILS = [0.90, 0.91, 0.92, 0.93, 0.94, 0.95, 0.96, 0.97, 0.98, 0.99]
AUDIO_EXT = ('.wav', '.flac', '.ogg', '.mp3', '.pcm', '.raw', '.opus', '.m4a')


# ---------------------------------------------------------------- audio

def en16k(x, sr):
    from scipy.signal import resample_poly
    x = np.asarray(x, dtype=np.float32)
    if x.ndim > 1: x = x.mean(axis=1)
    if sr != RATE:
        g = np.gcd(int(sr), RATE)
        x = resample_poly(x, RATE // g, int(sr) // g).astype(np.float32)
    return x


def charge16k(chemin):
    """Un fichier audio -> float32 mono 16 kHz dans [-1, 1]. .pcm/.raw : int16 little-endian 16 kHz mono."""
    if chemin.lower().endswith(('.pcm', '.raw')):
        return np.fromfile(chemin, '<i2').astype(np.float32) / 32768
    try:
        import soundfile as sf
        x, sr = sf.read(chemin, dtype='float32', always_2d=False)
    except Exception:
        try:
            with wave.open(chemin, 'rb') as w:
                sr, n, ch, sw = w.getframerate(), w.getnframes(), w.getnchannels(), w.getsampwidth()
                assert sw == 2, f'{chemin}: {8 * sw} bits'
                x = np.frombuffer(w.readframes(n), '<i2').astype(np.float32) / 32768
                if ch > 1: x = x.reshape(-1, ch)
        except Exception:
            import librosa
            x, sr = librosa.load(chemin, sr=None, mono=True)
    return en16k(x, sr)


def ecrit16k(chemin, x):
    """float -> wav 16 kHz 16 bits mono (sans soundfile : le module wave suffit)."""
    pcm = (np.clip(np.asarray(x, np.float32), -1, 1) * 32767).astype('<i2')
    os.makedirs(os.path.dirname(chemin) or '.', exist_ok=True)
    tmp = chemin + '.part'
    with wave.open(tmp, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(pcm.tobytes())
    os.replace(tmp, chemin)


def decoupe(x, secondes=20.0, mini=1.0):
    """Coupe un long enregistrement en morceaux de `secondes` au plus (les bouts de moins de `mini` s sautent)."""
    n = int(secondes * RATE)
    return [x[k:k + n] for k in range(0, len(x), n) if len(x[k:k + n]) >= mini * RATE]


def tenu_a_part(nom):
    """1 sur 5, par le SHA-1 du nom de fichier (sans dossier) : la même règle que heldOut() du serveur."""
    return int(hashlib.sha1(os.path.basename(str(nom)).encode()).hexdigest(), 16) % 5 == 0


# ---------------------------------------------------------------- téléchargements

def telecharge(urls, dest, essais=5, attente=10):
    """Télécharge la première adresse qui répond (plusieurs essais chacune) ; reprend un fichier déjà complet."""
    if isinstance(urls, str): urls = [urls]
    if os.path.exists(dest) and os.path.getsize(dest) > 0: return dest
    os.makedirs(os.path.dirname(dest) or '.', exist_ok=True)
    erreurs = []
    for url in urls:
        for k in range(essais):
            try:
                tmp = dest + '.part'
                req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0 (mesange-v3)'})
                with urllib.request.urlopen(req, timeout=120) as r, open(tmp, 'wb') as f:
                    shutil.copyfileobj(r, f, 1 << 20)
                if os.path.getsize(tmp) == 0: raise IOError('fichier vide')
                os.replace(tmp, dest)
                return dest
            except Exception as e:
                erreurs.append(f'{url} : {e}')
                time.sleep(attente * (k + 1))
    raise IOError('téléchargement impossible :\n  ' + '\n  '.join(erreurs[-6:]))


def lien_direct(url):
    """Un lien de partage Google Drive / Dropbox -> son lien de téléchargement direct ; les autres inchangés."""
    import re
    m = re.search(r'drive\.google\.com/(?:file/d/|open\?id=|uc\?id=)([\w-]+)', url)
    if m: return f'https://drive.usercontent.google.com/download?id={m.group(1)}&export=download&confirm=t'
    if 'dropbox.com' in url: return url.replace('dl=0', 'dl=1') if 'dl=0' in url else url + ('&' if '?' in url else '?') + 'dl=1'
    return url


# ---------------------------------------------------------------- export des clips du boîtier

POSITIFS = {'real', 'missed', 'vrai', 'vrais', 'appel', 'appels', 'positive', 'positives', 'positif', 'positifs', 'wake', 'mot'}


def _etiquettes_index(fichiers):
    """index.json / index.jsonl / index.csv de l'export -> {nom de fichier: étiquette}."""
    etiq = {}
    def ajoute(e):
        if not isinstance(e, dict): return
        f = e.get('file') or e.get('fichier') or e.get('path') or e.get('name')
        lab = e.get('label') or e.get('etiquette') or e.get('kind') or e.get('type')
        if f and lab: etiq[os.path.basename(str(f))] = str(lab).lower()
    for nom, lire in fichiers:
        base = os.path.basename(nom).lower()
        if not base.startswith('index'): continue
        try:
            texte = lire().decode('utf-8', 'replace')
            if base.endswith('.json'):
                d = json.loads(texte)
                for e in (d.get('clips') or d.get('items') or d.get('fichiers') or []) if isinstance(d, dict) else d: ajoute(e)
            elif base.endswith('.jsonl'):
                for ligne in texte.splitlines():
                    if ligne.strip(): ajoute(json.loads(ligne))
            elif base.endswith('.csv'):
                for e in csv.DictReader(io.StringIO(texte)): ajoute(e)
        except Exception as e:
            print('index illisible, ignoré :', nom, e)
    return etiq


def est_positif(nom, etiq):
    lab = etiq.get(os.path.basename(nom))
    if lab is not None: return lab in POSITIFS
    parts = [p.lower() for p in nom.replace('\\', '/').split('/')[:-1]]
    return any(p in POSITIFS for p in parts)


def lire_export(source, dossier):
    """L'export des clips du boîtier -> dossier/{neg_train, neg_test, vrais}/*.wav, et un résumé.

    source : un .zip, un .tar(.gz) ou un dossier. Clips : .wav/.flac/.pcm/.raw... (PCM : int16 16 kHz mono).
    Étiquette : celle de l'index (index.json {clips:[{file,label}]}, .jsonl ou .csv), sinon le nom du dossier.
    « real »/« missed » (vrais appels) -> vrais/ : jamais entraînés, seulement pour la note finale (rappel).
    Le reste (faux réveils, radio...) -> négatifs : 1 sur 5 (tenu_a_part) mis de côté pour la note finale,
    les autres pour l'entraînement, coupés en morceaux de 20 s au plus."""
    fait = os.path.join(dossier, 'resume.json')
    if os.path.exists(fait): return json.load(open(fait))
    tmp = os.path.join(dossier, '_brut')
    shutil.rmtree(tmp, ignore_errors=True); os.makedirs(tmp)
    if os.path.isdir(source):
        shutil.copytree(source, tmp, dirs_exist_ok=True)
    elif zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as z: z.extractall(tmp)
    elif tarfile.is_tarfile(source):
        with tarfile.open(source) as t:
            t.extractall(tmp, filter='data') if hasattr(tarfile, 'data_filter') else t.extractall(tmp)
    else:
        raise ValueError(f'{source} : ni zip, ni tar, ni dossier')
    tous = []
    for racine, _, fs in os.walk(tmp):
        for f in fs: tous.append(os.path.join(racine, f))
    etiq = _etiquettes_index([(os.path.relpath(p, tmp), (lambda p=p: open(p, 'rb').read())) for p in tous])
    r = {'vrais': 0, 'neg_train': 0, 'neg_test': 0, 'secondes_neg_test': 0.0, 'secondes_neg_train': 0.0, 'ignores': 0}
    for p in sorted(tous):
        rel = os.path.relpath(p, tmp)
        if not p.lower().endswith(AUDIO_EXT) or os.path.basename(p).startswith('._'): continue
        try:
            x = charge16k(p)
        except Exception as e:
            r['ignores'] += 1; print('clip illisible, ignoré :', rel, e); continue
        if len(x) < RATE // 4: r['ignores'] += 1; continue
        nom = rel.replace('/', '_').replace('\\', '_').rsplit('.', 1)[0]
        if est_positif(rel, etiq):
            ecrit16k(os.path.join(dossier, 'vrais', nom + '.wav'), x); r['vrais'] += 1
        elif tenu_a_part(os.path.basename(p)):
            ecrit16k(os.path.join(dossier, 'neg_test', nom + '.wav'), x)
            r['neg_test'] += 1; r['secondes_neg_test'] += len(x) / RATE
        else:
            for k, morceau in enumerate(decoupe(x, 20.0, 0.5)):
                ecrit16k(os.path.join(dossier, 'neg_train', f'{nom}_{k}.wav'), morceau)
                r['neg_train'] += 1; r['secondes_neg_train'] += len(morceau) / RATE
    shutil.rmtree(tmp, ignore_errors=True)
    json.dump(r, open(fait, 'w'), indent=1)
    return r


# ---------------------------------------------------------------- cache sur Google Drive

class Cache:
    """Chaque étape coûteuse : faite une fois, gardée dans `racine` (Google Drive), reprise à la session suivante.

    c.fait('nom') : l'étape est-elle déjà faite (marque locale ou sur Drive) ?
    c.garde('nom', ['dossier', ...]) : archive les dossiers sur Drive et marque l'étape faite.
    c.reprend('nom') : les dossiers gardés sont-ils là (les restaure depuis Drive au besoin) ?"""

    def __init__(self, racine, local='/content'):
        self.racine, self.local = racine, local
        if racine: os.makedirs(racine, exist_ok=True)

    def _marque(self, nom): return os.path.join(self.local, f'.fait-{nom}')

    def fait(self, nom):
        return os.path.exists(self._marque(nom)) or bool(self.racine and os.path.exists(os.path.join(self.racine, f'{nom}.ok')))

    def garde(self, nom, dossiers=()):
        if self.racine and dossiers:
            tmp = os.path.join(self.racine, f'{nom}.tar.part')
            with tarfile.open(tmp, 'w') as t:
                for d in dossiers: t.add(os.path.join(self.local, d), arcname=d)
            os.replace(tmp, os.path.join(self.racine, f'{nom}.tar'))
        if self.racine: open(os.path.join(self.racine, f'{nom}.ok'), 'w').write(json.dumps(list(dossiers)))
        open(self._marque(nom), 'w').write('ok')

    def reprend(self, nom):
        """True si l'étape est faite et ses dossiers présents ici (restaurés depuis Drive si besoin)."""
        if os.path.exists(self._marque(nom)): return True
        if not self.racine: return False
        ok, arch = os.path.join(self.racine, f'{nom}.ok'), os.path.join(self.racine, f'{nom}.tar')
        if not os.path.exists(ok): return False
        if os.path.exists(arch):
            with tarfile.open(arch) as t:
                t.extractall(self.local, filter='data') if hasattr(tarfile, 'data_filter') else t.extractall(self.local)
        open(self._marque(nom), 'w').write('ok')
        return True


def miroir(src, dst):
    """Copie src -> dst (fichiers nouveaux ou changés seulement)."""
    if not (src and dst and os.path.isdir(src)): return
    for racine, _, fs in os.walk(src):
        cible = os.path.join(dst, os.path.relpath(racine, src)); os.makedirs(cible, exist_ok=True)
        for f in fs:
            a, b = os.path.join(racine, f), os.path.join(cible, f)
            try:
                if not os.path.exists(b) or os.path.getsize(a) != os.path.getsize(b) or os.path.getmtime(a) > os.path.getmtime(b):
                    shutil.copy2(a, b + '.part'); os.replace(b + '.part', b)
            except OSError as e:
                print('miroir : ', a, e)


# ---------------------------------------------------------------- les corrections de microWakeWord

_CLIPS_AVANT = '''        audio_dataset = datasets.Dataset.from_dict(
            {"audio": [str(i) for i in filtered_paths]}
        ).cast_column("audio", datasets.Audio())

        # Convert all clips to 16 kHz sampling rate when accessed
        audio_dataset = audio_dataset.cast_column(
            "audio", datasets.Audio(sampling_rate=16000)
        )
'''
_CLIPS_APRES = '''        # (mesange v3) chemins seuls : l'audio est lu par soundfile (_mesange_charge), pas par datasets/torchcodec
        audio_dataset = datasets.Dataset.from_dict(
            {"audio": [str(i) for i in filtered_paths]}
        )
'''
_CLIPS_FONCTION = '''

def _mesange_charge(path):  # (mesange v3) remplace le décodage de datasets (torchcodec absent ou cassé sur Colab)
    import soundfile as _sf
    from scipy.signal import resample_poly as _rp
    if isinstance(path, dict): path = path.get("path")
    x, sr = _sf.read(path, dtype="float32", always_2d=False)
    if x.ndim > 1: x = x.mean(axis=1)
    if sr != 16000: x = _rp(x, 16000, sr).astype(np.float32)
    return x
'''

_TRAIN_DEBUT_AVANT = '''    best_minimization_quantity = 10000
    best_maximization_quantity = 0.0
    best_no_faph_cutoff = 1.0

    for training_step in range(1, training_steps_max + 1):'''
_TRAIN_DEBUT_APRES = '''    best_minimization_quantity = 10000
    best_maximization_quantity = 0.0
    best_no_faph_cutoff = 1.0

    # (mesange v3) reprise : repartir de la dernière évaluation gardée (resume.json + dernier checkpoint)
    import json as _json, shutil as _shutil
    _resume_path = os.path.join(config["train_dir"], "resume.json")
    _first_step = 1
    if os.path.exists(_resume_path):
        _r = _json.load(open(_resume_path))
        _first_step = int(_r["step"]) + 1
        best_minimization_quantity = _r["best_min"]
        best_maximization_quantity = _r["best_max"]
        best_no_faph_cutoff = _r["best_cutoff"]
        logging.info("Reprise a l'etape %d", _first_step)
        print("REPRISE a l'etape", _first_step, flush=True)

    for training_step in range(_first_step, training_steps_max + 1):'''

_TRAIN_FIN_AVANT = '''            logging.info(
                "So far the best minimization quantity is %.3f with best maximization quantity of %.5f%%; no faph cutoff is %.2f",'''
_TRAIN_FIN_APRES = '''            # (mesange v3) à chaque évaluation : checkpoint + étape, puis copie sur Drive (MESANGE_MIROIR)
            checkpoint.save(file_prefix=checkpoint_prefix)
            with open(_resume_path + ".part", "w") as _f:
                _json.dump({"step": training_step, "best_min": float(best_minimization_quantity), "best_max": float(best_maximization_quantity), "best_cutoff": float(best_no_faph_cutoff)}, _f)
            os.replace(_resume_path + ".part", _resume_path)
            print("ETAPE", training_step, "sur", training_steps_max, "gardee", flush=True)
            if os.environ.get("MESANGE_MIROIR"):
                try:
                    _shutil.copytree(config["train_dir"], os.environ["MESANGE_MIROIR"], dirs_exist_ok=True)
                except Exception as _e:
                    print("miroir Drive impossible :", _e, flush=True)

            logging.info(
                "So far the best minimization quantity is %.3f with best maximization quantity of %.5f%%; no faph cutoff is %.2f",'''


def _remplace(chemin, avant, apres, deja):
    texte = open(chemin, encoding='utf-8').read()
    if deja in texte: return 'déjà'
    assert texte.count(avant) == 1, f'{chemin} : passage attendu introuvable (microWakeWord a changé ?)'
    open(chemin, 'w', encoding='utf-8').write(texte.replace(avant, apres))
    return 'corrigé'


def patch_microwakeword(racine):
    """Les corrections du code de microWakeWord (commit épinglé), sans effet si déjà faites. Renvoie un résumé."""
    pkg = os.path.join(racine, 'microwakeword')
    r = {}
    # 1. data.py : validation ambiante 3 fois moins dense (sinon plus de mémoire vive : plantage du 31-10)
    r['data'] = _remplace(os.path.join(pkg, 'data.py'), '                    int(1000 * self.step * self.stride),\n',
                          '                    3 * int(1000 * self.step * self.stride),\n', '3 * int(1000 * self.step * self.stride)')
    # 2. clips.py : lire l'audio avec soundfile, plus avec datasets (torchcodec : « AudioDecoder not subscriptable »)
    c = os.path.join(pkg, 'audio', 'clips.py')
    r['clips'] = _remplace(c, _CLIPS_AVANT, _CLIPS_APRES, '(mesange v3) chemins seuls')
    texte = open(c, encoding='utf-8').read()
    if '_mesange_charge(path)' not in texte:
        assert texte.count('\nclass Clips') == 1, f'{c} : classe Clips introuvable'
        texte = texte.replace('\nclass Clips', _CLIPS_FONCTION + '\n\nclass Clips', 1)
    texte = texte.replace('clip["audio"]["array"]', '_mesange_charge(clip["audio"])')
    texte = texte.replace('rand_audio_entry["audio"]["array"]', '_mesange_charge(rand_audio_entry["audio"])')
    assert '["audio"]["array"]' not in texte, f'{c} : un accès audio non corrigé'
    open(c, 'w', encoding='utf-8').write(texte)
    # 3. train.py : reprendre à la dernière évaluation après une coupure (et copier sur Drive)
    t = os.path.join(pkg, 'train.py')
    r['train_debut'] = _remplace(t, _TRAIN_DEBUT_AVANT, _TRAIN_DEBUT_APRES, '(mesange v3) reprise')
    r['train_fin'] = _remplace(t, _TRAIN_FIN_AVANT, _TRAIN_FIN_APRES, '(mesange v3) à chaque évaluation')
    for f in ('data.py', 'audio/clips.py', 'train.py'):
        compile(open(os.path.join(pkg, f), encoding='utf-8').read(), f, 'exec')
    return r


# ---------------------------------------------------------------- le modèle noté comme le boîtier

FEATURES, FENETRE = 40, 5
RECUL = FENETRE * 3       # après un réveil, le firmware ignore 15 inférences (d.ignore = -WINDOW * 3)


def caracteristiques(pcm_int16):
    """Le frontend du boîtier (TFLite Micro, 40 bandes, comme ESPHome), quantifié en int8 comme feed() de mww.cc."""
    import pymicro_features
    fe, sortie, buf, at = pymicro_features.MicroFrontend(), [], np.asarray(pcm_int16, '<i2').tobytes(), 0
    while at + 320 <= len(buf):
        r = fe.process_samples(buf[at:at + 320])
        at += r.samples_read * 2
        if r.features:
            v = np.rint(np.asarray(r.features) * 25.6).astype(np.int32)
            sortie.append(np.clip((v * 256 + 333) // 666 - 128, -128, 127).astype(np.int8))
        if r.samples_read == 0: break
    return np.array(sortie, np.int8).reshape(-1, FEATURES)


def en_int16(x):
    x = np.asarray(x)
    return x if x.dtype == np.int16 else (np.clip(x, -1, 1) * 32767).astype(np.int16)


class ModeleBoitier:
    """Un modèle microWakeWord en flux (.tflite quantifié), appelé comme sur le boîtier."""

    def __init__(self, chemin):
        try:
            from ai_edge_litert.interpreter import Interpreter
        except Exception:
            from tensorflow.lite import Interpreter
        self.it = Interpreter(model_path=chemin, num_threads=1)
        self.it.allocate_tensors()
        i = self.it.get_input_details()[0]
        self.inp, self.out = i['index'], self.it.get_output_details()[0]['index']
        self.stride = int(i['shape'][1])
        assert i['dtype'] == np.int8 and i['shape'][2] == FEATURES, "ce n'est pas un modèle microWakeWord en flux"

    def scores(self, feats):
        """Sortie uint8 de chaque inférence, état remis à zéro au départ (comme après chaque échange)."""
        self.it.reset_all_variables()
        out = []
        for k in range(0, len(feats) - self.stride + 1, self.stride):
            self.it.set_tensor(self.inp, feats[k:k + self.stride][None])
            self.it.invoke()
            out.append(int(self.it.get_tensor(self.out).reshape(-1)[0]))
        return np.array(out, np.int32)


def scores_flux(modele, pcm, morceau_s=600):
    """Scores d'un long enregistrement, par morceaux de 10 min (mémoire bornée)."""
    pcm, n, out = en_int16(pcm), int(morceau_s * RATE), []
    for k in range(0, len(pcm), n):
        out.append(modele.scores(caracteristiques(pcm[k:k + n])))
    return np.concatenate(out) if out else np.zeros(0, np.int32)


def sommes_fenetre(s):
    p = np.concatenate([np.zeros(FENETRE - 1, np.int32), np.asarray(s, np.int32)])
    return np.convolve(p, np.ones(FENETRE, np.int32), 'valid')


def meilleur_score(s):
    """La meilleure moyenne sur 5 inférences (0..1) : le clip réveille le boîtier au seuil c si elle le dépasse."""
    return int(sommes_fenetre(s).max()) / FENETRE / 255 if len(s) else 0.0


def note_clip(modele, chemin, marge_s=0.5):
    """Le meilleur score d'un clip du mot, entouré de `marge_s` secondes de silence (sur le boîtier, il y a
    toujours du son avant le mot, et le modèle répond un peu après la fin du mot)."""
    z = np.zeros(int(marge_s * RATE), np.float32)
    return meilleur_score(modele.scores(caracteristiques(en_int16(np.concatenate([z, charge16k(chemin), z])))))


def reveille(meilleur, seuil):
    """Même comparaison que le firmware : somme > int(seuil*255)*5."""
    return round(meilleur * FENETRE * 255) > int(seuil * 255) * FENETRE


def reveils(s, seuil):
    """Nombre de réveils sur un long flux, avec la remise à zéro et le recul du firmware."""
    barre, recent, n, ignore, last = int(seuil * 255) * FENETRE, [0] * FENETRE, 0, 0, 0
    for x in s:
        last = (last + 1) % FENETRE; recent[last] = int(x)
        if ignore < 0: ignore += 1; continue
        if sum(recent) > barre: n += 1; recent = [0] * FENETRE; ignore = -RECUL
    return n


def flux_de_dossier(fichiers, blanc_s=0.3, max_s=None):
    """Les fichiers mis bout à bout (un petit silence entre deux), en int16 ; s'arrête à max_s secondes."""
    blanc, parts, total = np.zeros(int(blanc_s * RATE), np.int16), [], 0
    for f in fichiers:
        try: x = en_int16(charge16k(f))
        except Exception as e: print('ignoré', f, e); continue
        parts += [x, blanc]; total += len(x) + len(blanc)
        if max_s and total >= max_s * RATE: break
    return np.concatenate(parts) if parts else np.zeros(0, np.int16)


# ---------------------------------------------------------------- la table finale

def tableau_metriques(fa_sources, rappels, seuils=SEUILS):
    """fa_sources : {source: {'heures': h, 'scores': scores du flux}} ; rappels : {nom: [meilleur score de chaque clip]}.
    -> une ligne par seuil : réveils/heure par source, sur toute la parole française (« parole_fr » : les sources
    dont le nom commence par fr_), et le rappel (part des clips du mot qui réveillent)."""
    lignes = []
    for c in seuils:
        l = {'seuil': round(c, 2), 'fa_h': {}, 'rappel': {}}
        n_fr = h_fr = 0
        for src, d in fa_sources.items():
            n = reveils(d['scores'], c)
            l['fa_h'][src] = round(n / d['heures'], 3) if d['heures'] else None
            if src.startswith('fr_'): n_fr += n; h_fr += d['heures']
        l['fa_h']['parole_fr'] = round(n_fr / h_fr, 3) if h_fr else None
        for nom, best in rappels.items():
            l['rappel'][nom] = round(sum(reveille(b, c) for b in best) / len(best), 4) if len(best) else None
        lignes.append(l)
    return lignes


def seuil_conseille(lignes, fa_max=0.5):
    """Parmi les seuils à au plus `fa_max` réveil/heure sur la parole française, celui qui garde le plus de vrais
    appels (s'il y en a), puis le meilleur rappel « bruit », puis le plus bas ; aucun : celui au moins de faux
    réveils (à égalité : le plus bas)."""
    ok = [l for l in lignes if l['fa_h'].get('parole_fr') is not None and l['fa_h']['parole_fr'] <= fa_max]
    if ok:
        return max(ok, key=lambda l: (l['rappel'].get('vrais_appels') or 0, l['rappel'].get('bruit') or 0, -l['seuil']))['seuil']
    return min(lignes, key=lambda l: (l['fa_h'].get('parole_fr') or 0, l['seuil']))['seuil']


def texte_tableau(lignes):
    """La table en Markdown, pour l'affichage et le rapport."""
    sources = [k for k in lignes[0]['fa_h']] if lignes else []
    rappels = [k for k in lignes[0]['rappel']] if lignes else []
    tete = ['seuil'] + [f'réveils/h {s}' for s in sources] + [f'rappel {r}' for r in rappels]
    out = ['| ' + ' | '.join(tete) + ' |', '|' + '---|' * len(tete)]
    for l in lignes:
        cases = [f"{l['seuil']:.2f}"] + ['–' if l['fa_h'][s] is None else f"{l['fa_h'][s]:.2f}" for s in sources]
        cases += ['–' if l['rappel'][r] is None else f"{100 * l['rappel'][r]:.1f} %" for r in rappels]
        out.append('| ' + ' | '.join(cases) + ' |')
    return '\n'.join(out)


def versions(paquets=('tensorflow', 'numpy', 'keras', 'audiomentations', 'datasets', 'piper-tts', 'pymicro-features',
                      'ai-edge-litert', 'mmap-ninja', 'soundfile', 'scipy', 'protobuf')):
    from importlib import metadata
    r = {}
    for p in paquets:
        try: r[p] = metadata.version(p)
        except Exception: r[p] = None
    try: r['gpu'] = subprocess.run(['nvidia-smi', '--query-gpu=name', '--format=csv,noheader'], capture_output=True, text=True, timeout=20).stdout.strip() or None
    except Exception: r['gpu'] = None
    return r


# ---------------------------------------------------------------- commandes, voix, spectrogrammes (Colab)

def sh(cmd, flux=False, essais=1):
    """Une commande shell ; sa sortie s'affiche. En cas d'échec : une erreur avec la fin de la sortie (pour corriger)."""
    for k in range(essais):
        p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        fin = []
        for ligne in p.stdout:
            fin = (fin + [ligne.rstrip()])[-60:]
            if flux: print(ligne, end='', flush=True)
        if p.wait() == 0:
            if not flux and fin: print('\n'.join(fin[-5:]))
            return
        if k + 1 < essais: print(f'échec, nouvel essai ({k + 2}/{essais})…'); time.sleep(15)
    raise RuntimeError(f'commande en échec (code {p.returncode}) : {cmd}\n' + '\n'.join(fin))


def voix_piper(dossier, voix, base='https://huggingface.co/rhasspy/piper-voices/resolve/main/'):
    os.makedirs(dossier, exist_ok=True)
    for chemin, nom in voix:
        for ext in ('.onnx', '.onnx.json'):
            telecharge(f'{base}{chemin}/{nom}{ext}', os.path.join(dossier, nom + ext))


def synthese(dossier_voix, voix, textes, n, dossier, graine, eval_seulement=False):
    """n clips des `textes` par les voix Piper, en wav 16 kHz dans `dossier` (reprend où il en était).
    Locuteurs des voix à plusieurs locuteurs : 1 sur 5 (id % 5 == 0) réservé à l'évaluation ;
    eval_seulement=True ne prend que ceux-là (et seulement ces voix-là)."""
    import random
    from piper import PiperVoice, SynthesisConfig
    os.makedirs(dossier, exist_ok=True)
    deja = len([f for f in os.listdir(dossier) if f.endswith('.wav')])
    if deja >= n: return deja
    rnd = random.Random(graine * 1000 + deja)
    charges = []
    for _, nom in voix:
        v = PiperVoice.load(os.path.join(dossier_voix, nom + '.onnx'))
        loc = v.config.num_speakers or 1
        ids = [s for s in range(loc) if (s % 5 == 0) == eval_seulement] if loc > 1 else ([None] if not eval_seulement else [])
        if ids: charges.append((v, ids))
    assert charges, 'aucune voix utilisable'
    k = deja
    while k < n:
        v, ids = charges[k % len(charges)]
        cfg = SynthesisConfig(speaker_id=rnd.choice(ids), length_scale=rnd.uniform(0.75, 1.25),
                              noise_scale=rnd.uniform(0.4, 1.0), noise_w_scale=rnd.uniform(0.5, 1.0))
        audio = b''.join(c.audio_int16_bytes for c in v.synthesize(rnd.choice(textes), syn_config=cfg))
        x = en16k(np.frombuffer(audio, np.int16).astype(np.float32) / 32768, v.config.sample_rate)
        ecrit16k(os.path.join(dossier, f'{k:05d}.wav'), x)
        k += 1
    return k


def augmenteur(fonds, rirs, duree=3.2, snr=(-5, 10), proba_fond=0.75):
    from microwakeword.audio.augmentation import Augmentation
    return Augmentation(augmentation_duration_s=duree,
                        augmentation_probabilities={"SevenBandParametricEQ": 0.1, "TanhDistortion": 0.1, "PitchShift": 0.1,
                                                    "BandStopFilter": 0.1, "AddColorNoise": 0.1, "AddBackgroundNoise": proba_fond,
                                                    "Gain": 1.0, "RIR": 0.5},
                        impulse_paths=rirs, background_paths=fonds,
                        background_min_snr_db=snr[0], background_max_snr_db=snr[1],
                        min_jitter_s=0.195, max_jitter_s=0.205)


def spectrogrammes(source, cible, augmenter, positif=False, nom_mmap='neg_mmap'):
    """Les spectrogrammes d'un dossier de wav, rangés comme microWakeWord les attend (training/validation/testing).
    Refait l'étape entière si elle avait été coupée (marque cible/.ok)."""
    from mmap_ninja.ragged import RaggedMmap
    from microwakeword.audio.clips import Clips
    from microwakeword.audio.spectrograms import SpectrogramGeneration
    if os.path.exists(os.path.join(cible, '.ok')): return 'déjà'
    shutil.rmtree(cible, ignore_errors=True)
    clips = Clips(input_directory=source, file_pattern='*.wav', max_clip_duration_s=None, remove_silence=False,
                  random_split_seed=10, split_count=0.1)
    for split, nom in [('training', 'train'), ('validation', 'validation'), ('testing', 'test')]:
        if positif:
            gen = SpectrogramGeneration(clips=clips, augmenter=augmenter, slide_frames=1 if split == 'testing' else 10, step_ms=10)
            rep = 2 if split == 'training' else 1
        else:
            gen = SpectrogramGeneration(clips=clips, augmenter=augmenter, slide_frames=1, step_ms=10)
            rep = 1
        RaggedMmap.from_generator(out_dir=os.path.join(cible, split, nom_mmap),
                                  sample_generator=gen.spectrogram_generator(split=nom, repeat=rep),
                                  batch_size=100, verbose=True)
    open(os.path.join(cible, '.ok'), 'w').write('ok')
    return 'fait'
