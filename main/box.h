// The voice box: shared declarations between the modules.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define BOX_RATE 16000            // PCM everywhere: 16 kHz, 16-bit, mono
#define BOX_CHUNK 320             // 20 ms of samples
#define BOX_TYPE "voice-box/1"
#define BOX_CAPS "[\"mic\",\"speaker\",\"led-ring:7\",\"button\",\"mute\",\"wifi\",\"ble\",\"wake:hey-jarvis\"]"

// board.c: codecs, amplifier, keys
enum { KEY_MAIN = 1, KEY_MUTE = 2, KEY_THIRD = 4 };
void board_init(void);
int board_mic_read(int16_t *mono, int samples);      // blocks; returns samples read
int board_mic_read_raw(int16_t *four, int samples);  // the same, all four channels: R M N M
void board_play(const int16_t *mono, int samples);   // blocks until queued to the codec
void board_volume(int level);                        // 0-100
void board_amp(bool on);
uint32_t board_keys(void);                           // KEY_* pressed now

// leds.c
typedef enum { LED_OFF, LED_SOLID, LED_PULSE, LED_SPIN, LED_BREATHE, LED_BLINK2, LED_DOT, LED_OFFLINE, LED_ALERT } led_pattern_t;
void leds_init(void);
void leds_set(led_pattern_t pattern, uint32_t rgb);  // 0xRRGGBB

// sounds.c: short cues, synthesised, never fetched
typedef enum { SND_DING, SND_TIC, SND_ERROR, SND_CHIME, SND_CLICK, SND_DONE, SND_HEARD } sound_t;
void sounds_init(void);                              // starts the playback task
void sounds_play(sound_t s);
void speech_feed(const uint8_t *pcm, size_t len);    // response audio from Clavarium
void speech_stop(void);                              // drop what is queued
bool speech_idle(void);                              // nothing left to play
bool speech_waiting(void);                           // speech received, not played yet
bool speech_held(void);                              // main.c: true while someone speaks to the box

// store.c: settings in NVS; the key and the Wi-Fi password never leave it
typedef struct { char ssid[33], pass[65], url[128], room[48], id[48], key[64]; int volume; } box_conf_t;
extern box_conf_t conf;
bool store_load(void);                               // true when paired
void store_save(void);
void store_volume(int level);
void store_wipe(void);

// portal.c + dns.c: the setup access point
void portal_run(const char *why);                    // never returns: reboots once paired
void dns_start(void);

// link.c: the permanent connection
typedef enum { EV_KEY_SHORT, EV_KEY_LONG, EV_LINK_UP, EV_LINK_DOWN, EV_MSG, EV_SPEECH_DONE, EV_LISTEN_END, EV_TICK, EV_WAKE } ev_type_t;
typedef struct { ev_type_t type; int arg; char *text; } box_ev_t;
void post_event(ev_type_t type, int arg, char *text);
void link_start(void);
bool link_send_text(const char *json);
bool link_send_audio(const int16_t *pcm, int samples);
int  link_rssi(void);
const char *link_problem(void);                      // the last reason it could not reach Clavarium, or ""
int  link_tries(void);
bool link_up(void);
const char *link_was(void);                          // what kept it from Clavarium before this connection
void status_start(void);
void knock_note(int peak, int rms, int quiet);
int knock_seen(int i, int what);                     // i: 0 the newest; what: 0 peak, 1 rms, 2 background                             // a page of its state on its own address, and its name on the network
extern volatile bool speech_accept;                  // binary frames are played only while set

// improv.c: Wi-Fi (and pairing) over the USB line, from the flash page
void improv_start(void);

// mww.cc: the wake word, heard on the chip (microWakeWord)
bool mww_start(void);
void mww_feed(const int16_t *pcm, int n);            // mono, 16 kHz

// ota.c
bool ota_install(const char *url, const char *sha256, char *detail, size_t detail_len);

// Wi-Fi helpers (portal.c)
void wifi_base_init(void);
bool wifi_join(const char *ssid, const char *pass, int timeout_ms);
int  wifi_scan_json(char *out, size_t out_len);      // [{"ssid","rssi","secure"}]
bool wifi_connected(void);
bool pair_box(const char *code, char *why, size_t why_len);
