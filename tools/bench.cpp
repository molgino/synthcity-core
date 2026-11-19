// Lastmessung: wie viel Rechenzeit kostet eine Sekunde Klang?
// Ausgegeben wird der Echtzeit-Faktor (1.0 = gerade noch), einmal fuer ein normales Pattern
// und einmal fuer den schlimmsten Fall mit allen Stimmen.
#include "../src/engine.h"
#include <stdio.h>
#include <time.h>

static int SL(const char* i) { return paramSlot(paramFind(i)); }
static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static double run(Engine& e, int seconds, int block) {
    static float b[8192 * 2];
    int total = (int)(e.sr * seconds), done = 0;
    double t0 = now();
    while (done < total) { int n = block; if (done + n > total) n = total - done; e.render(b, n); done += n; }
    double dt = now() - t0;
    return (double)seconds / dt;          // Faktor gegenueber Echtzeit
}

int main(int argc, char** argv) {
    int block = argc > 1 ? atoi(argv[1]) : 64;
    static Engine e;
    printf("Blockgroesse %d Samples bei 48 kHz (%.1f ms Zeitbudget je Block)\n",
           block, block * 1000.0 / 48000.0);

    e.prepare(48000.0f); projectInit(e.P); e.P.pat[0].used = 1;
    { Track& tr = e.P.pat[0].t[0];
      tr.len = 16; tr.speed = 4; tr.p[SL("trk.note")] = 45;
      for (int s = 0; s < 16; s += 2) tr.s[s].on = 1; }
    e.start();
    printf("  eine Spur, Achtel        : %6.1f x Echtzeit\n", run(e, 4, block));

    e.prepare(48000.0f); projectInit(e.P); e.P.pat[0].used = 1;
    for (int t = 0; t < NTRACKS; t++) {
        Track& tr = e.P.pat[0].t[t];
        tr.len = 16; tr.speed = 8;
        tr.p[SL("trk.note")] = (int16_t)(36 + t * 4);
        tr.p[SL("trk.amp.rel")] = 110; tr.p[SL("trk.amp.sus")] = 95;
        tr.p[SL("trk.flt.reso")] = 100;
        for (int s = 0; s < 16; s++) tr.s[s].on = 1;
    }
    e.start();
    printf("  8 Spuren, 32stel, alle %2d Stimmen: %6.1f x Echtzeit\n", NVOICES, run(e, 4, block));
    return 0;
}
