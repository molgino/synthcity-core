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
        // Ausschnitt, samplegenau: gespielt wird von a bis b; mit Loop springt es von b
        // nach la zurueck. b = 0 heisst "bis zum Ende".
        int32_t a = 0, b = 0, la = 0;
        int32_t end() const { return (b > a && b <= n) ? b : n; }
        int32_t loopStart() const { return (la >= a && la < end()) ? la : a; }
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
        // Kommt ein Klang fuer einen Wunsch gleichen Namens, behaelt er dessen Ausschnitt.
        const bool wunsch = s[slot].n <= 0 && name && !strcmp(s[slot].name, name);
        const int32_t wa = s[slot].a, wb = s[slot].b, wla = s[slot].la;
        const uint8_t wl = s[slot].loop;
        for (int i = 0; i < n * ch; i++) data[used + i] = src[i];
        s[slot].at = used;
        s[slot].n = n;
        s[slot].ch = (uint8_t)ch;
        s[slot].rate = rate > 1.0f ? rate : 48000.0f;
        s[slot].loop = loop ? 1 : 0;
        s[slot].a = 0; s[slot].b = 0; s[slot].la = 0;
        snprintf(s[slot].name, SMP_NAME, "%s", name ? name : "");
        used += n * ch;
        if (wunsch) setTrim(slot, wa, wb, wla, wl != 0 || loop);
        return true;
    }

    // Ausschnitt setzen - immer in sich stimmig: a < b, la im Ausschnitt.
    void setTrim(int slot, int32_t a, int32_t b, int32_t la, bool loop) {
        if (slot < 0 || slot >= SMP_SLOTS || s[slot].n <= 0) return;
        Slot& sl = s[slot];
        if (b <= 0 || b > sl.n) b = sl.n;
        if (a < 0) a = 0;
        if (a > b - 2) a = b - 2 < 0 ? 0 : b - 2;
        if (la < a) la = a;
        if (la > b - 2) la = b - 2 < a ? a : b - 2;
        sl.a = a; sl.b = (b == sl.n) ? 0 : b; sl.la = la; sl.loop = loop ? 1 : 0;
    }

    // Naechster Nulldurchgang nach oben in der Naehe von f - an solchen Stellen knackt es
    // nicht, wenn das Sample beginnt, endet oder in die Schleife springt.
    int32_t zeroCross(int slot, int32_t f, int32_t radius = 2048) const {
        if (slot < 0 || slot >= SMP_SLOTS || s[slot].n <= 1) return f;
        const Slot& sl = s[slot];
        auto v = [&](int32_t i) {
            const float* d = data + sl.at + i * sl.ch;
            return sl.ch == 2 ? 0.5f * (d[0] + d[1]) : d[0];
        };
        if (f < 1) return 0;
        if (f >= sl.n) return sl.n;
        for (int32_t r = 0; r <= radius; r++) {
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                int32_t i = f + sgn * r;
                if (i < 1 || i >= sl.n) continue;
                if (v(i - 1) <= 0.0f && v(i) > 0.0f) return i;
            }
        }
        return f;                                      // keiner in der Naehe: Stelle bleibt
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
        if (p.s[i].a)    { snprintf(k, sizeof k, "smp.%d.a", i);    d.set(k, p.s[i].a); }
        if (p.s[i].b)    { snprintf(k, sizeof k, "smp.%d.b", i);    d.set(k, p.s[i].b); }
        if (p.s[i].la)   { snprintf(k, sizeof k, "smp.%d.la", i);   d.set(k, p.s[i].la); }
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
        if (p.s[i].n > 0 && !strcmp(p.s[i].name, nm)) {             // schon da: nur Ausschnitt
            snprintf(k, sizeof k, "smp.%d.a", i);  int32_t a = d.get(k, 0);
            snprintf(k, sizeof k, "smp.%d.b", i);  int32_t b = d.get(k, 0);
            snprintf(k, sizeof k, "smp.%d.la", i); int32_t la = d.get(k, 0);
            snprintf(k, sizeof k, "smp.%d.loop", i);
            p.setTrim(i, a, b, la, d.get(k, 0) != 0);
            continue;
        }
        p.s[i].n = 0;
        snprintf(p.s[i].name, SMP_NAME, "%s", nm);
        snprintf(k, sizeof k, "smp.%d.rate", i); p.s[i].rate = (float)d.get(k, 48000);
        snprintf(k, sizeof k, "smp.%d.ch", i);   p.s[i].ch = (uint8_t)d.get(k, 1);
        snprintf(k, sizeof k, "smp.%d.loop", i); p.s[i].loop = (uint8_t)d.get(k, 0);
        // Der Ausschnitt wartet mit, bis der Klang da ist - dann wird er angewendet.
        snprintf(k, sizeof k, "smp.%d.a", i);  p.s[i].a = d.get(k, 0);
        snprintf(k, sizeof k, "smp.%d.b", i);  p.s[i].b = d.get(k, 0);
        snprintf(k, sizeof k, "smp.%d.la", i); p.s[i].la = d.get(k, 0);
    }
}
