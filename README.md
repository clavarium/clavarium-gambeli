# Gambeli

**Gambeli** (the mountain chickadee) is a voice box for Clavarium: a small speaker in a room that you talk to. A press on the
button starts a sentence; the box sends it to Clavarium, which hands it to an assistant, and plays
the answer back. The ring of LEDs always says what the box is doing. It keeps no sound.

Board: **Waveshare ESP32-S3-AUDIO-Board** (ESP32-S3, 8 MB PSRAM, 16 MB flash, ES7210 microphones,
ES8311 speaker codec, 7 RGB LEDs, three keys on a TCA9555 expander). The pins come from Waveshare's
own examples.

## What it does

- **Wakes on a word** said in the room -- « Mésange », a microWakeWord model trained for it (training/mesange) --
  or on a press of the button. Deaf to its own voice: a second detector hears what it plays.
- **Hears a whole sentence**, up to five minutes: the end is found against the room's own noise (a TV, a
  fan), held through pauses; tuned on a simulator of thousands of real recordings (tools/sim).
- **Says the answer** in Clavarium's voice, sentence by sentence, at a level eased to the room; its own
  speech is never cut by a new one.
- **Plays a radio, a podcast, a story**, the assistant's voice over it; records a long meeting on request.
- **Says why it listened** (the word and its score, the button, a conversation going on) and what it cut short.

| Moment | Sound | Ring |
|---|---|---|
| Listening | rising | steady blue |
| Got it | two light notes | turning blue |
| Stopped listening | two falling notes | lights out |
| A message comes | a small bell | warm breathing |
| Recording | — | slow red glow |
| Alert | chime | bright red until a press: the only bright light |
| Offline | — | a faint amber glow |
| Updating | — | violet, turning |

By night (its place's time, from Clavarium) the lights fall to a few percent, with an hour's fade.

## With Clavarium

The box talks to a server over one WebSocket: its protocol is in **PROTOCOL.md** (Clavarium's DEVICES.md is
the same). Clavarium is the server today -- pairing, dictation, voice, the assistant -- but the protocol is
open: any server that speaks it can have the box.

## Setup

Not configured yet (or after a 5 s press), the box opens an open Wi-Fi network **Gambeli-XXXX**.
A phone joining it gets the setup page by itself: choose the Wi-Fi and its password, give
Clavarium's address, the room, and the pairing code shown by Clavarium. The box joins the Wi-Fi,
pairs (see [PROTOCOL.md](PROTOCOL.md)), and restarts connected. On failure the page says why.

## Build and flash

ESP-IDF 5.4:

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

For a browser flash (Chrome or Edge, Web Serial), one image holds everything:

```sh
idf.py merge-bin -o gambeli-full.bin    # write it at 0x0
```

## Setup over the cable

Right after a browser flash, the flash page can give the Wi-Fi (and the pairing) over the same
cable: Improv Wi-Fi, see [PROTOCOL.md](PROTOCOL.md). The access point stays for a box moved elsewhere.

## Without Clavarium

`test/server.mjs` speaks the same protocol: pairing code `TEST-0000`, every sentence saved as a
`.wav`, then played back to the box (or `--reply answer.wav`):

```sh
npm install
npm run server -- --port 8080      # then setup with http://<this computer>:8080
npm test
```

## Not yet

- The wake word: microWakeWord (Home Assistant), "Hey Jarvis" for now (model: esphome/micro-wake-word-models, Apache-2.0), built into the program; a double knock on the box is a press.
- Secure Boot, so that an update must be signed: needs a signing key held by the owner.
- NVS encryption (flash encryption).

## Its state, from a phone

On the same Wi-Fi, `http://gambeli-XXXX.local/` (XXXX as in its setup Wi-Fi) shows its firmware, its Wi-Fi
and signal, whether it reaches Clavarium, and if not, the last reason. No key, no password on it.
