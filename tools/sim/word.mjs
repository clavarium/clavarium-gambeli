// The wake word said by the four Piper voices, at three speeds: positives for wake.py.
import { createRequire } from 'node:module';
import { writeFileSync, mkdirSync } from 'node:fs';
const sherpa = createRequire(`${process.env.SIM_SHERPA ?? process.cwd()}/`)('sherpa-onnx-node');
const [out, ...words] = process.argv.slice(2); mkdirSync(out, { recursive: true });
for (const v of ['upmc-medium', 'siwis-medium', 'tom-medium', 'gilles-low']) {
  const dir = `${process.env.SIM_VOICES ?? "voices"}/vits-piper-fr_FR-${v}`;
  const tts = new sherpa.OfflineTts({ model: { vits: { model: `${dir}/fr_FR-${v}.onnx`, tokens: `${dir}/tokens.txt`, dataDir: `${dir}/espeak-ng-data` }, numThreads: 1 }, maxNumSentences: 1 });
  for (const [w, word] of words.entries()) for (const speed of [0.85, 1, 1.2]) {
    const a = tts.generate({ text: word, sid: 0, speed });
    const n = Math.floor(a.samples.length * 16000 / a.sampleRate); const b = Buffer.alloc(n * 2);
    for (let k = 0; k < n; k++) b.writeInt16LE(Math.max(-32767, Math.min(32767, Math.round((a.samples[Math.floor(k * a.sampleRate / 16000)] ?? 0) * 32767))), k * 2);
    writeFileSync(`${out}/${v}-${w}-${speed}.raw`, b);
  }
}
