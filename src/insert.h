// Inserts process the summed stereo track; the detector can use a separate dry track.
#pragma once
#include "param.h"
#include "dsp.h"

inline float crushRate(int value) {
    return 500.0f * exp2f((value - 1) * (6.5849625f / 126.0f));
}

inline const char* clipShapeName(int shape) {
    static const char* names[] = {"Hard", "Cubic soft", "Tanh", "Arctan"};
    return names[shape >= 0 && shape < CLIP_SHAPES ? shape : CLIP_HARD];
}

inline float clipTransfer(float x, int shape) {
    switch (shape) {
    case CLIP_CUBIC:
        x *= 2.0f / 3.0f;
        if (x >= 1.0f) return 1.0f;
        if (x <= -1.0f) return -1.0f;
        return 1.5f * x - 0.5f * x * x * x;
    case CLIP_TANH: return tanhf(x);
    case CLIP_ATAN: return atanf(x * 1.570796327f) * 0.636619772f;
    default: return x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
    }
}

inline float compressorReduction(float inputDb, float threshold, float knee, float slope) {
    const float over = inputDb - threshold;
    if (over <= -knee * 0.5f) return 0.0f;
    if (knee > 0.0f && over < knee * 0.5f) {
        const float distance = over + knee * 0.5f;
        return slope * distance * distance / (2.0f * knee);
    }
    return slope * over;
}

struct StereoChorus {
    // Fixed buffers for the audio thread. Reset only the cursor and valid length.
    enum { BUFFER = 8192, TABLE = 256 };
    float delay[2][BUFFER];
    int write = 0, valid = 0;
    double phase = 0.0;
    float depth = 0.0f, width = 0.0f;

    struct SineTable {
        float val[TABLE + 1];
        SineTable() {
            for (int i = 0; i < TABLE; i++) val[i] = sinf(i * (6.283185307f / TABLE));
            val[TABLE] = val[0];
        }
    };
    static const SineTable& sineTable() { static const SineTable table; return table; }
    static float mod(const float* t, float phase) {
        if (phase < 0.0f) phase += 1.0f;
        if (phase >= 1.0f) phase -= 1.0f;
        float index = phase * TABLE;
        int i = (int)index;
        return t[i] + (index - i) * (t[i + 1] - t[i]);
    }
    static void warm() { sineTable(); }

    void reset(float d, float w) {
        write = valid = 0;
        phase = 0.0f;
        depth = d;
        width = w;
    }
    float tap(int channel, float frames) const {
        int n = (int)frames;
        float fraction = frames - n;
        const float* line = delay[channel];
        float a = line[(write - n) & (BUFFER - 1)];
        float b = line[(write - n - 1) & (BUFFER - 1)];
        return a + fraction * (b - a);
    }
    void run(float* y, int mode, float speed, float targetDepth, float targetWidth,
             float samplesPerMs, float smoothing) {
        depth += smoothing * (targetDepth - depth);
        width += smoothing * (targetWidth - width);

        delay[0][write] = y[0];
        delay[1][write] = y[1];
        if (valid < 2 * BUFFER) valid++;
        // Wait for valid history, then bring in the delayed signal softly.
        const float longest = (mode ? 18.0f : 13.0f) * samplesPerMs;
        float ready = (valid - longest - 2.0f) / (5.0f * samplesPerMs);
        if (ready > 1.0f) ready = 1.0f;
        if (ready > 0.0f) {
            const float* table = sineTable().val;
            float excursion = depth * (mode ? 3.0f : 4.0f);
            for (int channel = 0; channel < 2; channel++) {
                float p = (float)phase + (channel ? -width : width) * 0.25f;
                float first = tap(channel, (9.0f + excursion * mod(table, p)) * samplesPerMs);
                if (mode) {
                    float second = tap(channel, (15.0f + excursion * mod(table, p + 0.25f)) * samplesPerMs);
                    first = 0.5f * (first + second);
                }
                y[channel] += 0.5f * ready * (first - y[channel]);
            }
        }

        write = (write + 1) & (BUFFER - 1);
        phase += speed;
        if (phase >= 1.0f) phase -= 1.0f;
    }
};

struct StereoInsert {
    struct Settings {
        InsertType type = INS_OFF;
        int source = 0, clipShape = CLIP_HARD;
        float clipCeil = 1.0f, clipDrive = 1.0f;
        float rmsCoeff = 0.0f, autoRelease = 0.0f;
        float mix = 0.0f, trim = 1.0f;
        float rate = 1.0f, levels = 0.0f, drive = 0.0f;
        float pre = 0.0f, tone = 0.0f;
        float threshold = -18.0f, knee = 6.0f, slope = 0.75f, quiet = 0.0f;
        float attack = 0.0f, release = 0.0f, hp = 0.0f;
        int chorusMode = 0;
        float chorusSpeed = 0.0f, chorusDepth = 0.0f, chorusWidth = 1.0f, chorusSamplesPerMs = 48.0f;
    };

    Settings active, next;
    int16_t values[INSERT_FIELDS] = {};
    float rateHz = 0.0f, fadeStep = 1.0f;
    float wet = 0.0f, trim = 1.0f, drive = 0.0f;
    float phase = 1.0f, held[2] = {}, pre1[2] = {}, pre2[2] = {}, tone[2] = {};
    float detector[2] = {}, peak = 0.0f, wantedGain = 1.0f, gain = 1.0f, rmsPower = 0.0f;
    unsigned detectorClock = 0;
    StereoChorus chorus;

    static float filterCoeff(float hz, float sr) {
        return 1.0f - expf(-6.283185307f * hz / sr);
    }
    static float dbGain(float db) { return exp2f(db * 0.166096405f); }
    static float slew(float x, float target, float amount) {
        float d = target - x;
        return d > amount ? x + amount : d < -amount ? x - amount : target;
    }
    static float quantize(float x, float levels) {
        if (!levels) return x;
        if (levels == 1.0f) return x > 0.0f ? 1.0f : x < 0.0f ? -1.0f : 0.0f;
        float code = x < -1.0f ? -levels : x > 1.0f ? levels : x * levels;
        int rounded = (int)(code + (code >= 0.0f ? 0.5f : -0.5f));
        return (rounded >= levels ? levels - 1.0f : (float)rounded) / levels;
    }

    void configure(const int16_t* p, float sr) {
        if (p[INS_TYPE] == INS_OFF && active.type == INS_OFF && next.type == INS_OFF) return;
        if (sr == rateHz) {
            int i = 0;
            while (i < INSERT_FIELDS && p[i] == values[i]) i++;
            if (i == INSERT_FIELDS) return;
        }
        memcpy(values, p, sizeof values);
        rateHz = sr;
        fadeStep = 1.0f / (0.005f * sr);
        Settings s;
        s.type = p[INS_TYPE] > INS_OFF && p[INS_TYPE] < INS_TYPE_COUNT
            ? (InsertType)p[INS_TYPE] : INS_OFF;
        s.mix = s.type == INS_OFF ? 0.0f : p[INS_MIX] * 0.01f;
        if (s.mix == 0.0f) s.type = INS_OFF;
        s.trim = dbGain(p[INS_TRIM]);
        if (s.type == INS_CRUSH) {
            const float hz = p[INS_RATE] ? crushRate(p[INS_RATE]) : sr;
            s.rate = hz < sr ? hz / sr : 1.0f;
            s.levels = p[INS_BITS] ? (float)(1u << (p[INS_BITS] - 1)) : 0.0f;
            s.drive = dbGain(p[INS_DRIVE]) - 1.0f;
            if (p[INS_PREFILTER] && hz < sr) s.pre = filterCoeff(0.4f * hz, sr);
            if (p[INS_TONE] < 20000 && p[INS_TONE] < sr * 0.49f)
                s.tone = filterCoeff((float)p[INS_TONE], sr);
        } else if (s.type == INS_COMPRESS) {
            s.source = p[INS_SOURCE];
            s.threshold = p[INS_THRESHOLD];
            s.knee = p[INS_KNEE];
            s.slope = 1.0f - 1.0f / p[INS_RATIO];
            s.quiet = dbGain(s.threshold - s.knee * 0.5f);
            s.attack = 1.0f - expf(-1000.0f / (p[INS_ATTACK] * sr));
            s.release = 1.0f - expf(-1000.0f / (p[INS_RELEASE] * sr));
            if (p[INS_SC_HPF]) s.hp = filterCoeff((float)p[INS_SC_HPF], sr);
            if (p[INS_DETECT]) s.rmsCoeff = 1.0f - expf(-1.0f / (0.010f * sr));
            if (p[INS_AUTO_RELEASE]) s.autoRelease = 1.0f - expf(-1.0f / (0.035f * sr));
        } else if (s.type == INS_CLIP) {
            s.clipShape = p[INS_CLIP_SHAPE];
            s.clipCeil = dbGain(p[INS_CLIP_CEIL]);
            s.clipDrive = dbGain(p[INS_DRIVE]) / s.clipCeil;
        } else if (s.type == INS_CHORUS) {
            s.chorusMode = p[INS_CH_MODE];
            s.chorusSpeed = p[INS_CH_RATE] * 0.01f / sr;
            s.chorusDepth = p[INS_CH_DEPTH] * 0.01f;
            s.chorusWidth = p[INS_CH_WIDTH] * 0.01f;
            constexpr float limit = (StereoChorus::BUFFER - 3.0f) / 18.0f;
            float samplesPerMs = sr * 0.001f;
            s.chorusSamplesPerMs = samplesPerMs < limit ? samplesPerMs : limit;
        }
        next = s;
        if (sameAlgorithm()) active = next;
    }
    bool sameAlgorithm() const {
        return active.type == next.type
            && (active.type != INS_CHORUS || active.chorusMode == next.chorusMode)
            && (active.type != INS_CLIP || active.clipShape == next.clipShape);
    }
    void resetState() {
        trim = active.trim;
        drive = active.drive;
        phase = 1.0f;
        for (int c = 0; c < 2; c++) held[c] = pre1[c] = pre2[c] = tone[c] = detector[c] = 0.0f;
        peak = rmsPower = 0.0f;
        gain = wantedGain = 1.0f;
        detectorClock = 0;
    }
    void reset() {
        active = next = Settings{};
        rateHz = 0.0f;
        wet = 0.0f;
        resetState();
        chorus.reset(0.0f, 0.0f);
    }
    bool processing() const { return active.type != INS_OFF || next.type != INS_OFF; }
    unsigned sources() const {
        unsigned a = active.source ? 1u << (active.source - 1) : 0;
        unsigned b = next.source ? 1u << (next.source - 1) : 0;
        return a | b;
    }
    // Call before looking up an external detector source so the switch is sample accurate.
    void advance() {
        bool matches = sameAlgorithm();
        if (!matches && wet == 0.0f) {
            bool enteringChorus = active.type != INS_CHORUS && next.type == INS_CHORUS;
            active = next;
            resetState();
            if (enteringChorus) chorus.reset(active.chorusDepth, active.chorusWidth);
            matches = true;
        }
        wet = slew(wet, matches ? active.mix : 0.0f, fadeStep);
    }
    void process(float& l, float& r, float keyL, float keyR) {
        if (active.type == INS_OFF) return;
        float y[2] = {l, r};
        if (active.type == INS_CRUSH) {
            float move = active.drive - drive;
            drive = fabsf(move) < 1e-5f ? active.drive : drive + fadeStep * move;
            const bool sample = phase >= 1.0f;
            if (sample) phase -= 1.0f;
            phase += active.rate;
            for (int c = 0; c < 2; c++) {
                float x = y[c];
                // Unity at zero drive and at full-scale; push intermediate levels into a soft knee.
                if (drive > 0.0f) x *= (1.0f + drive) / (1.0f + drive * fabsf(x));
                if (active.pre) {
                    pre1[c] += active.pre * (x - pre1[c]);
                    pre2[c] += active.pre * (pre1[c] - pre2[c]);
                    x = pre2[c];
                } else pre1[c] = pre2[c] = x;
                if (sample) held[c] = quantize(x, active.levels);
                y[c] = held[c];
                if (active.tone) {
                    tone[c] += active.tone * (y[c] - tone[c]);
                    y[c] = tone[c];
                } else tone[c] = y[c];
            }
        } else if (active.type == INS_CHORUS) {
            chorus.run(y, active.chorusMode, active.chorusSpeed, active.chorusDepth,
                       active.chorusWidth, active.chorusSamplesPerMs, fadeStep);
        } else if (active.type == INS_CLIP) {
            for (float& sample : y)
                sample = active.clipCeil * clipTransfer(sample * active.clipDrive, active.clipShape);
        } else {
            if (!active.source) { keyL = l; keyR = r; }
            if (active.hp) {
                detector[0] += active.hp * (keyL - detector[0]);
                detector[1] += active.hp * (keyR - detector[1]);
                keyL -= detector[0]; keyR -= detector[1];
            }
            if (active.rmsCoeff) {
                const float power = 0.5f * (keyL * keyL + keyR * keyR);
                rmsPower = undenorm(rmsPower + active.rmsCoeff * (power - rmsPower));
            } else {
                const float p = fabsf(keyL) > fabsf(keyR) ? fabsf(keyL) : fabsf(keyR);
                if (p > peak) peak = p;
            }
            // A peak window catches short transients. RMS uses a 10 ms detector.
            if ((detectorClock++ & 15) == 0) {
                wantedGain = 1.0f;
                const float level = active.rmsCoeff ? sqrtf(rmsPower) : peak;
                if (level > active.quiet && active.slope > 0.0f)
                    wantedGain = dbGain(-compressorReduction(20.0f * log10f(level), active.threshold, active.knee, active.slope));
                peak = 0.0f;
            }
            float release = active.release;
            if (active.autoRelease > 0.0f) {
                // Let small reductions recover quickly while heavier reductions use a slower tail.
                const bool autoIsFast = active.autoRelease > release;
                const float fast = autoIsFast ? active.autoRelease : release;
                const float slow = autoIsFast ? release : active.autoRelease;
                const float blend = gain * gain * gain;
                release = slow + blend * (fast - slow);
            }
            gain += (wantedGain < gain ? active.attack : release) * (wantedGain - gain);
            y[0] *= gain; y[1] *= gain;
        }
        trim = slew(trim, active.trim, fadeStep);
        if (wet >= 1.0f) {
            l = y[0] * trim;
            r = y[1] * trim;
        } else if (wet > 0.0f) {
            l += wet * (y[0] * trim - l);
            r += wet * (y[1] * trim - r);
        }
    }
    float reductionDb() const {
        return active.type == INS_COMPRESS && wet > 0.0f ? -20.0f * log10f(gain) : 0.0f;
    }
};
