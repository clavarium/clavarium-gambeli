import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import WebSocket from 'ws';
import { startServer, CODE } from './server.mjs';

const pair = (port, code) => fetch(`http://127.0.0.1:${port}/api/box/pair`, {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ code, room: 'Salon', type: 'voice-box/1', firmware: '0.1.0', capabilities: ['mic'] }),
});

test('a box pairs, says a sentence, and hears it back', async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'box-'));
    const s = startServer({ port: 18080, dir, log: () => {} });
    try {
        assert.equal((await pair(18080, 'WRONG-000')).status, 403);
        const r = await pair(18080, CODE);
        const box = await r.json();
        assert.equal(box.name, 'Salon');
        assert.equal(box.key.length, 43);

        const refused = new WebSocket('ws://127.0.0.1:18080/api/box/ws', { headers: { Authorization: 'Bearer nope' } });
        const status = await new Promise((ok) => refused.on('unexpected-response', (q, res) => ok(res.statusCode)));
        assert.equal(status, 401);

        const ws = new WebSocket('ws://127.0.0.1:18080/api/box/ws', { headers: { Authorization: `Bearer ${box.key}` } });
        await new Promise((ok) => ws.on('open', ok));
        const got = [];
        let audio = 0;
        const ended = new Promise((ok) => ws.on('message', (d, bin) => {
            if (bin) { audio += d.length; return; }
            const m = JSON.parse(d.toString());
            got.push(m.t);
            if (m.t === 'speak.end') ok();
        }));
        ws.send(JSON.stringify({ t: 'listen.start', reason: 'button' }));
        for (let i = 0; i < 10; i++) ws.send(Buffer.alloc(640, i));
        ws.send(JSON.stringify({ t: 'listen.end', reason: 'silence' }));
        await ended;
        assert.deepEqual(got, ['cue', 'speak.start', 'speak.end']);
        assert.equal(audio, 6400);
        assert.equal(fs.statSync(path.join(dir, 'sentence-1.wav')).size, 44 + 6400);
        ws.close();
    } finally {
        await s.close();
    }
});
