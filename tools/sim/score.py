"""Reads tracks.txt and the simulator's output: a sentence cut before its end, closed in time
(within 2.6 s of its end), or kept open too long."""
import sys, collections
truth = {l.split()[0]: (float(l.split()[2]), l.split()[3]) for l in open(sys.argv[1])}
tally = collections.Counter(); by = collections.defaultdict(collections.Counter); late = []
for l in sys.stdin:
    name, end, why = l.split(); end = float(end); real, kind = truth[name]
    v = 'cut' if 0 <= end < real - 0.05 else 'ok' if 0 <= end <= real + 2.6 else 'long'
    tally[v] += 1; by[kind][v] += 1
    if v == 'ok': late.append(end - real)
print(dict(tally), 'mean close %.2f s' % (sum(late) / max(len(late), 1)))
for k in sorted(by): print(' ', k, dict(by[k]))
