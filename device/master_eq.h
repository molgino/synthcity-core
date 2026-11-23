#pragma once
#include "../src/master.h"
#include <math.h>
#include <string.h>

static double masterEqResponseDb(const MasterEq& eq, double hz, double rate) {
    const double angle = 6.283185307179586 * hz / rate;
    const double c = cos(angle), s = sin(angle);
    const double c2 = c * c - s * s, s2 = 2 * c * s;
    double power = 1.0;
    for (int i = 0; i < eq.count; i++) {
        const auto& band = eq.bands[eq.enabled[i]];
        for (int j = 0; j < band.count; j++) {
            const MasterBiquad& filter = band.filter[j];
            const double nr = filter.b0 + filter.b1 * c + filter.b2 * c2;
            const double ni = filter.b1 * s + filter.b2 * s2;
            const double dr = 1.0 + filter.a1 * c + filter.a2 * c2;
            const double di = filter.a1 * s + filter.a2 * s2;
            power *= (nr * nr + ni * ni) / (dr * dr + di * di);
        }
    }
    return 10.0 * log10(fmax(power, 1e-16));
}

struct MasterSpectrum {
    static constexpr int N = 4096;
    static constexpr float FLOOR_DB = -72.0f;
    bool active = false;
    float power[N / 2 + 1] = {};

    // capture() runs in the audio callback. The UI holds the SDL audio lock
    // for reset() and snapshot() and analyzes its own copy after unlocking.
    void reset(bool on) {
        active = on;
        write = count = 0;
        fresh = false;
        if (on) memset(power, 0, sizeof power);
    }

    void capture(const float* stereo, int frames) {
        if (!active || frames <= 0) return;
        if (frames >= N) {
            stereo += 2 * (frames - N);
            frames = N;
        }
        const int tail = frames < N - write ? frames : N - write;
        memcpy(ring + 2 * write, stereo, 2 * tail * sizeof(float));
        memcpy(ring, stereo + 2 * tail, 2 * (frames - tail) * sizeof(float));
        write = (write + frames) % N;
        count += frames;
        if (count > N) count = N;
        fresh = true;
    }

    bool snapshot() {
        if (count < N || !fresh) return false;
        memcpy(samples, ring + 2 * write, 2 * (N - write) * sizeof(float));
        memcpy(samples + 2 * (N - write), ring, 2 * write * sizeof(float));
        fresh = false;
        return true;
    }

    void analyze() {
        if (!prepared) {
            constexpr float tau = 6.283185307179586f;
            for (int i = 0; i < N / 2; i++) {
                twiddle[i] = {cosf(tau * i / N), -sinf(tau * i / N)};
                window[i] = 0.5f * (1.0f - twiddle[i].re);
                window[i + N / 2] = 1.0f - window[i];
            }
            for (int i = 1; i < N; i++) reverse[i] = (reverse[i / 2] >> 1) | ((i & 1) * (N / 2));
            prepared = true;
        }
        for (int i = 0; i < N; i++)
            fft[reverse[i]] = {samples[2 * i] * window[i], samples[2 * i + 1] * window[i]};
        for (int size = 2; size <= N; size *= 2) {
            const int half = size / 2;
            for (int start = 0; start < N; start += size) {
                for (int i = 0; i < half; i++) {
                    const Complex a = fft[start + i], b = fft[start + i + half];
                    const Complex w = twiddle[i * (N / size)];
                    const Complex z = {w.re * b.re - w.im * b.im, w.re * b.im + w.im * b.re};
                    fft[start + i] = {a.re + z.re, a.im + z.im};
                    fft[start + i + half] = {a.re - z.re, a.im - z.im};
                }
            }
        }
        // Mirrored bins of the packed FFT give the mean power of left and right,
        // even when the channels are out of phase. Normalize for the Hann window.
        constexpr float scale = 4.0f / (N * N);
        for (int i = 1; i < N / 2; i++) {
            const float measured = (fft[i].norm() + fft[N - i].norm()) * scale;
            power[i] = fmaxf(measured, power[i] * 0.70f);
        }
        power[0] = fft[0].norm() * (0.5f * scale);
        power[N / 2] = fft[N / 2].norm() * (0.5f * scale);
    }

    float db(float firstBin, float nextBin) const {
        const int first = (int)ceilf(firstBin), last = (int)nextBin;
        float peak = 0;
        if (first <= last) {
            for (int i = first; i <= last && i <= N / 2; i++) peak = fmaxf(peak, power[i]);
        } else {
            const int i = (int)firstBin;
            const float t = firstBin - i;
            if (i < N / 2) peak = power[i] + t * (power[i + 1] - power[i]);
        }
        return fmaxf(FLOOR_DB, 10 * log10f(fmaxf(peak, 1e-12f)));
    }

private:
    struct Complex {
        float re, im;
        float norm() const { return re * re + im * im; }
    };
    bool fresh = false, prepared = false;
    int write = 0, count = 0;
    float ring[2 * N], samples[2 * N], window[N];
    Complex fft[N], twiddle[N / 2];
    int reverse[N] = {};
};
