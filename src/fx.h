// Effekte auf zwei Sende-Bussen. Sie stehen hinter den Stimmen, nicht in ihnen: jede Spur
// schickt einen Anteil hin, zurueck kommt eine Summe. So kostet Hall acht Spuren einmal,
// nicht achtmal - der Grund, warum Hardware es genauso macht.
#pragma once
#include "dsp.h"

enum { FX_MAXDELAY = 96000 };      // zwei Sekunden bei 48 kHz

// --- Bus A: Delay, im Takt ----------------------------------------------------------------
// Die Zeit ist ein Notenwert, keine Millisekundenzahl: ein Delay, das nicht zum Stueck passt,
// ist Krach. Der Teiler sagt, wie viele davon in eine Viertelnote gehen.
struct DelayDiv { const char* name; float beats; };
static const DelayDiv DELAY_DIV[] = {
    {"1/32",  0.125f}, {"1/16T", 1.0f / 6.0f}, {"1/16", 0.25f}, {"1/8T", 1.0f / 3.0f},
    {"1/8",   0.5f},   {"1/4T",  2.0f / 3.0f}, {"1/4",  1.0f},  {"1/2",  2.0f}, {"1/1", 4.0f},
};
enum { DELAY_DIV_N = (int)(sizeof DELAY_DIV / sizeof *DELAY_DIV) };

struct Delay {
    float l[FX_MAXDELAY], r[FX_MAXDELAY];
    int   at = 0, len = 12000;
    float lpL = 0.0f, lpR = 0.0f;

    void reset() {
        for (int i = 0; i < FX_MAXDELAY; i++) { l[i] = 0.0f; r[i] = 0.0f; }
        at = 0; lpL = lpR = 0.0f;
    }
    void setTime(int div, float bpm, float sr) {
        if (div < 0) div = 0;
        if (div >= DELAY_DIV_N) div = DELAY_DIV_N - 1;
        int n = (int)(sr * 60.0f / (bpm < 20.0f ? 20.0f : bpm) * DELAY_DIV[div].beats);
        len = n < 64 ? 64 : (n > FX_MAXDELAY ? FX_MAXDELAY : n);
    }
    // Kreuzweise Rueckkopplung: das Echo wandert zwischen links und rechts.
    void run(const float* inL, const float* inR, float* outL, float* outR, int n,
             float fb, float tone, float gain) {
        float c = tone * tone;                       // Hoehen im Echo daempfen
        for (int i = 0; i < n; i++) {
            int rd = at;
            float dl = l[rd], dr = r[rd];
            lpL = dl + (lpL - dl) * c;
            lpR = dr + (lpR - dr) * c;
            l[at] = undenorm(inL[i] + lpR * fb);
            r[at] = undenorm(inR[i] + lpL * fb);
            outL[i] += dl * gain;
            outR[i] += dr * gain;
            if (++at >= len) at = 0;
        }
    }
};

// --- Bus B: Hall als Feedback-Delay-Netzwerk ------------------------------------------------
// Vier Leitungen, eine Householder-Matrix dazwischen: wenig Rechnung, dichte Fahne.
struct Reverb {
    enum { N = 4, MAX = 4800 };
    float buf[N][MAX];
    int   at[N] = {0, 0, 0, 0}, len[N] = {1237, 1601, 1867, 2203};
    float lp[N] = {0, 0, 0, 0};

    void reset() {
        for (int i = 0; i < N; i++) { for (int k = 0; k < MAX; k++) buf[i][k] = 0.0f; at[i] = 0; lp[i] = 0.0f; }
    }
    void setSize(float size, float sr) {
        static const int base[N] = {1237, 1601, 1867, 2203};   // teilerfremd: keine stehenden Wellen
        float f = (0.35f + 0.65f * size) * sr / 48000.0f;
        for (int i = 0; i < N; i++) {
            int n = (int)(base[i] * f);
            len[i] = n < 64 ? 64 : (n > MAX ? MAX : n);
            if (at[i] >= len[i]) at[i] = 0;
        }
    }
    void run(const float* inL, const float* inR, float* outL, float* outR, int n,
             float decay, float damp, float gain) {
        float c = damp * damp;
        for (int i = 0; i < n; i++) {
            float v[N];
            for (int k = 0; k < N; k++) {
                float x = buf[k][at[k]];
                lp[k] = x + (lp[k] - x) * c;
                v[k] = lp[k];
            }
            // Householder: jede Leitung bekommt den halben Mittelwert aller abgezogen
            float s = 0.5f * (v[0] + v[1] + v[2] + v[3]);
            float w[N] = {s - v[0], s - v[1], s - v[2], s - v[3]};
            float inM = 0.5f * (inL[i] + inR[i]);
            for (int k = 0; k < N; k++) {
                buf[k][at[k]] = undenorm(inM + w[k] * decay);
                if (++at[k] >= len[k]) at[k] = 0;
            }
            outL[i] += 0.6f * gain * (v[0] + v[2]);
            outR[i] += 0.6f * gain * (v[1] + v[3]);
        }
    }
};
