// FX track controls and two low-rate modulators.
#pragma once
#include "machine.h"
#include "mod.h"

struct FxControls {
    enum Field { D_DIV, D_DIV_R, D_FB, D_TONE, D_VOL, D_ALGO, D_TIMING, D_MS_L, D_MS_R, D_BASE, D_WIDTH,
                 D_WOW, D_DRIVE, R_SIZE, R_DECAY, R_DAMP, R_VOL, R_ALGO, R_PRE, R_HPF, R_LPF,
                 R_LOW, R_LOW_HZ, R_HIGH, R_HIGH_HZ, R_WIDTH, FIELDS };
    struct Destination { Field field; const char* name; float span; };
    static const Destination* destinations() {
        static const Destination entries[] = {
            {D_DIV, "None", 0}, {D_FB, "Delay Feedback", 127}, {D_TONE, "Delay Tone", 127},
            {D_VOL, "Delay Level", 127}, {D_BASE, "Delay Base", 127}, {D_WIDTH, "Delay Width", 127},
            {D_MS_L, "Delay Time L (free)", 1000}, {D_MS_R, "Delay Time R (free)", 1000},
            {D_WOW, "Tape Wow", 100}, {D_DRIVE, "Tape Drive", 18},
            {R_SIZE, "Reverb Size", 127}, {R_DECAY, "Reverb Decay", 127}, {R_DAMP, "Reverb Damping", 127},
            {R_VOL, "Reverb Level", 127}, {R_PRE, "Reverb Predelay", 120}, {R_HPF, "Reverb Input HPF", 1000},
            {R_LPF, "Reverb Input LPF", 8000}, {R_LOW, "Reverb Low Shelf", 24},
            {R_HIGH, "Reverb High Shelf", 24}, {R_WIDTH, "Reverb Width", 100},
            {D_DIV, "Delay Division L", 8}, {D_DIV_R, "Delay Division R", 8},
        };
        static_assert(sizeof entries / sizeof entries[0] == FX_LFO_DEST_MAX + 1, "FX LFO destination range");
        return entries;
    }
    enum LfoField { WAVE, SYNC, RATE, HZ, DEPTH, PHASE, DEST, LFO_FIELDS };
    struct Layout {
        int params[FIELDS], slots[FIELDS], lfo[2][LFO_FIELDS];
        Layout() {
            static const char* const ids[] = {
                "fx.a.div", "fx.a.divr", "fx.a.fb", "fx.a.tone", "fx.a.vol", "fx.a.mode", "fx.a.timing",
                "fx.a.msl", "fx.a.msr", "fx.a.base", "fx.a.width", "fx.a.wow", "fx.a.drive",
                "fx.b.size", "fx.b.dec", "fx.b.damp", "fx.b.vol", "fx.b.mode", "fx.b.pre", "fx.b.hpf",
                "fx.b.lpf", "fx.b.low", "fx.b.lowhz", "fx.b.high", "fx.b.highhz", "fx.b.width"};
            static_assert(sizeof ids / sizeof ids[0] == FIELDS, "FX slot list");
            for (int i = 0; i < FIELDS; i++) {
                params[i] = paramFind(ids[i]);
                slots[i] = paramSlot(params[i]);
            }
            const char* fields[] = {"wave", "sync", "rate", "hz", "depth", "phase", "dest"};
            for (int i = 0; i < 2; i++) for (int j = 0; j < LFO_FIELDS; j++) {
                char key[24];
                snprintf(key, sizeof key, "fx.lfo%d.%s", i + 1, fields[j]);
                lfo[i][j] = paramSlot(paramFind(key));
            }
        }
    };
    static const Layout& layout() { static const Layout layout; return layout; }

    int base[FIELDS], lfo[2][LFO_FIELDS];
    bool active = false;

    template <class Read>
    void read(Read get) {
        const auto& map = layout();
        active = false;
        // Normalize project and lock values before they reach delay-line indices or enum tables.
        for (int i = 0; i < FIELDS; i++) base[i] = clampParam(map.params[i], get(map.slots[i]));
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < LFO_FIELDS; j++) lfo[i][j] = clampParam(paramOfSlot(PS_GLOBAL, map.lfo[i][j]), get(map.lfo[i][j]));
            const int dest = lfo[i][DEST];
            if (dest > 0 && dest <= FX_LFO_DEST_MAX && lfo[i][DEPTH]) active = true;
        }
    }

    void values(int (&out)[FIELDS], int64_t sample, float sr, float tempo) const {
        memcpy(out, base, sizeof base);
        if (!active) return;
        const auto* targets = destinations();
        // Sum both offsets first so bipolar LFOs can cancel at the end of a range.
        float offset[2] = {};
        for (int i = 0; i < 2; i++) {
            const int* v = lfo[i];
            if (v[DEST] < 1 || v[DEST] > FX_LFO_DEST_MAX || !v[DEPTH]) continue;
            const auto& target = targets[v[DEST]];
            const double hz = v[SYNC] ? tempo / (60.0 * LFO_RATES[ModState::clampRate(v[RATE])].beats)
                                      : (float)v[HZ] * 0.01f;
            const double p = sample * hz / sr + v[PHASE] / 128.0;
            const int64_t cycle = (int64_t)p;
            uint32_t seed = (uint32_t)cycle * 2654435761u + 0x9e3779b9u + i * 3571u;
            const float wave = ModState::wave(v[WAVE], (float)(p - cycle), machNoise(seed));
            offset[i] = wave * target.span * (v[DEPTH] * 0.01f);
        }
        for (int i = 0; i < 2; i++) {
            const int dest = lfo[i][DEST];
            if (dest < 1 || dest > FX_LFO_DEST_MAX) continue;
            if (i && dest == lfo[0][DEST]) continue;
            const float sum = offset[i] + (!i && dest == lfo[1][DEST] ? offset[1] : 0.0f);
            const auto f = targets[dest].field;
            int delta = (int)(sum + (sum >= 0 ? 0.5f : -0.5f));
            out[f] = clampParam(layout().params[f], base[f] + delta);
        }
    }
};
