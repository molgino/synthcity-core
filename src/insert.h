// Inserts process the summed stereo track; the detector can use a separate dry track.
#pragma once
#include "param.h"
#include "dsp.h"

inline float crushRate(int value) {
    return 500.0f * exp2f((value - 1) * (6.5849625f / 126.0f));
}

struct StereoInsert {
    struct Settings {
        InsertType type = INS_OFF;
        int source = 0;
        float mix = 0.0f, trim = 1.0f;
        float rate = 1.0f, levels = 0.0f, drive = 0.0f;
        float pre = 0.0f, tone = 0.0f;
        float threshold = -18.0f, knee = 6.0f, slope = 0.75f, quiet = 0.0f;
        float attack = 0.0f, release = 0.0f, hp = 0.0f;
    };

    Settings active, next;
    int16_t values[INSERT_FIELDS] = {};
    float rateHz = 0.0f, fadeStep = 1.0f;
    float wet = 0.0f, trim = 1.0f, drive = 0.0f;
    float phase = 1.0f, held[2] = {}, pre1[2] = {}, pre2[2] = {}, tone[2] = {};
    float detector[2] = {}, peak = 0.0f, wantedGain = 1.0f, gain = 1.0f;
    unsigned detectorClock = 0;

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
        }
        next = s;
        if (active.type == next.type) active = next;
    }
    bool processing() const { return active.type != INS_OFF || next.type != INS_OFF; }
    unsigned sources() const {
        unsigned a = active.source ? 1u << (active.source - 1) : 0;
        unsigned b = next.source ? 1u << (next.source - 1) : 0;
        return a | b;
    }
    // Call before looking up an external detector source so the switch is sample accurate.
    void advance() {
        if (active.type != next.type && wet == 0.0f) {
            active = next;
            trim = active.trim;
            drive = active.drive;
            phase = 1.0f;
            for (int c = 0; c < 2; c++) held[c] = pre1[c] = pre2[c] = tone[c] = detector[c] = 0.0f;
            peak = 0.0f;
            gain = wantedGain = 1.0f;
            detectorClock = 0;
        }
        wet = slew(wet, active.type == next.type ? active.mix : 0.0f, fadeStep);
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
        } else {
            if (!active.source) { keyL = l; keyR = r; }
            if (active.hp) {
                detector[0] += active.hp * (keyL - detector[0]);
                detector[1] += active.hp * (keyR - detector[1]);
                keyL -= detector[0]; keyR -= detector[1];
            }
            float p = fabsf(keyL) > fabsf(keyR) ? fabsf(keyL) : fabsf(keyR);
            if (p > peak) peak = p;
            // Peak hold keeps short transients in the detector with one logarithm per 16 samples.
            if ((detectorClock++ & 15) == 0) {
                wantedGain = 1.0f;
                if (peak > active.quiet && active.slope > 0.0f) {
                    float over = 20.0f * log10f(peak) - active.threshold;
                    if (active.knee && over < active.knee * 0.5f) {
                        float k = over + active.knee * 0.5f;
                        over = k * k / (2.0f * active.knee);
                    }
                    wantedGain = dbGain(-over * active.slope);
                }
                peak = 0.0f;
            }
            gain += (wantedGain < gain ? active.attack : active.release) * (wantedGain - gain);
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
