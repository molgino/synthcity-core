// Wie ein Wert angezeigt wird, steht EINMAL hier - Geraet, Handy und Plugin zeigen dasselbe.
// Die Zahl im Modell ist immer 0..127 (oder der Bereich aus der Tabelle); erst hier wird
// daraus "1.2 kHz", "0.46 s" oder "-6.4 dB".
#pragma once
#include "param.h"
#include "dsp.h"
#include <stdio.h>

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
