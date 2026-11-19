// Klang-Tests. Gemessen wird am ERGEBNIS (Pegel, Frequenz, Gleichheit), nicht an internen
// Zwischenwerten - so bleiben die Tests gueltig, wenn die Umsetzung sich aendert.
#include "../src/engine.h"
#include "../src/wav.h"
#include <stdio.h>
#include <math.h>

static int fails = 0, checks = 0;
static void chk(bool ok, const char* what) {
    checks++;
    if (!ok) { fails++; printf("  FEHLER: %s\n", what); }
}
static void grp(const char* n) { printf("- %s\n", n); }

// --- Messhilfen -------------------------------------------------------------------------------
static float rms(const float* x, int n, int stride = 2) {
    double s = 0; for (int i = 0; i < n; i++) s += (double)x[i * stride] * x[i * stride];
    return (float)sqrt(s / (n ? n : 1));
}
// Goertzel: wie stark steckt genau diese Frequenz im Signal?
static float bin(const float* x, int n, float hz, float sr, int stride = 2) {
    double w = 2.0 * M_PI * hz / sr, cw = cos(w), c = 2.0 * cw, s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { double s0 = x[i * stride] + c * s1 - s2; s2 = s1; s1 = s0; }
    double re = s1 - s2 * cw, im = s2 * sin(w);
    return (float)(sqrt(re * re + im * im) / n);
}
static bool sane(const float* x, int n) {
    for (int i = 0; i < n; i++) if (!(x[i] >= -1.0f && x[i] <= 1.0f)) return false;
    return true;
}

static int SL(const char* id) { return paramSlot(paramFind(id)); }

// Ein Pattern mit vier Vierteln auf Spur 0.
static void setupBeat(Engine& e, int note = 69) {
    projectInit(e.P);
    e.P.pat[0].used = 1;
    Track& tr = e.P.pat[0].t[0];
    tr.len = 16; tr.speed = 4;
    tr.p[SL("trk.note")] = (int16_t)note;
    tr.p[SL("trk.amp.rel")] = 30;
    for (int s = 0; s < 16; s += 4) tr.s[s].on = 1;
}

static float* buf(int frames) { static float b[48000 * 8 * 2]; (void)frames; return b; }

int main() {
    printf("Klang-Tests\n");
    static Engine e;

    grp("Ohne Start bleibt es still");
    {
        e.prepare(48000.0f); setupBeat(e);
        float* b = buf(512);
        e.render(b, 512);
        chk(rms(b, 512) == 0.0f, "Stille vor dem Start");
    }

    grp("Gleiches Ergebnis bei jeder Blockgroesse");
    {
        const int N = 48000;                       // eine Sekunde
        static float a[N * 2], c[N * 2];
        e.prepare(48000.0f); setupBeat(e); e.start();
        for (int i = 0; i < N * 2; i++) a[i] = 0;
        int done = 0; while (done < N) { int n = 64; if (done + n > N) n = N - done; e.render(a + done * 2, n); done += n; }
        e.prepare(48000.0f); setupBeat(e); e.start();
        done = 0; while (done < N) { int n = 977; if (done + n > N) n = N - done; e.render(c + done * 2, n); done += n; }
        int bad = 0;
        for (int i = 0; i < N * 2; i++) if (a[i] != c[i]) bad++;
        chk(bad == 0, "64er und 977er Bloecke liefern dasselbe Signal");
        chk(rms(a, N) > 0.01f, "es kommt ueberhaupt Klang heraus");
        chk(sane(a, N * 2), "keine ueberhoehten Werte, kein NaN");
    }

    grp("Tonhoehe stimmt");
    {
        const int N = 24000;
        float* b = buf(N);
        e.prepare(48000.0f); setupBeat(e, 69);     // A4 = 440 Hz
        e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 127;
        e.start(); e.render(b, N);
        float at440 = bin(b, N, 440.0f, 48000.0f);
        float at500 = bin(b, N, 523.0f, 48000.0f);
        chk(at440 > at500 * 4.0f, "440 Hz ist deutlich staerker als ein Nachbarton");

        e.prepare(48000.0f); setupBeat(e, 57);     // eine Oktave tiefer
        e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 127;
        e.start(); e.render(b, N);
        chk(bin(b, N, 220.0f, 48000.0f) > bin(b, N, 440.0f, 48000.0f), "Note 57 klingt als 220 Hz");
    }

    grp("Filter daempft die Hoehen");
    {
        const int N = 24000;
        static float open_[48000 * 2], shut[48000 * 2];
        e.prepare(48000.0f); setupBeat(e, 45);
        e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 127;
        e.P.pat[0].t[0].p[SL("trk.flt.env")] = 64;
        e.start(); e.render(open_, N);
        e.prepare(48000.0f); setupBeat(e, 45);
        e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 40;    // tief
        e.P.pat[0].t[0].p[SL("trk.flt.env")] = 64;
        e.start(); e.render(shut, N);
        float hiOpen = bin(open_, N, 4000.0f, 48000.0f), hiShut = bin(shut, N, 4000.0f, 48000.0f);
        chk(hiShut < hiOpen * 0.5f, "geschlossenes Filter nimmt die 4 kHz weg");
        chk(rms(shut, N) > 0.0f, "der Grundton bleibt hoerbar");
    }

    grp("Huellkurve: langer Anstieg beginnt leise");
    {
        const int N = 24000;
        float* b = buf(N);
        e.prepare(48000.0f); setupBeat(e, 57);
        e.P.pat[0].t[0].p[SL("trk.len")] = 127;         // lange Note, sonst greift der Anstieg nicht
        e.P.pat[0].t[0].p[SL("trk.amp.atk")] = 100;     // langsam
        e.P.pat[0].t[0].p[SL("trk.amp.dec")] = 127;
        e.P.pat[0].t[0].p[SL("trk.amp.sus")] = 100;
        e.start(); e.render(b, N);
        float early = rms(b, 1000), later = rms(b + 2 * 8000, 4000);
        chk(early < later * 0.5f, "Anfang leiser als die Mitte");
    }

    grp("P-Lock auf einem Step wirkt");
    {
        const int N = 12000;
        static float plain[24000 * 2], locked[24000 * 2];
        e.prepare(48000.0f); setupBeat(e, 45);
        e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 127;
        e.start(); e.render(plain, N);
        e.prepare(48000.0f); setupBeat(e, 45);
        e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 127;
        e.P.pat[0].t[0].s[0].setLock(SL("trk.note"), 69);   // erster Step eine Oktave hoeher
        e.start(); e.render(locked, N);
        // 110 Hz hat bei 440 Hz eine kraeftige Oberwelle - entscheidend ist der GRUNDTON:
        // er verschwindet, wenn der Step auf eine andere Note gelockt ist.
        chk(bin(locked, N, 110.0f, 48000.0f) < bin(plain, N, 110.0f, 48000.0f) * 0.2f,
            "der Grundton der Spur verschwindet auf dem gelockten Step");
        chk(bin(locked, N, 440.0f, 48000.0f) > bin(plain, N, 440.0f, 48000.0f) * 2.0f,
            "dafuer klingt die gelockte Note");
    }

    grp("Panorama");
    {
        const int N = 12000;
        float* b = buf(N);
        e.prepare(48000.0f); setupBeat(e, 57);
        e.P.pat[0].t[0].p[SL("trk.pan")] = 0;       // ganz links
        e.start(); e.render(b, N);
        float l = rms(b, N), r = rms(b + 1, N);
        chk(l > 0.01f && r < l * 0.05f, "ganz links heisst rechts fast still");
    }

    grp("Mehr Anschlaege als Stimmen");
    {
        const int N = 48000;
        float* b = buf(N);
        e.prepare(48000.0f);
        projectInit(e.P); e.P.pat[0].used = 1;
        for (int t = 0; t < NTRACKS; t++) {
            Track& tr = e.P.pat[0].t[t];
            tr.len = 16; tr.speed = 8;
            tr.p[SL("trk.note")] = (int16_t)(40 + t * 3);
            tr.p[SL("trk.amp.rel")] = 110;           // lange Ausklaenge stapeln sich
            tr.p[SL("trk.amp.sus")] = 90;
            for (int s = 0; s < 16; s++) tr.s[s].on = 1;
        }
        e.start(); e.render(b, N);
        chk(sane(b, N * 2), "Stimmen stehlen erzeugt keine Aussetzer, der Ausgang bleibt in den Grenzen");
        chk(rms(b, N) > 0.01f, "es klingt weiter");
    }

    grp("Referenz-Aufnahme");
    {
        const int N = 96000;                         // zwei Sekunden
        static float b[96000 * 2];
        e.prepare(48000.0f);
        projectInit(e.P); e.P.pat[0].used = 1;
        Track& t0 = e.P.pat[0].t[0];
        t0.len = 16; t0.speed = 4;
        t0.p[SL("trk.note")] = 45; t0.p[SL("trk.flt.freq")] = 60;
        t0.p[SL("trk.flt.env")] = 96; t0.p[SL("trk.amp.dec")] = 55;
        for (int s = 0; s < 16; s += 4) t0.s[s].on = 1;
        t0.s[8].setLock(SL("trk.note"), 57);
        Track& t1 = e.P.pat[0].t[1];
        t1.len = 12; t1.speed = 4;                   // Polymetrik gegen die Vier
        t1.p[SL("trk.note")] = 69; t1.p[SL("trk.src.b")] = 100;
        t1.p[SL("trk.amp.dec")] = 30; t1.p[SL("trk.vol")] = 85;
        for (int s = 2; s < 12; s += 3) t1.s[s].on = 1;
        e.start(); e.render(b, N);

        const char* ref = "tests/ref/pattern.wav";
        int rf = 0, rs = 0, rc = 0;
        int16_t* d = wavRead(ref, &rf, &rs, &rc);
        if (!d) {
            wavWrite(ref, b, N, 48000);
            printf("  Referenz neu geschrieben: %s\n", ref);
        } else {
            chk(rf == N && rs == 48000 && rc == 2, "Referenz passt in Laenge und Format");
            int bad = 0;
            if (rf == N && rc == 2)
                for (int i = 0; i < N * 2; i++) {
                    float x = b[i]; if (x > 1.0f) x = 1.0f; if (x < -1.0f) x = -1.0f;
                    int got = (int)(x * 32767.0f);
                    if (abs(got - d[i]) > 2) bad++;
                }
            chk(bad == 0, "Signal stimmt mit der Referenz ueberein");
            free(d);
        }
    }

    printf(fails ? "\nFEHLGESCHLAGEN: %d von %d Pruefungen\n" : "\nOK: %d Pruefungen ok, %d Fehler\n",
           fails ? fails : checks, fails ? checks : fails);
    return fails ? 1 : 0;
}
