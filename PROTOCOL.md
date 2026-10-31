# Voice box protocol, v1

Between a box and Clavarium. Everything goes over HTTPS / WSS; the box trusts the usual public
roots (ESP-IDF's certificate bundle).

## Pairing

`POST {clavarium}/api/box/pair`

```json
{ "code": "K7Q2-M9PX", "room": "Living room", "type": "voice-box/1", "firmware": "0.1.0",
  "capabilities": ["mic", "speaker", "led-ring:7", "button", "mute", "wifi", "ble"] }
```

- `200` `{ "id": "<uuid>", "key": "<secret, 43 characters>", "name": "Living room" }`: the key is given once.
- `403`: unknown or expired code. `429`: too many tries.

The code comes from Clavarium (8 characters, valid 10 minutes).

## Connection

`wss://{clavarium}/api/box/ws` with `Authorization: Bearer <key>`.

- `401`: the key was withdrawn. The box forgets its settings and goes back to setup.
- Connection lost: the box reconnects by itself after 1, 2, 4… up to 60 seconds, showing "offline" meanwhile.

Text frames carry one JSON object each, with its type in `t`. Binary frames carry audio:
**PCM 16 kHz, 16-bit, mono, little-endian**, in 20 ms pieces (640 bytes).

### Box → Clavarium

| `t` | Fields | When |
|---|---|---|
| `hello` | `firmware`, `capabilities`, `rssi`, `volume`, `muted` | on every connection |
| `listen.start` | `reason`: `button` \| `wake` | a sentence begins; binary frames follow |
| `listen.end` | `reason`: `silence` \| `button` \| `timeout` (15 s at most) | the sentence ends |
| `event` | `name`: `button` (+`alert`), `button.long`, `mute.on`, `mute.off`, `battery` (+`level`), `error` (+`detail`) | when it happens |
| `wifi.scan.result` | `id`, `networks`: [{`ssid`, `rssi`, `secure`}] | answers `wifi.scan` |
| `ack` | `id`, `ok`, `detail`? | answers any command that has an `id` |
| `ping` | — | every 30 s |

### Clavarium → box

| `t` | Fields | Effect |
|---|---|---|
| `cue` | `name`: `thinking` \| `error` \| `done` | plays the cue and shows the state |
| `speak.start` | `rate`: 16000 | binary frames follow, played as they come |
| `speak.end` | — | the answer is complete; back to idle once played |
| `alert` | `id`, `text`? | chimes (3 times at most), orange until a press; then `event` `button` and `ack` |
| `led` | `id`, `pattern`: `off` \| `solid` \| `pulse` \| `spin`, `color`: `#RRGGBB` | the ring |
| `volume` | `id`, `level`: 0-100 | the volume, kept |
| `wifi.scan` | `id` | answered with `wifi.scan.result` |
| `wifi.join` | `id`, `ssid`, `password` | tries it, keeps the old one if it fails; `ack` once back online |
| `reboot` | `id` | restarts after its `ack` |
| `ota` | `id`, `url`, `sha256` | downloads, checks, installs, restarts |
| `pong` | — | answers `ping` |

An unknown command with an `id` gets `ack` `ok: false`, `detail: "unknown"`; anything unexpected
is ignored, never fatal.

## Privacy

Nothing leaves the microphone outside a sentence, and nothing is recorded on the box. Mute stops
the microphone. The Wi-Fi password and the key stay in the box's storage: never logged, never sent back.
