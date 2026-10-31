"""THE BIG SOUND CAMPAIGN (docs/TESTS-SON.md): tracks built after the personas from real recordings --
French read speech (MLS), sounds of daily life (ESC-50), real rooms (MIT impulse responses) -- then run
through the box's own end-of-sentence detection (sim, C) and its wake-word model (wake.py). Seeded: the same
tracks each time.

campaign.py <data dir> <out dir> [tracks]   ->  <out>/endpoint.txt, <out>/wake.txt, and a summary printed.
"""
import sys, os, io, glob, random, subprocess, json
import numpy as np, soundfile as sf, pyarrow.parquet as pq
from scipy.signal import fftconvolve, resample_poly

R = 16000
data, out = sys.argv[1], sys.argv[2]
count = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
os.makedirs(f'{out}/tracks', exist_ok=True)
rng = random.Random(31102026); nrng = np.random.default_rng(31102026)

# ---- sources
speech_files = sorted(glob.glob(f'{data}/real/speech/*.flac'))
speakers = {}
for p in speech_files: speakers.setdefault(os.path.basename(p).split('_')[0], []).append(p)
speaker_ids = sorted(speakers)
def speech(speaker=None):
    s = speaker or rng.choice(speaker_ids)
    x, r = sf.read(rng.choice(speakers[s]), dtype='float32')
    return (x if r == R else resample_poly(x, R, r)) * 32767, s
rirs = [sf.read(p, dtype='float32')[0] for p in sorted(glob.glob(f'{data}/real/rir/**/*.wav', recursive=True))]
esc = {}
for p in sorted(glob.glob(f'{data}/real/esc50/**/*.parquet', recursive=True)):
    for row in pq.read_table(p, columns=['category', 'audio']).to_pylist():
        x, r = sf.read(io.BytesIO(row['audio']['bytes']), dtype='float32')
        if x.ndim > 1: x = x.mean(axis=1)
        esc.setdefault(row['category'], []).append((x if r == R else resample_poly(x, R, r)) * 32767)
box_voice = [np.fromfile(p, np.int16).astype(np.float32) for p in sorted(glob.glob(f'{data}/phrases/*.raw'))]
word = [np.fromfile(p, np.int16).astype(np.float32) for p in sorted(glob.glob(f'{data}/word/*-0-*.raw')) + sorted(glob.glob(f'{data}/word/*-1-*.raw'))]

def rms(x): v = x[np.abs(x) > 200]; return float(np.sqrt(np.mean(v ** 2))) if len(v) else 1.0
def level(x, target): return x * (target / rms(x))
def room(x, wet):
    if not wet: return x
    h = rng.choice(rirs); h = h / (np.abs(h).max() + 1e-9)
    y = fftconvolve(x, h)[: len(x) + R // 2]
    return y * (rms(x) / rms(y))
def bed(n, kind, lvl):
    """What fills the room: quiet hiss, a fan, a TV (other people talking), or a sound of daily life on and off."""
    hiss = nrng.standard_normal(n).astype(np.float32) * 40
    if kind == 'quiet': return hiss
    if kind == 'fan': return hiss + np.convolve(nrng.standard_normal(n), np.ones(8) / 8, 'same').astype(np.float32) * lvl * 2.5
    out_ = np.zeros(n, np.float32); at = 0
    while at < n:
        if kind == 'tv': x, _ = speech()
        else: x = rng.choice(esc[kind])
        x = level(x, lvl)[: n - at]; out_[at:at + len(x)] += x
        at += len(x) + (int(R * rng.uniform(0.05, 0.4)) if kind == 'tv' else int(R * rng.uniform(0.5, 4)))
    return hiss + out_

# ---- the personas (docs/TESTS-SON.md): voice level, pauses, room, what is around
PERSONAS = {
    'P1-voix-faible-tele':    dict(voice=350, pauses=[0.3, 0.8, 1.5], wet=True,  around=('tv', 250)),
    'P2-enfant':              dict(voice=900, pauses=[0.2, 0.5], wet=False, around=('quiet', 0), child=True),
    'P3-cuisine-hotte':       dict(voice=800, pauses=[0.3, 0.8], wet=True,  around=('fan', 180)),
    'P4-pauses-reflexion':    dict(voice=800, pauses=[1.0, 1.6, 2.2], wet=False, around=('quiet', 0)),
    'P5-piece-qui-resonne':   dict(voice=1500, pauses=[0.3, 0.8], wet=True, around=('quiet', 0), very_wet=True),
    'P8-voiture':             dict(voice=900, pauses=[0.3, 0.8], wet=False, around=('engine', 400)),
    'P10-chien':              dict(voice=800, pauses=[0.3, 0.8], wet=True,  around=('dog', 900)),
    'P10-bebe':               dict(voice=800, pauses=[0.3, 0.8], wet=True,  around=('crying_baby', 700)),
    'P11-chuchote-nuit':      dict(voice=200, pauses=[0.3, 0.8], wet=False, around=('quiet', 0)),
    'menage-aspirateur':      dict(voice=1000, pauses=[0.3, 0.8], wet=True, around=('vacuum_cleaner', 500)),
    'pluie-fenetre-ouverte':  dict(voice=700, pauses=[0.3, 0.8], wet=True, around=('rain', 300)),
}

def child(x):   # a higher, quicker voice: played 20 % faster
    return resample_poly(x, 5, 6).astype(np.float32)

endpoint_lines, wake_lines = [], []
for t in range(count):
    name = rng.choice(sorted(PERSONAS)); p = PERSONAS[name]
    s = rng.choice(speaker_ids)
    parts = []
    for k in range(rng.randint(1, 3)):
        x, _ = speech(s); x = x[: int(R * rng.uniform(3, 9))]
        if p.get('child'): x = child(x)
        parts.append(x)
        if k: parts.insert(-1, np.zeros(int(R * rng.choice(p['pauses'])), np.float32))
    sentence = level(np.concatenate(parts), p['voice'])
    sentence = room(sentence, p['wet'] and (p.get('very_wet') or rng.random() < 0.6))
    idle, lead = 30.0, 0.5
    n = int(R * (idle + lead)) + len(sentence) + R * 6
    kind, lvl = p['around']
    track = bed(n, kind, lvl)
    s0 = int(R * (idle + lead)); track[s0:s0 + len(sentence)] += sentence
    path = f'{out}/tracks/{t:04d}-{name}.raw'
    np.clip(track, -32767, 32767).astype(np.int16).tofile(path)
    endpoint_lines.append(f'{path} {idle} {(s0 + len(sentence)) / R:.2f} {name}')
open(f'{out}/endpoint.txt', 'w').write('\n'.join(endpoint_lines) + '\n')
print(len(endpoint_lines), 'end-of-sentence tracks')

# ---- the wake word: said by the voices we have, in the rooms of the personas; and hours with no word at all
pos_dir = f'{out}/wake'; os.makedirs(pos_dir, exist_ok=True)
for t in range(min(count // 2, 400)):
    name = rng.choice(sorted(PERSONAS)); p = PERSONAS[name]
    w = word[t % len(word)]
    if p.get('child'): w = child(w)
    w = room(level(w, p['voice']), p['wet'])
    kind, lvl = p['around']
    track = bed(R * 4, kind, lvl); track[R:R + len(w)] += w[: R * 3]
    path = f'{pos_dir}/pos-{t:04d}-{name}.raw'; np.clip(track, -32767, 32767).astype(np.int16).tofile(path)
    wake_lines.append(f'{path} pos {name}')
# negatives: an hour of each -- people talking (a TV, a dinner), daily life, and the box's own voice back in the mic
for kind, what in [('tv', 'tele-1h'), ('dog', 'chien-1h'), ('vacuum_cleaner', 'aspirateur-1h'), ('crying_baby', 'bebe-1h')]:
    path = f'{pos_dir}/neg-{what}.raw'; np.clip(bed(R * 3600, kind, 800), -32767, 32767).astype(np.int16).tofile(path)
    wake_lines.append(f'{path} neg {what}')
own = np.concatenate([np.concatenate([level(rng.choice(box_voice), 1500), np.zeros(int(R * rng.uniform(0.3, 1.5)), np.float32)]) for _ in range(900)])
for cancel, what in [(1.0, 'propre-voix-sans-annulation'), (0.1, 'propre-voix-echo-attenue-20dB')]:
    echo = room(own * cancel, True)
    path = f'{pos_dir}/neg-{what}.raw'; np.clip(echo + bed(len(echo), 'quiet', 0), -32767, 32767).astype(np.int16).tofile(path)
    wake_lines.append(f'{path} neg {what}')
open(f'{out}/wake.txt', 'w').write('\n'.join(wake_lines) + '\n')
print(len(wake_lines), 'wake-word tracks')
