// Sample-Vorrat. Ein gemeinsamer Speicherblock, aus dem die Plaetze der Reihe nach bedient
// werden: im Audio-Thread wird nie etwas angefordert, geladen wird ausserhalb.
// Mono - ein Stereo-Sample wird beim Laden zusammengelegt (Stereo kommt spaeter).
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>

enum { SMP_SLOTS = 64, SMP_POOL = 1 << 21, SMP_NAME = 24 };   // 2 Mio. Samples = rund 44 s

struct SamplePool {
    struct Slot {
        int32_t at = 0, n = 0;      // Lage im Block
        float   rate = 48000.0f;    // Aufnahmerate: bestimmt die Grundstimmung
        uint8_t loop = 0;
        char    name[SMP_NAME] = {0};
    };
    float data[SMP_POOL];
    Slot  s[SMP_SLOTS];
    int   used = 0;                 // so weit ist der Block belegt

    void clear() {
        used = 0;
        for (int i = 0; i < SMP_SLOTS; i++) { s[i].n = 0; s[i].at = 0; s[i].name[0] = 0; }
    }
    bool empty(int i) const { return i < 0 || i >= SMP_SLOTS || s[i].n <= 0; }

    // Legt ein Sample ab. Der Block waechst nur nach vorn; wer einen Platz neu belegt,
    // laesst den alten Bereich liegen - erst clear() gibt ihn zurueck.
    bool set(int slot, const float* src, int n, float rate, const char* name, bool loop = false) {
        if (slot < 0 || slot >= SMP_SLOTS || n <= 0) return false;
        if (used + n > SMP_POOL) return false;
        for (int i = 0; i < n; i++) data[used + i] = src[i];
        s[slot].at = used;
        s[slot].n = n;
        s[slot].rate = rate > 1.0f ? rate : 48000.0f;
        s[slot].loop = loop ? 1 : 0;
        snprintf(s[slot].name, SMP_NAME, "%s", name ? name : "");
        used += n;
        return true;
    }
    int freeFrames() const { return SMP_POOL - used; }
};
