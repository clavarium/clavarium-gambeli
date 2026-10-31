# End-of-sentence simulator

Runs the box's own detection (`main/endpoint.c`) on a fixed, reproducible test set, on a computer.

    node tools/sim/phrases.mjs DATA/phrases          # French pieces, four Piper voices (sherpa-onnx-node)
    python3 tools/sim/tracks.py DATA/phrases DATA    # 240 tracks: pauses, three distances, quiet/fan/TV rooms (seeded)
    tools/sim/sweep.sh DATA "" "-DEP_CARRY=1.8"      # scores each setting: cut / ok / long

`cut`: closed before the speaker finished. `ok`: closed within 2.6 s of the end. `long`: kept open.
A TV as loud as the speaker is beyond loudness alone: most `long` left are those.
