"""End of sentence with a neural voice detector (ESPHome's micro VAD, same features as the wake word) instead
of loudness: speech is what the model calls voice. Same rules as main/endpoint.c otherwise. Output as sim.c's.
ep_vad.py <vad.json> <silence seconds> [threshold] < list (path idle)"""
import sys, numpy as np
from pymicro_wakeword import MicroWakeWord, MicroWakeWordFeatures
cfg, silence = sys.argv[1], float(sys.argv[2])
thr = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
for line in sys.stdin:
    path, idle = line.split()[:2]; idle = float(idle)
    pcm = np.fromfile(path, np.int16)
    vad = MicroWakeWord.from_config(cfg); feats = MicroWakeWordFeatures()
    start = int((idle + 0.3) * 16000)   # the box listens after its ding, as sim.c skips 15 chunks
    voiced = False; heard = False; run = 0.0; quiet = 0.0; t = 0.0; end, why = -1, 'never'
    for at in range(0, len(pcm) - 320, 320):
        for f in feats.process_streaming(pcm[at:at + 320].tobytes()):
            p = vad.process_streaming_prob(f)
            if p is not None and p != 0.0: voiced = p > thr
        if at < start: continue
        t += 0.02
        if voiced: run += 0.02; quiet = 0.0; heard = heard or run >= 0.16
        else: run = 0.0; quiet += 0.02
        if heard and quiet >= silence: end, why = (at + 320) / 16000, 'silence'; break
        if not heard and t >= 15: end, why = (at + 320) / 16000, 'nobody'; break
        if t >= 300: end, why = (at + 320) / 16000, 'toolong'; break
    print(path, f'{end:.2f}', why, flush=True)
