"""wake_campaign.py <data dir> <campaign dir> [cutoff]: the wake word on the campaign's tracks -- positives
(said once, in a persona's room) found or missed; negatives (an hour each, no word) false wakes per hour."""
import sys, collections, numpy as np
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from wake import wakes
data, camp = sys.argv[1], sys.argv[2]
cutoff = float(sys.argv[3]) if len(sys.argv) > 3 else None
found = collections.defaultdict(lambda: [0, 0])
for line in open(f'{camp}/wake.txt'):
    path, kind, name = line.split()
    pcm = np.fromfile(path, np.int16)
    hits = wakes(f'{data}/mesange.json', pcm, cutoff)
    if kind == 'pos':
        found[name][0] += bool(hits); found[name][1] += 1
    else:
        print(f'faux réveils {name}: {len(hits)} en {len(pcm) / 16000 / 3600:.2f} h', flush=True)
for name, (ok, n) in sorted(found.items()): print(f'mot trouvé {name}: {ok}/{n}')
tot = [sum(v[0] for v in found.values()), sum(v[1] for v in found.values())]
print(f'mot trouvé en tout: {tot[0]}/{tot[1]}')
