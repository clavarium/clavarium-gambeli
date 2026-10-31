// Runs the box's own end-of-sentence detection (main/endpoint.c) on recorded tracks.
// Each track: 16 kHz int16 raw; its first `idle` seconds are the room before the wake word.
// Prints, per track: name and the second the sentence was closed (or -1), and why.
#include "../../main/endpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(int argc, char **argv)
{
    char name[512]; double idle;
    while (scanf("%511s %lf", name, &idle) == 2) {
        ep_forget();
        FILE *f = fopen(name, "rb");
        if (!f) { printf("%s -1 missing\n", name); continue; }
        short pcm[320]; long chunk = 0; int idle_chunks = (int)(idle * 50); double end = -1; const char *why = "never";
        while (fread(pcm, 2, 320, f) == 320) {
            double e = 0; for (int i = 0; i < 320; i++) e += (double)pcm[i] * pcm[i];
            float rms = sqrtf(e / 320);
            if (chunk < idle_chunks) ep_room(rms);
            else {
                if (chunk == idle_chunks) ep_begin();
                if (chunk >= idle_chunks + 15) {   // the box skips its "ding" (15 chunks)
                    ep_result_t r = ep_chunk(rms);
                    if (r != EP_GOING) { end = (chunk + 1) / 50.0; why = r == EP_SILENCE ? "silence" : r == EP_NOBODY ? "nobody" : "toolong"; break; }
                }
            }
            chunk++;
        }
        fclose(f);
        printf("%s %.2f %s\n", name, end, why);
    }
    return 0;
}
