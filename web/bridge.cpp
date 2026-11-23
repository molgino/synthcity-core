// Bruecke Kern -> Oberflaeche. Die Oberflaeche kennt NUR diese Funktionen; sie weiss nichts
// ueber Stimmen, Filter oder Dateien. Alles, was sie anzeigt, fragt sie aus der Parametertabelle
// ab - deshalb erscheint ein neuer Regler von selbst, ohne eine Zeile JavaScript.
#include "../src/engine.h"
#include "../src/text.h"
#include "../src/demo.h"
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

// mach: die Machine der gemeinten Spur, oder -1. Sie bestimmt, was ihre beiden Regler heissen.
const char* scParamText(int i, float v, int mach) {
    if (i < 0 || i >= NPARAMS) return "";
    paramText(PARAMS[i], v, gBuf, sizeof gBuf, mach);
    return gBuf;
}
const char* scMachName(int m)  { return machineAt(m)->name(); }
const char* scMachLabel(int m, int which) {
    return machineAt(m)->label(which);
}
int scMachCount() { return machineCount(); }
int scMachSteps(int m, int k) { return machineAt(m)->steps(k); }
int scMachUsesSmp(int m) { return machineAt(m)->usesSample() ? 1 : 0; }

// Wer in ein Pattern schreibt, macht es zu einem benutzten - sonst fehlt es im Dokument
// (und damit nach dem naechsten Laden) und die Pattern-Leiste zeigt es als leer.
static Pattern& W() { gEng.P.pat[gEng.pat].used = 1; return gEng.P.pat[gEng.pat]; }
static Pattern& WP(int p) { gEng.P.pat[p].used = 1; return gEng.P.pat[p]; }

// --- Werte ------------------------------------------------------------------------------------
int  scGet(int track, int slot) {
    if (track < 0) return gEng.P.g[slot];
    if (track >= NTRACKS) return 0;                 // die FX-Spur hat keine eigenen Grundwerte
    return gEng.P.pat[gEng.pat].t[track].p[slot];
}
void scSet(int track, int slot, int v) {
    if (track < 0) {
        int index = paramOfSlot(PS_GLOBAL, slot);
        if (index < 0) return;
        if (masterSetting(index)) v = v < PARAMS[index].lo ? PARAMS[index].lo : v > PARAMS[index].hi ? PARAMS[index].hi : v;
        gEng.P.g[slot] = (int16_t)v;
        // Das Projekttempo gilt nur, wo das Pattern kein eigenes hat.
        if (slot == paramSlot(paramFind("bpm"))) gEng.setTempo(gEng.effTempo(gEng.pat));
    } else if (track < NTRACKS && slot >= 0 && slot < TRACK_PARAM_CAP) {
        const int first = insertParamSlot(0);
        if (slot >= first) {
            int p = INSERT_FIRST + slot - first;
            if (p >= NPARAMS) return;
            v = v < PARAMS[p].lo ? PARAMS[p].lo : v > PARAMS[p].hi ? PARAMS[p].hi : v;
        }
        W().t[track].p[slot] = (int16_t)v;
    }
}

// Machine wechseln: die beiden Quellregler fangen bei den Grundwerten DIESER Machine an.
void scSetMachine(int t, int m) { machineSelect(W().t[t].p, m); }

// --- Steps und Locks ---------------------------------------------------------------------------
// Spur NTRACKS (8) ist die FX-Spur des Patterns: dieselben Aufrufe, Lock-Slots sind dort global.
static Track& TR(int t)  { Pattern& pt = gEng.P.pat[gEng.pat]; return t == NTRACKS ? pt.fx : pt.t[t]; }
static Track& TRW(int t) { Pattern& pt = W(); return t == NTRACKS ? pt.fx : pt.t[t]; }
int  scFxTrack() { return NTRACKS; }
int  scStepGet(int t, int s) { return TR(t).s[s].on; }
void scStepSet(int t, int s, int on) { TRW(t).s[s].on = (uint8_t)(t == NTRACKS && on ? 2 : on); }
int  scNLocks(int t, int s) { return TR(t).s[s].nLock; }
int  scLockSlot(int t, int s, int i) { return TR(t).s[s].lock[i].slot; }
int  scLockVal(int t, int s, int i)  { return TR(t).s[s].lock[i].val; }
int  scLockSlide(int t, int s, int i){ return TR(t).s[s].lock[i].slide; }
// -32768 = dieser Step hat keinen eigenen Wert fuer diesen Regler
int  scLockGet(int t, int s, int slot) {
    const Lock* l = TR(t).s[s].findLock(slot);
    return l ? l->val : -32768;
}
void scLockSet(int t, int s, int slot, int v, int slide) {
    if (t < NTRACKS && slot >= insertParamSlot(0)) return;
    if (t == NTRACKS) {
        int p = paramOfSlot(PS_GLOBAL, slot);
        if (p < 0 || !fxLockable(p)) return;
    }
    Step& st = TRW(t).s[s];
    st.setLock(slot, (int16_t)v, (uint8_t)(t == NTRACKS ? 0 : slide));
    if (t == NTRACKS) st.on = 2;
}
void scLockClear(int t, int s, int slot) {
    Step& st = TRW(t).s[s];
    for (int i = 0; i < st.nLock; i++)
        if (st.lock[i].slot == slot) { st.lock[i] = st.lock[--st.nLock]; break; }
    if (t == NTRACKS && !st.nLock) st.on = 0;
}

// --- Spur ----------------------------------------------------------------------------------------
int  scTrackLen(int t)   { return TR(t).len; }
void scSetTrackLen(int t, int n) { TRW(t).len = (uint8_t)(n < 1 ? 1 : n > NSTEPS ? NSTEPS : n); }
int  scSpeed(int t)      { return TR(t).speed; }
void scSetSpeed(int t, int n) { if (t == NTRACKS) { TRW(t).speed = (uint8_t)n; return; } W(); gEng.setDiv(t, n); }
int  scNTracks() { return NTRACKS; }
int  scNSteps()  { return NSTEPS; }

// Wo steht der Zeiger gerade? Aus der Sample-Position gerechnet, nicht mitgezaehlt.
int scPlayStep(int t) {
    if (!gEng.seq.running) return -1;
    if (t == NTRACKS) return gEng.fxStepNow();
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

// --- Pattern ------------------------------------------------------------------------------
int  scNPatterns()      { return NPATTERNS; }
int  scPattern()        { return gEng.pat; }
int  scPatNext()        { return gEng.patNext; }
void scPatQueue(int p)  { gEng.queuePattern(p); }
// Sofort umschalten, ohne auf einen Takt zu warten: so folgt das Modell dem Klang-Kern.
void scSetPattern(int p) {
    if (p < 0 || p >= NPATTERNS) return;
    gEng.pat = p; gEng.patNext = -1; gEng.switchAt = -1;
    gEng.seq.toPattern(gEng.seq.pos);
    gEng.seq.bpm = gEng.effTempo(p);
    gEng.resetTrackValues();
}
// --- Kopieren und Einfuegen ------------------------------------------------------------------
// Die Ablage liegt im Kern, nicht in der Seite: beide Kerne (Anzeige und Klang) bekommen
// dieselben Befehle und halten so dieselbe Ablage. Kopiert wird ein Abbild - was danach an der
// Quelle geschieht, aendert die Ablage nicht mehr.
static Pattern gClipPat;  static int gHasPat = 0;
static Track   gClipTrk;  static int gHasTrk = 0;
static Step    gClipStep; static int gHasStep = 0; static bool gClipStepFx = false;
void scClipPat(int p)   { if (p < 0 || p >= NPATTERNS) return; gClipPat = gEng.P.pat[p]; gHasPat = 1; }
int  scHasClipPat()     { return gHasPat; }
void scPastePat(int p) {
    if (!gHasPat || p < 0 || p >= NPATTERNS) return;
    gEng.P.pat[p] = gClipPat; gEng.P.pat[p].used = 1;
    if (p == gEng.pat) { gEng.resetTrackValues(); gEng.setTempo(gEng.effTempo(p)); }
}
void scClipTrack(int t) { if (t < 0 || t >= NTRACKS) return; gClipTrk = gEng.P.pat[gEng.pat].t[t]; gHasTrk = 1; }
int  scHasClipTrack()   { return gHasTrk; }
// mode 0: nur Trigs und Locks (der Klang der Ziel-Spur bleibt), 1: alles - Klang, Laenge, Trigs
void scPasteTrack(int t, int mode) {
    if (!gHasTrk || t < 0 || t >= NTRACKS) return;
    Track& tr = W().t[t];
    if (mode == 1) tr = gClipTrk;
    else for (int s = 0; s < NSTEPS; s++) tr.s[s] = gClipTrk.s[s];
    if (mode == 1) gEng.resetTrackValues();
}
void scClearTrigs(int t) {
    if (t < 0 || t > NTRACKS) return;
    Track& tr = TRW(t);
    for (int s = 0; s < NSTEPS; s++) { tr.s[s].on = 0; tr.s[s].nLock = 0; }
}
void scClipStep(int t, int s) {
    if (t < 0 || t > NTRACKS || s < 0 || s >= NSTEPS) return;
    gClipStep = TR(t).s[s]; gHasStep = 1; gClipStepFx = t == NTRACKS;
}
int  scHasClipStep()    { return gHasStep; }
void scPasteStep(int t, int s) {
    if (!gHasStep || t < 0 || t > NTRACKS || s < 0 || s >= NSTEPS) return;
    if ((t == NTRACKS) != gClipStepFx) return;         // Spur-Locks passen nicht auf die FX-Spur
    TRW(t).s[s] = gClipStep;
}
int  scPatUsed(int p)   { return (p >= 0 && p < NPATTERNS) ? gEng.P.pat[p].used : 0; }
void scPatCopy(int a, int b) { patternCopy(gEng.P, a, b); }
void scPatClear(int p)  {
    patternClear(gEng.P, p);
    if (p == gEng.pat) gEng.resetTrackValues();
}
// --- Song ---------------------------------------------------------------------------------
int  scSongN()          { return gEng.P.song.n; }
int  scSongPat(int i)   { return (i >= 0 && i < gEng.P.song.n) ? gEng.P.song.e[i].pat : 0; }
int  scSongRep(int i)   { return (i >= 0 && i < gEng.P.song.n) ? gEng.P.song.e[i].rep : 1; }
void scSongSet(int i, int pat, int rep) {
    if (i < 0 || i >= gEng.P.song.n) return;
    gEng.P.song.e[i].pat = (uint8_t)(pat < 0 ? 0 : (pat >= NPATTERNS ? NPATTERNS - 1 : pat));
    gEng.P.song.e[i].rep = (uint8_t)(rep < 1 ? 1 : (rep > 99 ? 99 : rep));
}
void scSongAdd(int pat, int rep) {
    if (gEng.P.song.n >= NSONG) return;
    gEng.P.song.e[gEng.P.song.n] = SongStep();
    gEng.P.song.n++;
    scSongSet(gEng.P.song.n - 1, pat, rep);
}
void scSongDel(int i) {
    Song& s = gEng.P.song;
    if (i < 0 || i >= s.n) return;
    for (int k = i; k < s.n - 1; k++) s.e[k] = s.e[k + 1];
    s.n--;
    if (!s.n) gEng.songStop();
}
// Felder einer Zeile: 0 Pattern (bei LOOP: Zielzeile), 1 Wiederholungen (LOOP: wie oft, 0 = immer),
// 2 Typ, 3 Laenge (0 = AUTO), 4 Start-Step, 5 Tempo (0 = keins), 6 Mutes (Bitmaske), 7 Scene (0 = bleibt)
int  scSongGet(int i, int f) {
    if (i < 0 || i >= gEng.P.song.n) return 0;
    const SongStep& r = gEng.P.song.e[i];
    switch (f) { case 0: return r.pat; case 1: return r.rep; case 2: return r.type; case 3: return r.len;
                 case 4: return r.start; case 5: return r.bpm; case 6: return r.mute; case 7: return r.morph; }
    return 0;
}
void scSongSetF(int i, int f, int v) {
    if (i < 0 || i >= gEng.P.song.n) return;
    SongStep& r = gEng.P.song.e[i];
    auto k = [](int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); };
    switch (f) {
    case 0: r.pat = (uint8_t)k(v, 0, r.type == SONG_LOOP ? NSONG - 1 : NPATTERNS - 1); break;
    case 1: r.rep = (uint8_t)k(v, r.type == SONG_LOOP ? 0 : 1, 99); break;
    case 2: r.type = (uint8_t)k(v, 0, 2); if (r.type == SONG_LOOP) { r.pat = 0; r.rep = 1; } else if (r.rep < 1) r.rep = 1; break;
    case 3: r.len = (uint8_t)k(v, 0, NSTEPS); break;
    case 4: r.start = (uint8_t)k(v, 0, NSTEPS - 1); break;
    case 5: r.bpm = (int16_t)(v <= 0 ? 0 : k(v, 30, 300)); break;
    case 6: r.mute = (uint16_t)k(v, 0, 0xFFFF); break;
    case 7: r.morph = (uint8_t)k(v, 0, 101); break;
    }
}
// Zeile i verdoppeln (die Kopie steht direkt darunter) / um eins verschieben.
void scSongDup(int i) {
    Song& s = gEng.P.song;
    if (i < 0 || i >= s.n || s.n >= NSONG) return;
    for (int k = s.n; k > i + 1; k--) s.e[k] = s.e[k - 1];
    s.e[i + 1] = s.e[i]; s.n++;
}
void scSongMove(int i, int dir) {
    Song& s = gEng.P.song;
    int j = i + (dir < 0 ? -1 : 1);
    if (i < 0 || i >= s.n || j < 0 || j >= s.n) return;
    SongStep t = s.e[i]; s.e[i] = s.e[j]; s.e[j] = t;
}
int  scSongMuteNow()    { return gEng.songOn ? gEng.songMute : 0; }
// --- Song als WAV rechnen (offline, im Kern der Oberflaeche) -------------------------------
// Von Zeile 'von' bis zum Ende von Zeile 'bis', dann 'tail' Sekunden Ausklang. Gerechnet wird in
// Haeppchen (scExportRender), damit die Oberflaeche dazwischen atmen kann.
static int gExpBis = -1, gExpTail = 0, gExpPhase = 0, gExpPatVor = 0; static bool gExpDrin = false; static int64_t gExpUrsprung = 0;
static int64_t gExpMax = 0, gExpDone = 0;
static MasterMeter gExpMeter;
int scExportBegin(int von, int bis, int tailSec, int maxSec) {
    if (!gEng.P.song.n) return 0;
    gExpPatVor = gEng.pat;
    gEng.prepare(gEng.sr);
    for (int t = 0; t < NTRACKS; t++) gEng.mute[t] = false;
    gEng.morph = 0.0f;
    gEng.songStart(von);                           // Uhr steht: die Zeile beginnt sofort
    if (!gEng.songOn) return 0;
    gEng.seq.bpm = gEng.effTempo(gEng.pat);        // wie start(), ohne den Song vorn neu zu beginnen
    gEng.seq.start();
    gEng.enterRow(gEng.songPos);
    gExpBis = bis < 0 || bis >= gEng.P.song.n ? gEng.P.song.n - 1 : bis;
    gExpTail = (int)(tailSec * gEng.sr);
    gExpMax = (int64_t)maxSec * (int64_t)gEng.sr;
    gExpPhase = 0; gExpDone = 0; gExpDrin = false;
    gExpMeter.reset(gEng.sr);
    gEng.masteringExport = true;
    return 1;
}
// Rechnet hoechstens 'frames' Bilder nach out (stereo verschraenkt). Rueckgabe: gerechnete Bilder;
// 0 = fertig. Phase 0: Song bis zum Ende der Zielzeile, Phase 1: Ausklang.
int scExportRender(float* out, int frames) {
    int done = 0;
    while (done < frames) {
        int c = frames - done; if (c > 512) c = 512;
        if (gExpPhase == 0) {
            if (gEng.songPos == gExpBis && !gExpDrin) { gExpDrin = true; gExpUrsprung = gEng.seq.origin; }
            // Ende: die Zielzeile ist vorbei (der Wechsel passiert genau am Blockende, die naechste
            // Zeile hat noch nichts gespielt), der Song hat angehalten, oder die Hoechstdauer.
            bool ende = !gEng.seq.running || !gEng.songOn || gExpDone >= gExpMax
                        || (gExpDrin && (gEng.songPos != gExpBis || gEng.seq.origin != gExpUrsprung));   // auch: dieselbe Zeile beginnt neu
            if (!ende && gEng.songPos == gExpBis && gEng.songArmed && gEng.switchAt > gEng.seq.pos) {
                int64_t bis = gEng.switchAt - gEng.seq.pos;           // genau bis zum Zeilenende
                if (bis < c) c = (int)bis;
            }
            if (ende) { gEng.songStop(); gEng.stop(); gExpPhase = 1; continue; }
        } else {
            if (gExpTail <= 0) break;
            if (c > gExpTail) c = gExpTail;
            gExpTail -= c;
        }
        gEng.render(out + done * 2, c);
        for (int i = 0; i < c; i++) {
            float l = out[2 * (done + i)], r = out[2 * (done + i) + 1];
            gExpMeter.add(l < -1 ? -1 : l > 1 ? 1 : l, r < -1 ? -1 : r > 1 ? 1 : r);
        }
        done += c; gExpDone += c;
    }
    return done;
}
int  scExportSeconds() { return (int)(gExpDone / (int64_t)gEng.sr); }
float scExportLufs() { return gExpMeter.integrated(); }
float scExportPeak() { return gExpMeter.peak.db(); }
void scExportEnd() {
    gEng.masteringExport = false;
    gEng.songStop(); gEng.stop(); gEng.prepare(gEng.sr);
    gEng.pat = gExpPatVor; gEng.resetTrackValues();
}
void scSongStart()      { gEng.songStart(); }
void scSongStop()       { gEng.songStop(); }
int  scSongOn()         { return gEng.songOn ? 1 : 0; }
int  scSongPos()        { return gEng.songPos; }

// --- Tempo je Pattern: 0 = das Projekttempo gilt ------------------------------------------
int  scPatBpm(int p)    { return (p >= 0 && p < NPATTERNS) ? gEng.P.pat[p].bpm : 0; }
void scSetPatBpm(int p, int v) {
    if (p < 0 || p >= NPATTERNS) return;
    WP(p).bpm = (int16_t)(v <= 0 ? 0 : (v < 30 ? 30 : (v > 300 ? 300 : v)));
    if (p == gEng.pat) gEng.setTempo(gEng.effTempo(p));
}
// --- Swing je Pattern: 0 = der des Projekts ---------------------------------------------
int  scPatSwing(int p)  { return (p >= 0 && p < NPATTERNS) ? gEng.P.pat[p].swing : 0; }
void scSetPatSwing(int p, int v) {
    if (p < 0 || p >= NPATTERNS) return;
    WP(p).swing = (uint8_t)(v <= 0 ? 0 : (v < 50 ? 50 : (v > 80 ? 80 : v)));
}
int  scSwingNow()       { return gEng.swingOf(gEng.pat); }
int  scTempoNow()       { return (int)(gEng.tempoOf(gEng.pat) + 0.5); }

// --- Skala des Patterns: Geschwindigkeit, Wechsellaenge, Tonart ----------------------------
int  scPatMult(int p)   { return (p >= 0 && p < NPATTERNS) ? gEng.P.pat[p].mult : SPEED_1X; }
void scSetPatMult(int p, int m) {
    if (p < 0 || p >= NPATTERNS) return;
    WP(p).mult = (uint8_t)(m < 0 ? 0 : (m >= NSPEEDS ? NSPEEDS - 1 : m));
    if (p == gEng.pat) gEng.setTempo(gEng.effTempo(p));   // umrechnen, nicht springen
}
int  scPatChg(int p)    { return (p >= 0 && p < NPATTERNS) ? gEng.P.pat[p].chg : 16; }
void scSetPatChg(int p, int n) {
    if (p < 0 || p >= NPATTERNS) return;
    WP(p).chg = (uint8_t)(n < 1 ? 1 : (n > NSTEPS ? NSTEPS : n));
}
int  scPatRoot(int p)   { return (p >= 0 && p < NPATTERNS) ? gEng.P.pat[p].root : 0; }
int  scPatScale(int p)  { return (p >= 0 && p < NPATTERNS) ? gEng.P.pat[p].scale : 0; }
void scSetPatKey(int p, int root, int scale) {
    if (p < 0 || p >= NPATTERNS) return;
    WP(p).root = (uint8_t)(((root % 12) + 12) % 12);
    gEng.P.pat[p].scale = (uint8_t)(scale < 0 ? 0 : (scale >= NSCALES ? NSCALES - 1 : scale));
}
int  scNSpeeds()        { return NSPEEDS; }
const char* scSpeedName(int i) { return (i >= 0 && i < NSPEEDS) ? SPEEDS[i].name : ""; }
int  scNScales()        { return NSCALES; }
const char* scScaleName(int i) { return (i >= 0 && i < NSCALES) ? SCALES[i].name : ""; }
const char* scNoteName(int i)  { return NOTE_NAMES[((i % 12) + 12) % 12]; }
int  scNDivs()          { return NDIVS; }
const char* scDivName(int i)   { return (i >= 0 && i < NDIVS) ? DIVS[i].name : ""; }
int  scDivValue(int i)  { return (i >= 0 && i < NDIVS) ? DIVS[i].div : 4; }
// Eine Stufe in der Tonart des laufenden Patterns weiter - fuer die NOTE-Tasten.
int  scScaleStep(int note, int dir) {
    const Pattern& pt = gEng.P.pat[gEng.pat];
    return scaleStep(note, pt.root, pt.scale, dir);
}
int  scScaleDeg(int note, int d) {
    const Pattern& pt = gEng.P.pat[gEng.pat];
    return scaleDegrees(note, d, pt.root, pt.scale);
}
int  scScaleSize() { return scaleSize(gEng.P.pat[gEng.pat].scale); }
int  scScaleQuant(int note) {
    const Pattern& pt = gEng.P.pat[gEng.pat];
    return scaleQuant(note, pt.root, pt.scale);
}
int  scScaleHas(int note) {
    const Pattern& pt = gEng.P.pat[gEng.pat];
    return scaleHas(note, pt.root, pt.scale) ? 1 : 0;
}

// --- Performance-Ebene -----------------------------------------------------------------------
void  scMute(int t, int on) { if (t >= 0 && t < NTRACKS) gEng.mute[t] = on != 0; }
int   scIsMute(int t)       { return (t >= 0 && t < NTRACKS && gEng.mute[t]) ? 1 : 0; }
void  scSetMorph(float v)   { gEng.morph = v; }
float scMorph()            { return gEng.morph; }
float scGetMorph()          { return gEng.morph; }
void  scFill(int on)        { gEng.seq.fill = on != 0; }
int   scIsFill()            { return gEng.seq.fill ? 1 : 0; }
void  scStepLoop(int on, int from, int len) {
    gEng.seq.loopOn = on != 0; gEng.seq.loopFrom = from; gEng.seq.loopLen = len;
}
void scSceneSet(int scene, int t, int slot, int v) {
    if (slot >= insertParamSlot(0)) return;
    W().sc[scene][t].set(slot, (int16_t)v);
}
void  scSceneClear(int scene, int t, int slot) {
    if (scene < 0 || scene >= NSCENES || t < 0 || t >= NTRACKS) return;
    W().sc[scene][t].clear(slot);
}
// Eine ganze Scene des laufenden Patterns leeren - alle Spuren.
void  scSceneClearAll(int scene) {
    if (scene < 0 || scene >= NSCENES) return;
    for (int t = 0; t < NTRACKS; t++) gEng.P.pat[gEng.pat].sc[scene][t].n = 0;
}
int   scSceneCount(int scene, int t) {
    if (scene < 0 || scene >= NSCENES || t < 0 || t >= NTRACKS) return 0;
    return gEng.P.pat[gEng.pat].sc[scene][t].n;
}
int   scSceneGet(int scene, int t, int slot) {
    const Lock* l = gEng.P.pat[gEng.pat].sc[scene][t].find(slot);
    return l ? l->val : -32768;
}

// Von Hand gespielt - dieselbe Kette wie ein Trig.
void scPlayNote(int t, int note, int vel) {
    Ev e{gEng.seq.pos, (uint8_t)t, EV_TRIG, 0, (int16_t)note, (int16_t)vel, (int32_t)(gEng.sr * 0.3f)};
    gEng.fire(e);
}

int scEnumAvailable(int p, int value, int mach) {
    return p >= 0 && p < NPARAMS && (!isModDestination(PARAMS[p]) || modDestinationName(value, mach));
}
int scInsertStage(int p) { return isInsertParam(p) ? insertStage(p) : -1; }
int scInsertVisible(int track, int p) {
    if (track < 0 || track >= NTRACKS || !isInsertParam(p)) return 0;
    int type = scGet(track, insertParamSlot(insertStage(p)));
    return insertFieldVisible(type, insertField(p));
}
float scInsertReduction(int track, int stage) {
    if (track < 0 || track >= NTRACKS || stage < 0 || stage >= INSERT_STAGES) return 0.0f;
    return gEng.inserts[track][stage].reductionDb();
}

// --- Sample-Vorrat ------------------------------------------------------------------------------
// Geladen wird hier, im Hauptfaden; der Audio-Faden liest nur. Die Oberflaeche schiebt die
// entpackten Samples als Fliesskomma herein - das Entpacken macht der Browser.
int   scSmpSlots()        { return SMP_SLOTS; }
int   scSmpFrames(int i)  { return (i >= 0 && i < SMP_SLOTS) ? gEng.pool.s[i].n : 0; }
float scSmpRate(int i)    { return (i >= 0 && i < SMP_SLOTS) ? gEng.pool.s[i].rate : 0.0f; }
int   scSmpLoop(int i)    { return (i >= 0 && i < SMP_SLOTS) ? gEng.pool.s[i].loop : 0; }
const char* scSmpName(int i) { return (i >= 0 && i < SMP_SLOTS) ? gEng.pool.s[i].name : ""; }
int   scSmpFree()         { return gEng.pool.freeFrames(); }
int   scSmpWanted(int i)  { return gEng.pool.wanted(i) ? 1 : 0; }
int   scSmpCh(int i)      { return (i >= 0 && i < SMP_SLOTS) ? gEng.pool.s[i].ch : 1; }
// Zeiger auf die Samples selbst - die Oberflaeche liest sie zurueck, um sie dem Audio-Faden
// nachzureichen (und spaeter, um die Wellenform zu zeichnen).
const float* scSmpData(int i) {
    return (i >= 0 && i < SMP_SLOTS) ? gEng.pool.data + gEng.pool.s[i].at : gEng.pool.data;
}
void  scSmpClear()        { gEng.recStop(); gEng.pool.clear(); }
int   scSmpSet(int slot, const float* d, int n, float rate, const char* name, int loop, int ch) {
    return gEng.pool.set(slot, d, n, rate, name, loop != 0, ch) ? 1 : 0;
}
// Ausschnitt und Schleife, samplegenau.
int   scSmpA(int i)  { return (i >= 0 && i < SMP_SLOTS) ? gEng.pool.s[i].a : 0; }
int   scSmpB(int i)  { return (i >= 0 && i < SMP_SLOTS) ? gEng.pool.s[i].end() : 0; }
int   scSmpLA(int i) { return (i >= 0 && i < SMP_SLOTS) ? gEng.pool.s[i].loopStart() : 0; }
void  scSmpSetTrim(int i, int a, int b, int la, int loop) { gEng.pool.setTrim(i, a, b, la, loop != 0); }
int   scSmpZero(int i, int f) { return gEng.pool.zeroCross(i, f); }
void  scSmpPreview(int i)     { gEng.preview(i); }
void  scSmpPreviewStop()      { gEng.previewStop(); }
int   scSmpPreviewing()       { return gEng.previewing() ? 1 : 0; }

int scSmpCutCount(int slot) { return scSmpFrames(slot) ? gEng.pool.s[slot].cutCount : 0; }
int scSmpCut(int slot, int i) {
    return i >= 0 && i < scSmpCutCount(slot) ? gEng.pool.s[slot].cuts[i] : 0;
}
int scSmpCutAdd(int slot, int frame) { return gEng.pool.addCut(slot, frame); }
int scSmpCutMove(int slot, int i, int frame) { return gEng.pool.setCut(slot, i, frame); }
int scSmpCutRemove(int slot, int i) { return gEng.pool.removeCut(slot, i); }
void scSmpCutsClear(int slot) { if (scSmpFrames(slot)) gEng.pool.s[slot].cutCount = 0; }
void scSmpSliceGrid(int slot, int count) { gEng.pool.sliceGrid(slot, count); }
int scSmpSliceTransients(int slot) {
    int32_t cuts[SMP_SLICES - 1];
    int count = gEng.pool.detectTransients(slot, cuts);
    gEng.pool.setCuts(slot, cuts, count);
    return count;
}
int scSmpSliceAt(int slot, int frame) {
    if (!scSmpFrames(slot)) return 0;
    const auto& sl = gEng.pool.s[slot];
    int index = 0;
    for (int i = 0; i < sl.cutCount; i++)
        if (sl.cuts[i] > sl.a && sl.cuts[i] < sl.end() && sl.cuts[i] <= frame) index++;
    return index;
}
void scSmpSlicePreview(int slot, int slice) { gEng.preview(slot, 6.0f, slice); }

static Track sliceUndoTrack;
static int16_t sliceUndoApplied[NSTEPS];
static int sliceUndoPat = -1, sliceUndoIndex = -1;

int scSliceLocks(int track, uint32_t seed) {
    if (track < 0 || track >= NTRACKS) return -1;
    const int machine = paramSlot(paramFind("trk.machine"));
    const int sample = MachSmpl::slotSmp(), slice = knobSlot(MachChop::C_SLICE);
    const int follow = knobSlot(MachChop::C_FOLLOW), grid = knobSlot(MachChop::C_SLICES);
    Track& tr = gEng.P.pat[gEng.pat].t[track];
    if (tr.p[machine] != 4) return -1;
    int counts[NSTEPS] = {};
    int written = 0;
    for (int i = 0; i < tr.len; i++) {
        const Step& st = tr.s[i];
        if (st.on != 1) continue;
        const Lock* smp = st.findLock(sample);
        const Lock* size = st.findLock(grid);
        int slot = smp ? smp->val : tr.p[sample];
        if (gEng.pool.empty(slot)) continue;
        if (st.nLock + !st.findLock(slice) + !st.findLock(follow) > NLOCKS) return -2;
        counts[i] = gEng.pool.s[slot].sliceCount(MachChop::nSlices(size ? size->val : tr.p[grid]));
        written++;
    }
    if (!written) return 0;
    sliceUndoTrack = tr;
    for (int16_t& value : sliceUndoApplied) value = -1;
    sliceUndoPat = gEng.pat;
    sliceUndoIndex = track;
    uint32_t rng = seed;
    int ordinal = 0;
    for (int i = 0; i < tr.len; i++) {
        if (!counts[i]) continue;
        int value = seed ? (int)(machRng(rng) % (uint32_t)counts[i]) : ordinal % counts[i];
        sliceUndoApplied[i] = (int16_t)value;
        tr.s[i].setLock(slice, (int16_t)value);
        tr.s[i].setLock(follow, 0);
        ordinal++;
    }
    W();
    return written;
}
int scSliceLocksUndo(int track) {
    if (sliceUndoPat != gEng.pat || sliceUndoIndex != track || sliceUndoPat < 0) return 0;
    Track& tr = W().t[track];
    const int slice = knobSlot(MachChop::C_SLICE), follow = knobSlot(MachChop::C_FOLLOW);
    for (int i = 0; i < NSTEPS; i++) {
        if (sliceUndoApplied[i] < 0) continue;
        const Lock* currentSlice = tr.s[i].findLock(slice);
        const Lock* currentFollow = tr.s[i].findLock(follow);
        if (!currentSlice || currentSlice->val != sliceUndoApplied[i] || currentSlice->slide
            || !currentFollow || currentFollow->val != 0 || currentFollow->slide) continue;
        for (int slot = slice; slot <= follow; slot++) {
            const Lock* before = sliceUndoTrack.s[i].findLock(slot);
            if (before) tr.s[i].setLock(slot, before->val, before->slide);
            else scLockClear(track, i, slot);
        }
    }
    sliceUndoPat = -1;
    return 1;
}

// Aufnahme: nimmt auf, was hinten herauskommt.
int   scRecStart(int slot, float seconds, const char* name, int ch) {
    return gEng.recStart(slot, seconds, name, ch);
}
void  scRecStop()   { gEng.recStop(); }
int   scRecOn()     { return gEng.recOn ? 1 : 0; }
int   scRecFrames() { return gEng.recFrames(); }

// --- Dokument: dieselben Zeilen wie die Datei und wie der Plugin-Zustand ------------------------
const char* scSave() {
    static Doc d;
    d.clear();
    projectToDoc(gEng.P, d);
    poolToDoc(gEng.pool, d);
    d.set("prj.pat", gEng.pat);          // mit welchem Pattern das Projekt aufgeht
    return d.writeText(gDoc, sizeof gDoc) ? gDoc : nullptr;
}
static bool readProjectText(const char* text, Doc& d) {
    d.clear();
    static char tmp[DOC_MAX * 48];
    size_t len = strlen(text);
    if (len >= sizeof tmp) return false;
    memcpy(tmp, text, len + 1);
    char* line = tmp;
    while (line && *line) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char* eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            char* end = eq - 1;
            while (end > line && (*end == ' ' || *end == '\t')) *end-- = 0;
            char* v = eq + 1;
            while (*v == ' ' || *v == '\t') v++;
            char* ve = v;
            while (*ve) ve++;
            while (ve > v && (ve[-1] == ' ' || ve[-1] == '\r')) *--ve = 0;
            if (Doc::isNumber(v)) d.set(line, (int32_t)strtol(v, nullptr, 10));
            else                  d.setStr(line, v);
        }
        line = nl ? nl + 1 : nullptr;
    }
    return !d.overflow;
}

static void applyProjectDoc(const Doc& d) {
    gEng.recStop();
    sliceUndoPat = -1;
    projectFromDoc(d, gEng.P);
    gEng.resetInserts();
    gEng.mastering.reset();
    poolFromDoc(d, gEng.pool);
    int p0 = d.get("prj.pat", 0);
    gEng.pat = (p0 >= 0 && p0 < NPATTERNS) ? p0 : 0;
    gEng.patNext = -1; gEng.switchAt = -1;
    gEng.seq.toPattern(gEng.seq.pos);
    gEng.seq.bpm = gEng.effTempo(gEng.pat);
    gEng.resetTrackValues();
}

int scLoad(const char* text) {
    static Doc d;
    if (!readProjectText(text, d)) return 0;
    applyProjectDoc(d);
    return 1;
}

// --- Ein Pattern zum Anfangen (steht in src/demo.h) -------------------------------------------
void scDemo() { demoProject(gEng); gEng.resetInserts(); gEng.mastering.reset(); }

// Speicher fuer die Oberflaeche (Audio-Puffer, Text) - einmal angefordert, nie im Render.
void* scAlloc(int bytes) { return malloc((size_t)bytes); }

// Ein WIEDERVERWENDETER Block fuer Uebergaben (Sample-Daten, Text). Ohne ihn fordert jede
// Uebergabe neuen Speicher an, der nie zurueckkommt - nach ein paar Samples ist Schluss.
static void*  gScratch = 0;
static size_t gScratchN = 0;
void* scScratch(int bytes) {
    size_t n = (size_t)(bytes < 16 ? 16 : bytes);
    if (n > gScratchN) {
        void* p = malloc(n);
        if (!p) return 0;
        gScratch = p; gScratchN = n;
    }
    return gScratch;
}

}  // extern "C"
