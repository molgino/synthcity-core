// Effekte auf zwei Sende-Bussen. Sie stehen hinter den Stimmen, nicht in ihnen: jede Spur
// schickt einen Anteil hin, zurueck kommt eine Summe. So kostet Hall acht Spuren einmal,
// nicht achtmal - der Grund, warum Hardware es genauso macht.
#pragma once
#include "master.h"
#include "fx_control.h"

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

inline const char* delayTypeName(int mode) {
    static const char* names[] = {"Ping Pong", "Stereo", "Tape"};
    return names[mode >= 0 && mode < DLY_TYPES ? mode : DLY_PING_PONG];
}
inline const char* delayTimingName(int timing) {
    static const char* names[] = {"Linked Sync", "Dual Sync", "Free L/R"};
    return names[timing >= 0 && timing < DLY_TIMINGS ? timing : DLY_LINKED];
}

struct Delay {
    float l[FX_MAXDELAY], r[FX_MAXDELAY];
    int at = 0, lenL = 12000, lenR = 12000, mode = DLY_PING_PONG;
    float lpL = 0.0f, lpR = 0.0f, currentL = 0.0f, currentR = 0.0f;
    float phase = 0, phaseInc = 0, wow = 0, tapeDrive = 1, tapeSlew = 0;
    int lastBase = -1, lastWidth = -1;
    float lastRate = 0.0f;
    bool hpOn = false, lpOn = false;
    Svf hp[2], lp[2];

    void reset() {
        for (int i = 0; i < FX_MAXDELAY; i++) { l[i] = 0.0f; r[i] = 0.0f; }
        at = 0; lpL = lpR = phase = currentL = currentR = 0.0f;
        lastRate = 0;
        for (int c = 0; c < 2; c++) { hp[c].reset(); lp[c].reset(); }
    }
    static int length(float frames) {
        return frames < 1 ? 1 : frames > FX_MAXDELAY ? FX_MAXDELAY : (int)frames;
    }
    void configure(const int* v, float bpm, float sr) {
        using F = FxControls;
        const float beatFrames = sr * 60.0f / (bpm < 20.0f ? 20.0f : bpm);
        // Keep the legacy minimum for the clocked tap and allow 1 ms when free.
        const int syncL = length(beatFrames * DELAY_DIV[v[F::D_DIV]].beats);
        const int syncR = length(beatFrames * DELAY_DIV[v[F::D_DIV_R]].beats);
        const int timing = v[F::D_TIMING];
        lenL = timing == DLY_FREE ? length(v[F::D_MS_L] * sr * 0.001f) : (syncL < 64 ? 64 : syncL);
        lenR = timing == DLY_FREE ? length(v[F::D_MS_R] * sr * 0.001f)
             : timing == DLY_DUAL ? (syncR < 64 ? 64 : syncR) : lenL;
        mode = v[F::D_ALGO];
        if (mode == DLY_TAPE) {
            if (currentL <= 0.0f) { currentL = lenL; currentR = lenR; }
            phaseInc = 0.61f / sr;
            tapeSlew = 1.0f / (0.045f * sr);
            wow = v[F::D_WOW] * sr * 0.000025f;
            tapeDrive = StereoInsert::dbGain(v[F::D_DRIVE]);
        } else {
            currentL = lenL;
            currentR = lenR;
        }
        const int base = v[F::D_BASE], width = v[F::D_WIDTH];
        if (lastBase == base && lastWidth == width && lastRate == sr) return;
        lastBase = base; lastWidth = width; lastRate = sr;
        hpOn = base != 0;
        lpOn = width < 127;
        const float low = hpOn ? pFreq(base) : 20.0f;
        const float high = low * exp2f(width * 10.0f / 127.0f);
        for (int c = 0; c < 2; c++) {
            if (hpOn) hp[c].set(low, 0.70710678f, sr);
            else hp[c].reset();
            if (lpOn) lp[c].set(high, 0.70710678f, sr);
            else lp[c].reset();
        }
    }
    int readIndex(int delay) const {
        int idx = at - delay;
        return idx < 0 ? idx + FX_MAXDELAY : idx;
    }
    float tapeTap(const float* data, float delay) const {
        delay = delay < 1.0f ? 1.0f : delay > FX_MAXDELAY - 1.0f ? FX_MAXDELAY - 1.0f : delay;
        int frames = (int)delay;
        float first = data[readIndex(frames)], next = data[readIndex(frames + 1)];
        return first + (delay - frames) * (next - first);
    }
    void run(const float* inL, const float* inR, float* outL, float* outR, int n,
             float fb, float tone, float gain) {
        const float c = (1.0f - tone) * (1.0f - tone) * 0.92f;
        const bool tape = mode == DLY_TAPE;
        const bool pingPong = mode == DLY_PING_PONG;
        for (int i = 0; i < n; i++) {
            float dl, dr;
            if (tape) {
                currentL += tapeSlew * (lenL - currentL);
                currentR += tapeSlew * (lenR - currentR);
                // One slow wow cycle and a quieter, faster flutter component.
                const float fast = phase * 9.0f;
                const float flutter = (fabsf(fast - (int)fast - 0.5f) * 4.0f - 1.0f) * 0.12f;
                const float wowPhase = fabsf(phase - 0.5f) * 4.0f - 1.0f;
                dl = tapeTap(l, currentL + wow * (wowPhase + flutter));
                dr = tapeTap(r, currentR + wow * (-wowPhase + flutter));
                phase += phaseInc;
                if (phase >= 1.0f) phase -= 1.0f;
            } else {
                dl = l[readIndex(lenL)]; dr = r[readIndex(lenR)];
            }
            if (hpOn) { dl = hp[0].run(dl, FLT_HP); dr = hp[1].run(dr, FLT_HP); }
            if (lpOn) { dl = lp[0].run(dl, FLT_LP); dr = lp[1].run(dr, FLT_LP); }
            lpL = dl + (lpL - dl) * c;
            lpR = dr + (lpR - dr) * c;
            float newL = inL[i] + (pingPong ? lpR : lpL) * fb;
            float newR = inR[i] + (pingPong ? lpL : lpR) * fb;
            if (tape) {
                newL = softSat(newL * tapeDrive) / tapeDrive;
                newR = softSat(newR * tapeDrive) / tapeDrive;
            }
            l[at] = undenorm(newL);
            r[at] = undenorm(newR);
            outL[i] += dl * gain;
            outR[i] += dr * gain;
            if (++at == FX_MAXDELAY) at = 0;
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
inline const char* reverbTypeName(int mode) {
    static const char* names[] = {"Studio", "Room", "Plate", "Hall"};
    return names[mode >= 0 && mode < REV_TYPES ? mode : REV_STUDIO];
}

struct Reverb {
    enum { N = 8, MAX = 9600, PRE = 24001 };
    float buf[N][MAX], preDelay[2][PRE];
    int at[N] = {}, len[N] = {}, preAt = 0, preFrames = 0, algorithm = REV_STUDIO;
    float lp[N] = {}, feedback[N] = {};
    Allpass ap[4];
    Svf inputHp[2], inputLp[2];
    MasterBiquad lowShelf, highShelf;
    float sr = 0.0f, width = 1.0f, diffusion = 0.62f, sideInput = 0.0f;
    int last[FxControls::FIELDS];
    bool hpOn = false, lpOn = false, lowOn = false, highOn = false;

    void reset() {
        memset(buf, 0, sizeof buf);
        memset(preDelay, 0, sizeof preDelay);
        for (int i = 0; i < N; i++) { at[i] = 0; lp[i] = 0.0f; }
        for (auto& a : ap) a.reset();
        for (int c = 0; c < 2; c++) { inputHp[c].reset(); inputLp[c].reset(); }
        lowShelf = highShelf = MasterBiquad{};
        preAt = 0; sr = 0;
    }
    void configure(const int* v, float rate) {
        using F = FxControls;
        bool newRate = sr != rate;
        if (newRate || v[F::R_SIZE] != last[F::R_SIZE] || v[F::R_ALGO] != last[F::R_ALGO]) {
            static const int lines[][N] = {
                {1559, 1907, 2251, 2683, 3011, 3407, 3793, 4243},
                {509, 701, 947, 1151, 1409, 1669, 1999, 2293},
                {877, 1279, 1429, 1993, 2579, 2957, 3361, 3613},
                {1907, 2371, 3001, 3307, 3907, 4591, 5231, 5897}
            };
            static const int diffusors[][4] = {{142, 107, 379, 277}, {61, 97, 149, 223},
                                               {113, 167, 283, 421}, {181, 271, 431, 631}};
            static const float diffusionByMode[] = {0.62f, 0.56f, 0.72f, 0.68f};
            static const float stereoInput[] = {0.0f, 0.42f, 0.26f, 0.38f};
            algorithm = v[F::R_ALGO];
            diffusion = diffusionByMode[algorithm];
            sideInput = stereoInput[algorithm];
            const float scale = (0.45f + 1.5f * pUni(v[F::R_SIZE])) * rate / 48000.0f;
            for (int i = 0; i < N; i++) {
                const int n = (int)(lines[algorithm][i] * scale);
                len[i] = n < 64 ? 64 : n > MAX ? MAX : n;
                if (at[i] >= len[i]) at[i] = 0;
            }
            for (int i = 0; i < 4; i++) {
                const int n = (int)(diffusors[algorithm][i] * rate / 48000.0f);
                ap[i].len = n < 8 ? 8 : n > Allpass::MAX ? Allpass::MAX : n;
                if (ap[i].at >= ap[i].len) ap[i].at = 0;
            }
            newRate = true; // also invalidates tail and filter coefficients
        }
        if (newRate || v[F::R_DECAY] != last[F::R_DECAY]) {
            static const float decayByMode[] = {1.0f, 0.56f, 0.9f, 1.25f};
            const float rt = 0.3f * powf(50.0f, pUni(v[F::R_DECAY])) * decayByMode[algorithm];
            for (int i = 0; i < N; i++) feedback[i] = powf(10.0f, -3.0f * len[i] / (rate * rt));
        }
        if (newRate || v[F::R_PRE] != last[F::R_PRE]) {
            const int frames = (int)(v[F::R_PRE] * rate * 0.001f);
            preFrames = frames < PRE ? frames : PRE - 1;
        }
        if (newRate || v[F::R_HPF] != last[F::R_HPF] || v[F::R_LPF] != last[F::R_LPF]) {
            hpOn = v[F::R_HPF] > 0;
            lpOn = v[F::R_LPF] < 20000;
            for (int c = 0; c < 2; c++) {
                if (hpOn) inputHp[c].set(v[F::R_HPF], 0.70710678f, rate);
                else inputHp[c].reset();
                if (lpOn) inputLp[c].set(v[F::R_LPF], 0.70710678f, rate);
                else inputLp[c].reset();
            }
        }
        if (newRate || v[F::R_LOW] != last[F::R_LOW] || v[F::R_LOW_HZ] != last[F::R_LOW_HZ]) {
            lowOn = v[F::R_LOW] != 0;
            if (lowOn) lowShelf.eq(v[F::R_LOW_HZ], v[F::R_LOW], rate, EQ_LOW_SHELF, 0.70710678f);
            else lowShelf = MasterBiquad{};
        }
        if (newRate || v[F::R_HIGH] != last[F::R_HIGH] || v[F::R_HIGH_HZ] != last[F::R_HIGH_HZ]) {
            highOn = v[F::R_HIGH] != 0;
            if (highOn) highShelf.eq(v[F::R_HIGH_HZ], v[F::R_HIGH], rate, EQ_HIGH_SHELF, 0.70710678f);
            else highShelf = MasterBiquad{};
        }
        width = v[F::R_WIDTH] * 0.01f;
        sr = rate;
        for (int i = F::R_SIZE; i < F::FIELDS; i++) last[i] = v[i];
    }

    void run(const float* inL, const float* inR, float* outL, float* outR, int n,
             float damp, float gain) {
        const float c = damp * 0.85f;
        const float h = 0.35355339f;
        for (int i = 0; i < n; i++) {
            float inputL = inL[i], inputR = inR[i];
            if (hpOn) { inputL = inputHp[0].run(inputL, FLT_HP); inputR = inputHp[1].run(inputR, FLT_HP); }
            if (lpOn) { inputL = inputLp[0].run(inputL, FLT_LP); inputR = inputLp[1].run(inputR, FLT_LP); }
            preDelay[0][preAt] = inputL;
            preDelay[1][preAt] = inputR;
            if (preFrames) {
                int at = preAt - preFrames;
                if (at < 0) at += PRE;
                inputL = preDelay[0][at];
                inputR = preDelay[1][at];
            }
            if (++preAt == PRE) preAt = 0;
            float x = 0.5f * (inputL + inputR);
            const float side = 0.5f * (inputL - inputR) * sideInput;
            for (int a = 0; a < 4; a++) x = ap[a].run(x, diffusion);
            float v[N];
            for (int k = 0; k < N; k++) {
                float y = buf[k][at[k]];
                lp[k] = y * (1.0f - c) + lp[k] * c;
                v[k] = lp[k];
            }
            for (int st = 1; st < N; st <<= 1)
                for (int k = 0; k < N; k += st << 1)
                    for (int j = k; j < k + st; j++) { float p = v[j], q = v[j + st]; v[j] = p + q; v[j + st] = p - q; }
            for (int k = 0; k < N; k++) {
                buf[k][at[k]] = undenorm(x + ((k & 1) ? -side : side) + v[k] * h * feedback[k]);
                if (++at[k] >= len[k]) at[k] = 0;
            }
            float l = lp[0] + lp[2] + lp[4] + lp[6], r = lp[1] + lp[3] + lp[5] + lp[7];
            if (lowOn) { l = lowShelf.tick(l, 0); r = lowShelf.tick(r, 1); }
            if (highOn) { l = highShelf.tick(l, 0); r = highShelf.tick(r, 1); }
            if (width != 1.0f) {
                const float mid = (l + r) * 0.5f, pan = (l - r) * 0.5f * width;
                l = mid + pan; r = mid - pan;
            }
            outL[i] += 0.25f * gain * l;
            outR[i] += 0.25f * gain * r;
        }
    }
};
