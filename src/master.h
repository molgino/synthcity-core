// Stereo mastering and export metering. The DSP carries no audio-buffer lookahead.
#pragma once
#include "insert.h"
#include <stdio.h>

struct MasterBiquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z[2][2] = {};
    void set(float b0_, float b1_, float b2_, float a0, float a1_, float a2_) {
        b0 = b0_ / a0; b1 = b1_ / a0; b2 = b2_ / a0; a1 = a1_ / a0; a2 = a2_ / a0;
    }
    float tick(float x, int ch) {
        float y = b0 * x + z[ch][0];
        z[ch][0] = b1 * x - a1 * y + z[ch][1];
        z[ch][1] = b2 * x - a2 * y;
        return y;
    }
    void eq(float hz, float db, float sr, int kind) {
        float angle = 6.283185307f * (hz < sr * 0.45f ? hz : sr * 0.45f) / sr;
        float C = cosf(angle), S = sinf(angle);
        float A = StereoInsert::dbGain(db * 0.5f), a = S * 0.70710678f;
        if (kind == 0) {
            float ap = A + 1, am = A - 1, t = 2 * sqrtf(A) * a;
            set(A * (ap - am*C + t), 2*A*(am - ap*C), A*(ap - am*C - t),
                ap + am*C + t, -2*(am + ap*C), ap + am*C - t);
        } else if (kind == 1) {
            set(1 + a*A, -2*C, 1 - a*A, 1 + a/A, -2*C, 1 - a/A);
        } else {
            float ap = A + 1, am = A - 1, t = 2 * sqrtf(A) * a;
            set(A * (ap + am*C + t), -2*A*(am + ap*C), A*(ap + am*C - t),
                ap - am*C + t, 2*(am - ap*C), ap - am*C - t);
        }
    }
};

struct InterpolatedPeak {
    float h[2][4] = {};
    float peak = 0;
    void add(float l, float r) {
        float x[] = {l, r};
        for (int ch = 0; ch < 2; ch++) {
            float* p = h[ch];
            p[0] = p[1]; p[1] = p[2]; p[2] = p[3]; p[3] = x[ch];
            float top = fabsf(x[ch]);
            if (top > peak) peak = top;
            for (int i = 1; i < 4; i++) {
                float t = i * 0.25f;
                float v = p[1] + 0.5f * t * (p[2] - p[0] + t *
                    (2*p[0] - 5*p[1] + 4*p[2] - p[3] + t * (3*(p[1] - p[2]) + p[3] - p[0])));
                if (fabsf(v) > peak) peak = fabsf(v);
            }
        }
    }
    float db() const {
        InterpolatedPeak end = *this;
        end.add(0, 0); end.add(0, 0);
        return end.peak > 0 ? 20.0f * log10f(end.peak) : -120.0f;
    }
};

struct MasterMeter {
    enum { BINS = 1201 };
    struct Bin { double sum; unsigned count; };
    Bin histogram[BINS] = {};
    MasterBiquad shelf, hp;
    InterpolatedPeak peak;
    double blocks[4] = {}, energy = 0;
    int interval = 4800, samples = 0, block = 0, valid = 0;

    void reset(float sr) {
        memset(histogram, 0, sizeof histogram);
        memset(blocks, 0, sizeof blocks);
        shelf = MasterBiquad{}; hp = MasterBiquad{};
        peak = InterpolatedPeak{};
        interval = (int)(sr * 0.1f + 0.5f);
        if (interval < 1) interval = 1;
        energy = 0; samples = block = valid = 0;
        // K weighting with sample-rate-dependent coefficients.
        float K = tanf(3.141592654f * 1681.97445f / sr);
        float Q = 0.70717524f, high = 1.5848647f, mid = 1.2587209f;
        shelf.set(high + mid*K/Q + K*K, 2*(K*K - high), high - mid*K/Q + K*K,
                  1 + K/Q + K*K, 2*(K*K - 1), 1 - K/Q + K*K);
        K = tanf(3.141592654f * 38.13547f / sr); Q = 0.50032704f;
        hp.set(1 + K/Q + K*K, -2*(1 + K/Q + K*K), 1 + K/Q + K*K,
               1 + K/Q + K*K, 2*(K*K - 1), 1 - K/Q + K*K);
    }
    static float loudness(double e) { return e > 0 ? -0.691f + 10 * log10f((float)e) : -120.0f; }
    void add(float l, float r) {
        peak.add(l, r);
        l = hp.tick(shelf.tick(l, 0), 0);
        r = hp.tick(shelf.tick(r, 1), 1);
        energy += (double)l*l + (double)r*r;
        if (++samples != interval) return;
        blocks[block] = energy;
        block = (block + 1) & 3;
        samples = 0; energy = 0;
        if (valid < 4) valid++;
        if (valid < 4) return;
        double mean = (blocks[0] + blocks[1] + blocks[2] + blocks[3]) / (4.0 * interval);
        float db = loudness(mean);
        if (db < -70.0f) return;
        int bin = (int)((db + 100) * 10 + 0.5f);
        if (bin >= BINS) bin = BINS - 1;
        histogram[bin].sum += mean;
        histogram[bin].count++;
    }
    float integrated() const {
        double total = 0; unsigned count = 0;
        for (const Bin& b : histogram) { total += b.sum; count += b.count; }
        if (!count) return -120.0f;
        float gate = loudness(total / count) - 10.0f;
        total = 0; count = 0;
        for (int i = 0; i < BINS; i++)
            if (i * 0.1f - 100.0f >= gate) { total += histogram[i].sum; count += histogram[i].count; }
        return count ? loudness(total / count) : -120.0f;
    }
};

struct MasterChain {
    enum Field { ON, TRIM, LOW, MID, FREQ, HIGH, THRESHOLD, RATIO, ATTACK, RELEASE,
                 MAKEUP, CLIP, LIMIT, CEILING, AB, FIELDS };
    static constexpr int LOW_HZ = 100, HIGH_HZ = 9000;
    int16_t values[FIELDS] = {};
    float sr = 0, inputTrim = 1, clip = 1, ceiling = 1, gain = 1, release = 1, mix = 0;
    float audPower = 0, dspPower = 0, smooth = 0;
    bool enabled = false;
    StereoInsert compressor;
    MasterBiquad eq[3];

    static void configureEq(MasterBiquad (&bands)[3], float rate, int low, int mid, int freq, int high) {
        bands[0].eq(LOW_HZ, low, rate, 0);
        bands[1].eq(freq, mid, rate, 1);
        bands[2].eq(HIGH_HZ, high, rate, 2);
    }

    static const int* slots() {
        static int index[FIELDS] = {};
        static bool init = false;
        if (!init) {
            const char* fields[FIELDS] = {"on", "trim", "low", "mid", "freq", "high", "threshold", "ratio",
                "attack", "release", "makeup", "clip", "limit", "ceiling", "ab"};
            for (int i = 0; i < FIELDS; i++) {
                char id[32]; snprintf(id, sizeof id, "master.%s", fields[i]);
                index[i] = paramSlot(paramFind(id));
            }
            init = true;
        }
        return index;
    }
    void reset() {
        sr = 0; gain = 1; mix = 0; enabled = false; audPower = dspPower = 0;
        compressor.reset();
        for (auto& band : eq) band = MasterBiquad{};
    }
    void configure(const int16_t* global, float rate) {
        const int* index = slots();
        if (!global[index[ON]]) {
            if (enabled) reset();
            return;
        }
        bool dirty = !enabled || rate != sr;
        for (int i = 0; i < FIELDS; i++) {
            if (values[i] != global[index[i]]) dirty = true;
            values[i] = global[index[i]];
        }
        if (!dirty) return;
        enabled = true;
        sr = rate;
        inputTrim = StereoInsert::dbGain(values[TRIM]);
        clip = StereoInsert::dbGain(values[CLIP]);
        // A Catmull–Rom interpolator has a maximum L1 kernel norm of 1.25.
        // Bounding each sample by 0.8*ceiling keeps its interpolated peak below ceiling.
        ceiling = 0.8f * StereoInsert::dbGain(values[CEILING] * 0.1f);
        release = 1 - expf(-1 / (0.08f * rate));
        smooth = 1 - expf(-1 / (0.3f * rate));
        configureEq(eq, rate, values[LOW], values[MID], values[FREQ], values[HIGH]);
        int16_t p[INSERT_FIELDS] = {};
        p[INS_TYPE] = INS_COMPRESS; p[INS_MIX] = 100; p[INS_TRIM] = values[MAKEUP];
        p[INS_THRESHOLD] = values[THRESHOLD]; p[INS_RATIO] = values[RATIO];
        p[INS_ATTACK] = values[ATTACK]; p[INS_RELEASE] = values[RELEASE];
        p[INS_KNEE] = 6; p[INS_SC_HPF] = 80;
        compressor.configure(p, rate);
    }
    void run(float& l, float& r, bool offline) {
        float dryL = l, dryR = r;
        l *= inputTrim; r *= inputTrim;
        for (int i = 0; i < 3; i++) {
            // Keep even a zero-gain band's state current across edits.
            l = eq[i].tick(l, 0); r = eq[i].tick(r, 1);
        }
        compressor.advance();
        compressor.process(l, r, 0, 0);
        if (values[CLIP]) { l = softClip(l * clip); r = softClip(r * clip); }
        if (values[LIMIT]) {
            float peak = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
            float limit = peak > ceiling ? ceiling / peak : 1.0f;
            gain += release * (1.0f - gain);
            if (gain > limit) gain = limit;
            l *= gain; r *= gain;
        } else gain = 1;
        if (offline) return;
        audPower += smooth * (dryL*dryL + dryR*dryR - audPower);
        dspPower += smooth * (l*l + r*r - dspPower);
        mix = StereoInsert::slew(mix, values[AB] ? 1.0f : 0.0f, 1.0f / (0.02f * sr));
        if (mix > 0) {
            float match = audPower > 1e-12f ? sqrtf(dspPower / audPower) : 1.0f;
            if (match > 4) match = 4;
            l += mix * (softClip(dryL * match) - l);
            r += mix * (softClip(dryR * match) - r);
        }
    }
};
