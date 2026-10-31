"""Builds the simulator's test set, the same every time (seeded): sentences made of the Piper pieces
(phrases.mjs) with thinking pauses, at three distances, in four rooms (quiet, fan, TV, TV starting
mid-sentence). Writes <out>/tracks/*.raw and <out>/tracks.txt: path, idle seconds, speech end, kind."""
import os, sys, random, glob
import numpy as np
R = 16000
src, out = sys.argv[1], sys.argv[2]
rng = random.Random(31102026); nrng = np.random.default_rng(31102026)
os.makedirs(f'{out}/tracks', exist_ok=True)
pieces = {}
for p in sorted(glob.glob(f'{src}/*.raw')):
    v, i = os.path.basename(p)[:-4].rsplit('-', 1)
    pieces.setdefault(v, []).append(np.fromfile(p, dtype=np.int16).astype(np.float32))
voices = sorted(pieces)
def rms(x): return float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0
def norm(x, level): return x * (level / max(rms(x[np.abs(x) > 300]) if np.any(np.abs(x) > 300) else 1, 1))
def room(n, kind, level):
    white = nrng.standard_normal(n).astype(np.float32)
    if kind == 'fan':   # a low hum
        k = np.ones(8) / 8; white = np.convolve(white, k, 'same')
    return white / max(rms(white), 1e-6) * level
def tv(n, voice, level):
    out_ = np.zeros(n, np.float32); at = 0
    while at < n:
        x = norm(rng.choice(pieces[voice]), level); x = x[: n - at]; out_[at:at + len(x)] += x
        at += len(x) + int(R * rng.uniform(0.1, 0.6))
    return out_
lines = []
distances = {'near': 2500, 'mid': 800, 'far': 300}
rooms = ['quiet', 'fan', 'tv', 'tv-late']
fillers = [15, 16, 17, 18, 19]
for t in range(240):
    voice = rng.choice(voices); dist = rng.choice(list(distances)); kind = rng.choice(rooms)
    level = distances[dist]
    n_parts = rng.randint(1, 4); parts = []
    for k in range(n_parts):
        idx = rng.randrange(15) if rng.random() > 0.25 else rng.choice(fillers)
        x = norm(pieces[voice][idx], level * (0.5 if (k == n_parts - 1 and rng.random() < 0.3) else 1))   # sometimes trails off
        parts.append(x)
        if k < n_parts - 1: parts.append(np.zeros(int(R * rng.choice([0.15, 0.4, 0.8, 1.2, 1.6])), np.float32))   # a breath ... a thinking pause
    sentence = np.concatenate(parts)
    idle = 30.0; lead = 0.5
    total = int(R * (idle + lead)) + len(sentence) + R * 6
    base = room(total, 'fan' if kind == 'fan' else 'quiet', 150 if kind == 'fan' else 50)
    if kind in ('tv', 'tv-late'):
        tvv = rng.choice([v for v in voices if v != voice]); tl = rng.choice([150, 300])
        start = 0 if kind == 'tv' else int(R * (idle + lead)) + len(sentence) // 2
        base[start:] += tv(total - start, tvv, tl)
    s0 = int(R * (idle + lead)); base[s0:s0 + len(sentence)] += sentence
    name = f'{out}/tracks/{t:03d}-{dist}-{kind}-{n_parts}.raw'
    np.clip(base, -32767, 32767).astype(np.int16).tofile(name)
    lines.append(f'{name} {idle} {(s0 + len(sentence)) / R:.2f} {dist}-{kind}')
open(f'{out}/tracks.txt', 'w').write('\n'.join(lines) + '\n')
print(len(lines), 'tracks')
