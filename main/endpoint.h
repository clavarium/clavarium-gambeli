// Where a sentence ends: pure arithmetic on the loudness of 20 ms chunks, shared by the box and by the
// simulator on a computer (tools/sim), which tunes it on hundreds of recordings.
#pragma once
#include <stdbool.h>

typedef enum { EP_GOING, EP_SILENCE, EP_NOBODY, EP_TOO_LONG } ep_result_t;

void ep_room(float rms);        // between sentences: the room is learnt all the time
void ep_begin(int nobody_seconds);  // a sentence may start now; nobody speaking that long: EP_NOBODY
ep_result_t ep_chunk(float rms);// one 20 ms chunk of the sentence
bool ep_heard(void);            // a voice was heard in this sentence
float ep_bar(void);             // for the status page and the simulator
void ep_forget(void);
float ep_room_level(void);      // the room's floor now: quiet ~50-100, noisy several hundred           // the simulator: a new room
