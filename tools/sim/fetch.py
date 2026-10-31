"""Fetches the open datasets the sound tests use, into <out>: French speech (MLS, CC-BY 4.0: a dev subset),
sounds of daily life (ESC-50, CC-BY-NC: for tests only, never shipped), rooms (MIT impulse responses)."""
import sys, os, io, tarfile, glob
from huggingface_hub import hf_hub_download, list_repo_files
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
files = list_repo_files('facebook/multilingual_librispeech', repo_type='dataset')
speech = [f for f in files if f.startswith('data/mls_french/dev/audio/')][:40]
os.makedirs(f'{out}/speech', exist_ok=True)
for f in speech:
    p = hf_hub_download('facebook/multilingual_librispeech', f, repo_type='dataset', local_dir=f'{out}/dl')
    with tarfile.open(p) as t:
        for m in t.getmembers():
            if m.isfile() and m.name.endswith(('.flac', '.opus', '.wav')):
                m.name = os.path.basename(m.name); t.extract(m, f'{out}/speech', filter='data')
    os.remove(p)
print('speech', len(os.listdir(f'{out}/speech')))
for f in [x for x in list_repo_files('davidscripka/MIT_environmental_impulse_responses', repo_type='dataset') if x.endswith('.wav')]:
    hf_hub_download('davidscripka/MIT_environmental_impulse_responses', f, repo_type='dataset', local_dir=f'{out}/rir')
print('rir', len(glob.glob(f'{out}/rir/**/*.wav', recursive=True)))
for f in [x for x in list_repo_files('ashraq/esc50', repo_type='dataset') if x.endswith('.parquet')]:
    hf_hub_download('ashraq/esc50', f, repo_type='dataset', local_dir=f'{out}/esc50')
print('esc50 done')
