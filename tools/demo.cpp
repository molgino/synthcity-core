// Erzeugt demo.wav: ein kurzes Stueck, das zeigt, was der Kern schon kann -
// P-Locks, Trig-Bedingungen, Micro-Timing, Ratchets und eine polymetrische Spur.
#include "../src/engine.h"
#include "../src/wav.h"
#include <stdio.h>

static int SL(const char* i) { return paramSlot(paramFind(i)); }

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "demo.wav";
    static Engine e;
    e.prepare(48000.0f);
    projectInit(e.P);
    e.P.g[SL("bpm")] = 124;
    e.P.pat[0].used = 1;

    // Spur 0: Bass auf den Vierteln, der achte Step haengt eine Oktave hoeher (P-Lock).
    Track& b = e.P.pat[0].t[0];
    b.len = 16; b.speed = 4;
    b.p[SL("trk.note")] = 33; b.p[SL("trk.flt.freq")] = 52; b.p[SL("trk.flt.env")] = 100;
    b.p[SL("trk.flt.reso")] = 60; b.p[SL("trk.amp.dec")] = 52; b.p[SL("trk.vol")] = 108;
    for (int s = 0; s < 16; s += 4) b.s[s].on = 1;
    b.s[8].setLock(SL("trk.note"), 45);
    b.s[12].on = 1;
    b.s[12].setLock(SL("trk.note"), 40, 1);                       // gleitet hinauf (Slide)
    b.s[14].on = 1; b.s[14].setLock(SL("trk.micro"), 30);        // leicht nach hinten geschoben
    b.s[14].setLock(SL("trk.ratchet"), 3);                        // dreifacher Anschlag

    // Spur 1: Akkordflaeche, nur in der zweiten Haelfte (Bedingung 2:2).
    Track& p = e.P.pat[0].t[1];
    p.len = 16; p.speed = 2;
    p.p[SL("trk.note")] = 64; p.p[SL("trk.src.a")] = 70; p.p[SL("trk.src.b")] = 20;
    p.p[SL("trk.amp.atk")] = 70; p.p[SL("trk.amp.sus")] = 92; p.p[SL("trk.amp.rel")] = 80;
    p.p[SL("trk.len")] = 90; p.p[SL("trk.flt.freq")] = 72; p.p[SL("trk.vol")] = 88;
    p.p[SL("trk.pan")] = 48;
    for (int s = 0; s < 16; s += 8) {
        p.s[s].on = 1; p.s[s].setLock(SL("trk.cond"), (int16_t)condAB(2, 2, false));
    }
    p.s[8].setLock(SL("trk.note"), 67);

    // Spur 2: polymetrische Melodie ueber 12 Steps - sie wandert gegen die Vier.
    Track& m = e.P.pat[0].t[2];
    m.len = 12; m.speed = 4;
    m.p[SL("trk.note")] = 76; m.p[SL("trk.src.b")] = 110; m.p[SL("trk.amp.dec")] = 28;
    m.p[SL("trk.flt.freq")] = 90; m.p[SL("trk.vol")] = 84; m.p[SL("trk.pan")] = 82;
    const int notes[12] = {76, 0, 79, 0, 0, 83, 0, 76, 0, 0, 72, 0};
    for (int s = 0; s < 12; s++)
        if (notes[s]) { m.s[s].on = 1; m.s[s].setLock(SL("trk.note"), (int16_t)notes[s]); }
    m.s[5].setLock(SL("trk.prob"), 60);                           // taucht nur manchmal auf

    // Scenes: A dumpf und eng, B offen und breit - der Morph faehrt im Stueck hinueber.
    e.P.pat[0].sc[0][0].set(SL("trk.flt.freq"), 44);
    e.P.pat[0].sc[1][0].set(SL("trk.flt.freq"), 86);
    e.P.pat[0].sc[0][2].set(SL("trk.vol"), 60);
    e.P.pat[0].sc[1][2].set(SL("trk.vol"), 96);

    const int N = 48000 * 16;
    static float buf[48000 * 16 * 2];
    e.start();
    int done = 0;
    while (done < N) {
        int n = 128; if (done + n > N) n = N - done;
        float t = (float)done / N;
        e.morph = t < 0.25f ? 0.0f : (t < 0.75f ? (t - 0.25f) * 2.0f : 1.0f);   // Crossfader
        e.mute[2] = t < 0.25f;                                   // Melodie kommt spaeter dazu
        e.seq.loopOn = (t > 0.88f && t < 0.95f);                 // kurzer Step-Loop als Stotterer
        e.seq.loopFrom = 0; e.seq.loopLen = 2;
        e.render(buf + done * 2, n);
        done += n;
    }
    if (!wavWrite(out, buf, N, 48000)) { printf("konnte %s nicht schreiben\n", out); return 1; }
    printf("%s geschrieben (%d Sekunden)\n", out, N / 48000);
    return 0;
}
