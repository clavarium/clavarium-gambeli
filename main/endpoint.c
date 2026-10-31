/* Settings chosen on the simulator's 240 tracks (tools/sim, 31-10): sentences cut 14 -> 2.

   THE BAR FOLLOWS THE ROOM, as the page's dictation does (web/voice-core.js): the floor is a low quantile of
   the last thirty seconds, and a steady noise that started lately (a TV) raises the bar at once. A sound counts
   as a voice only when held as long as a syllable (a click, a breath do not). The box is further from a mouth
   than a phone is: START times the room to start; once a sentence has started, CARRY times the room as it was
   then carries it on (3 times cut a voice mid-sentence; 1.6 times the quiet room alone kept listening to a TV). */
#include "endpoint.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef EP_START
#define EP_START 2.0f
#endif
#ifndef EP_CARRY
#define EP_CARRY 1.5f
#endif
#ifndef EP_SILENCE_CHUNKS
#define EP_SILENCE_CHUNKS 125      // 2.5 s
#endif
#define ROOM 1500                  // thirty seconds
#define RECENT 150                 // three seconds
#define HELD 8                     // 160 ms: a syllable
#ifndef EP_ABSOLUTE
#define EP_ABSOLUTE 60.0f
#endif
#define ABSOLUTE EP_ABSOLUTE
#ifndef EP_RELATIVE
#define EP_RELATIVE 0.3f          // a started sentence: its own voice, not much quieter than it began (a TV behind is)
#endif
#define LONGEST_CHUNKS (300 * 50)   // five minutes: only a TV that never stops

static float room[ROOM], recent[RECENT], floor_ = ABSOLUTE, at_start = ABSOLUTE;
static int room_n, room_at, recent_n, recent_at, since;
static int chunks, quiet, run, nobody_chunks = 15 * 50;
static bool heard;
static float voice, smooth;   // how loud this speaker is (smoothed peak of the voiced chunks)

static int by_value(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }

static float learn(float rms)
{
    static float sorted[ROOM];
    room[room_at] = rms; room_at = (room_at + 1) % ROOM; if (room_n < ROOM) room_n++;
    recent[recent_at] = rms; recent_at = (recent_at + 1) % RECENT; if (recent_n < RECENT) recent_n++;
    if (++since >= 16 && room_n >= 50) {
        since = 0;
        memcpy(sorted, room, room_n * sizeof(float));
        qsort(sorted, room_n, sizeof(float), by_value);
        floor_ = sorted[room_n / 10];
    }
    float steady = 0;
    if (recent_n >= RECENT) { steady = recent[0]; for (int i = 1; i < RECENT; i++) if (recent[i] < steady) steady = recent[i]; }
    return fmaxf(fmaxf(floor_, steady), ABSOLUTE);
}

void ep_room(float rms) { learn(rms); }
void ep_begin(int nobody_seconds) { nobody_chunks = nobody_seconds * 50; chunks = quiet = run = 0; heard = false; voice = smooth = 0; }
bool ep_heard(void) { return heard; }
float ep_bar(void) { return heard ? fmaxf(at_start * EP_CARRY, voice * EP_RELATIVE) : at_start * EP_START; }

ep_result_t ep_chunk(float rms)
{
    float room_now = learn(rms);
    if (!heard) at_start = room_now;
    chunks++;
    smooth = smooth * 0.8f + rms * 0.2f;
    if (run >= HELD - 1 && smooth > voice) voice = smooth;
    if (rms >= ep_bar()) { if (++run >= HELD) heard = true; quiet = 0; }
    else { run = 0; quiet++; }
    if (heard && quiet >= EP_SILENCE_CHUNKS) return EP_SILENCE;
    if (!heard && chunks >= nobody_chunks) return EP_NOBODY;
    if (chunks >= LONGEST_CHUNKS) return EP_TOO_LONG;
    return EP_GOING;
}

/* The simulator: a new room for each track. */
void ep_forget(void)
{
    memset(room, 0, sizeof room); memset(recent, 0, sizeof recent);
    room_n = room_at = recent_n = recent_at = since = 0; floor_ = at_start = ABSOLUTE;
    ep_begin(15);
}
