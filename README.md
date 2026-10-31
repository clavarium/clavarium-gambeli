# Gambeli

**Gambeli** (the mountain chickadee) is a voice box for Clavarium: a small speaker in a room that you talk to. A press on the
button starts a sentence; the box sends it to Clavarium, which hands it to an assistant, and plays
the answer back. The ring of LEDs always says what the box is doing. It keeps no sound.

Board: **Waveshare ESP32-S3-AUDIO-Board** (ESP32-S3, 8 MB PSRAM, 16 MB flash, ES7210 microphones,
ES8311 speaker codec, 7 RGB LEDs, three keys on a TCA9555 expander). The pins come from Waveshare's
own examples.

## What it does

| State | Sound | Ring |
|---|---|---|
| Setup (its own Wi-Fi) | — | blue, slow pulse |
| Idle, connected | — | off (a red dot when muted) |
| Listening (short press) | rising "ding" | blue |
| Thinking | soft tick | blue, turning |
| Speaking | the answer | white, breathing |
| Error | two notes down | red, two blinks |
| Alert | chime, 3 times at most | orange until a press |
| Offline | — | amber blink every 5 s |
| Updating | — | violet, turning |

Keys: the first one listens (and stops an answer to listen again); held 5 s it opens setup. The
second one mutes the microphone. The third one listens too.

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

- A wake word (esp-sr): only the button for now.
- Secure Boot, so that an update must be signed: needs a signing key held by the owner.
- NVS encryption (flash encryption).
