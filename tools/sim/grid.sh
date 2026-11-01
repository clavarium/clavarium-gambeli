#!/bin/sh
# Searches the end-of-sentence settings on a campaign (campaign.py): grid.sh <data dir> <campaign dir>
# Score = ok - 2*cut - 0.5*long (being cut off costs more than waiting); best first.
D=$1; C=$2; H=$(dirname "$0"); P=${PYTHON:-python3}
for st in 1.6 2.0 2.5; do for ca in 1.3 1.5 1.8; do for re in 0.2 0.3 0.4; do for si in 125 150 175; do for ab in 40 60; do
  gcc -O2 -DEP_START=$st -DEP_CARRY=$ca -DEP_RELATIVE=$re -DEP_SILENCE_CHUNKS=$si -DEP_ABSOLUTE=$ab -o "$D/sim-grid" "$H/sim.c" "$H/../../main/endpoint.c" -lm || exit 1
  r=$(cut -d' ' -f1,2 "$C/endpoint.txt" | "$D/sim-grid" | $P "$H/score.py" "$C/endpoint.txt" | head -1)
  echo "$st $ca $re $si $ab | $r"
done; done; done; done; done | $P -c "
import sys, ast
rows=[]
for l in sys.stdin:
    k, r = l.split(' | ', 1); d = ast.literal_eval(r.split(' mean')[0]); rows.append((d.get('ok',0) - 2*d.get('cut',0) - 0.5*d.get('long',0), k, r.strip()))
for s, k, r in sorted(rows, reverse=True)[:12]: print(f'{s:7.1f}  start carry rel silence abs = {k}  {r}')
"
