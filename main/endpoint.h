// Where a sentence ends: pure arithmetic on the loudness of 20 ms chunks, shared by the box and by the
// simulator on a computer (tools/sim), which tunes it on hundreds of recordings.
#pragma once
#include <stdbool.h>

typedef enum { EP_GOING, EP_SILENCE, EP_NOBODY, EP_TOO_LONG } ep_result_t;

void ep_room(float rms);        // between sentences: the room is learnt all the time
void ep_begin(void);            // a sentence may start now
ep_result_t ep_chunk(float rms);// one 20 ms chunk of the sentence
bool ep_heard(void);            // a voice was heard in this sentence
float ep_bar(void);             // for the status page and the simulator
void ep_forget(void);           // the simulator: a new room
