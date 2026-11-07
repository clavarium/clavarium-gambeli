#!/usr/bin/env python3
"""Tests locaux du carnet v3 (sans Colab, sans GPU ; il faut numpy et scipy) :

    python3 test_v3.py                       # carnet (JSON, cellules, syntaxe) + outils purs de mesange_v3.py
    MWW=/chemin/microWakeWord python3 test_v3.py      # + les corrections de microWakeWord (2 fois : idempotent)
    MESANGE_V1=/chemin/mesange.tflite EVALUATE=/chemin/evaluate.py python3 test_v3.py
                                             # + le modèle noté comme le boîtier, comparé à evaluate.py
                                             #   (il faut pymicro_features et ai_edge_litert)
"""
import ast, importlib.util, json, os, shutil, subprocess, sys, tempfile, wave, zipfile

ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ICI)
import numpy as np
import mesange_v3 as m

OK = []
def ok(nom): OK.append(nom); print('ok  ', nom)


def test_carnet():
    nb = json.load(open(os.path.join(ICI, 'mesange-entrainement-v3.ipynb'), encoding='utf-8'))
    assert nb['nbformat'] == 4 and nb['metadata']['accelerator'] == 'GPU'
    cells = nb['cells']
    assert len(cells) == 14, len(cells)
    assert cells[0]['cell_type'] == 'markdown'
    src = [''.join(c['source']) for c in cells]
    for c in cells[1:]:
        assert c['cell_type'] == 'code' and c['outputs'] == [] and c['execution_count'] is None
    titres = {}
    for i, s in enumerate(src[1:], 1):
        for ligne in s.splitlines():
            if ligne.startswith(f'# {i}.'): titres[i] = ligne; break
    assert sorted(titres) == list(range(1, 14)), f'titres manquants : {set(range(1, 14)) - set(titres)}'
    attendu = {1: 'outils', 2: 'Installation', 3: 'Paramètres', 4: 'voix', 5: 'Bruits', 6: 'spectrogrammes', 7: 'microWakeWord',
               8: 'FRANÇAIS', 9: 'note finale', 10: 'Réglages', 11: 'entraînement', 12: 'ÉVALUATION', 13: 'paquet final'}
    for i, mot in attendu.items(): assert mot in titres[i], (i, titres[i])
    assert src[1].startswith('%%writefile /content/mesange_v3.py\n')
    assert src[1].split('\n', 2)[2].rstrip('\n') == open(os.path.join(ICI, 'mesange_v3.py'), encoding='utf-8').read().rstrip('\n'), \
        'cellule 1 différente de mesange_v3.py : relancer construire_v3.py'
    assert 'NE PAS MODIFIER' in src[12] and 'seuil_conseille' in src[12] and 'tableau_metriques' in src[12]
    assert "NEGATIFS = ''" in src[3] and "mount('/content/drive')" in src[3]
    assert 'files.download' in src[13] and 'mesange-v3.zip' in src[13] and 'metriques-v3.json' in src[13]
    assert "os.kill(os.getpid(), 9)" in src[2] and '.installed-v3' in src[2]
    assert '4665173cd35f1cff9a61e06fc427f124766c488e' in src[2]
    for i, s in enumerate(src[1:], 1):     # syntaxe, sans les commandes IPython
        lignes = [l for l in s.splitlines() if not l.lstrip().startswith(('%', '!'))]
        compile('\n'.join(lignes), f'cellule {i}', 'exec')
    # le carnet est bien celui que construire_v3.py fabrique
    spec = importlib.util.spec_from_file_location('construire_v3', os.path.join(ICI, 'construire_v3.py'))
    c = importlib.util.module_from_spec(spec); spec.loader.exec_module(c)
    assert c.nb == nb, 'carnet pas à jour : relancer construire_v3.py'
    try:
        import nbformat
        nbformat.validate(nbformat.reads(json.dumps(nb), as_version=4)); ok('nbformat.validate')
    except ImportError:
        pass
    ok('carnet : JSON, 14 cellules attendues, syntaxe, cellule 1 = mesange_v3.py')


def test_audio(d):
    t = np.arange(48000 * 2) / 48000
    x48 = (0.5 * np.sin(2 * np.pi * 440 * t)).astype(np.float32)
    with wave.open(f'{d}/a48.wav', 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(48000)
        w.writeframes((np.repeat(x48[:, None], 2, 1) * 32767).astype('<i2').tobytes())
    y = m.charge16k(f'{d}/a48.wav')
    assert y.dtype == np.float32 and abs(len(y) - 32000) <= 1 and 0.45 < np.abs(y).max() < 0.55
    m.ecrit16k(f'{d}/b.wav', y)
    z = m.charge16k(f'{d}/b.wav'); assert len(z) == len(y) and np.abs(z - y).max() < 1e-3
    (np.arange(16000) % 100 * 100).astype('<i2').tofile(f'{d}/c.pcm')
    assert len(m.charge16k(f'{d}/c.pcm')) == 16000
    assert [len(p) for p in m.decoupe(np.zeros(16000 * 45), 20.0, 1.0)] == [320000, 320000, 80000]
    assert [len(p) for p in m.decoupe(np.zeros(16000 * 40 + 8000), 20.0, 1.0)] == [320000, 320000]
    ok('audio : lecture 48 kHz stéréo -> 16 kHz mono, wav, pcm, découpe')


def test_tenu_a_part():
    noms = [f'2026-11-0{j}T0{k}-12-33.wav' for j in range(1, 8) for k in range(10)]
    py = [m.tenu_a_part(n) for n in noms]
    assert 5 <= sum(py) <= 25
    serveur = os.environ.get('WAKELEARN', '/resources/clavarium/server/wakelearn.mjs')
    if os.path.exists(serveur) and shutil.which('node'):
        js = ("import { createHash } from 'node:crypto'; import { basename } from 'node:path';"
              "const heldOut = (file) => BigInt(`0x${createHash('sha1').update(basename(String(file))).digest('hex')}`) % 5n === 0n;"
              f"console.log(JSON.stringify({json.dumps(noms)}.map(heldOut)));")
        r = subprocess.run(['node', '--input-type=module', '-e', js], capture_output=True, text=True, check=True)
        assert json.loads(r.stdout) == py, 'tenu_a_part différent de heldOut() du serveur'
        assert "% 5n === 0n" in open(serveur).read(), 'la règle heldOut du serveur a changé'
        ok('tenu_a_part = heldOut() du serveur (wakelearn.mjs)')
    else:
        ok('tenu_a_part (sans comparaison au serveur)')


def test_export(d):
    src = f'{d}/exp'
    os.makedirs(f'{src}/real'); os.makedirs(f'{src}/false'); os.makedirs(f'{src}/radio')
    bruit = lambda s, n: (np.random.default_rng(s).standard_normal(n) * 3000).astype('<i2')
    for k in range(6): m.ecrit16k(f'{src}/real/r{k}.wav', bruit(k, 25600) / 32768)
    for k in range(40): m.ecrit16k(f'{src}/false/f{k}.wav', bruit(100 + k, 25600) / 32768)
    bruit(7, 16000 * 50).tofile(f'{src}/radio/long.pcm')          # 50 s de « radio » en PCM brut
    bruit(8, 2000).tofile(f'{src}/radio/court.pcm')                # trop court : ignoré
    m.ecrit16k(f'{src}/radio/etiq.wav', bruit(9, 16000) / 32768)    # l'index dit « missed » : un vrai appel
    json.dump({'clips': [{'file': 'etiq.wav', 'label': 'missed'}, {'file': 'f0.wav', 'label': 'false'}]}, open(f'{src}/index.json', 'w'))
    z = f'{d}/export.zip'
    with zipfile.ZipFile(z, 'w') as zz:
        for r, _, fs in os.walk(src):
            for f in fs: zz.write(os.path.join(r, f), os.path.relpath(os.path.join(r, f), src))
    out = f'{d}/boitier'
    r = m.lire_export(z, out)
    assert r['vrais'] == 7, r
    neg = 40 + 1
    tenus = sum(m.tenu_a_part(f'f{k}.wav') for k in range(40)) + m.tenu_a_part('long.pcm')
    assert r['neg_test'] == tenus and r['ignores'] == 1, r
    attendus_train = (40 - sum(m.tenu_a_part(f'f{k}.wav') for k in range(40))) + (0 if m.tenu_a_part('long.pcm') else 3)
    assert r['neg_train'] == attendus_train, (r, attendus_train)
    assert len(os.listdir(f'{out}/vrais')) == 7 and len(os.listdir(f'{out}/neg_train')) == attendus_train
    assert all(len(m.charge16k(f'{out}/neg_train/{f}')) <= 20 * 16000 for f in os.listdir(f'{out}/neg_train'))
    assert m.lire_export(z, out) == r                                  # déjà fait : rien de refait
    t = f'{d}/export.tar.gz'
    shutil.make_archive(t[:-7], 'gztar', src)
    r2 = m.lire_export(t, f'{d}/boitier2'); assert r2 == r, (r2, r)
    assert m.lien_direct('https://drive.google.com/file/d/AbC-12_x/view?usp=sharing').endswith('id=AbC-12_x&export=download&confirm=t')
    assert m.lien_direct('https://example.org/a.zip') == 'https://example.org/a.zip'
    ok(f"export : zip et tar, index + dossiers, PCM long découpé ({r})")


def test_cache(d):
    local, drive = f'{d}/content', f'{d}/drive'
    os.makedirs(f'{local}/voix'); open(f'{local}/voix/a.txt', 'w').write('x')
    c = m.Cache(drive, local)
    assert not c.fait('voix') and not c.reprend('voix')
    c.garde('voix', ['voix'])
    assert c.fait('voix') and os.path.exists(f'{drive}/voix.tar')
    shutil.rmtree(local); os.makedirs(local)                          # une nouvelle machine Colab
    c2 = m.Cache(drive, local)
    assert c2.reprend('voix') and open(f'{local}/voix/a.txt').read() == 'x'
    c3 = m.Cache('', f'{d}/sans'); os.makedirs(f'{d}/sans'); c3.garde('z'); assert c3.reprend('z')
    os.makedirs(f'{d}/src/sub'); open(f'{d}/src/sub/f', 'w').write('1')
    m.miroir(f'{d}/src', f'{d}/dst'); assert open(f'{d}/dst/sub/f').read() == '1'
    ok('cache Drive : garde, reprise sur une machine neuve, miroir')


def test_notes():
    s = np.zeros(200, np.int32); s[50:55] = 250                         # un pic : 5 sorties à 250
    assert m.meilleur_score(s) == 250 / 255
    assert m.reveille(m.meilleur_score(s), 0.97) and not m.reveille(m.meilleur_score(s), 0.99)
    assert m.reveils(s, 0.97) == 1 and m.reveils(s, 0.99) == 0
    long = np.zeros(10000, np.int32)
    for k in range(0, 10000, 500): long[k:k + 10] = 255              # 20 rafales de 10 -> 20 réveils (recul de 15)
    assert m.reveils(long, 0.95) == 20
    fa = {'fr_mls': {'heures': 2.0, 'scores': long}, 'fr_radio_tv': {'heures': 2.0, 'scores': np.zeros(10, np.int32)},
          'boitier': {'heures': 0.5, 'scores': s}}
    rap = {'bruit': [0.99, 0.95, 0.91, 0.5], 'propre': [1.0, 1.0]}
    lignes = m.tableau_metriques(fa, rap)
    assert [l['seuil'] for l in lignes] == m.SEUILS
    assert lignes[0]['fa_h']['fr_mls'] == 10.0 and lignes[0]['fa_h']['parole_fr'] == 5.0 and lignes[0]['fa_h']['boitier'] == 2.0
    assert lignes[0]['rappel']['bruit'] == 0.75
    assert m.seuil_conseille(lignes) == 0.90                           # 5 réveils/h partout : le moins de réveils, puis le plus bas
    calme = m.tableau_metriques({'fr_mls': {'heures': 1.0, 'scores': np.zeros(10, np.int32)}}, rap)
    assert m.seuil_conseille(calme) == 0.90                            # 0 réveil/h partout : le meilleur rappel, le plus bas
    t = m.texte_tableau(lignes); assert t.count('\n') == 11 and '0.95' in t
    ok('notes : fenêtre de 5, recul du firmware, réveils/heure, rappel, seuil conseillé')


def test_patch(d):
    src = os.environ.get('MWW')
    if not src: print('--  corrections microWakeWord : MWW non donné, sauté'); return
    dst = f'{d}/mww'; shutil.copytree(src, dst, ignore=shutil.ignore_patterns('.git'))
    r1 = m.patch_microwakeword(dst); r2 = m.patch_microwakeword(dst)
    assert set(r1.values()) == {'corrigé'} and set(r2.values()) == {'déjà'}, (r1, r2)
    t = open(f'{dst}/microwakeword/train.py').read()
    assert t.count('(mesange v3)') == 2 and 'range(_first_step, training_steps_max + 1)' in t
    c = open(f'{dst}/microwakeword/audio/clips.py').read()
    assert 'cast_column' not in c and c.count('_mesange_charge(') == 3
    assert '3 * int(1000 * self.step * self.stride)' in open(f'{dst}/microwakeword/data.py').read()
    # la lecture corrigée de Clips, sur de vrais fichiers (il faut datasets ; tensorflow simulé si absent)
    try:
        import datasets  # noqa
    except ImportError:
        ok('corrections microWakeWord : appliquées, idempotentes, compilent (Clips non essayé : pas de datasets)'); return
    code = f'''
import sys, types, numpy as np, wave, os
import datasets  # avant les modules simulés (datasets cherche tensorflow)
for n in ["tensorflow", "tensorflow.lite", "tensorflow.lite.experimental", "tensorflow.lite.experimental.microfrontend",
          "tensorflow.lite.experimental.microfrontend.python", "tensorflow.lite.experimental.microfrontend.python.ops"]:
    sys.modules.setdefault(n, types.ModuleType(n))
sys.modules["tensorflow.lite.experimental.microfrontend.python.ops"].audio_microfrontend_op = None
for n in ["webrtcvad", "audio_metadata", "pymicro_features"]:
    mod = sys.modules.setdefault(n, types.ModuleType(n)); mod.MicroFrontend = None; mod.Vad = None
sys.path.insert(0, "{dst}")
from microwakeword.audio.clips import Clips
os.makedirs("{d}/w", exist_ok=True)
for k in range(10):
    with wave.open(f"{d}/w/{{k}}.wav", "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050); w.writeframes((np.ones(22050) * 1000).astype("<i2").tobytes())
c = Clips(input_directory="{d}/w", file_pattern="*.wav", random_split_seed=10, split_count=0.1)
xs = list(c.audio_generator(split="train")); assert len(xs) == 8 and all(len(x) == 16000 for x in xs), [len(x) for x in xs]
assert len(list(c.audio_generator(split="test"))) == 1 and len(c.get_random_clip()) == 16000
print("clips ok")
'''
    r = subprocess.run([sys.executable, '-c', code], capture_output=True, text=True)
    assert r.returncode == 0 and 'clips ok' in r.stdout, r.stderr[-2000:]
    ok('corrections microWakeWord : appliquées, idempotentes ; Clips lit l\'audio sans torchcodec (22 kHz -> 16 kHz)')


def test_boitier(d):
    v1, ev = os.environ.get('MESANGE_V1'), os.environ.get('EVALUATE')
    if not v1: print('--  modèle noté comme le boîtier : MESANGE_V1 non donné, sauté'); return
    import time
    mod = m.ModeleBoitier(v1)
    rng = np.random.default_rng(3)
    clips = []
    for k in range(4):
        x = (rng.standard_normal(16000 * 2) * (500 + 2000 * k)).astype('<i2')
        p = f'{d}/n{k}.wav'; m.ecrit16k(p, x / 32768); clips.append(p)
    a = [m.meilleur_score(mod.scores(m.caracteristiques(m.en_int16(m.charge16k(p))))) for p in clips]
    assert all(0 <= m.note_clip(mod, p) <= 1 for p in clips)
    pcm = (rng.standard_normal(16000 * 300) * 1500).astype('<i2')
    t0 = time.time(); s = m.scores_flux(mod, pcm, morceau_s=60); dt = time.time() - t0
    assert len(s) > 300 * 30
    print(f'     5 min de bruit notées en {dt:.1f} s ({300 / dt:.0f}x temps réel) ; max {s.max()}')
    if ev:
        spec = importlib.util.spec_from_file_location('evaluate', ev); e = importlib.util.module_from_spec(spec); spec.loader.exec_module(e)
        em = e.Model(v1)
        b = [int(e.window_sums(em.scores(e.features(e.load_pcm(p)))).max()) / 5 / 255 for p in clips]
        assert np.allclose(a, b), (a, b)
        assert m.reveils(s, 0.9) == e.wakes(s, 0.9) and m.reveils(s, 0.5) == e.wakes(s, 0.5)
        ok(f'modèle noté comme le boîtier = evaluate.py (scores {[round(x, 3) for x in a]})')
    else:
        ok('modèle noté comme le boîtier (sans comparaison à evaluate.py)')


if __name__ == '__main__':
    d = tempfile.mkdtemp(prefix='mesange-v3-')
    try:
        test_carnet(); test_audio(d); test_tenu_a_part(); test_export(d); test_cache(d); test_notes(); test_patch(d); test_boitier(d)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print(f'{len(OK)} tests passés')
