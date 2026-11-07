#!/usr/bin/env python3
"""Fabrique mesange-entrainement-v3.ipynb : les cellules ci-dessous + mesange_v3.py (cellule 1, %%writefile).

    python3 construire_v3.py      # après avoir changé une cellule ou mesange_v3.py ; puis python3 test_v3.py
"""
import json, os

ICI = os.path.dirname(os.path.abspath(__file__))
SORTIE = os.path.join(ICI, 'mesange-entrainement-v3.ipynb')

ENTETE = '''import os, sys, json, glob, shutil, tempfile
import numpy as np
sys.path.insert(0, '/content'); os.chdir('/content')
from mesange_v3 import *
P = json.load(open('/content/params.json'))   # écrit par la cellule 3
cache = Cache(P['drive'])
'''

CELLULES = []
def md(texte): CELLULES.append(('markdown', texte.strip('\n')))
def code(texte, entete=True): CELLULES.append(('code', ((ENTETE + '\n') if entete else '') + texte.strip('\n')))


md('''
# Gambeli : mot de réveil « Mésange », version 3

**Pourquoi la v3** : le modèle v1 se réveille sur la radio, la télé et la conversation en français (« mes anges »,
« mise en… »). La v3 apprend avec, comme contre-exemples : de la parole française lue (MLS), de la parole de radio
et de télé (MediaSpeech FR), des mots qui sonnent pareil, et les vrais faux réveils du boîtier (session radio du 07-11).

**Mode d'emploi** (aussi fait par l'assistant de Chrome, voir CHROME-AGENT.md) :
1. *Exécution › Modifier le type d'exécution* › **GPU T4** › *Enregistrer*.
2. Cellule 3 : mettre le lien de l'export des clips du boîtier dans `NEGATIFS` (ou laisser vide).
3. *Exécution › Tout exécuter*. La cellule 2 redémarre la session **la première fois** : relancer alors *Tout exécuter*.
4. Compter ≈ 3 h. Tout est gardé sur Google Drive (dossier `gambeli-mesange-v3`) : après une coupure,
   *Tout exécuter* reprend où cela s'était arrêté (l'entraînement repart de sa dernière étape gardée).
5. À la fin, `mesange-v3.zip` se télécharge (aussi copié sur Drive) : mesange.tflite, mesange.json, metriques-v3.json.

La cellule 12 (évaluation) est la mesure de référence : **ne pas la modifier**.
''')

CELLULES.append(('code', '%%writefile /content/mesange_v3.py\n# 1. Les outils du carnet (mesange_v3.py) : rien à changer ici.\n'
                 + open(os.path.join(ICI, 'mesange_v3.py'), encoding='utf-8').read().rstrip('\n')))

code('''
# 2. Installation (une seule fois par session Colab) -- la session redémarre ensuite d'elle-même : c'est normal,
#    relancez alors « Tout exécuter ». Versions épinglées : celles qui ont tourné sur Colab le 31-10.
import os, sys, subprocess
sys.path.insert(0, '/content'); os.chdir('/content')
from mesange_v3 import sh, patch_microwakeword, versions
MWW_COMMIT = '4665173cd35f1cff9a61e06fc427f124766c488e'     # microWakeWord, « Pin dependencies (#98) »
PAQUETS = ["piper-tts==1.3.0", "pymicro-features==2.0.2", "ai-edge-litert==2.3.0", "audiomentations==0.43.1",
           "mmap-ninja==0.9.0", "webrtcvad-wheels==2.0.14.post1", "soundfile", "pyyaml", "librosa",
           "git+https://github.com/whatsnowplaying/audio-metadata@d4ebb238e6a401bb1a5aaaac60c9e2b3cb30929f"]
VERIF = ("import tensorflow as tf, numpy, microwakeword, pymicro_features, audiomentations, mmap_ninja, soundfile, yaml; "
         "from microwakeword.audio.clips import Clips; from microwakeword.audio.spectrograms import SpectrogramGeneration; "
         "from piper import PiperVoice, SynthesisConfig; from ai_edge_litert.interpreter import Interpreter; "
         "print('tensorflow', tf.__version__, 'numpy', numpy.__version__, 'GPU', len(tf.config.list_physical_devices('GPU')))")

def verifie():
    r = subprocess.run([sys.executable, '-c', VERIF], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip()[-3000:])
    return r.returncode == 0, r.stdout

if os.path.exists('/content/.installed-v3'):
    print('Déjà installé : on continue.')
else:
    if not os.path.isdir('microWakeWord/.git'):
        sh('git clone -q https://github.com/kahrendt/microWakeWord', essais=3)
    sh(f'git -C microWakeWord fetch -q origin && git -C microWakeWord checkout -q -f {MWW_COMMIT}', essais=3)
    print(patch_microwakeword('/content/microWakeWord'))
    sh('pip install -q --no-deps -e ./microWakeWord', essais=2)
    sh('pip install -q ' + ' '.join(f'"{p}"' for p in PAQUETS), essais=3)
    ok, sortie = verifie()
    if not ok:   # secours : la TensorFlow connue pour aller avec microWakeWord (2.21) et son protobuf
        sh('pip install -q "tensorflow==2.21.0" "protobuf>=6.31.1,<8" "numpy>=2.0,<2.4"', essais=2)
        ok, sortie = verifie()
    assert ok, "Les bibliothèques ne s'importent pas (voir l'erreur au-dessus) : corriger PAQUETS puis relancer."
    if 'GPU 0' in sortie:
        print("ATTENTION : pas de GPU. Exécution › Modifier le type d'exécution › GPU T4, puis tout relancer.")
        raise SystemExit('pas de GPU')
    open('/content/.installed-v3', 'w').write('ok')
    print('Installé. La session redémarre (normal) : relancez « Tout exécuter ».')
    os.kill(os.getpid(), 9)
''', entete=False)

code('''
# 3. Paramètres, Google Drive (pour reprendre après une coupure) et l'export des clips du boîtier.
import os, sys, json, glob
sys.path.insert(0, '/content'); os.chdir('/content')
from mesange_v3 import *

MOT, NOM = 'Mésange', 'mesange'
NEGATIFS = ''          # lien de l'export des clips du boîtier (zip) ; vide : un fichier déposé dans /content
                       # (negatifs*.zip) ou sur Drive (gambeli-mesange-v3/negatifs.zip) ; sinon on s'en passe.
ETAPES = 15000         # étapes d'entraînement (v2 : 15000)

assert os.path.exists('/content/.installed-v3'), "Lancez d'abord la cellule 2."
drive = ''
try:
    from google.colab import drive as _d
    _d.mount('/content/drive')
    drive = '/content/drive/MyDrive/gambeli-mesange-v3'
    os.makedirs(drive, exist_ok=True)
except Exception as e:
    print('Google Drive non monté (', e, ') : pas de reprise possible après une coupure, on continue quand même.')

export = ''
if NEGATIFS.strip().startswith('http'):
    export = telecharge(lien_direct(NEGATIFS.strip()), '/content/negatifs.zip')
else:
    trouves = sorted(glob.glob('/content/negatifs*.zip') + glob.glob('/content/*export*.zip') + ([f'{drive}/negatifs.zip'] if drive else []))
    trouves = [f for f in trouves if os.path.exists(f)]
    export = trouves[0] if trouves else ''
if export and drive and not os.path.exists(f'{drive}/negatifs.zip'):
    shutil.copy(export, f'{drive}/negatifs.zip')
boitier = lire_export(export, '/content/boitier') if export else None
print('Export du boîtier :', boitier or 'aucun (le carnet tourne sans)')

P = dict(mot=MOT, nom=NOM, etapes=ETAPES, drive=drive, export=export, boitier=boitier)
json.dump(P, open('/content/params.json', 'w'), indent=1)
print(json.dumps(P, ensure_ascii=False, indent=1))
''', entete=False)

code('''
# 4. Le mot dit par des voix françaises de synthèse (Piper) : 3000 pour apprendre ; 400 par des locuteurs
#    jamais entendus à l'entraînement, pour la note finale ; et les mots qui sonnent pareil (contre-exemples).
VOIX = [('fr/fr_FR/mls/medium', 'fr_FR-mls-medium'), ('fr/fr_FR/upmc/medium', 'fr_FR-upmc-medium'),
        ('fr/fr_FR/siwis/medium', 'fr_FR-siwis-medium'), ('fr/fr_FR/tom/medium', 'fr_FR-tom-medium'),
        ('fr/fr_FR/gilles/low', 'fr_FR-gilles-low')]
MOT = P['mot']
DITES = [MOT, MOT + ' !', MOT + ' ?', MOT + '.', 'Mé-sange', MOT.lower(), MOT + ', dis-moi', MOT + ', écoute']
# Ce que le boîtier a pris pour le mot (07-11) et ce qui sonne comme lui, sans l'être.
VOISINS = ['mes anges', 'les anges', 'des anges', 'mais sans', 'mes sangles', 'mélange', 'le mélange', 'mésentente',
           'message', 'messages', 'méchant', 'mes enfants', 'les enfants', 'des ans', 'mais en', 'mais encore',
           'mise en œuvre', 'mise en jeu', 'mise en route', 'mise en place', 'mes doigts', 'les ongles', 'mes ongles',
           'mais si', 'mais ça change', 'des oranges', 'des mésaventures', 'mais je mange', 'massage', 'ma sauge',
           'mes amis', 'mais attends', 'mes affaires', 'les échanges', 'mes ennemis', 'messagerie', 'mais enfin']

if cache.reprend('voix'):
    print('voix et exemples repris de Drive')
else:
    voix_piper('voix', VOIX)
    print(synthese('voix', VOIX, DITES, 3000, 'generated_samples', graine=1), 'exemples du mot (apprentissage)')
    print(synthese('voix', VOIX, DITES, 400, 'eval_positifs', graine=2, eval_seulement=True), 'exemples du mot (note finale)')
    print(synthese('voix', VOIX, VOISINS, 1600, 'near_words', graine=3), 'mots voisins')
    cache.garde('voix', ['voix', 'generated_samples', 'eval_positifs', 'near_words'])
print(len(os.listdir('generated_samples')), len(os.listdir('eval_positifs')), len(os.listdir('near_words')))
''')

code('''
# 5. Bruits et pièces pour « salir » les exemples : réponses impulsionnelles MIT, AudioSet (1 tranche), FMA (musique).
#    (Corrections du 31-10 reprises : lecture par soundfile, AudioSet en parquet, RIR par snapshot_download.)
import io, tempfile
from pathlib import Path
from tqdm import tqdm

if cache.reprend('bruits'):
    print('bruits repris de Drive')
else:
    if len(glob.glob('mit_rirs/*.wav')) < 100:
        from huggingface_hub import snapshot_download
        src = snapshot_download('davidscripka/MIT_environmental_impulse_responses', repo_type='dataset')
        for p in tqdm([p for p in Path(src).rglob('*') if p.suffix.lower() in ('.wav', '.flac', '.mp3', '.ogg')]):
            try: ecrit16k(f'mit_rirs/{p.stem}.wav', charge16k(str(p)))
            except Exception as e: print('ignoré', p.name, e)
    if len(glob.glob('audioset_16k/*.wav')) < 100:
        import pyarrow.parquet as pq
        pqf = telecharge('https://huggingface.co/datasets/agkphysics/AudioSet/resolve/main/data/bal_train/09.parquet', 'audioset/09.parquet')
        k = 0
        for batch in pq.ParquetFile(pqf).iter_batches(batch_size=64, columns=['audio']):
            for a in batch.column('audio').to_pylist():
                k += 1
                try:
                    with tempfile.NamedTemporaryFile(suffix=os.path.splitext(a.get('path') or 'x.flac')[1] or '.flac') as t:
                        t.write(a['bytes']); t.flush(); ecrit16k(f'audioset_16k/{k}.wav', charge16k(t.name))
                except Exception as e:
                    if k < 5: print('ignoré', k, e)
        os.remove(pqf)
    if len(glob.glob('fma_16k/*.wav')) < 100:
        z = telecharge('https://huggingface.co/datasets/mchl914/fma_xsmall/resolve/main/fma_xs.zip', 'fma/fma_xs.zip')
        sh('cd fma && unzip -q -o fma_xs.zip')
        for p in tqdm(list(Path('fma').glob('**/*.mp3'))):
            out = f'fma_16k/{p.stem}.wav'
            if not os.path.exists(out):
                try: ecrit16k(out, charge16k(str(p)))
                except Exception as e: print('ignoré', p.name, e)
        shutil.rmtree('fma', ignore_errors=True)
    cache.garde('bruits', ['mit_rirs', 'audioset_16k', 'fma_16k'])
print(len(os.listdir('mit_rirs')), 'RIR,', len(os.listdir('audioset_16k')), 'audioset,', len(os.listdir('fma_16k')), 'fma')
''')

code('''
# 6. Les exemples du mot, « salis » (bruit, pièce, distorsion), en spectrogrammes pour l'entraînement.
aug = augmenteur(['fma_16k', 'audioset_16k'], ['mit_rirs'])
print(spectrogrammes('generated_samples', 'generated_augmented_features', aug, positif=True, nom_mmap='wakeword_mmap'))
''')

code('''
# 7. Contre-exemples tout prêts de microWakeWord (parole anglaise, dîner, bruits sans parole).
os.makedirs('negative_datasets', exist_ok=True)
for f in ['dinner_party.zip', 'dinner_party_eval.zip', 'no_speech.zip', 'speech.zip']:
    marque = f'negative_datasets/.{f}.ok'
    if os.path.exists(marque): continue
    z = telecharge('https://huggingface.co/datasets/kahrendt/microwakeword/resolve/main/' + f, f'dl/{f}')
    sh(f'unzip -q -o {z} -d negative_datasets')
    os.remove(z); open(marque, 'w').write('ok')
print(sorted(os.listdir('negative_datasets')))
''')

code('''
# 8. Contre-exemples FRANÇAIS (le cœur de la v3) :
#  a) parole lue : MLS français (CC-BY 4.0), ≈ 15 h, locuteurs d'apprentissage ;
#  b) radio et télé : MediaSpeech FR (CC-BY 4.0, ≈ 10 h d'émissions) : 4 fichiers sur 5 (le 5e sert à la note finale) ;
#  c) les mots voisins (cellule 4) ; d) les faux réveils du boîtier (export, cellule 3) : 4 sur 5.
import random, tarfile
from tqdm import tqdm
from huggingface_hub import list_repo_files, hf_hub_download
MLS = 'facebook/multilingual_librispeech'

if not os.path.exists('fr_speech/.ok'):
    os.makedirs('fr_speech', exist_ok=True)
    fichiers = sorted(f for f in list_repo_files(MLS, repo_type='dataset') if f.startswith('data/mls_french/train/audio/'))
    random.Random(2).shuffle(fichiers)
    for f in tqdm(fichiers[:60]):
        p = hf_hub_download(MLS, f, repo_type='dataset', local_dir='mls_dl')
        with tarfile.open(p) as t:
            for m in t.getmembers():
                if m.isfile() and m.name.lower().endswith(('.flac', '.wav', '.opus')):
                    nom = os.path.basename(m.name).rsplit('.', 1)[0]
                    with tempfile.NamedTemporaryFile(suffix=os.path.splitext(m.name)[1]) as tmp:
                        tmp.write(t.extractfile(m).read()); tmp.flush()
                        ecrit16k(f'fr_speech/{nom}.wav', charge16k(tmp.name))
        os.remove(p)
    open('fr_speech/.ok', 'w').write('ok')
print(len(os.listdir('fr_speech')), 'enregistrements MLS')

if not os.path.exists('ms_train/.ok'):
    tgz = telecharge(['https://openslr.elda.org/resources/108/FR.tgz', 'https://www.openslr.org/resources/108/FR.tgz',
                      'https://openslr.trmal.net/resources/108/FR.tgz'], 'dl/mediaspeech_FR.tgz')
    with tarfile.open(tgz) as t:
        for m in tqdm(t.getmembers()):
            if not (m.isfile() and m.name.lower().endswith(('.flac', '.wav'))): continue
            nom = os.path.basename(m.name).rsplit('.', 1)[0]
            dest = f"{'ms_test' if tenu_a_part(nom) else 'ms_train'}/{nom}.wav"
            with tempfile.NamedTemporaryFile(suffix=os.path.splitext(m.name)[1]) as tmp:
                tmp.write(t.extractfile(m).read()); tmp.flush()
                ecrit16k(dest, charge16k(tmp.name))
    os.remove(tgz)
    open('ms_train/.ok', 'w').write('ok')
print(len(os.listdir('ms_train')), 'extraits radio/télé (apprentissage),', len(os.listdir('ms_test')), '(note finale)')

SOURCES = [('fr_speech', 'negative_datasets/fr_speech'), ('ms_train', 'negative_datasets/fr_media'),
           ('near_words', 'negative_datasets/near_words')]
if P['boitier'] and len(glob.glob('boitier/neg_train/*.wav')) >= 20:
    SOURCES.append(('boitier/neg_train', 'negative_datasets/boitier'))
aug_neg = augmenteur(['fma_16k', 'audioset_16k'], ['mit_rirs'], duree=None, proba_fond=0.3, snr=(5, 20))
for source, cible in SOURCES:
    print(cible, spectrogrammes(source, cible, aug_neg))
''')

code('''
# 9. Les données de la note finale, jamais vues à l'entraînement : parole française lue (MLS, locuteurs de test,
#    ≈ 4 h), radio/télé (MediaSpeech, le 5e mis de côté), faux réveils du boîtier mis de côté, et le mot
#    (locuteurs réservés, propre et « sali »), plus les vrais appels de l'export s'il y en a.
import random, tarfile
from huggingface_hub import list_repo_files, hf_hub_download
EVAL_MLS_HEURES = 4.0
if not os.path.exists('eval_mls/.ok'):
    os.makedirs('eval_mls', exist_ok=True)
    fichiers = sorted(f for f in list_repo_files('facebook/multilingual_librispeech', repo_type='dataset')
                      if f.startswith('data/mls_french/test/audio/'))
    total = 0.0
    for f in fichiers:
        if total >= EVAL_MLS_HEURES * 3600: break
        p = hf_hub_download('facebook/multilingual_librispeech', f, repo_type='dataset', local_dir='mls_dl')
        with tarfile.open(p) as t:
            for m in t.getmembers():
                if m.isfile() and m.name.lower().endswith(('.flac', '.wav', '.opus')):
                    with tempfile.NamedTemporaryFile(suffix=os.path.splitext(m.name)[1]) as tmp:
                        tmp.write(t.extractfile(m).read()); tmp.flush(); x = charge16k(tmp.name)
                    ecrit16k(f"eval_mls/{os.path.basename(m.name).rsplit('.', 1)[0]}.wav", x); total += len(x) / RATE
        os.remove(p)
    open('eval_mls/.ok', 'w').write('ok')

if not os.path.exists('eval_positifs_bruit/.ok'):
    random.seed(7); np.random.seed(7)
    aug_eval = augmenteur(['fma_16k', 'audioset_16k'], ['mit_rirs'], snr=(0, 15), proba_fond=1.0)
    for f in sorted(glob.glob('eval_positifs/*.wav')):
        x = aug_eval.augment_clip(charge16k(f))
        ecrit16k(f"eval_positifs_bruit/{os.path.basename(f)}", np.concatenate([x, np.zeros(RATE // 2, np.float32)]))
    open('eval_positifs_bruit/.ok', 'w').write('ok')
for d in ['eval_mls', 'ms_test', 'eval_positifs', 'eval_positifs_bruit', 'boitier/neg_test', 'boitier/vrais']:
    print(d, len(glob.glob(d + '/*.wav')), 'fichiers')
''')

code('''
# 10. Réglages de l'entraînement (ceux de la v2, plus les nouveaux contre-exemples français).
import yaml
def neg(d, poids, penalite=1.0):
    return {"features_dir": d, "sampling_weight": poids, "penalty_weight": penalite, "truth": False,
            "truncation_strategy": "random", "type": "mmap"}
features = [
    {"features_dir": "generated_augmented_features", "sampling_weight": 2.0, "penalty_weight": 1.0, "truth": True,
     "truncation_strategy": "truncate_start", "type": "mmap"},
    neg("negative_datasets/speech", 10.0), neg("negative_datasets/dinner_party", 10.0), neg("negative_datasets/no_speech", 5.0),
    neg("negative_datasets/fr_speech", 12.0),                 # parole française lue
    neg("negative_datasets/fr_media", 12.0),                  # radio et télé françaises
    neg("negative_datasets/near_words", 4.0, 2.0),            # « mes anges », « mise en… »
]
if os.path.exists('negative_datasets/boitier/.ok'):
    features.append(neg("negative_datasets/boitier", 6.0, 3.0))   # les vrais faux réveils du boîtier
features.append({"features_dir": "negative_datasets/dinner_party_eval", "sampling_weight": 0.0, "penalty_weight": 1.0,
                 "truth": False, "truncation_strategy": "split", "type": "mmap"})   # validation seulement
config = {
    "window_step_ms": 10, "train_dir": "trained_models/wakeword", "features": features,
    "training_steps": [int(P['etapes'])], "positive_class_weight": [1], "negative_class_weight": [20],
    "learning_rates": [0.001], "batch_size": 128,
    "time_mask_max_size": [0], "time_mask_count": [0], "freq_mask_max_size": [0], "freq_mask_count": [0],
    "eval_step_interval": 500, "clip_duration_ms": 1500,
    "target_minimization": 0.9, "minimization_metric": None, "maximization_metric": "average_viable_recall",
}
yaml.dump(config, open('training_parameters.yaml', 'w'))
print([f['features_dir'] for f in features])
''')

code('''
# 11. L'entraînement (≈ 1 h 30 à 2 h sur T4). Il reprend à sa dernière étape gardée (toutes les 500 étapes,
#     copiée sur Drive) : après une coupure, relancer simplement les cellules depuis le début.
#     Puis la conversion en modèle quantifié pour le boîtier. « ETAPE n sur 15000 gardee » montre l'avancement.
TFLITE = 'trained_models/wakeword/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite'
MIROIR = f"{P['drive']}/trained_models_wakeword" if P['drive'] else ''
if MIROIR and not os.path.exists('trained_models/wakeword/resume.json') and os.path.exists(f'{MIROIR}/resume.json'):
    miroir(MIROIR, 'trained_models/wakeword'); print('entraînement repris de Drive')
if os.path.exists('trained_models/wakeword/resume.json'):
    print('dernière étape gardée :', json.load(open('trained_models/wakeword/resume.json'))['step'])
fini = False
if os.path.exists(TFLITE) and os.path.exists('trained_models/wakeword/resume.json'):
    fini = json.load(open('trained_models/wakeword/resume.json'))['step'] >= int(P['etapes'])
if fini:
    print('Entraînement et conversion déjà faits.')
else:
    os.environ['MESANGE_MIROIR'] = MIROIR
    sh("python -m microwakeword.model_train_eval --training_config='training_parameters.yaml' --train 1 "
       "--restore_checkpoint 1 --test_tf_nonstreaming 0 --test_tflite_nonstreaming 0 --test_tflite_nonstreaming_quantized 0 "
       "--test_tflite_streaming 0 --test_tflite_streaming_quantized 1 --use_weights best_weights "
       "mixednet --pointwise_filters 64,64,64,64 --repeat_in_block 1,1,1,1 "
       "--mixconv_kernel_sizes '[5], [7,11], [9,15], [23]' --residual_connection 0,0,0,0 "
       "--first_conv_filters 32 --first_conv_kernel_size 5 --stride 3", flux=True)
    miroir('trained_models/wakeword', MIROIR)
assert os.path.exists(TFLITE), 'le modèle quantifié manque : voir la sortie au-dessus'
print('modèle :', TFLITE, os.path.getsize(TFLITE), 'octets')
''')

code('''
# 12. ÉVALUATION -- mesure de référence, NE PAS MODIFIER.
#     Le modèle est noté exactement comme le boîtier le fait tourner (frontend 40 bandes, int8, moyenne des
#     5 dernières sorties, 15 inférences ignorées après un réveil) :
#      - réveils par heure sur de la parole française jamais entendue (MLS test, radio/télé MediaSpeech mise de côté)
#        et sur les faux réveils du boîtier mis de côté ;
#      - rappel : part des « Mésange » de locuteurs jamais entendus qui réveillent (propre, et avec bruit/pièce),
#        et des vrais appels de l'export.
import time
TFLITE = 'trained_models/wakeword/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite'
t0 = time.time()
modele = ModeleBoitier(TFLITE)
fa = {}
for nom, d in [('fr_mls', 'eval_mls'), ('fr_radio_tv', 'ms_test'), ('boitier', 'boitier/neg_test')]:
    fs = sorted(glob.glob(d + '/*.wav'))
    if not fs: continue
    pcm = flux_de_dossier(fs)
    fa[nom] = {'heures': len(pcm) / RATE / 3600, 'scores': scores_flux(modele, pcm)}
    print(f"{nom} : {fa[nom]['heures']:.2f} h")
rappels = {}
for nom, d in [('propre', 'eval_positifs'), ('bruit', 'eval_positifs_bruit'), ('vrais_appels', 'boitier/vrais')]:
    fs = sorted(glob.glob(d + '/*.wav'))
    if fs: rappels[nom] = [note_clip(modele, f) for f in fs]   # entouré de 0,5 s de silence
lignes = tableau_metriques(fa, rappels)
conseil = seuil_conseille(lignes)
metriques = {
    'modele': 'mesange v3', 'etapes': int(P['etapes']), 'date': time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime()),
    'heures': {k: round(v['heures'], 3) for k, v in fa.items()}, 'clips': {k: len(v) for k, v in rappels.items()},
    'seuils': lignes, 'seuil_conseille': conseil,
    'regle_seuil': 'au plus 0,5 réveil/h sur la parole française, puis le plus de vrais appels, le meilleur rappel « bruit », le plus bas ; sinon le moins de réveils',
    'export_boitier': P['boitier'], 'versions': versions(), 'duree_evaluation_s': round(time.time() - t0),
}
json.dump(metriques, open('metriques-v3.json', 'w'), indent=1, ensure_ascii=False)
print(texte_tableau(lignes))
print('seuil conseillé :', conseil)
''')

code('''
# 13. Le paquet final : mesange.tflite + mesange.json + metriques-v3.json dans mesange-v3.zip, téléchargé (et copié sur Drive).
import zipfile
TFLITE = 'trained_models/wakeword/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite'
m = json.load(open('metriques-v3.json'))
manifeste = {'wake_word': P['mot'],   # même forme que le mesange.json de la v1
             'micro': {'probability_cutoff': m['seuil_conseille'], 'sliding_window_size': 5, 'feature_step_size': 10,
                       'tensor_arena_size': 30000, 'minimum_esphome_version': '2024.7.0'},
             'version': 2, 'type': 'micro', 'trained_languages': ['fr']}
os.makedirs('sortie', exist_ok=True)
shutil.copy(TFLITE, f"sortie/{P['nom']}.tflite")
json.dump(manifeste, open(f"sortie/{P['nom']}.json", 'w'), indent=2, ensure_ascii=False)
shutil.copy('metriques-v3.json', 'sortie/metriques-v3.json')
shutil.copy('training_parameters.yaml', 'sortie/training_parameters.yaml')
with zipfile.ZipFile('mesange-v3.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir('sortie')): z.write(f'sortie/{f}', f)
print(zipfile.ZipFile('mesange-v3.zip').namelist(), os.path.getsize('mesange-v3.zip'), 'octets')
if P['drive']:
    shutil.copy('mesange-v3.zip', f"{P['drive']}/mesange-v3.zip"); print('copié sur Drive :', f"{P['drive']}/mesange-v3.zip")
from google.colab import files
files.download('mesange-v3.zip')
print(texte_tableau(m['seuils']))
print('seuil conseillé :', m['seuil_conseille'])
''')


def cellule(t, src):
    lignes = src.split('\n')
    source = [l + '\n' for l in lignes[:-1]] + [lignes[-1]]
    if t == 'markdown': return {'cell_type': 'markdown', 'metadata': {}, 'source': source}
    return {'cell_type': 'code', 'metadata': {}, 'execution_count': None, 'outputs': [], 'source': source}


nb = {'nbformat': 4, 'nbformat_minor': 0,
      'metadata': {'accelerator': 'GPU', 'colab': {'provenance': [], 'gpuType': 'T4'},
                   'kernelspec': {'name': 'python3', 'display_name': 'Python 3'}, 'language_info': {'name': 'python'}},
      'cells': [cellule(t, s) for t, s in CELLULES]}
if __name__ == '__main__':
    with open(SORTIE, 'w', encoding='utf-8') as f:
        json.dump(nb, f, ensure_ascii=False, indent=1); f.write('\n')
    print(SORTIE, len(nb['cells']), 'cellules')
