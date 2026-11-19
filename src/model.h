// Datenmodell: Projekt -> Pattern -> Track -> Step. Der wirksame Wert ist immer dieselbe Kette
// Lock -> (spaeter Scene) -> Track-Grundwert. Locks liegen duenn besetzt als Liste, nicht als
// Feld ueber alle Parameter - ein Step traegt selten mehr als eine Handvoll.
#pragma once
#include "param.h"
#include "doc.h"

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
    int16_t p[64];              // Spurparameter nach Slot
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
};

struct Pattern {
    Track t[NTRACKS];
    SceneTrack sc[NSCENES][NTRACKS];
    uint8_t used = 0;
};

// Ein Song ist eine Kette: Pattern, wie oft, dann das naechste. Am Ende geht es vorn weiter.
enum { NSONG = 64 };
struct SongStep { uint8_t pat = 0, rep = 1; };
struct Song {
    uint8_t  n = 0;
    SongStep e[NSONG];
};

struct Project {
    int16_t g[32];              // globale Parameter nach Slot
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
        P.pat[pi].used = 0;
        for (int t = 0; t < NTRACKS; t++) {
            Track& tr = P.pat[pi].t[t];
            tr.len = 16; tr.speed = 4;
            for (int i = 0; i < NPARAMS; i++)
                if (PARAMS[i].scope == PS_TRACK) tr.p[paramSlot(i)] = PARAMS[i].def;
            for (int s = 0; s < NSTEPS; s++) { tr.s[s].on = 0; tr.s[s].nLock = 0; }
        }
        for (int k = 0; k < NSCENES; k++)
            for (int t = 0; t < NTRACKS; t++) P.pat[pi].sc[k][t].n = 0;
    }
}

// Pattern kopieren und leeren - ohne das kann man kein zweites Pattern bauen.
inline void patternClear(Project& P, int i) {
    if (i < 0 || i >= NPATTERNS) return;
    Pattern& pt = P.pat[i];
    pt.used = 0;
    for (int t = 0; t < NTRACKS; t++) {
        Track& tr = pt.t[t];
        tr.len = 16; tr.speed = 4;
        for (int k = 0; k < NPARAMS; k++)
            if (PARAMS[k].scope == PS_TRACK) tr.p[paramSlot(k)] = PARAMS[k].def;
        for (int s = 0; s < NSTEPS; s++) { tr.s[s].on = 0; tr.s[s].nLock = 0; }
        for (int k = 0; k < NSCENES; k++) pt.sc[k][t].n = 0;
    }
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

// --- Dokument schreiben ---------------------------------------------------------------------
// Geschrieben wird nur, was vom Standard abweicht: die Datei bleibt klein und lesbar, und ein
// Parameter, den es spaeter nicht mehr gibt, stoert niemanden.
inline void projectToDoc(const Project& P, Doc& d) {
    char k[DOC_KEY], k2[DOC_KEY];
    if (P.song.n) {
        d.set("song.n", P.song.n);
        for (int i = 0; i < P.song.n; i++) {
            snprintf(k, sizeof k, "song.%d.pat", i); d.set(k, P.song.e[i].pat);
            snprintf(k, sizeof k, "song.%d.rep", i); d.set(k, P.song.e[i].rep);
        }
    }
    for (int i = 0; i < NPARAMS; i++) {
        if (PARAMS[i].scope != PS_GLOBAL) continue;
        int slot = paramSlot(i);
        if (P.g[slot] != PARAMS[i].def) d.set(PARAMS[i].id, P.g[slot]);
    }
    for (int pi = 0; pi < NPATTERNS; pi++) {
        const Pattern& pt = P.pat[pi];
        if (!pt.used) continue;
        snprintf(k, sizeof k, "pat.%d.used", pi); d.set(k, 1);
        for (int t = 0; t < NTRACKS; t++) {
            const Track& tr = pt.t[t];
            if (tr.len != 16)  { snprintf(k, sizeof k, "pat.%d.trk.%d.len", pi, t);   d.set(k, tr.len); }
            if (tr.speed != 4) { snprintf(k, sizeof k, "pat.%d.trk.%d.speed", pi, t); d.set(k, tr.speed); }
            for (int i = 0; i < NPARAMS; i++) {
                if (PARAMS[i].scope != PS_TRACK) continue;
                int slot = paramSlot(i);
                if (tr.p[slot] == PARAMS[i].def) continue;
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
        snprintf(k, sizeof k, "song.%d.rep", i);
        int r = d.get(k, 1);
        P.song.e[i].rep = (uint8_t)(r < 1 ? 1 : (r > 99 ? 99 : r));
    }
    for (int i = 0; i < NPARAMS; i++) {
        if (PARAMS[i].scope != PS_GLOBAL) continue;
        P.g[paramSlot(i)] = (int16_t)d.get(PARAMS[i].id, PARAMS[i].def);
    }
    for (int pi = 0; pi < NPATTERNS; pi++) {
        snprintf(k, sizeof k, "pat.%d.used", pi);
        if (!d.get(k, 0)) continue;
        Pattern& pt = P.pat[pi];
        pt.used = 1;
        for (int t = 0; t < NTRACKS; t++) {
            Track& tr = pt.t[t];
            snprintf(k, sizeof k, "pat.%d.trk.%d.len", pi, t);   tr.len = (uint8_t)d.get(k, 16);
            snprintf(k, sizeof k, "pat.%d.trk.%d.speed", pi, t); tr.speed = (uint8_t)d.get(k, 4);
            for (int i = 0; i < NPARAMS; i++) {
                if (PARAMS[i].scope != PS_TRACK) continue;
                docKeyTrack(k, sizeof k, pi, t, PARAMS[i].id);
                tr.p[paramSlot(i)] = (int16_t)d.get(k, PARAMS[i].def);
            }
            for (int k2i = 0; k2i < NSCENES; k2i++) {
                SceneTrack& sct = pt.sc[k2i][t];
                sct.n = 0;
                for (int i = 0; i < NPARAMS; i++) {
                    if (PARAMS[i].scope != PS_TRACK || !(PARAMS[i].flags & PF_LOCK)) continue;
                    docKeyScene(k, sizeof k, pi, k2i, t, PARAMS[i].id);
                    if (d.has(k)) sct.set(paramSlot(i), (int16_t)d.get(k, 0));
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
                    char sub2[56]; snprintf(sub2, sizeof sub2, "%s.slide", sub);
                    char k2[DOC_KEY]; docKeyStep(k2, sizeof k2, pi, t, s, sub2);
                    st.setLock(paramSlot(i), (int16_t)v, (uint8_t)d.get(k2, 0));
                }
            }
        }
    }
}
