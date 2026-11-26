// Sample-Vorrat. Ein gemeinsamer Speicherblock, aus dem die Plaetze der Reihe nach bedient
// werden: im Audio-Thread wird nie etwas angefordert, geladen wird ausserhalb.
// Mono oder Stereo; Stereo liegt verschraenkt (L,R,L,R) im selben Block.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "doc.h"

enum { SMP_SLOTS = 64, SMP_POOL = 1 << 21, SMP_NAME = 24, SMP_SLICES = 64 };   // 2 Mio. Samples = rund 44 s

// Run once while importing audio, outside render(). The envelope correlation searches
// for repeated onsets. Short or unpitched one-shots can have no useful estimate.
inline int estimateSampleBpm(const float* data, int frames, float rate, int channels) {
    if (!data || frames < 1 || !(rate >= 1000.f && rate <= 384000.f) || (channels != 1 && channels != 2)) return 0;
    const int hop = (int)(rate / 200.0f + 0.5f);
    const float hz = rate / hop;
    enum { MAX_HOPS = 4000 };
    float flux[MAX_HOPS];
    const int n = frames / hop < MAX_HOPS ? frames / hop : MAX_HOPS;
    float env = 0.0f, sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float energy = 0.0f;
        for (int j = 0; j < hop * channels; j++) {
            const float v = data[i * hop * channels + j];
            energy += v * v;
        }
        energy = sqrtf(energy / (hop * channels));
        const float rise = energy - env;
        flux[i] = rise > 0.0f ? rise : 0.0f;
        sum += flux[i] * flux[i];
        env += (rise > 0.0f ? 0.8f : 0.16f) * rise;
    }
    float best = 0.24f;
    int period = 0;
    if (sum > 1e-6f && n >= (int)(hz * 1.1f)) {
        for (int lag = (int)(hz * 0.30f); lag <= (int)hz && lag * 2 < n; lag++) {
            float same = 0.0f, left = 0.0f, right = 0.0f;
            for (int i = lag; i < n; i++) {
                const float a = flux[i - lag], b = flux[i];
                same += a * b; left += a * a; right += b * b;
            }
            if (left <= sum * 0.12f || right <= sum * 0.12f) continue;
            const float score = same / sqrtf(left * right);
            // At a tie prefer the shorter period over an exact multiple.
            if (score > best + 0.018f) { best = score; period = lag; }
        }
    }
    if (period) return (int)(60.0f * hz / period + 0.5f);
    // Approximate a short loop from its duration at a fixed reference tempo.
    const float seconds = frames / rate;
    if (seconds < 0.25f) return 0;
    float beats = 0.5f;
    while (beats * 1.41421356f < seconds * 2.0f && beats < 1024.f) beats *= 2.0f;
    return (int)(60.f * beats / seconds + 0.5f);
}

struct SamplePool {
    struct Slot {
        int estimatedBpm = 0;      // computed at import, never in the audio callback
        int32_t at = 0, n = 0;      // Lage im Block, n = Bilder (nicht Werte)
        float   rate = 48000.0f;    // Aufnahmerate: bestimmt die Grundstimmung
        uint8_t loop = 0;
        uint8_t ch = 1;             // 1 = mono, 2 = stereo verschraenkt
        char    name[SMP_NAME] = {0};
        // Ausschnitt, samplegenau: gespielt wird von a bis b; mit Loop springt es von b
        // nach la zurueck. b = 0 heisst "bis zum Ende".
        int32_t a = 0, b = 0, la = 0;
        int32_t cuts[SMP_SLICES - 1] = {};
        int cutCount = 0;

        // Markers outside the current trim are retained for later edits.
        int sliceCount(int grid) const {
            if (!cutCount) return grid;
            int count = 1;
            for (int i = 0; i < cutCount; i++) if (cuts[i] > a && cuts[i] < end()) count++;
            return count;
        }
        int sliceBoundary(int index, int grid) const {
            if (index <= 0) return a;
            if (!cutCount) return a + (int)((int64_t)(end() - a) * index / grid);
            for (int i = 0; i < cutCount; i++)
                if (cuts[i] > a && cuts[i] < end() && --index == 0) return cuts[i];
            return end();
        }
        int32_t end() const { return (b > a && b <= n) ? b : n; }
        int32_t loopStart() const { return (la >= a && la < end()) ? la : a; }
    };
    float data[SMP_POOL];
    Slot  s[SMP_SLOTS];
    int   used = 0;                 // so weit ist der Block belegt

    void clear() {
        used = 0;
        for (int i = 0; i < SMP_SLOTS; i++) { s[i] = Slot{}; }
    }
    bool empty(int i) const { return i < 0 || i >= SMP_SLOTS || s[i].n <= 0; }

    // Legt ein Sample ab. Der Block waechst nur nach vorn; wer einen Platz neu belegt,
    // laesst den alten Bereich liegen - erst clear() gibt ihn zurueck.
    bool set(int slot, const float* src, int n, float rate, const char* name,
             bool loop = false, int ch = 1, int estimatedBpm = 0) {
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
        s[slot].estimatedBpm = estimatedBpm;
        s[slot].loop = loop ? 1 : 0;
        s[slot].a = 0; s[slot].b = 0; s[slot].la = 0;
        snprintf(s[slot].name, SMP_NAME, "%s", name ? name : "");
        used += n * ch;
        if (!wunsch) s[slot].cutCount = 0;
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

    bool setCut(int slot, int index, int frame) {
        if (empty(slot)) return false;
        Slot& sl = s[slot];
        if (index < 0 || index >= sl.cutCount) return false;
        int lo = index ? sl.cuts[index - 1] : 0;
        int hi = index + 1 < sl.cutCount ? sl.cuts[index + 1] : sl.n;
        if (frame - lo < 2 || hi - frame < 2) return false;
        sl.cuts[index] = frame;
        return true;
    }
    int addCut(int slot, int frame) {
        if (empty(slot)) return -1;
        Slot& sl = s[slot];
        if (frame <= 0 || frame >= sl.n || sl.cutCount == SMP_SLICES - 1) return -1;
        int i = 0;
        while (i < sl.cutCount && sl.cuts[i] < frame) i++;
        int lo = i ? sl.cuts[i - 1] : 0;
        int hi = i < sl.cutCount ? sl.cuts[i] : sl.n;
        if (frame - lo < 2 || hi - frame < 2) return -1;
        for (int j = sl.cutCount++; j > i; j--) sl.cuts[j] = sl.cuts[j - 1];
        sl.cuts[i] = frame;
        return i;
    }
    bool removeCut(int slot, int index) {
        if (empty(slot) || index < 0 || index >= s[slot].cutCount) return false;
        Slot& sl = s[slot];
        for (int i = index + 1; i < sl.cutCount; i++) sl.cuts[i - 1] = sl.cuts[i];
        sl.cutCount--;
        return true;
    }
    void sliceGrid(int slot, int count) {
        if (empty(slot) || count < 2 || count > SMP_SLICES) return;
        Slot& sl = s[slot];
        sl.cutCount = 0;
        for (int i = 1; i < count; i++) {
            int f = sl.a + (int)((int64_t)(sl.end() - sl.a) * i / count);
            if (f > sl.a) addCut(slot, f);
        }
    }
    int detectTransients(int slot, int32_t* cuts) const {
        if (empty(slot)) return 0;
        const Slot& sl = s[slot];
        const int window = (int)(sl.rate * 0.005f) + 1;
        const int spacing = window * 6;
        float peak = 0.0f;
        int last = sl.a;
        int count = 0;
        for (int f = sl.a; f < sl.end() - 1 && count < SMP_SLICES - 1; f += window) {
            float energy = 0.0f;
            for (int j = f; j < sl.end() && j < f + window; j++) {
                for (int c = 0; c < sl.ch; c++) {
                    float v = data[sl.at + j * sl.ch + c];
                    energy += v * v;
                }
            }
            energy /= window * sl.ch;
            if (f - last >= spacing && energy > 0.001f && energy > peak * 4.0f) {
                cuts[count++] = f;
                last = f;
            }
            peak = energy > peak ? energy : peak * 0.75f;
        }
        return count;
    }
    void setCuts(int slot, const int32_t* cuts, int count) {
        if (empty(slot)) return;
        Slot& sl = s[slot];
        memcpy(sl.cuts, cuts, (size_t)count * sizeof sl.cuts[0]);
        sl.cutCount = count;
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
        s[slot] = Slot{};
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
// Ein Platz unter einem Schluessel-Stamm ("smp.3" im Projekt, "smp" in einem Klang-Preset)
inline void slotToDoc(const SamplePool& p, int i, Doc& d, const char* base) {
    char k[DOC_KEY];
    snprintf(k, sizeof k, "%s.name", base);   d.setStr(k, p.s[i].name);
    snprintf(k, sizeof k, "%s.rate", base);   d.set(k, (int32_t)p.s[i].rate);
    snprintf(k, sizeof k, "%s.ch", base);     d.set(k, p.s[i].ch);
    for (int j = 0; j < p.s[i].cutCount; j++) {
        snprintf(k, sizeof k, "%s.cut.%d", base, j);
        d.set(k, p.s[i].cuts[j]);
    }
    if (p.s[i].loop) { snprintf(k, sizeof k, "%s.loop", base); d.set(k, 1); }
    if (p.s[i].a)    { snprintf(k, sizeof k, "%s.a", base);    d.set(k, p.s[i].a); }
    if (p.s[i].b)    { snprintf(k, sizeof k, "%s.b", base);    d.set(k, p.s[i].b); }
    if (p.s[i].la)   { snprintf(k, sizeof k, "%s.la", base);   d.set(k, p.s[i].la); }
}
inline void poolToDoc(const SamplePool& p, Doc& d) {
    char b[16];
    for (int i = 0; i < SMP_SLOTS; i++) {
        if (p.s[i].n <= 0 && !p.s[i].name[0]) continue;
        snprintf(b, sizeof b, "smp.%d", i);
        slotToDoc(p, i, d, b);
    }
}
// Beim Laden bleibt ein Sample liegen, das schon da ist und denselben Namen traegt.
// Alle anderen Plaetze werden zu Wuenschen: Name bekannt, Klang fehlt.
// Ein Platz aus dem Dokument (Name nicht leer): liegt dort schon dasselbe Sample, bleibt es
// und bekommt nur den Ausschnitt; sonst wird der Platz zum Wunsch, den die Huelle erfuellt.
inline void slotFromDoc(const Doc& d, const char* base, SamplePool& p, int i) {
    char k[DOC_KEY];
    snprintf(k, sizeof k, "%s.name", base);
    char nm[SMP_NAME]; snprintf(nm, sizeof nm, "%s", d.getStr(k, ""));
    p.s[i].cutCount = 0;
    for (int j = 0, prev = 0; j < SMP_SLICES - 1; j++) {
        snprintf(k, sizeof k, "%s.cut.%d", base, j);
        int frame = d.get(k, 0);
        if (frame <= prev) break;
        p.s[i].cuts[p.s[i].cutCount++] = frame;
        prev = frame;
    }
    if (p.s[i].n > 0 && !strcmp(p.s[i].name, nm)) {             // schon da: nur Ausschnitt
        snprintf(k, sizeof k, "%s.a", base);  int32_t a = d.get(k, 0);
        snprintf(k, sizeof k, "%s.b", base);  int32_t b = d.get(k, 0);
        snprintf(k, sizeof k, "%s.la", base); int32_t la = d.get(k, 0);
        snprintf(k, sizeof k, "%s.loop", base);
        p.setTrim(i, a, b, la, d.get(k, 0) != 0);
        return;
    }
    p.s[i].n = 0;
    p.s[i].estimatedBpm = 0;
    snprintf(p.s[i].name, SMP_NAME, "%s", nm);
    snprintf(k, sizeof k, "%s.rate", base); p.s[i].rate = (float)d.get(k, 48000);
    snprintf(k, sizeof k, "%s.ch", base);   p.s[i].ch = (uint8_t)d.get(k, 1);
    snprintf(k, sizeof k, "%s.loop", base); p.s[i].loop = (uint8_t)d.get(k, 0);
    // Der Ausschnitt wartet mit, bis der Klang da ist - dann wird er angewendet.
    snprintf(k, sizeof k, "%s.a", base);  p.s[i].a = d.get(k, 0);
    snprintf(k, sizeof k, "%s.b", base);  p.s[i].b = d.get(k, 0);
    snprintf(k, sizeof k, "%s.la", base); p.s[i].la = d.get(k, 0);
}
inline void poolFromDoc(const Doc& d, SamplePool& p) {
    char k[DOC_KEY], b[16];
    for (int i = 0; i < SMP_SLOTS; i++) {
        snprintf(b, sizeof b, "smp.%d", i);
        snprintf(k, sizeof k, "%s.name", b);
        if (!d.getStr(k, "")[0]) {
            p.s[i].cutCount = 0;
            if (p.s[i].n <= 0) p.s[i].name[0] = 0;
            continue;
        }
        slotFromDoc(d, b, p, i);
    }
}
