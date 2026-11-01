// Spoken pieces for the simulator: French phrases in four Piper voices, 16 kHz int16 raw files.
import { createRequire } from 'node:module';
import { writeFileSync, mkdirSync } from 'node:fs';
const require = createRequire(`${process.env.SIM_SHERPA ?? process.cwd()}/`);
const sherpa = require('sherpa-onnx-node');
const out = process.argv[2]; mkdirSync(out, { recursive: true });
const voices = ['upmc-medium', 'siwis-medium', 'tom-medium', 'gilles-low'];
const phrases = [
  'Est-ce que tu peux me dire', 'quelle heure il est à New York', 'et du coup techniquement ça veut dire',
  'que le boîtier ne devrait pas me couper', 'je voudrais que tu me rappelles', 'demain matin vers huit heures',
  'tu sais quoi', 'en fait je me demandais', 'si on pouvait regarder la météo du week-end',
  'il était une fois un petit chaperon rouge', 'qui allait chez sa mère-grand', 'avec une galette et un petit pot de beurre',
  'allume la lumière du salon', 'mets un peu de musique douce', 'combien de temps faut-il pour cuire des pâtes',
  'euh', 'attends', 'comment dire', 'voilà', 'et puis aussi',
];
for (const v of voices) {
  const dir = `${process.env.SIM_VOICES ?? 'voices'}/vits-piper-fr_FR-${v}`;
  const tts = new sherpa.OfflineTts({ model: { vits: { model: `${dir}/fr_FR-${v}.onnx`, tokens: `${dir}/tokens.txt`, dataDir: `${dir}/espeak-ng-data` }, numThreads: 1 }, maxNumSentences: 1 });
  phrases.forEach((p, i) => {
    const a = tts.generate({ text: p, sid: 0, speed: 1.0 });
    const n = Math.floor(a.samples.length * 16000 / a.sampleRate); const b = Buffer.alloc(n * 2);
    for (let k = 0; k < n; k++) { const x = k * a.sampleRate / 16000, j = Math.floor(x); const s = a.samples[j] ?? 0; b.writeInt16LE(Math.max(-32767, Math.min(32767, Math.round(s * 32767))), k * 2); }
    writeFileSync(`${out}/${v}-${String(i).padStart(2, '0')}.raw`, b);
  });
  console.log(v, 'done');
}
