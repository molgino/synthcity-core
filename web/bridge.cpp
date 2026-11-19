// Bruecke Kern -> Oberflaeche. Die Oberflaeche kennt NUR diese Funktionen; sie weiss nichts
// ueber Stimmen, Filter oder Dateien. Alles, was sie anzeigt, fragt sie aus der Parametertabelle
// ab - deshalb erscheint ein neuer Regler von selbst, ohne eine Zeile JavaScript.
#include "../src/engine.h"
#include "../src/text.h"
#include <string.h>

static Engine  gEng;
static char    gBuf[4096];
static char    gDoc[DOC_MAX * 48];

extern "C" {

// --- Leben ---------------------------------------------------------------------------------
void scInit(float sr) { gEng.prepare(sr); }
void scStart() { gEng.start(); }
void scStop()  { gEng.stop(); }
int  scIsRunning() { return gEng.seq.running ? 1 : 0; }
void scRender(float* out, int frames) { gEng.render(out, frames); }

// --- Parametertabelle: die Oberflaeche baut sich daraus selbst -------------------------------
int   scNParams() { return NPARAMS; }
const char* scParamId(int i)   { return (i >= 0 && i < NPARAMS) ? PARAMS[i].id : ""; }
const char* scParamName(int i) { return (i >= 0 && i < NPARAMS) ? PARAMS[i].name : ""; }
int   scParamScope(int i) { return PARAMS[i].scope; }
int   scParamKind(int i)  { return PARAMS[i].kind; }
int   scParamSlot(int i)  { return paramSlot(i); }
int   scParamLo(int i)    { return PARAMS[i].lo; }
int   scParamHi(int i)    { return PARAMS[i].hi; }
int   scParamDef(int i)   { return PARAMS[i].def; }
int   scParamFlags(int i) { return PARAMS[i].flags; }
// Auswahllisten: die Oberflaeche fragt, welche Werte sinnvoll sind - sie kennt die Zahlen nicht.
int scEnumCount(int i) { return (i >= 0 && i < NPARAMS) ? enumCount(PARAMS[i]) : 0; }
int scEnumValue(int i, int k) { return (i >= 0 && i < NPARAMS) ? enumValue(PARAMS[i], k) : 0; }
const char* scEnumGroup(int i, int k) { return (i >= 0 && i < NPARAMS) ? enumGroup(PARAMS[i], k) : ""; }

const char* scParamText(int i, float v) {
    if (i < 0 || i >= NPARAMS) return "";
    paramText(PARAMS[i], v, gBuf, sizeof gBuf);
    return gBuf;
}

// --- Werte ------------------------------------------------------------------------------------
int  scGet(int track, int slot) {
    if (track < 0) return gEng.P.g[slot];
    return gEng.P.pat[gEng.pat].t[track].p[slot];
}
void scSet(int track, int slot, int v) {
    if (track < 0) {
        gEng.P.g[slot] = (int16_t)v;
        if (slot == paramSlot(paramFind("bpm"))) gEng.seq.bpm = v;
    } else gEng.P.pat[gEng.pat].t[track].p[slot] = (int16_t)v;
}

// --- Steps und Locks ---------------------------------------------------------------------------
int  scStepGet(int t, int s) { return gEng.P.pat[gEng.pat].t[t].s[s].on; }
void scStepSet(int t, int s, int on) { gEng.P.pat[gEng.pat].t[t].s[s].on = (uint8_t)on; }
int  scNLocks(int t, int s) { return gEng.P.pat[gEng.pat].t[t].s[s].nLock; }
int  scLockSlot(int t, int s, int i) { return gEng.P.pat[gEng.pat].t[t].s[s].lock[i].slot; }
int  scLockVal(int t, int s, int i)  { return gEng.P.pat[gEng.pat].t[t].s[s].lock[i].val; }
int  scLockSlide(int t, int s, int i){ return gEng.P.pat[gEng.pat].t[t].s[s].lock[i].slide; }
// -32768 = dieser Step hat keinen eigenen Wert fuer diesen Regler
int  scLockGet(int t, int s, int slot) {
    const Lock* l = gEng.P.pat[gEng.pat].t[t].s[s].findLock(slot);
    return l ? l->val : -32768;
}
void scLockSet(int t, int s, int slot, int v, int slide) {
    gEng.P.pat[gEng.pat].t[t].s[s].setLock(slot, (int16_t)v, (uint8_t)slide);
}
void scLockClear(int t, int s, int slot) {
    Step& st = gEng.P.pat[gEng.pat].t[t].s[s];
    for (int i = 0; i < st.nLock; i++)
        if (st.lock[i].slot == slot) { st.lock[i] = st.lock[--st.nLock]; return; }
}

// --- Spur ----------------------------------------------------------------------------------------
int  scTrackLen(int t)   { return gEng.P.pat[gEng.pat].t[t].len; }
void scSetTrackLen(int t, int n) { gEng.P.pat[gEng.pat].t[t].len = (uint8_t)(n < 1 ? 1 : n > NSTEPS ? NSTEPS : n); }
int  scSpeed(int t)      { return gEng.P.pat[gEng.pat].t[t].speed; }
void scSetSpeed(int t, int n) { gEng.P.pat[gEng.pat].t[t].speed = (uint8_t)(n < 1 ? 1 : n); }
int  scNTracks() { return NTRACKS; }
int  scNSteps()  { return NSTEPS; }

// Wo steht der Zeiger gerade? Aus der Sample-Position gerechnet, nicht mitgezaehlt.
int scPlayStep(int t) {
    if (!gEng.seq.running) return -1;
    const Track& tr = gEng.P.pat[gEng.pat].t[t];
    double sp = gEng.seq.stepLen(tr);
    if (sp < 1.0) return -1;
    long long k = (long long)(gEng.seq.pos / sp);
    int len = tr.len < 1 ? 1 : tr.len;
    if (gEng.seq.loopOn && gEng.seq.loopLen > 0) {
        int w = gEng.seq.loopLen > len ? len : gEng.seq.loopLen;
        return (gEng.seq.loopFrom + (int)(((k % w) + w) % w)) % len;
    }
    return (int)(((k % len) + len) % len);
}

// --- Performance-Ebene -----------------------------------------------------------------------
void  scMute(int t, int on) { gEng.mute[t] = on != 0; }
int   scIsMute(int t)       { return gEng.mute[t] ? 1 : 0; }
void  scSetMorph(float v)   { gEng.morph = v; }
float scGetMorph()          { return gEng.morph; }
void  scFill(int on)        { gEng.seq.fill = on != 0; }
int   scIsFill()            { return gEng.seq.fill ? 1 : 0; }
void  scStepLoop(int on, int from, int len) {
    gEng.seq.loopOn = on != 0; gEng.seq.loopFrom = from; gEng.seq.loopLen = len;
}
void  scSceneSet(int scene, int t, int slot, int v) { gEng.P.pat[gEng.pat].sc[scene][t].set(slot, (int16_t)v); }
int   scSceneGet(int scene, int t, int slot) {
    const Lock* l = gEng.P.pat[gEng.pat].sc[scene][t].find(slot);
    return l ? l->val : -32768;
}

// Von Hand gespielt - dieselbe Kette wie ein Trig.
void scPlayNote(int t, int note, int vel) {
    Ev e{gEng.seq.pos, (uint8_t)t, EV_TRIG, 0, (int16_t)note, (int16_t)vel, (int32_t)(gEng.sr * 0.3f)};
    gEng.fire(e);
}

// --- Dokument: dieselben Zeilen wie die Datei und wie der Plugin-Zustand ------------------------
const char* scSave() {
    static Doc d;
    d.clear();
    projectToDoc(gEng.P, d);
    int at = 0;
    for (int i = 0; i < d.n && at < (int)sizeof gDoc - 64; i++)
        at += snprintf(gDoc + at, sizeof gDoc - at, "%s = %d\n", d.e[i].k, d.e[i].v);
    gDoc[at] = 0;
    return gDoc;
}
void scLoad(const char* text) {
    static Doc d;
    d.clear();
    static char tmp[DOC_MAX * 48];
    snprintf(tmp, sizeof tmp, "%s", text);
    char* line = tmp;
    while (line && *line) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char* eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            char* end = eq - 1;
            while (end > line && (*end == ' ' || *end == '\t')) *end-- = 0;
            d.set(line, (int32_t)strtol(eq + 1, nullptr, 10));
        }
        line = nl ? nl + 1 : nullptr;
    }
    projectFromDoc(d, gEng.P);
    gEng.seq.bpm = gEng.P.g[paramSlot(paramFind("bpm"))];
    gEng.resetTrackValues();
}

// --- Ein Pattern zum Anfangen -------------------------------------------------------------------
void scDemo() {
    projectInit(gEng.P);
    gEng.P.g[paramSlot(paramFind("bpm"))] = 124;
    gEng.seq.bpm = 124;
    gEng.P.pat[0].used = 1;
    const int note = paramSlot(paramFind("trk.note")),  freq = paramSlot(paramFind("trk.flt.freq"));
    const int env  = paramSlot(paramFind("trk.flt.env")), dec = paramSlot(paramFind("trk.amp.dec"));
    const int reso = paramSlot(paramFind("trk.flt.reso")), vol = paramSlot(paramFind("trk.vol"));
    const int pan  = paramSlot(paramFind("trk.pan")),    shape = paramSlot(paramFind("trk.src.b"));
    const int len  = paramSlot(paramFind("trk.len")),    sus = paramSlot(paramFind("trk.amp.sus"));
    const int atk  = paramSlot(paramFind("trk.amp.atk")), cond = paramSlot(paramFind("trk.cond"));

    Track& b = gEng.P.pat[0].t[0];                       // Bass
    b.p[note] = 33; b.p[freq] = 52; b.p[env] = 100; b.p[reso] = 60; b.p[dec] = 52; b.p[vol] = 108;
    for (int s = 0; s < 16; s += 4) b.s[s].on = 1;
    b.s[8].setLock(note, 45);
    b.s[12].on = 1; b.s[12].setLock(note, 40, 1);        // gleitet hinauf

    Track& p = gEng.P.pat[0].t[1];                       // Flaeche
    p.speed = 2;
    p.p[note] = 64; p.p[shape] = 20; p.p[atk] = 70; p.p[sus] = 92; p.p[len] = 90;
    p.p[freq] = 72; p.p[vol] = 88; p.p[pan] = 48;
    for (int s = 0; s < 16; s += 8) { p.s[s].on = 1; p.s[s].setLock(cond, (int16_t)condAB(2, 2, false)); }
    p.s[8].setLock(note, 67);

    Track& m = gEng.P.pat[0].t[2];                       // Melodie, 12 Steps gegen die Vier
    m.len = 12;
    m.p[note] = 76; m.p[shape] = 110; m.p[dec] = 28; m.p[freq] = 90; m.p[vol] = 84; m.p[pan] = 82;
    const int ns[12] = {76, 0, 79, 0, 0, 83, 0, 76, 0, 0, 72, 0};
    for (int s = 0; s < 12; s++) if (ns[s]) { m.s[s].on = 1; m.s[s].setLock(note, (int16_t)ns[s]); }

    gEng.P.pat[0].sc[0][0].set(freq, 44);                // Scene A dumpf
    gEng.P.pat[0].sc[1][0].set(freq, 86);                // Scene B offen
    gEng.resetTrackValues();
}

// Speicher fuer die Oberflaeche (Audio-Puffer, Text) - einmal angefordert, nie im Render.
void* scAlloc(int bytes) { return malloc((size_t)bytes); }

}  // extern "C"
