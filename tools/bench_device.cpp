// Lastmessung im Szenario vom Geraet: 2x SMPL (eine mit TIME-Warp), 1x CHOP, 1x POLY mit Akkord,
// LFO und Locks. Gibt Mittel und schlimmsten 256er-Block aus (Anteil am Zeitbudget).
#include "../src/engine.h"
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
static int SL(const char* i) { return paramSlot(paramFind(i)); }
static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static float smp[48000 * 4 * 2];
int main(int argc, char** argv) {
    int which = argc > 1 ? atoi(argv[1]) : -1;   // -1 alle, sonst nur diese Spur
    int seconds = argc > 2 ? atoi(argv[2]) : 8;
    static Engine e;
    e.prepare(48000.0f); projectInit(e.P); e.P.pat[0].used = 1;
    for (int i = 0; i < 48000 * 4; i++) {           // Stereo-Loop: Ton + Rauschen + Schlaege
        float t = i / 48000.0f, env = expf(-fmodf(t, 0.5f) * 8.0f);
        float n = ((i * 1103515245u + 12345u) >> 9 & 0xffff) / 32768.0f - 1.0f;
        smp[i * 2] = 0.4f * sinf(t * 2 * 3.14159f * 110) * env + 0.1f * n * env;
        smp[i * 2 + 1] = 0.4f * sinf(t * 2 * 3.14159f * 165) * env + 0.1f * n * env;
    }
    e.pool.set(0, smp, 48000 * 4, 48000.0f, "loop", true, 2);
    e.pool.set(1, smp, 48000 * 2, 48000.0f, "hit", false, 2);
    auto setup = [&](int t, int mach) { Track& tr = e.P.pat[0].t[t]; machineSelect(tr.p, mach); tr.len = 16; tr.speed = 4; return &tr; };
    Track* a = setup(0, 3);     // SMPL Loop, WARP TIME
    a->p[SL("trk.smp")] = 0; a->p[SL("trk.src.f")] = 2 * 128 / 3 + 20; a->p[SL("trk.src.c")] = 80;
    for (int s = 0; s < 16; s += 4) a->s[s].on = 1;
    Track* b = setup(1, 3);     // SMPL Hits
    b->p[SL("trk.smp")] = 1;
    for (int s = 0; s < 16; s++) if (s % 3 == 0) { b->s[s].on = 1; b->s[s].setLock(SL("trk.src.a"), (int16_t)(s * 7), 0); b->s[s].setLock(SL("trk.note"), (int16_t)(55 + s), 0); }
    Track* c = setup(2, 4);     // CHOP, FOLLOW STEP
    c->p[SL("trk.smp")] = 0; c->p[SL("trk.src.c")] = 64;
    for (int s = 0; s < 16; s++) { c->s[s].on = 1; if (s & 1) c->s[s].setLock(SL("trk.flt.freq"), (int16_t)(40 + s * 5), 0); }
    Track* d = setup(3, 0);     // POLY, Akkord, LFO auf Filter, Locks
    d->p[SL("trk.nt2")] = 29; d->p[SL("trk.nt3")] = 32; d->p[SL("trk.amp.rel")] = 90; d->p[SL("trk.flt.reso")] = 70;
    d->p[SL("trk.lfo1.depth")] = 100; d->p[SL("trk.lfo1.dest")] = 1;
    for (int s = 0; s < 16; s += 2) { d->s[s].on = 1; d->s[s].setLock(SL("trk.note"), (int16_t)(48 + (s % 5) * 2), 0); d->s[s].setLock(SL("trk.flt.freq"), (int16_t)(60 + s * 3), 0); }
    for (int t = 0; t < 4; t++) if (which >= 0 && t != which) e.mute[t] = true;
    e.start();
    static float out[256 * 2];
    int blocks = seconds * 48000 / 256;
    double sum = 0, worst = 0;
    uint64_t h = 1469598103934665603ull;   // Pruefsumme ueber jedes Sample (bitgenau)
    for (int i = 0; i < blocks; i++) {
        double t0 = now(); e.render(out, 256); double dt = now() - t0;
        sum += dt; if (dt > worst) worst = dt;
        if (getenv("SC_DUMP")) { static FILE* df = fopen(getenv("SC_DUMP"), "wb"); fwrite(out, 4, 512, df); }
        for (int k = 0; k < 512; k++) { uint32_t u; memcpy(&u, &out[k], 4); h = (h ^ u) * 1099511628211ull; }
    }
    printf("hash %016llx  ", (unsigned long long)h);
    double budget = 256.0 / 48000.0;
    printf("Spur %2d: Mittel %5.2f %%, schlimmster Block %5.2f %% (x86)\n", which, sum / blocks / budget * 100, worst / budget * 100);
    return 0;
}
