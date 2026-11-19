// Sample-Vorrat. Ein gemeinsamer Speicherblock, aus dem die Plaetze der Reihe nach bedient
// werden: im Audio-Thread wird nie etwas angefordert, geladen wird ausserhalb.
// Mono oder Stereo; Stereo liegt verschraenkt (L,R,L,R) im selben Block.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "doc.h"

enum { SMP_SLOTS = 64, SMP_POOL = 1 << 21, SMP_NAME = 24 };   // 2 Mio. Samples = rund 44 s

struct SamplePool {
    struct Slot {
        int32_t at = 0, n = 0;      // Lage im Block, n = Bilder (nicht Werte)
        float   rate = 48000.0f;    // Aufnahmerate: bestimmt die Grundstimmung
        uint8_t loop = 0;
        uint8_t ch = 1;             // 1 = mono, 2 = stereo verschraenkt
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
    bool set(int slot, const float* src, int n, float rate, const char* name,
             bool loop = false, int ch = 1) {
        if (slot < 0 || slot >= SMP_SLOTS || n <= 0) return false;
        if (ch < 1) ch = 1;
        if (ch > 2) ch = 2;
        if (used + n * ch > SMP_POOL) return false;
        for (int i = 0; i < n * ch; i++) data[used + i] = src[i];
        s[slot].at = used;
        s[slot].n = n;
        s[slot].ch = (uint8_t)ch;
        s[slot].rate = rate > 1.0f ? rate : 48000.0f;
        s[slot].loop = loop ? 1 : 0;
        snprintf(s[slot].name, SMP_NAME, "%s", name ? name : "");
        used += n * ch;
        return true;
    }
    // Platz fuer eine Aufnahme freihalten, ohne schon Daten zu haben.
    int reserve(int slot, int frames, float rate, const char* name, int ch = 1) {
        if (slot < 0 || slot >= SMP_SLOTS || frames <= 0) return -1;
        if (ch < 1) ch = 1;
        if (ch > 2) ch = 2;
        if (used + frames * ch > SMP_POOL) return -1;
        int at = used;
        s[slot].at = at; s[slot].n = 0; s[slot].ch = (uint8_t)ch;
        s[slot].rate = rate > 1.0f ? rate : 48000.0f; s[slot].loop = 0;
        snprintf(s[slot].name, SMP_NAME, "%s", name ? name : "");
        used += frames * ch;
        return at;
    }
    int freeFrames() const { return SMP_POOL - used; }

    // Ein Platz, dessen Name bekannt ist, dessen Klang aber fehlt: das Projekt verlangt ihn.
    bool wanted(int i) const { return i >= 0 && i < SMP_SLOTS && s[i].n <= 0 && s[i].name[0]; }
};

// --- Der Vorrat im Dokument ---------------------------------------------------------------
// Geschrieben werden NAMEN, nicht Klaenge: eine Projektdatei bleibt klein, und wer sie
// oeffnet, sieht, welche Samples sie verlangt. Das Suchen ist Sache der Huelle - am Geraet
// die Karte, im Browser der eigene Speicher.
inline void poolToDoc(const SamplePool& p, Doc& d) {
    char k[DOC_KEY];
    for (int i = 0; i < SMP_SLOTS; i++) {
        if (p.s[i].n <= 0 && !p.s[i].name[0]) continue;
        snprintf(k, sizeof k, "smp.%d.name", i);   d.setStr(k, p.s[i].name);
        snprintf(k, sizeof k, "smp.%d.rate", i);   d.set(k, (int32_t)p.s[i].rate);
        snprintf(k, sizeof k, "smp.%d.ch", i);     d.set(k, p.s[i].ch);
        if (p.s[i].loop) { snprintf(k, sizeof k, "smp.%d.loop", i); d.set(k, 1); }
    }
}
// Beim Laden bleibt ein Sample liegen, das schon da ist und denselben Namen traegt.
// Alle anderen Plaetze werden zu Wuenschen: Name bekannt, Klang fehlt.
inline void poolFromDoc(const Doc& d, SamplePool& p) {
    char k[DOC_KEY];
    for (int i = 0; i < SMP_SLOTS; i++) {
        snprintf(k, sizeof k, "smp.%d.name", i);
        const char* nm = d.getStr(k, "");
        if (!nm[0]) {
            if (p.s[i].n <= 0) p.s[i].name[0] = 0;
            continue;
        }
        if (p.s[i].n > 0 && !strcmp(p.s[i].name, nm)) continue;     // schon da
        p.s[i].n = 0;
        snprintf(p.s[i].name, SMP_NAME, "%s", nm);
        snprintf(k, sizeof k, "smp.%d.rate", i); p.s[i].rate = (float)d.get(k, 48000);
        snprintf(k, sizeof k, "smp.%d.ch", i);   p.s[i].ch = (uint8_t)d.get(k, 1);
        snprintf(k, sizeof k, "smp.%d.loop", i); p.s[i].loop = (uint8_t)d.get(k, 0);
    }
}
