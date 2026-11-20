// Modulation je Stimme: zwei LFOs und eine Mod-Huellkurve (Attack/Decay), jeder mit einem Ziel.
// Gerechnet wird in der Steuerrate der Stimme. Die Werte der Stimme (pf) bleiben unberuehrt -
// moduliert wird eine Kopie (pm), aus der Klang und Filter lesen.
#pragma once
#include "param.h"
#include "dsp.h"
#include <stdio.h>
#include <math.h>

enum { LFO_SIN, LFO_TRI, LFO_SAW, LFO_SQR, LFO_SH, LFO_WAVES };
enum { LFO_FREE, LFO_TRIG, LFO_ONE, LFO_MODES };

inline const char* lfoWaveName(int i) {
    static const char* n[LFO_WAVES] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "S&H"};
    return n[i < 0 ? 0 : (i >= LFO_WAVES ? LFO_WAVES - 1 : i)];
}
inline const char* lfoModeName(int i) {
    static const char* n[LFO_MODES] = {"FREE", "TRIG", "ONE SHOT"};
    return n[i < 0 ? 0 : (i >= LFO_MODES ? LFO_MODES - 1 : i)];
}
// Geschwindigkeit als Notenwert - ein LFO, der nicht im Takt laeuft, ist in einer Groovebox selten gemeint.
struct LfoRate { const char* name; float beats; };
static const LfoRate LFO_RATES[] = {
    {"16 BARS", 64}, {"8 BARS", 32}, {"4 BARS", 16}, {"2 BARS", 8}, {"1 BAR", 4}, {"1/2", 2},
    {"1/4.", 1.5f}, {"1/4", 1}, {"1/4T", 2.0f / 3}, {"1/8.", 0.75f}, {"1/8", 0.5f}, {"1/8T", 1.0f / 3},
    {"1/16", 0.25f}, {"1/16T", 1.0f / 6}, {"1/32", 0.125f}};
enum { LFO_NRATES = (int)(sizeof LFO_RATES / sizeof *LFO_RATES) };

// Ziele: was sich waehrend eines Tons sinnvoll bewegen laesst.
struct ModDest { const char* name; const char* id; float range; };
static const ModDest MOD_DESTS[] = {
    {"NONE", nullptr, 0}, {"PITCH", "trk.note", 31.5f}, {"SRC A", "trk.src.a", 127},
    {"SRC B", "trk.src.b", 127}, {"CUTOFF", "trk.flt.freq", 127}, {"RESO", "trk.flt.reso", 127},
    {"FLT ENV", "trk.flt.env", 127}, {"VOLUME", "trk.vol", 127}, {"PAN", "trk.pan", 127},
    {"SEND A", "trk.send.a", 127}, {"SEND B", "trk.send.b", 127}};
enum { MOD_NDEST = (int)(sizeof MOD_DESTS / sizeof *MOD_DESTS) };
inline int modDestSlot(int d) {
    static int s[MOD_NDEST] = {-2};
    if (s[0] == -2) for (int i = 0; i < MOD_NDEST; i++) s[i] = MOD_DESTS[i].id ? paramSlot(paramFind(MOD_DESTS[i].id)) : -1;
    return (d <= 0 || d >= MOD_NDEST) ? -1 : s[d];
}

struct ModSlots { int wave[2], rate[2], depth[2], dest[2], mode[2], eAtk, eDec, eDepth, eDest; };
inline const ModSlots& modSlots() {
    static ModSlots m;
    static bool ok = false;
    if (!ok) {
        const char* w[2] = {"trk.lfo1.", "trk.lfo2."};
        char b[32];
        for (int k = 0; k < 2; k++) {
            snprintf(b, sizeof b, "%swave", w[k]);  m.wave[k]  = paramSlot(paramFind(b));
            snprintf(b, sizeof b, "%srate", w[k]);  m.rate[k]  = paramSlot(paramFind(b));
            snprintf(b, sizeof b, "%sdepth", w[k]); m.depth[k] = paramSlot(paramFind(b));
            snprintf(b, sizeof b, "%sdest", w[k]);  m.dest[k]  = paramSlot(paramFind(b));
            snprintf(b, sizeof b, "%smode", w[k]);  m.mode[k]  = paramSlot(paramFind(b));
        }
        m.eAtk = paramSlot(paramFind("trk.menv.atk"));   m.eDec  = paramSlot(paramFind("trk.menv.dec"));
        m.eDepth = paramSlot(paramFind("trk.menv.depth")); m.eDest = paramSlot(paramFind("trk.menv.dest"));
        ok = true;
    }
    return m;
}

struct ModState {
    float ph[2] = {0, 0}, sh[2] = {0, 0};
    uint32_t rng = 0x9e3779b9u;
    double spb = 24000.0;       // Samples je Viertel - aus dem Tempo beim Anschlag
    uint32_t t = 0;             // Samples seit dem Anschlag (fuer die Mod-Huellkurve)
    float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng >> 8) * (2.0f / 16777216.0f) - 1.0f; }

    // pos = Transport-Position des Anschlags in Samples: FREE-LFOs laufen am Takt, nicht an der Note.
    void start(const float* pf, double samplesPerBeat, int64_t pos, uint32_t seed) {
        const ModSlots& m = modSlots();
        spb = samplesPerBeat > 1 ? samplesPerBeat : 24000.0;
        t = 0; rng = seed * 2654435761u + 1;
        for (int k = 0; k < 2; k++) {
            int mode = (int)(pf[m.mode[k]] + 0.5f);
            float per = (float)(spb * LFO_RATES[clampRate(pf[m.rate[k]])].beats);
            if (mode == LFO_FREE && pos >= 0) { double c = (double)pos / per; ph[k] = (float)(c - (double)(int64_t)c); }
            else ph[k] = 0.0f;
            sh[k] = rnd();
        }
    }
    static int clampRate(float v) { int i = (int)(v + 0.5f); return i < 0 ? 0 : (i >= LFO_NRATES ? LFO_NRATES - 1 : i); }
    static float wave(int w, float ph, float sh) {
        switch (w) {
        case LFO_SIN: return sinf(6.2831853f * ph);
        case LFO_TRI: return ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f;
        case LFO_SAW: return 1.0f - 2.0f * ph;
        case LFO_SQR: return ph < 0.5f ? 1.0f : -1.0f;
        default:      return sh;
        }
    }
    // Einen Steuertakt weiter (n Samples) und die Modulation auf pm anwenden.
    void apply(const float* pf, float* pm, int n, float sr) {
        const ModSlots& m = modSlots();
        float off[MOD_NDEST] = {0};
        for (int k = 0; k < 2; k++) {
            int d = (int)(pf[m.dest[k]] + 0.5f);
            int mode = (int)(pf[m.mode[k]] + 0.5f);
            float per = (float)(spb * LFO_RATES[clampRate(pf[m.rate[k]])].beats);
            float w = wave((int)(pf[m.wave[k]] + 0.5f), ph[k], sh[k]);
            if (d > 0 && d < MOD_NDEST) off[d] += pBip(pf[m.depth[k]]) * w;
            if (mode == LFO_ONE && ph[k] >= 1.0f) continue;
            ph[k] += (float)n / per;
            if (ph[k] >= 1.0f) {
                if (mode == LFO_ONE) ph[k] = 1.0f - 1e-6f;      // bleibt am Ende stehen
                else { ph[k] -= (float)(int)ph[k]; sh[k] = rnd(); }
            }
        }
        {   // Mod-Huellkurve: linear hinauf, exponentiell hinab
            int d = (int)(pf[m.eDest] + 0.5f);
            if (d > 0 && d < MOD_NDEST) {
                float a = pTime(pf[m.eAtk]) * sr, dc = pTime(pf[m.eDec]) * sr;
                float e = (float)t < a ? (float)t / a : expf(-((float)t - a) / (dc * 0.4f));
                off[d] += pBip(pf[m.eDepth]) * e;
            }
            t += (uint32_t)n;
        }
        for (int d = 1; d < MOD_NDEST; d++) {
            if (off[d] == 0.0f) continue;
            int sl = modDestSlot(d);
            float v = pf[sl] + off[d] * MOD_DESTS[d].range;
            float lo = 0.0f, hi = 127.0f;
            pm[sl] = v < lo ? lo : (v > hi ? hi : v);
        }
    }
};
