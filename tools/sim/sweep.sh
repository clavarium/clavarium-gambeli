#!/bin/sh
# Tries the settings on the test set: sweep.sh <data dir> "-DEP_START=2.5 -DEP_CARRY=1.8 ..." ...
D=$1; shift; P=${PYTHON:-python3}; H=$(dirname "$0")
for flags in "$@"; do
  gcc -O2 $flags -o "$D/sim-try" "$H/sim.c" "$H/../../main/endpoint.c" -lm || exit 1
  echo "== $flags"; cut -d' ' -f1,2 "$D/tracks.txt" | "$D/sim-try" | $P "$H/score.py" "$D/tracks.txt" | head -1
done
