// Erzeugt demo.wav: ein kurzes Stueck, das zeigt, was der Kern schon kann -
// P-Locks, Trig-Bedingungen, Micro-Timing, Ratchets und eine polymetrische Spur.
#include "../src/engine.h"
#include "../src/demo.h"
#include "../src/wav.h"
#include <stdio.h>

static int SL(const char* i) { return paramSlot(paramFind(i)); }

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "demo.wav";
    static Engine e;
    e.prepare(48000.0f);
    demoProject(e);

    const int N = 48000 * 16;
    static float buf[48000 * 16 * 2];
    e.start();
    int done = 0;
    while (done < N) {
        int n = 128; if (done + n > N) n = N - done;
        float t = (float)done / N;
        e.morph = t < 0.25f ? 0.0f : (t < 0.75f ? (t - 0.25f) * 2.0f : 1.0f);   // Crossfader
        e.mute[4] = t < 0.25f;                                   // Melodie kommt spaeter dazu
        if (done == 0) e.queuePattern(0);
        if (t >= 0.5f && e.pat == 0 && e.patNext < 0) e.queuePattern(1);        // zweiter Teil
        e.seq.loopOn = (t > 0.88f && t < 0.95f);                 // kurzer Step-Loop als Stotterer
        e.seq.loopFrom = 0; e.seq.loopLen = 2;
        e.render(buf + done * 2, n);
        done += n;
    }
    if (!wavWrite(out, buf, N, 48000)) { printf("konnte %s nicht schreiben\n", out); return 1; }
    printf("%s geschrieben (%d Sekunden)\n", out, N / 48000);
    return 0;
}
