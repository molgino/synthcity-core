// Wie ein Wert angezeigt wird, steht EINMAL hier - Geraet, Handy und Plugin zeigen dasselbe.
// Die Zahl im Modell ist immer 0..127 (oder der Bereich aus der Tabelle); erst hier wird
// daraus "1.2 kHz", "0.46 s" oder "-6.4 dB".
#pragma once
#include "param.h"
#include "dsp.h"
#include "machine.h"
#include "fx.h"
#include "insert.h"
#include "seq.h"
#include "mod.h"
#include <stdio.h>

// --- Auswahlwerte: Namen statt Zahlen ---------------------------------------------------------
// Eine Zahl, die man nachschlagen muss, ist keine Anzeige. Wer die Bedeutung kennt, schreibt sie
// hier auf - einmal, fuer Geraet, Handy und Plugin.
inline void condText(int v, char* out, int n) {
    switch (v) {
    case CND_NONE:  snprintf(out, n, "--");    return;
    case CND_FILL:  snprintf(out, n, "FILL");  return;
    case CND_NFILL: snprintf(out, n, "/FILL"); return;
    case CND_PRE:   snprintf(out, n, "PRE");   return;
    case CND_NPRE:  snprintf(out, n, "/PRE");  return;
    case CND_NEI:   snprintf(out, n, "NEI");   return;
    case CND_NNEI:  snprintf(out, n, "/NEI");  return;
    case CND_1ST:   snprintf(out, n, "1ST");   return;
    case CND_N1ST:  snprintf(out, n, "/1ST");  return;
    default: break;
    }
    if (v >= CND_PROB1 && v <= CND_PROB100) { snprintf(out, n, "%d %%", v - CND_PROB1 + 1); return; }
    if (v >= CND_AB0 && v < CND_NAB0) {
        int i = v - CND_AB0;
        snprintf(out, n, "%d:%d", i % 8 + 1, i / 8 + 2);
        return;
    }
    if (v >= CND_NAB0 && v <= CND_MAX) {
        int i = v - CND_NAB0;
        snprintf(out, n, "/%d:%d", i % 8 + 1, i / 8 + 2);
        return;
    }
    snprintf(out, n, "--");
}
// Abstand einer Akkordnote: 0 = aus, sonst Halbtoene um 25 herum, mit dem Namen des Intervalls.
inline void chordText(int v, char* out, int n) {
    if (v <= 0) { snprintf(out, n, "OFF"); return; }
    static const char* iv[12] = {"unison", "min 2nd", "maj 2nd", "min 3rd", "maj 3rd",
                                 "4th", "tritone", "5th", "min 6th", "maj 6th",
                                 "min 7th", "maj 7th"};
    int d = v - 25;
    int a = d < 0 ? -d : d;
    if (a == 12 || a == 24) snprintf(out, n, "%+d octave", d);
    else                    snprintf(out, n, "%+d %s", d, iv[a % 12]);
}
inline const char* vmodeName(int v) {
    static const char* n[] = {"POLY", "MONO", "LEGATO"};
    return n[v < 0 ? 0 : (v > 2 ? 2 : v)];
}
inline const char* fltTypeName(int v) {
    static const char* n[] = {"LOWPASS", "BANDPASS", "HIGHPASS", "NOTCH", "LADDER 24"};
    return n[((v % FLT_N) + FLT_N) % FLT_N];
}

// Wie viele sinnvolle Werte hat dieser Regler zur Auswahl? 0 = er ist stufenlos.
// Die Liste steht dort, wo auch die Bedeutung steht - die Oberflaeche kennt die Zahlen nicht.
// Letzter Teil einer ID ("trk.lfo1.rate" -> "rate"); ohne strrchr, das es freistehend nicht gibt.
inline const char* modField(const char* id) {
    const char* f = id;
    for (const char* c = id; *c; c++) if (*c == '.') f = c + 1;
    return f;
}
inline int enumCount(const ParamDef& d) {
    if (!strncmp(d.id, "trk.ins", 7) && d.kind == PK_ENUM) return d.hi + 1;
    if (!strcmp(d.id, "trk.machine"))  return machineCount();
    if (!strcmp(d.id, "fx.a.div"))     return DELAY_DIV_N;
    if (!strcmp(d.id, "trk.vmode"))    return 3;
    if (!strncmp(d.id, "trk.lfo", 7) || !strncmp(d.id, "trk.menv.", 9)) {
        const char* f = modField(d.id);
        if (!strcmp(f, "wave")) return LFO_WAVES;
        if (!strcmp(f, "rate")) return LFO_NRATES;
        if (!strcmp(f, "dest")) return MOD_NDEST;
        if (!strcmp(f, "mode")) return LFO_MODES;
    }
    if (!strcmp(d.id, "trk.flt.type")) return FLT_N;
    if (d.kind == PK_COND)             return 9 + 19 + 34;
    if (d.kind == PK_ONOFF)            return 2;
    return 0;
}
// Ueberschrift ueber einer Gruppe gleichartiger Werte - eine Liste aus 62 Zeilen am Stueck
// liest niemand. Auch das weiss der Kern, nicht die Oberflaeche.
inline const char* enumGroup(const ParamDef& d, int i) {
    if (!strncmp(d.id, "trk.", 4) && !strcmp(modField(d.id), "dest") && i > 0 && i < MOD_NDEST)
        return MOD_DESTS[i].trig ? "At trig (sampled once per note)" : "While the note plays";
    if (d.kind != PK_COND) return "";
    if (i < 9)  return "Fixed";
    if (i < 28) return "Probability";
    if (i < 45) return "Every A-th of B";
    return "All but every A-th of B";
}
inline int enumValue(const ParamDef& d, int i) {
    if (d.kind != PK_COND) return i;
    if (i < 9) return i;                                  // keine, FILL, PRE, NEI, 1ST ...
    i -= 9;
    if (i < 19) return condProb((i + 1) * 5);             // 5 % bis 95 %
    i -= 19;
    static const int bs[4] = {2, 3, 4, 8};                // jeder A-te von B Durchlaeufen
    int neg = 0;
    if (i >= 17) { neg = 1; i -= 17; }
    for (int k = 0; k < 4; k++) {
        if (i < bs[k]) return condAB(i + 1, bs[k], neg != 0);
        i -= bs[k];
    }
    return CND_NONE;
}

// mach: welche Machine die Spur gerade benutzt. Nur sie weiss, was ihre beiden eigenen
// Regler bedeuten - 64 heisst bei POLY "kein Detune" und bei FM "Verhaeltnis 2.5 : 1".
inline void paramText(const ParamDef& d, float v, char* out, int n, int mach = -1) {
    if (!strncmp(d.id, "trk.ins", 7)) {
        const char* field = d.id + 9;
        int value = (int)v;
        if (!strcmp(field, "type")) {
            static const char* names[] = {"OFF", "CRUSH / DRIVE", "COMPRESSOR"};
            snprintf(out, n, "%s", names[value >= 0 && value < INS_TYPE_COUNT ? value : 0]);
            return;
        }
        if (!strcmp(field, "source")) {
            if (value) snprintf(out, n, "TRACK %d", value);
            else snprintf(out, n, "SELF");
            return;
        }
        if (!strcmp(field, "rate")) {
            if (value) {
                float hz = crushRate(value);
                if (hz >= 1000.0f) snprintf(out, n, "%.1f kHz", hz / 1000.0f);
                else snprintf(out, n, "%.0f Hz", hz);
            } else snprintf(out, n, "NATIVE");
            return;
        }
        if (!strcmp(field, "bits")) {
            if (value) snprintf(out, n, "%d bit", value);
            else snprintf(out, n, "OFF");
            return;
        }
        if (!strcmp(field, "tone") && value == d.hi) { snprintf(out, n, "OPEN"); return; }
        if (!strcmp(field, "ratio")) { snprintf(out, n, "%d:1", value); return; }
        if (!strcmp(field, "sc.hpf") && !value) { snprintf(out, n, "OFF"); return; }
    }
    if (mach >= 0) {
        if (!strncmp(d.id, "trk.src.", 8) && d.id[8] >= 'a' && d.id[8] <= 'h' && !d.id[9]) {
            machineAt(mach)->text(d.id[8] - 'a', v, out, n); return;
        }
    }
    if (!strcmp(d.id, "fx.a.div")) {
        int i = (int)(v + 0.5f);
        snprintf(out, n, "%s", DELAY_DIV[i < 0 ? 0 : (i >= DELAY_DIV_N ? DELAY_DIV_N - 1 : i)].name);
        return;
    }
    // Werte, deren Einheit nur aus ihrer Bedeutung folgt, stehen vor der allgemeinen Regel.
    if (!strcmp(d.id, "trk.len")) {          // Zwoelftel eines Steps: 12 = ein ganzer Step
        int i = (int)(v + 0.5f);
        double st = lenSteps(i);
        if (st < 0)       snprintf(out, n, "INF");
        else if (i <= 96) snprintf(out, n, "%.2f Steps", st);
        else              snprintf(out, n, "%.0f Steps", st);
        return;
    }
    if (!strcmp(d.id, "trk.micro")) {        // Verschiebung in Zwoelfteln, 24 = genau auf dem Step
        snprintf(out, n, "%+.2f Steps", ((double)v - 24.0) / 24.0);
        return;
    }
    if (!strcmp(d.id, "trk.machine"))  { snprintf(out, n, "%s", machineAt((int)(v + 0.5f))->name()); return; }
    if (!strcmp(d.id, "trk.bw.base")) {
        if (v < 0.5f) { snprintf(out, n, "OFF"); return; }
        float hz = pFreq(v); if (hz >= 1000.0f) snprintf(out, n, "%.1f kHz", hz / 1000.0f); else snprintf(out, n, "%.0f Hz", hz); return;
    }
    if (!strcmp(d.id, "trk.bw.width")) {
        if (v > 126.5f) snprintf(out, n, "OPEN"); else snprintf(out, n, "%.1f oct", (double)(v * 10.0f / 127.0f)); return;
    }
    if (!strcmp(d.id, "trk.flt.type")) { snprintf(out, n, "%s", fltTypeName((int)(v + 0.5f))); return; }
    if (!strncmp(d.id, "trk.nt", 6))    { chordText((int)(v + 0.5f), out, n); return; }
    if (!strcmp(d.id, "trk.vmode"))     { snprintf(out, n, "%s", vmodeName((int)(v + 0.5f))); return; }
    if (!strncmp(d.id, "trk.lfo", 7) || !strncmp(d.id, "trk.menv.", 9)) {
        const char* f = modField(d.id);
        int i = (int)(v + 0.5f);
        if (!strcmp(f, "wave")) { snprintf(out, n, "%s", lfoWaveName(i)); return; }
        if (!strcmp(f, "rate")) { snprintf(out, n, "%s", LFO_RATES[ModState::clampRate(v)].name); return; }
        if (!strcmp(f, "mode")) { snprintf(out, n, "%s", lfoModeName(i)); return; }
        if (!strcmp(f, "dest")) { snprintf(out, n, "%s", MOD_DESTS[i < 0 ? 0 : (i >= MOD_NDEST ? 0 : i)].name); return; }
        if (!strcmp(f, "depth")) { snprintf(out, n, "%+.0f %%", (double)pBip(v) * 100.0); return; }
    }
    if (d.kind == PK_COND)             { condText((int)(v + 0.5f), out, n); return; }
    if (!strcmp(d.id, "trk.flt.freq")) {
        float hz = pFreq(v);
        if (hz >= 1000.0f) snprintf(out, n, "%.1f kHz", hz / 1000.0f);
        else               snprintf(out, n, "%.0f Hz", hz);
        return;
    }
    switch (d.kind) {
    case PK_LEVEL: {
        float lin = pLevel(v);
        if (lin <= 0.0f) snprintf(out, n, "-oo dB");
        else             snprintf(out, n, "%.1f dB", 20.0f * log10f(lin));
        return; }
    case PK_TIME: {
        float s = pTime(v);
        if (s < 1.0f) snprintf(out, n, "%.0f ms", s * 1000.0f);
        else          snprintf(out, n, "%.2f s", s);
        return; }
    case PK_NOTE: {
        static const char* nm[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        int k = (int)(v + 0.5f);
        snprintf(out, n, "%s%d", nm[((k % 12) + 12) % 12], k / 12 - 1);
        return; }
    // Die Mitte eines bipolaren Reglers ist die Mitte SEINES Bereichs, nicht immer 64.
    case PK_BIP:   snprintf(out, n, "%+d", (int)(v + 0.5f) - (d.lo + d.hi + 1) / 2); return;
    case PK_ONOFF: snprintf(out, n, v > 0.5f ? "ON" : "OFF");     return;
    default: break;
    }
    if (d.unit && d.unit[0]) snprintf(out, n, "%.0f %s", (double)v, d.unit);
    else                     snprintf(out, n, "%.0f", (double)v);
}
