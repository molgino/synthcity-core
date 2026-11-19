// Wie ein Wert angezeigt wird, steht EINMAL hier - Geraet, Handy und Plugin zeigen dasselbe.
// Die Zahl im Modell ist immer 0..127 (oder der Bereich aus der Tabelle); erst hier wird
// daraus "1.2 kHz", "0.46 s" oder "-6.4 dB".
#pragma once
#include "param.h"
#include "dsp.h"
#include "machine.h"
#include "seq.h"
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
inline const char* fltTypeName(int v) {
    static const char* n[] = {"TIEFPASS", "BANDPASS", "HOCHPASS", "KERBE"};
    return n[((v % FLT_N) + FLT_N) % FLT_N];
}

// Wie viele sinnvolle Werte hat dieser Regler zur Auswahl? 0 = er ist stufenlos.
// Die Liste steht dort, wo auch die Bedeutung steht - die Oberflaeche kennt die Zahlen nicht.
inline int enumCount(const ParamDef& d) {
    if (!strcmp(d.id, "trk.machine"))  return machineCount();
    if (!strcmp(d.id, "trk.flt.type")) return FLT_N;
    if (d.kind == PK_COND)             return 9 + 19 + 34;
    if (d.kind == PK_ONOFF)            return 2;
    return 0;
}
// Ueberschrift ueber einer Gruppe gleichartiger Werte - eine Liste aus 62 Zeilen am Stueck
// liest niemand. Auch das weiss der Kern, nicht die Oberflaeche.
inline const char* enumGroup(const ParamDef& d, int i) {
    if (d.kind != PK_COND) return "";
    if (i < 9)  return "Fest";
    if (i < 28) return "Wahrscheinlichkeit";
    if (i < 45) return "Jeder A-te von B";
    return "Ausser jedem A-ten von B";
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

inline void paramText(const ParamDef& d, float v, char* out, int n) {
    // Werte, deren Einheit nur aus ihrer Bedeutung folgt, stehen vor der allgemeinen Regel.
    if (!strcmp(d.id, "trk.len")) {          // Zwoelftel eines Steps: 12 = ein ganzer Step
        snprintf(out, n, "%.2f Steps", (double)v / 12.0);
        return;
    }
    if (!strcmp(d.id, "trk.micro")) {        // Verschiebung in Zwoelfteln, 24 = genau auf dem Step
        snprintf(out, n, "%+.2f Steps", ((double)v - 24.0) / 24.0);
        return;
    }
    if (!strcmp(d.id, "trk.machine"))  { snprintf(out, n, "%s", machineAt((int)(v + 0.5f))->name()); return; }
    if (!strcmp(d.id, "trk.flt.type")) { snprintf(out, n, "%s", fltTypeName((int)(v + 0.5f))); return; }
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
    case PK_ONOFF: snprintf(out, n, v > 0.5f ? "AN" : "AUS");     return;
    default: break;
    }
    if (d.unit && d.unit[0]) snprintf(out, n, "%.0f %s", (double)v, d.unit);
    else                     snprintf(out, n, "%.0f", (double)v);
}
