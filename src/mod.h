// Modulation je Stimme: zwei LFOs und eine Mod-Huellkurve (Attack/Decay), jeder mit einem Ziel.
// Gerechnet wird in der Steuerrate der Stimme. Die Werte der Stimme (pf) bleiben unberuehrt -
// moduliert wird eine Kopie (pm), aus der Klang und Filter lesen.
#pragma once
#include "param.h"
#include "dsp.h"
#include <stdio.h>
#include <math.h>

enum { LFO_SIN, LFO_TRI, LFO_SAW, LFO_SQR, LFO_SH, LFO_EXP, LFO_RAMP, LFO_WAVES };
enum { LFO_FREE, LFO_TRIG, LFO_ONE, LFO_HOLD, LFO_HALF, LFO_MODES };

inline const char* lfoWaveName(int i) {
    static const char* n[LFO_WAVES] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "S&H", "EXP", "RAMP"};
    return n[i < 0 ? 0 : (i >= LFO_WAVES ? LFO_WAVES - 1 : i)];
}
inline const char* lfoModeName(int i) {
    static const char* n[LFO_MODES] = {"FREE", "TRIG", "ONE", "HOLD", "HALF"};
    return n[i < 0 ? 0 : (i >= LFO_MODES ? LFO_MODES - 1 : i)];
}
// Geschwindigkeit als Notenwert - ein LFO, der nicht im Takt laeuft, ist in einer Groovebox selten gemeint.
struct LfoRate { const char* name; float beats; };
static const LfoRate LFO_RATES[] = {
    {"16 BARS", 64}, {"8 BARS", 32}, {"4 BARS", 16}, {"2 BARS", 8}, {"1 BAR", 4}, {"1/2", 2},
    {"1/4.", 1.5f}, {"1/4", 1}, {"1/4T", 2.0f / 3}, {"1/8.", 0.75f}, {"1/8", 0.5f}, {"1/8T", 1.0f / 3},
    {"1/16", 0.25f}, {"1/16T", 1.0f / 6}, {"1/32", 0.125f}};
enum { LFO_NRATES = (int)(sizeof LFO_RATES / sizeof *LFO_RATES) };

// Ziele: jeder lockbare Spurwert. Die ersten elf bleiben in dieser Reihenfolge - so steht der
// Index im Dokument. "trig" heisst: wird beim Anschlag einmal abgetastet (VEL, LEN, PROB ...),
// alle anderen bewegen sich waehrend die Note klingt.
struct ModDest { const char* name; const char* id; float range; bool trig; };
static const ModDest MOD_DESTS[] = {
    {"NONE", nullptr, 0, false}, {"PITCH", "trk.note", 31.5f, false}, {"SRC A", "trk.src.a", 127, false},
    {"SRC B", "trk.src.b", 127, false}, {"CUTOFF", "trk.flt.freq", 127, false}, {"RESO", "trk.flt.reso", 127, false},
    {"FLT ENV", "trk.flt.env", 127, false}, {"VOLUME", "trk.vol", 127, false}, {"PAN", "trk.pan", 127, false},
    {"SEND A", "trk.send.a", 127, false}, {"SEND B", "trk.send.b", 127, false},
    {"FLT TYPE", "trk.flt.type", 10, false}, {"AMP ATK", "trk.amp.atk", 127, false},
    {"AMP DEC", "trk.amp.dec", 127, false}, {"AMP SUS", "trk.amp.sus", 127, false},
    {"AMP REL", "trk.amp.rel", 127, false},
    {"VELOCITY", "trk.vel", 127, true}, {"LENGTH", "trk.len", 126, true}, {"PROB", "trk.prob", 100, true},
    {"MICRO", "trk.micro", 48, true}, {"RETRIG", "trk.ratchet", 7, true}, {"KEY", "trk.quant", 1, true},
    {"NT2", "trk.nt2", 49, true}, {"NT3", "trk.nt3", 49, true}, {"NT4", "trk.nt4", 49, true},
    {"SAMPLE", "trk.smp", 63, true},
    {"SRC C", "trk.src.c", 127, false}, {"SRC D", "trk.src.d", 127, false}, {"SRC E", "trk.src.e", 127, false},
    {"SRC F", "trk.src.f", 127, false}, {"SRC G", "trk.src.g", 127, false}, {"SRC H", "trk.src.h", 127, false},
    {"BW BASE", "trk.bw.base", 127, false}, {"BW WIDTH", "trk.bw.width", 127, false},
    {"FILTER ENV SOURCE", "trk.flt.envmode", 1, true},
    {"FILTER ATTACK", "trk.flt.atk", 127, true},
    {"FILTER DECAY", "trk.flt.dec", 127, true},
    {"FILTER SUSTAIN", "trk.flt.sus", 100, true},
    {"FILTER RELEASE", "trk.flt.rel", 127, true},
    {"OSC 1 WAVE", "trk.poly1.wave", 3, false},
    {"OSC 1 VOL", "trk.poly1.level", 100, false},
    {"OSC 1 TUNE", "trk.poly1.tune", 48, false},
    {"OSC 1 FINE", "trk.poly1.fine", 200, false},
    {"OSC 1 PW", "trk.poly1.pw", 95, false},
    {"OSC 2 WAVE", "trk.poly2.wave", 3, false},
    {"OSC 2 VOL", "trk.poly2.level", 100, false},
    {"OSC 2 TUNE", "trk.poly2.tune", 48, false},
    {"OSC 2 FINE", "trk.poly2.fine", 200, false},
    {"OSC 2 PW", "trk.poly2.pw", 95, false},
    {"OP C RATIO", "trk.fm1.ratio", 1600, false},
    {"OP C LEVEL", "trk.fm1.level", 101, false},
    {"OP C FINE", "trk.fm1.fine", 200, false},
    {"OP C ENV", "trk.fm1.env", 1, true},
    {"OP C ATK", "trk.fm1.atk", 127, true},
    {"OP C DEC", "trk.fm1.dec", 127, true},
    {"OP C SUS", "trk.fm1.sus", 100, true},
    {"OP C REL", "trk.fm1.rel", 127, true},
    {"OP A RATIO", "trk.fm2.ratio", 1600, false},
    {"OP A LEVEL", "trk.fm2.level", 101, false},
    {"OP A FINE", "trk.fm2.fine", 200, false},
    {"OP A ENV", "trk.fm2.env", 1, true},
    {"OP A ATK", "trk.fm2.atk", 127, true},
    {"OP A DEC", "trk.fm2.dec", 127, true},
    {"OP A SUS", "trk.fm2.sus", 100, true},
    {"OP A REL", "trk.fm2.rel", 127, true},
    {"OP B1 RATIO", "trk.fm3.ratio", 1600, false},
    {"OP B1 LEVEL", "trk.fm3.level", 101, false},
    {"OP B1 FINE", "trk.fm3.fine", 200, false},
    {"OP B1 ENV", "trk.fm3.env", 1, true},
    {"OP B1 ATK", "trk.fm3.atk", 127, true},
    {"OP B1 DEC", "trk.fm3.dec", 127, true},
    {"OP B1 SUS", "trk.fm3.sus", 100, true},
    {"OP B1 REL", "trk.fm3.rel", 127, true},
    {"OP B2 RATIO", "trk.fm4.ratio", 1600, false},
    {"OP B2 LEVEL", "trk.fm4.level", 101, false},
    {"OP B2 FINE", "trk.fm4.fine", 200, false},
    {"OP B2 ENV", "trk.fm4.env", 1, true},
    {"OP B2 ATK", "trk.fm4.atk", 127, true},
    {"OP B2 DEC", "trk.fm4.dec", 127, true},
    {"OP B2 SUS", "trk.fm4.sus", 100, true},
    {"OP B2 REL", "trk.fm4.rel", 127, true},
    {"SLICE", "trk.sample.slice", 63, true}, {"FOLLOW", "trk.sample.follow", 2, true}};
enum { MOD_NDEST = (int)(sizeof MOD_DESTS / sizeof *MOD_DESTS) };
inline int modDestSlot(int d) {
    static int s[MOD_NDEST] = {-2};
    if (s[0] == -2) for (int i = 0; i < MOD_NDEST; i++) s[i] = MOD_DESTS[i].id ? paramSlot(paramFind(MOD_DESTS[i].id)) : -1;
    return (d <= 0 || d >= MOD_NDEST) ? -1 : s[d];
}
// Welches Ziel gehoert zu einem Spur-Slot? (-1: keins)
inline int modDestOfSlot(int slot) {
    struct DestMap {
        int bySlot[VOICE_PARAM_CAP];
        DestMap() {
            for (int& d : bySlot) d = -1;
            for (int d = 1; d < MOD_NDEST; d++) bySlot[modDestSlot(d)] = d;
        }
    };
    static const DestMap map;
    return slot >= 0 && slot < VOICE_PARAM_CAP ? map.bySlot[slot] : -1;
}
// Bereich eines Ziels - einmal nachgeschlagen (vorher lief je Steuertakt eine Namenssuche).
inline void modDestRange(int d, float& lo, float& hi) {
    static float L[MOD_NDEST], H[MOD_NDEST];
    static bool ok = false;
    if (!ok) {
        for (int i = 1; i < MOD_NDEST; i++) { const ParamDef& pd = PARAMS[paramFind(MOD_DESTS[i].id)]; L[i] = pd.lo; H[i] = pd.hi; }
        ok = true;
    }
    lo = L[d]; hi = H[d];
}

struct ModSlots { int wave[2], rate[2], depth[2], dest[2], mode[2], speed[2], mult[2], fade[2], phase[2], eAtk, eDec, eDepth, eDest, eMode; };
// Die Tabelle wird einmal gebaut; modSlots() selbst bleibt klein genug zum Einbetten
// (sie wird pro Stimme und Block mehrfach gefragt).
inline const ModSlots& modSlotsBuild() {
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
            snprintf(b, sizeof b, "%sspd", w[k]);   m.speed[k] = paramSlot(paramFind(b));
            snprintf(b, sizeof b, "%smult", w[k]);  m.mult[k]  = paramSlot(paramFind(b));
            snprintf(b, sizeof b, "%sfade", w[k]);  m.fade[k]  = paramSlot(paramFind(b));
            snprintf(b, sizeof b, "%sphase", w[k]); m.phase[k] = paramSlot(paramFind(b));
        }
        m.eAtk = paramSlot(paramFind("trk.menv.atk"));   m.eDec  = paramSlot(paramFind("trk.menv.dec"));
        m.eDepth = paramSlot(paramFind("trk.menv.depth")); m.eDest = paramSlot(paramFind("trk.menv.dest"));
        m.eMode = paramSlot(paramFind("trk.menv.mode"));
        ok = true;
    }
    return m;
}
inline const ModSlots& modSlots() { static const ModSlots& m = modSlotsBuild(); return m; }

struct ModState {
    float ph[2] = {0, 0}, sh[2] = {0, 0};
    uint32_t rng = 0x9e3779b9u;
    double spb = 24000.0;       // Samples je Viertel - aus dem Tempo beim Anschlag
    int64_t origin = 0;
    uint32_t trackSeed = 0;
    uint64_t t = 0;             // Samples seit dem Anschlag (fuer die Mod-Huellkurve)
    float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng >> 8) * (2.0f / 16777216.0f) - 1.0f; }

    // pos = Transport-Position des Anschlags in Samples: FREE-LFOs laufen am Takt, nicht an der Note.
    void start(const float* pf, double samplesPerBeat, int64_t pos, uint32_t seed, uint32_t modulationSeed = 0, int64_t globalTime = -1) {
        const ModSlots& m = modSlots();
        spb = samplesPerBeat > 1 ? samplesPerBeat : 24000.0;
        origin = globalTime >= 0 ? globalTime : (pos >= 0 ? pos : 0);
        trackSeed = modulationSeed;
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
        case LFO_EXP: return (1.0f - ph) * (1.0f - ph) * (1.0f - ph);
        case LFO_RAMP: return ph;
        default:      return sh;
        }
    }
    static int modeOf(const float* pf, int k) {
        return (int)(pf[modSlots().mode[k]] + 0.5f);
    }
    static bool extended(const float* pf, int k) {
        const auto& m = modSlots();
        return pf[m.speed[k]] > -64.5f || pf[m.phase[k]] != 0 || pf[m.fade[k]] != 0
            || modeOf(pf, k) >= LFO_HOLD;
    }
    static double cyclesPerSample(const float* pf, int k, double spb, float sr) {
        const auto& m = modSlots();
        float speed = pf[m.speed[k]];
        if (speed < -64.5f) return 1.0 / (spb * LFO_RATES[clampRate(pf[m.rate[k]])].beats);
        int mult = (int)pf[m.mult[k]];
        if (mult < 0) mult = 0;
        if (mult > 23) mult = 23;
        if (mult >= 12) spb = sr * 0.5;
        return speed * (double)(1u << (mult % 12)) / (512.0 * spb);
    }
    static float newLfo(const float* pf, int k, int64_t pos, uint64_t elapsed,
                        double spb, float sr, uint32_t seed) {
        const auto& m = modSlots();
        int mode = modeOf(pf, k);
        double inc = cyclesPerSample(pf, k, spb, sr);
        double travel = inc * (double)elapsed;
        if (mode == LFO_FREE || mode == LFO_HOLD)
            travel = inc * ((double)pos + (mode == LFO_FREE ? (double)elapsed : 0.0));
        if (mode == LFO_ONE || mode == LFO_HALF) {
            double span = mode == LFO_ONE ? 1.0 - 1e-6 : 0.5;
            travel = travel < -span ? -span : travel > span ? span : travel;
        }
        double cycle = travel + pf[m.phase[k]] / 128.0;
        int64_t whole = (int64_t)cycle;
        if (cycle < whole) whole--;
        float phase = (float)(cycle - whole);
        uint32_t h = seed * 2654435761u ^ (uint32_t)whole * 40503u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        float result = wave((int)(pf[m.wave[k]] + 0.5f), phase, (h >> 8) * (2.0f / 16777216.0f) - 1.0f);
        float fade = pf[m.fade[k]];
        if (fade) {
            // One quarter note at magnitude 16, with a useful range from 1/16 to four beats.
            float progress = (float)(elapsed * 16.0 / (spb * fabsf(fade)));
            if (progress > 1.0f) progress = 1.0f;
            result *= fade < 0 ? progress : 1.0f - progress;
        }
        return result;
    }
    static float envelope(const float* pf, double samples, float sr) {
        const auto& m = modSlots();
        float a = pTime(pf[m.eAtk]) * sr, dc = pTime(pf[m.eDec]) * sr;
        bool loop = pf[m.eMode] > 0.5f;
        if (loop) {
            double period = a + dc;
            samples -= (int64_t)(samples / period) * period;
        }
        if (samples < a) return (float)samples / a;
        float tail = expf(-((float)samples - a) / (dc * 0.4f));
        if (!loop) return tail;
        return tail > 0.082085f ? (tail - 0.082085f) / (1.0f - 0.082085f) : 0.0f;
    }

    // Einen Steuertakt weiter (n Samples) und die Modulation auf pm anwenden.
    bool apply(const float* pf, float* pm, int n, float sr) {
        const ModSlots& m = modSlots();
        // Hoechstens drei Ziele (2 LFOs, Mod-Huellkurve): nur diese werden angefasst, statt alle
        // Ziele durchzugehen. Summiert wird in derselben Reihenfolge wie vorher - bitgleich.
        int nd = 0, dd[3]; float off[3];
        auto add = [&](int d, float v) {
            for (int i = 0; i < nd; i++) if (dd[i] == d) { off[i] += v; return; }
            dd[nd] = d; off[nd] = 0.0f; off[nd] += v; nd++;
        };
        for (int k = 0; k < 2; k++) {
            int d = (int)(pf[m.dest[k]] + 0.5f);
            int mode = (int)(pf[m.mode[k]] + 0.5f);
            if (extended(pf, k)) {
                if (d > 0 && d < MOD_NDEST && pf[m.depth[k]] != 64.0f)
                    add(d, pBip(pf[m.depth[k]]) * newLfo(pf, k, origin, t, spb, sr, trackSeed + k));
                continue;
            }
            float per = (float)(spb * LFO_RATES[clampRate(pf[m.rate[k]])].beats);
            // Ohne Ziel (oder Tiefe 0) wird die Welle nicht gebraucht - die Phase laeuft trotzdem weiter.
            if (d > 0 && d < MOD_NDEST && pf[m.depth[k]] != 64.0f)
                add(d, pBip(pf[m.depth[k]]) * wave((int)(pf[m.wave[k]] + 0.5f), ph[k], sh[k]));
            else if (d > 0 && d < MOD_NDEST) add(d, 0.0f);
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
                double elapsed = (double)t + (pf[m.eMode] > 0.5f ? origin : 0);
                add(d, pBip(pf[m.eDepth]) * envelope(pf, elapsed, sr));
            }
            t += (uint32_t)n;
        }
        bool amp = false;
        for (int i = 0; i < nd; i++) {
            int d = dd[i];
            if (off[i] == 0.0f || MOD_DESTS[d].trig) continue;      // Anschlag-Ziele: siehe trigOffset
            int sl = modDestSlot(d);
            float v = pf[sl] + off[i] * MOD_DESTS[d].range;
            float lo, hi; modDestRange(d, lo, hi);
            pm[sl] = v < lo ? lo : (v > hi ? hi : v);
            if (d >= 12 && d <= 15) amp = true;
        }
        return amp;
    }

    // Wert von LFO k an einer Transport-Position, ohne Stimme: fuer Ziele, die nur beim Anschlag
    // zaehlen. FREE folgt dem Takt; TRIG und ONE SHOT stehen beim Anschlag am Anfang.
    static float lfoAt(const float* pf, int k, int64_t pos, double spb, uint32_t seed) {
        const ModSlots& m = modSlots();
        int mode = (int)(pf[m.mode[k]] + 0.5f);
        double per = spb * LFO_RATES[clampRate(pf[m.rate[k]])].beats;
        double c = (mode == LFO_FREE && pos >= 0 && per > 1) ? (double)pos / per : 0.0;
        int64_t zyklus = (int64_t)c;
        float ph = (float)(c - (double)zyklus);
        uint32_t h = seed * 2654435761u ^ (uint32_t)zyklus * 40503u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        float sh = (h >> 8) * (2.0f / 16777216.0f) - 1.0f;
        return wave((int)(pf[m.wave[k]] + 0.5f), ph, sh);
    }
    // Versatz (in Werte-Einheiten) fuer ein Anschlag-Ziel - Summe aus beiden LFOs und der
    // Mod-Huellkurve (die beim Anschlag bei 0 steht, ausser ihr Attack ist 0).
    static float trigOffset(const float* pf, int d, int64_t pos, double spb, uint32_t seed, float sr = 48000, int64_t globalTime = -1) {
        if (d <= 0 || d >= MOD_NDEST) return 0.0f;
        const ModSlots& m = modSlots();
        int64_t phaseTime = globalTime >= 0 ? globalTime : (pos < 0 ? 0 : pos);
        float o = 0.0f;
        for (int k = 0; k < 2; k++)
            if ((int)(pf[m.dest[k]] + 0.5f) == d) {
                float value = extended(pf, k) ? newLfo(pf, k, phaseTime, 0, spb, sr, seed + k)
                                               : lfoAt(pf, k, pos, spb, seed + k);
                o += pBip(pf[m.depth[k]]) * value;
            }
        if ((int)(pf[m.eDest] + 0.5f) == d) {
            if (pf[m.eMode] > 0.5f) o += pBip(pf[m.eDepth]) * envelope(pf, phaseTime, sr);
            else if (pf[m.eAtk] < 0.5f) o += pBip(pf[m.eDepth]);
        }
        return o * MOD_DESTS[d].range;
    }
};
