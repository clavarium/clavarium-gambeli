"""Runs the box's wake-word model on a computer (pymicro-wakeword: the same features and model as the chip).
wake.py <model.json> <file.raw>...  ->  per file: the seconds at which it woke. 16 kHz int16 raw.
As the box: a detection needs the mean of the last `sliding_window_size` probabilities over the cutoff, and
then the model is quiet for three windows."""
import sys, numpy as np
from pymicro_wakeword import MicroWakeWord, MicroWakeWordFeatures

def wakes(config, pcm, cutoff=None):
    ww = MicroWakeWord.from_config(config)
    if cutoff is not None: ww.probability_cutoff = cutoff
    feats = MicroWakeWordFeatures()
    hits, t = [], 0.0
    for at in range(0, len(pcm) - 320, 320):
        for f in feats.process_streaming(pcm[at:at + 320].tobytes()):
            if ww.process_streaming(f):
                at_s = round(at / 16000, 2)
                if not hits or at_s - hits[-1] > 1.0: hits.append(at_s)   # one word, one wake (the box is quiet ~1 s after)
    return hits

if __name__ == '__main__':
    cfg = sys.argv[1]
    for p in sys.argv[2:]:
        print(p, wakes(cfg, np.fromfile(p, dtype=np.int16)))
