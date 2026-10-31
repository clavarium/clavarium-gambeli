#!/usr/bin/env node
// A small Clavarium stand-in that speaks protocol v1 (PROTOCOL.md), to try the box without Clavarium.
// Pairing accepts one fixed code; each sentence heard is saved as a .wav, then played back to the
// box (a parrot), or reply.wav when given. Usage: node test/server.mjs [--port 8080] [--reply reply.wav]
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { WebSocketServer } from 'ws';

export const CODE = 'TEST-0000';

export function wav(pcm, rate = 16000) {
    const h = Buffer.alloc(44);
    h.write('RIFF', 0); h.writeUInt32LE(36 + pcm.length, 4); h.write('WAVE', 8);
    h.write('fmt ', 12); h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(1, 22);
    h.writeUInt32LE(rate, 24); h.writeUInt32LE(rate * 2, 28); h.writeUInt16LE(2, 32); h.writeUInt16LE(16, 34);
    h.write('data', 36); h.writeUInt32LE(pcm.length, 40);
    return Buffer.concat([h, pcm]);
}

function pcmOf(file) {   // the data chunk of a 16 kHz mono 16-bit .wav
    const b = fs.readFileSync(file);
    let at = 12;
    while (at + 8 <= b.length) {
        const id = b.toString('ascii', at, at + 4), size = b.readUInt32LE(at + 4);
        if (id === 'data') return b.subarray(at + 8, at + 8 + size);
        at += 8 + size + (size & 1);
    }
    throw new Error(`${file}: no audio data`);
}

export function startServer({ port = 8080, dir = '.', reply = null, log = console.log } = {}) {
    const keys = new Map();   // key -> box
    const server = http.createServer((req, res) => {
        if (req.method === 'POST' && req.url === '/api/box/pair') {
            let body = '';
            req.on('data', (c) => { body += c; });
            req.on('end', () => {
                let p = {};
                try { p = JSON.parse(body); } catch { /* answered below */ }
                if (p.code !== CODE) { res.writeHead(403).end(); return; }
                const box = { id: crypto.randomUUID(), key: crypto.randomBytes(32).toString('base64url'), name: p.room || 'Box' };
                keys.set(box.key, box);
                log(`paired ${box.name} (${p.type}, firmware ${p.firmware})`);
                res.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify(box));
            });
            return;
        }
        res.writeHead(404).end();
    });
    const wss = new WebSocketServer({ noServer: true });
    server.on('upgrade', (req, socket, head) => {
        const key = (req.headers.authorization || '').replace(/^Bearer /, '');
        if (req.url !== '/api/box/ws' || !keys.has(key)) {
            socket.end('HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n');
            return;
        }
        wss.handleUpgrade(req, socket, head, (ws) => talk(ws, keys.get(key)));
    });
    let n = 0;
    function talk(ws, box) {
        let chunks = null;
        const send = (o) => ws.send(JSON.stringify(o));
        ws.on('message', (data, binary) => {
            if (binary) { if (chunks) chunks.push(Buffer.from(data)); return; }
            let m;
            try { m = JSON.parse(data.toString()); } catch { return; }
            if (m.t === 'ping') send({ t: 'pong' });
            else if (m.t === 'hello') log(`${box.name}: hello, firmware ${m.firmware}, rssi ${m.rssi}, muted ${m.muted}`);
            else if (m.t === 'listen.start') chunks = [];
            else if (m.t === 'listen.end' && chunks) {
                const pcm = Buffer.concat(chunks);
                chunks = null;
                const file = path.join(dir, `sentence-${++n}.wav`);
                fs.writeFileSync(file, wav(pcm));
                log(`${box.name}: ${(pcm.length / 32000).toFixed(1)} s heard (${m.reason}) -> ${file}`);
                send({ t: 'cue', name: 'thinking' });
                const answer = reply ? pcmOf(reply) : pcm;
                setTimeout(() => {
                    send({ t: 'speak.start', rate: 16000 });
                    for (let at = 0; at < answer.length; at += 640) ws.send(answer.subarray(at, at + 640));
                    send({ t: 'speak.end' });
                }, 300);
            } else log(`${box.name}: ${JSON.stringify(m)}`);
        });
    }
    server.listen(port);
    return { server, keys, close: () => new Promise((r) => { wss.close(); server.close(r); }) };
}

if (import.meta.url === `file://${process.argv[1]}`) {
    const arg = (name) => { const i = process.argv.indexOf(name); return i > 0 ? process.argv[i + 1] : null; };
    const port = Number(arg('--port') || 8080);
    startServer({ port, reply: arg('--reply') });
    console.log(`test server on http://0.0.0.0:${port} -- pairing code ${CODE}`);
}
