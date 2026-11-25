// Datenmodell: Projekt -> Pattern -> Track -> Step. Der wirksame Wert ist immer dieselbe Kette
// Lock -> (spaeter Scene) -> Track-Grundwert. Locks liegen duenn besetzt als Liste, nicht als
// Feld ueber alle Parameter - ein Step traegt selten mehr als eine Handvoll.
#pragma once
#include "param.h"
#include "doc.h"
#include "scale.h"

enum { NTRACKS = 8, NSTEPS = 128, NPATTERNS = 16, NLOCKS = 12, NSCENES = 2, NSCLOCK = 16 };

struct Lock {
    uint16_t slot;      // Slot des Spurparameters (Reihenfolge der Tabelle)
    int16_t  val;
    uint8_t  slide;     // 1 = bis zum naechsten Trig auf diesen Wert gleiten
};

struct Step {
    uint8_t on = 0;     // 0 leer, 1 Trig, 2 trigless (nur Werte, kein Anschlag)
    uint8_t nLock = 0;
    Lock lock[NLOCKS];

    const Lock* findLock(int slot) const {
        for (int i = 0; i < nLock; i++) if (lock[i].slot == slot) return &lock[i];
        return nullptr;
    }
    bool setLock(int slot, int16_t v, uint8_t slide = 0) {
        for (int i = 0; i < nLock; i++)
            if (lock[i].slot == slot) { lock[i].val = v; lock[i].slide = slide; return true; }
        if (nLock >= NLOCKS) return false;
        lock[nLock++] = {(uint16_t)slot, v, slide};
        return true;
    }
};

struct Track {
    int16_t p[TRACK_PARAM_CAP] = {};              // Spurparameter nach Slot
    uint8_t len = 16;           // Steps je Durchlauf (Polymetrik)
    uint8_t speed = 4;          // Taktteiler: 4 = 16tel, 2 = 8tel, 8 = 32stel
    Step s[NSTEPS];
};

// Eine Scene haelt je Spur eine Handvoll abweichender Werte. Der Morph-Regler faehrt
// stufenlos zwischen Scene A und Scene B; wo eine Scene nichts sagt, gilt der Grundwert.
struct SceneTrack {
    uint8_t n = 0;
    Lock v[NSCLOCK];
    const Lock* find(int slot) const {
        for (int i = 0; i < n; i++) if (v[i].slot == slot) return &v[i];
        return nullptr;
    }
    bool set(int slot, int16_t val) {
        for (int i = 0; i < n; i++) if (v[i].slot == slot) { v[i].val = val; return true; }
        if (n >= NSCLOCK) return false;
        v[n++] = {(uint16_t)slot, val, 0};
        return true;
    }
    bool clear(int slot) {
        for (int i = 0; i < n; i++) if (v[i].slot == slot) { v[i] = v[--n]; return true; }
        return false;
    }
};

struct Pattern {
    Track t[NTRACKS];
    SceneTrack sc[NSCENES][NTRACKS];
    uint8_t used = 0;
    int16_t bpm = 0;            // eigenes Tempo; 0 = das des Projekts
    uint8_t swing = 0;          // eigener Swing 50..80 %; 0 = der des Projekts
    uint8_t mult = SPEED_1X;    // Geschwindigkeit (Index in SPEEDS)
    uint8_t chg = 16;           // nach so vielen Steps wechselt ein angewaehltes Pattern
    uint8_t root = 0;           // Grundton 0..11 (C..B)
    uint8_t scale = 0;          // Tonleiter (Index in SCALES), 0 = chromatisch
    // FX-Spur: eigener Sequencer fuer die Projekt-Effekte. Ihre Steps sind trigless und locken
    // globale Werte (Delay, Reverb, Master) - Lock-Slots sind hier GLOBALE Slots.
    Track fx;
};
inline bool masterSetting(int i) {
    return PARAMS[i].scope == PS_GLOBAL && !strncmp(PARAMS[i].id, "master.", 7) && strcmp(PARAMS[i].id, "master.vol");
}
// Darf die FX-Spur diesen Projektwert locken? Effekte und Master ja, Tempo und Swing nicht.
inline bool fxLockable(int i) {
    return PARAMS[i].scope == PS_GLOBAL && (!strncmp(PARAMS[i].id, "fx.", 3) || !strcmp(PARAMS[i].id, "master.vol"));
}
inline void fxTrackClear(Track& f) {
    f.len = 16; f.speed = 4;
    for (int s = 0; s < NSTEPS; s++) { f.s[s].on = 0; f.s[s].nLock = 0; }
}

// Ein Song ist eine Kette von Zeilen (wie der Octatrack-Arranger): Pattern, wie oft, wie lang,
// ab welchem Step, in welchem Tempo, welche Spuren stumm, wohin die Scene steht. Dazu Zeilen,
// die springen (LOOP: zurueck zu Zeile pat, rep-mal; 0 = immer) oder anhalten (HALT).
// Am Ende geht es vorn weiter.
enum { NSONG = 64 };
enum { SONG_PAT = 0, SONG_LOOP = 1, SONG_HALT = 2 };
struct SongStep {
    uint8_t pat = 0, rep = 1;
    uint8_t type = SONG_PAT;
    uint8_t len = 0;            // Steps je Durchlauf; 0 = AUTO (Wechsellaenge des Patterns)
    uint8_t start = 0;          // ab diesem Step (in Sechzehnteln des Patterns)
    int16_t bpm = 0;            // 0 = Tempo des Patterns/Projekts
    uint16_t mute = 0;          // Bit t: Spur t ist in dieser Zeile stumm
    uint8_t morph = 0;          // 0 = Scene-Stellung bleibt; 1..101 = Morph 0..100 % (A..B)
};
struct Song {
    uint8_t  n = 0;
    SongStep e[NSONG];
};

struct Project {
    int16_t g[GLOBAL_PARAM_CAP];              // globale Parameter nach Slot
    Pattern pat[NPATTERNS];
    Song    song;
};

// --- Grundwerte aus der Parametertabelle --------------------------------------------------
inline void projectInit(Project& P) {
    P.song.n = 0;
    for (int i = 0; i < NPARAMS; i++) {
        const ParamDef& d = PARAMS[i];
        int slot = paramSlot(i);
        if (d.scope == PS_GLOBAL) P.g[slot] = d.def;
    }
    for (int pi = 0; pi < NPATTERNS; pi++) {
        P.pat[pi].used = 0; P.pat[pi].bpm = 0; P.pat[pi].swing = 0; P.pat[pi].mult = SPEED_1X;
        P.pat[pi].chg = 16; P.pat[pi].root = 0; P.pat[pi].scale = 0;
        for (int t = 0; t < NTRACKS; t++) {
            Track& tr = P.pat[pi].t[t];
            tr.len = 16; tr.speed = 4;
            for (int i = 0; i < NPARAMS; i++)
                if (PARAMS[i].scope == PS_TRACK) tr.p[paramSlot(i)] = PARAMS[i].def;
            for (int s = 0; s < NSTEPS; s++) { tr.s[s].on = 0; tr.s[s].nLock = 0; }
        }
        for (int k = 0; k < NSCENES; k++)
            for (int t = 0; t < NTRACKS; t++) P.pat[pi].sc[k][t].n = 0;
        fxTrackClear(P.pat[pi].fx);
    }
}

// Pattern kopieren und leeren - ohne das kann man kein zweites Pattern bauen.
inline void patternClear(Project& P, int i) {
    if (i < 0 || i >= NPATTERNS) return;
    Pattern& pt = P.pat[i];
    pt.used = 0; pt.bpm = 0; pt.swing = 0; pt.mult = SPEED_1X; pt.chg = 16; pt.root = 0; pt.scale = 0;
    for (int t = 0; t < NTRACKS; t++) {
        Track& tr = pt.t[t];
        tr.len = 16; tr.speed = 4;
        for (int k = 0; k < NPARAMS; k++)
            if (PARAMS[k].scope == PS_TRACK) tr.p[paramSlot(k)] = PARAMS[k].def;
        for (int s = 0; s < NSTEPS; s++) { tr.s[s].on = 0; tr.s[s].nLock = 0; }
        for (int k = 0; k < NSCENES; k++) pt.sc[k][t].n = 0;
    }
    fxTrackClear(pt.fx);
}
// Hat das Pattern irgendeinen Trig oder Lock? Dann gehoert es ins Dokument, auch wenn
// niemand es als benutzt markiert hat.
inline bool patternHasSteps(const Pattern& pt) {
    for (int t = 0; t < NTRACKS; t++)
        for (int s = 0; s < NSTEPS; s++)
            if (pt.t[t].s[s].on || pt.t[t].s[s].nLock) return true;
    for (int s = 0; s < NSTEPS; s++) if (pt.fx.s[s].nLock) return true;
    return false;
}

inline void patternCopy(Project& P, int from, int to) {
    if (from < 0 || from >= NPATTERNS || to < 0 || to >= NPATTERNS || from == to) return;
    P.pat[to] = P.pat[from];
    P.pat[to].used = 1;
}

// Wirksamer Wert eines Spurparameters auf einem Step (Lock schlaegt Grundwert).
inline int16_t effective(const Track& tr, int step, int slot) {
    const Lock* l = tr.s[step].findLock(slot);
    return l ? l->val : tr.p[slot];
}

// Dieselbe Kette mit Scenes: Lock -> Morph zwischen A und B -> Grundwert.
// Der Lock gewinnt immer; wo eine Scene nichts sagt, wird gegen den Grundwert gefahren.
inline float sceneValue(const Pattern& pt, int t, int slot, float morph) {
    float base = (float)pt.t[t].p[slot];
    const Lock* a = pt.sc[0][t].find(slot);
    const Lock* b = pt.sc[1][t].find(slot);
    if (!a && !b) return base;
    float va = a ? (float)a->val : base, vb = b ? (float)b->val : base;
    return va + (vb - va) * morph;
}
inline float effectiveF(const Pattern& pt, int t, int step, int slot, float morph) {
    const Lock* l = pt.t[t].s[step].findLock(slot);
    return l ? (float)l->val : sceneValue(pt, t, slot, morph);
}

// Keep the original file default. Writing 0 dB sustain explicitly lets older builds
// also play newly saved tracks at their intended level.
inline int trackDocDefault(int param) {
    static const int sustain = paramFind("trk.amp.sus");
    static const int sourceBpm = paramFind("trk.sample.bpm");
    if (param == sourceBpm) return SAMPLE_BPM_LEGACY;
    return param == sustain ? 0 : PARAMS[param].def;
}

// --- Dokument schreiben ---------------------------------------------------------------------
// Geschrieben wird nur, was vom Standard abweicht: die Datei bleibt klein und lesbar, und ein
// Parameter, den es spaeter nicht mehr gibt, stoert niemanden.
inline void projectToDoc(const Project& P, Doc& d) {
    char k[DOC_KEY], k2[DOC_KEY];
    if (P.song.n) {
        d.set("song.n", P.song.n);
        for (int i = 0; i < P.song.n; i++) {
            const SongStep& r = P.song.e[i];
            snprintf(k, sizeof k, "song.%d.pat", i); d.set(k, r.pat);
            snprintf(k, sizeof k, "song.%d.rep", i); d.set(k, r.rep);
            if (r.type)  { snprintf(k, sizeof k, "song.%d.type", i);  d.set(k, r.type); }
            if (r.len)   { snprintf(k, sizeof k, "song.%d.len", i);   d.set(k, r.len); }
            if (r.start) { snprintf(k, sizeof k, "song.%d.start", i); d.set(k, r.start); }
            if (r.bpm)   { snprintf(k, sizeof k, "song.%d.bpm", i);   d.set(k, r.bpm); }
            if (r.mute)  { snprintf(k, sizeof k, "song.%d.mute", i);  d.set(k, r.mute); }
            if (r.morph) { snprintf(k, sizeof k, "song.%d.morph", i); d.set(k, r.morph); }
        }
    }
    for (int i = 0; i < NPARAMS; i++) {
        if (PARAMS[i].scope != PS_GLOBAL) continue;
        int slot = paramSlot(i);
        if (P.g[slot] != PARAMS[i].def) d.set(PARAMS[i].id, P.g[slot]);
    }
    for (int pi = 0; pi < NPATTERNS; pi++) {
        const Pattern& pt = P.pat[pi];
        if (!pt.used && !patternHasSteps(pt)) continue;     // Steps gehen nie verloren
        snprintf(k, sizeof k, "pat.%d.used", pi); d.set(k, 1);
        if (pt.bpm) { snprintf(k, sizeof k, "pat.%d.bpm", pi); d.set(k, pt.bpm); }
        if (pt.swing) { snprintf(k, sizeof k, "pat.%d.swing", pi); d.set(k, pt.swing); }
        if (pt.mult != SPEED_1X) { snprintf(k, sizeof k, "pat.%d.mult", pi); d.set(k, pt.mult); }
        if (pt.chg != 16)  { snprintf(k, sizeof k, "pat.%d.chg", pi);   d.set(k, pt.chg); }
        if (pt.root)       { snprintf(k, sizeof k, "pat.%d.root", pi);  d.set(k, pt.root); }
        if (pt.scale)      { snprintf(k, sizeof k, "pat.%d.scale", pi); d.set(k, pt.scale); }
        for (int t = 0; t < NTRACKS; t++) {
            const Track& tr = pt.t[t];
            if (tr.len != 16)  { snprintf(k, sizeof k, "pat.%d.trk.%d.len", pi, t);   d.set(k, tr.len); }
            if (tr.speed != 4) { snprintf(k, sizeof k, "pat.%d.trk.%d.speed", pi, t); d.set(k, tr.speed); }
            for (int i = 0; i < NPARAMS; i++) {
                if (PARAMS[i].scope != PS_TRACK) continue;
                int slot = paramSlot(i);
                if (tr.p[slot] == trackDocDefault(i)) continue;
                static const int tempo = paramFind("trk.sample.bpm"), machineSlot = paramSlot(paramFind("trk.machine"));
                if (i == tempo && tr.p[slot] == SAMPLE_BPM_AUTO) {
                    const int machine = tr.p[machineSlot];
                    if (machine != MACHINE_SAMPLE && machine != MACHINE_LEGACY_CHOP) continue;
                }
                docKeyTrack(k, sizeof k, pi, t, PARAMS[i].id);
                d.set(k, tr.p[slot]);
            }
            for (int k = 0; k < NSCENES; k++)
                for (int i = 0; i < pt.sc[k][t].n; i++) {
                    int pIdx = paramOfSlot(PS_TRACK, pt.sc[k][t].v[i].slot);
                    if (pIdx < 0) continue;
                    docKeyScene(k2, sizeof k2, pi, k, t, PARAMS[pIdx].id);
                    d.set(k2, pt.sc[k][t].v[i].val);
                }
            for (int s = 0; s < NSTEPS; s++) {
                const Step& st = tr.s[s];
                if (!st.on && !st.nLock) continue;
                docKeyStep(k, sizeof k, pi, t, s, "");
                d.set(k, st.on);
                for (int li = 0; li < st.nLock; li++) {
                    int pIdx = paramOfSlot(PS_TRACK, st.lock[li].slot);
                    if (pIdx < 0) continue;
                    char sub[48];
                    snprintf(sub, sizeof sub, "%s", PARAMS[pIdx].id + 4);
                    docKeyStep(k, sizeof k, pi, t, s, sub);
                    d.set(k, st.lock[li].val);
                    if (st.lock[li].slide) {
                        char sub2[56]; snprintf(sub2, sizeof sub2, "%s.slide", sub);
                        docKeyStep(k, sizeof k, pi, t, s, sub2);
                        d.set(k, 1);
                    }
                }
            }
        }
        // FX-Spur: pat.N.fx.len, pat.N.fx.st.S = 2, pat.N.fx.st.S.fx.a.fb = 80
        const Track& f = pt.fx;
        if (f.len != 16)  { snprintf(k, sizeof k, "pat.%d.fx.len", pi);   d.set(k, f.len); }
        if (f.speed != 4) { snprintf(k, sizeof k, "pat.%d.fx.speed", pi); d.set(k, f.speed); }
        for (int s = 0; s < NSTEPS; s++) {
            const Step& st = f.s[s];
            if (!st.nLock) continue;
            snprintf(k, sizeof k, "pat.%d.fx.st.%d", pi, s); d.set(k, 2);
            for (int li = 0; li < st.nLock; li++) {
                int pIdx = paramOfSlot(PS_GLOBAL, st.lock[li].slot);
                if (pIdx < 0) continue;
                snprintf(k, sizeof k, "pat.%d.fx.st.%d.%s", pi, s, PARAMS[pIdx].id);
                d.set(k, st.lock[li].val);
            }
        }
    }
}

// --- Dokument lesen --------------------------------------------------------------------------
inline void projectFromDoc(const Doc& d, Project& P) {
    projectInit(P);
    char k[DOC_KEY];
    int sn = d.get("song.n", 0);
    P.song.n = (uint8_t)(sn < 0 ? 0 : (sn > NSONG ? NSONG : sn));
    for (int i = 0; i < P.song.n; i++) {
        snprintf(k, sizeof k, "song.%d.pat", i);
        int pt = d.get(k, 0);
        P.song.e[i].pat = (uint8_t)(pt < 0 || pt >= NPATTERNS ? 0 : pt);
        SongStep& z = P.song.e[i];
        auto lies = [&](const char* f, int def, int lo, int hi) {
            snprintf(k, sizeof k, "song.%d.%s", i, f);
            int v = d.get(k, def);
            return v < lo ? lo : (v > hi ? hi : v);
        };
        z.type  = (uint8_t)lies("type", 0, 0, 2);
        z.rep   = (uint8_t)lies("rep", 1, z.type == SONG_LOOP ? 0 : 1, 99);
        if (z.type == SONG_LOOP) z.pat = (uint8_t)lies("pat", 0, 0, NSONG - 1);
        z.len   = (uint8_t)lies("len", 0, 0, NSTEPS);
        z.start = (uint8_t)lies("start", 0, 0, NSTEPS - 1);
        z.bpm   = (int16_t)lies("bpm", 0, 0, 300);
        z.mute  = (uint16_t)lies("mute", 0, 0, 0xFFFF);
        z.morph = (uint8_t)lies("morph", 0, 0, 101);
    }
    for (int i = 0; i < NPARAMS; i++) {
        if (PARAMS[i].scope != PS_GLOBAL) continue;
        const ParamDef& p = PARAMS[i];
        int value = d.get(p.id, p.def);
        if (masterSetting(i)) value = value < p.lo ? p.lo : value > p.hi ? p.hi : value;
        P.g[paramSlot(i)] = (int16_t)value;
    }
    for (int pi = 0; pi < NPATTERNS; pi++) {
        snprintf(k, sizeof k, "pat.%d.used", pi);
        if (!d.get(k, 0)) continue;
        Pattern& pt = P.pat[pi];
        pt.used = 1;
        snprintf(k, sizeof k, "pat.%d.bpm", pi);
        int pb = d.get(k, 0);
        pt.bpm = (int16_t)(pb < 0 ? 0 : (pb > 300 ? 300 : pb));
        auto lies = [&](const char* name, int def, int lo, int hi) {
            snprintf(k, sizeof k, "pat.%d.%s", pi, name);
            int v = d.get(k, def);
            return (uint8_t)(v < lo ? lo : (v > hi ? hi : v));
        };
        pt.mult  = lies("mult", SPEED_1X, 0, NSPEEDS - 1);
        pt.chg   = lies("chg", 16, 1, NSTEPS);
        pt.root  = lies("root", 0, 0, 11);
        pt.scale = lies("scale", 0, 0, NSCALES - 1);
        pt.swing = lies("swing", 0, 0, 80);
        if (pt.swing && pt.swing < 50) pt.swing = 50;
        for (int t = 0; t < NTRACKS; t++) {
            Track& tr = pt.t[t];
            snprintf(k, sizeof k, "pat.%d.trk.%d.len", pi, t);   tr.len = (uint8_t)d.get(k, 16);
            snprintf(k, sizeof k, "pat.%d.trk.%d.speed", pi, t); tr.speed = (uint8_t)d.get(k, 4);
            for (int i = 0; i < NPARAMS; i++) {
                if (PARAMS[i].scope != PS_TRACK) continue;
                docKeyTrack(k, sizeof k, pi, t, PARAMS[i].id);
                int v = d.get(k, trackDocDefault(i));
                static const int tempo = paramFind("trk.sample.bpm"), machine = paramSlot(paramFind("trk.machine"));
                if (i == tempo && !d.has(k) && tr.p[machine] != MACHINE_SAMPLE && tr.p[machine] != MACHINE_LEGACY_CHOP)
                    v = SAMPLE_BPM_AUTO;
                if (isInsertParam(i) || isExtendedVoiceParam(i)) v = clampParam(i, v);
                tr.p[paramSlot(i)] = (int16_t)v;
            }
            for (int k2i = 0; k2i < NSCENES; k2i++) {
                SceneTrack& sct = pt.sc[k2i][t];
                sct.n = 0;
                for (int i = 0; i < NPARAMS; i++) {
                    if (PARAMS[i].scope != PS_TRACK || !(PARAMS[i].flags & PF_LOCK)) continue;
                    docKeyScene(k, sizeof k, pi, k2i, t, PARAMS[i].id);
                    if (d.has(k)) {
                        int value = d.get(k, 0);
                        if (isExtendedVoiceParam(i)) value = clampParam(i, value);
                        sct.set(paramSlot(i), (int16_t)value);
                    }
                }
            }
            for (int s = 0; s < NSTEPS; s++) {
                docKeyStep(k, sizeof k, pi, t, s, "");
                int on = d.get(k, 0);
                Step& st = tr.s[s];
                st.on = (uint8_t)on; st.nLock = 0;
                for (int i = 0; i < NPARAMS; i++) {
                    if (PARAMS[i].scope != PS_TRACK || !(PARAMS[i].flags & PF_LOCK)) continue;
                    char sub[48]; snprintf(sub, sizeof sub, "%s", PARAMS[i].id + 4);
                    docKeyStep(k, sizeof k, pi, t, s, sub);
                    if (!d.has(k)) continue;
                    int v = d.get(k, 0);
                    if (isExtendedVoiceParam(i)) v = clampParam(i, v);
                    char sub2[56]; snprintf(sub2, sizeof sub2, "%s.slide", sub);
                    char k2[DOC_KEY]; docKeyStep(k2, sizeof k2, pi, t, s, sub2);
                    st.setLock(paramSlot(i), (int16_t)v, (uint8_t)d.get(k2, 0));
                }
            }
        }
        {   // FX-Spur
            Track& f = pt.fx;
            fxTrackClear(f);
            snprintf(k, sizeof k, "pat.%d.fx.len", pi);   f.len = (uint8_t)d.get(k, 16);
            snprintf(k, sizeof k, "pat.%d.fx.speed", pi); f.speed = (uint8_t)d.get(k, 4);
            for (int s = 0; s < NSTEPS; s++) {
                snprintf(k, sizeof k, "pat.%d.fx.st.%d", pi, s);
                if (!d.get(k, 0)) continue;
                for (int i = 0; i < NPARAMS; i++) {
                    if (!fxLockable(i)) continue;
                    snprintf(k, sizeof k, "pat.%d.fx.st.%d.%s", pi, s, PARAMS[i].id);
                    if (d.has(k)) f.s[s].setLock(paramSlot(i), (int16_t)d.get(k, 0), 0);
                }
                if (f.s[s].nLock) f.s[s].on = 2;
            }
        }
    }
}
