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
        // TONE 1 = hell (kein Filter), 0 = dunkel. Nie ganz zu: c < 1, sonst verstummt das Echo.
        float c = (1.0f - tone) * (1.0f - tone) * 0.92f;
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
// Vier Allpaesse verwischen den Eingang (Diffusion), dann acht Leitungen mit einer Hadamard-
// Matrix dazwischen. Die Abklingzeit ist eine echte RT60 in Sekunden: jede Leitung bekommt
// genau die Daempfung, die ihrer Laenge entspricht - so klingt der Raum gleichmaessig aus.
struct Allpass {
    enum { MAX = 1024 };
    float b[MAX]; int at = 0, len = 100;
    void reset() { for (int i = 0; i < MAX; i++) b[i] = 0.0f; at = 0; }
    float run(float x, float g) {
        float d = b[at];
        float y = d - g * x;
        b[at] = undenorm(x + g * y);
        if (++at >= len) at = 0;
        return y;
    }
};
struct Reverb {
    enum { N = 8, MAX = 9600 };
    float buf[N][MAX];
    int   at[N] = {0}, len[N] = {0};
    float lp[N] = {0};
    Allpass ap[4];
    float sr = 48000.0f;

    void reset() {
        for (int i = 0; i < N; i++) { for (int k = 0; k < MAX; k++) buf[i][k] = 0.0f; at[i] = 0; lp[i] = 0.0f; }
        static const int al[4] = {142, 107, 379, 277};
        for (int i = 0; i < 4; i++) { ap[i].reset(); ap[i].len = al[i]; }
        setSize(0.5f, sr);
    }
    void setSize(float size, float rate) {
        sr = rate;
        // teilerfremd: keine stehenden Wellen. 32 .. 88 ms bei mittlerer Groesse.
        static const int base[N] = {1559, 1907, 2251, 2683, 3011, 3407, 3793, 4243};
        float f = (0.45f + 1.5f * size) * sr / 48000.0f;
        for (int i = 0; i < N; i++) {
            int n = (int)(base[i] * f);
            len[i] = n < 64 ? 64 : (n > MAX ? MAX : n);
            if (at[i] >= len[i]) at[i] = 0;
        }
        static const int al[4] = {142, 107, 379, 277};
        for (int i = 0; i < 4; i++) {
            int n = (int)((float)al[i] * sr / 48000.0f);
            ap[i].len = n < 8 ? 8 : (n > Allpass::MAX ? Allpass::MAX : n);
            if (ap[i].at >= ap[i].len) ap[i].at = 0;
        }
    }
    // decay 0..1 -> 0,3 .. 15 s; damp 0..1 -> Hoehen klingen schneller ab (nie ganz zu).
    void run(const float* inL, const float* inR, float* outL, float* outR, int n,
             float decay, float damp, float gain) {
        float rt = 0.3f * powf(50.0f, decay);
        float g[N];
        for (int k = 0; k < N; k++) g[k] = powf(10.0f, -3.0f * (float)len[k] / (sr * rt));
        float c = damp * 0.85f;
        const float h = 0.35355339f;                 // 1/sqrt(8): Hadamard bleibt verlustfrei
        for (int i = 0; i < n; i++) {
            float x = 0.5f * (inL[i] + inR[i]);
            for (int a = 0; a < 4; a++) x = ap[a].run(x, 0.62f);
            float v[N];
            for (int k = 0; k < N; k++) {
                float y = buf[k][at[k]];
                lp[k] = y * (1.0f - c) + lp[k] * c;
                v[k] = lp[k];
            }
            // schnelle Walsh-Hadamard-Transformation (8 Punkte)
            for (int st = 1; st < N; st <<= 1)
                for (int k = 0; k < N; k += st << 1)
                    for (int j = k; j < k + st; j++) { float p = v[j], q = v[j + st]; v[j] = p + q; v[j + st] = p - q; }
            for (int k = 0; k < N; k++) {
                buf[k][at[k]] = undenorm(x + v[k] * h * g[k]);
                if (++at[k] >= len[k]) at[k] = 0;
            }
            float l = lp[0] + lp[2] + lp[4] + lp[6], r = lp[1] + lp[3] + lp[5] + lp[7];
            outL[i] += 0.25f * gain * l;
            outR[i] += 0.25f * gain * r;
        }
    }
};
