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

    grp("Slide: der Ton gleitet zum naechsten Lock");
    {
        const int N = 36000;
        static float b[48000 * 2];
        e.prepare(48000.0f); setupBeat(e, 45);
        Track& tr = e.P.pat[0].t[0];
        tr.p[SL("trk.flt.freq")] = 127;
        tr.p[SL("trk.amp.sus")] = 100; tr.p[SL("trk.amp.dec")] = 127;
        tr.s[4].setLock(SL("trk.note"), 57, 1);          // eine Oktave hoch, gleitend
        e.start(); e.render(b, N);
        // Der zweite Step beginnt bei Sample 24000 und dauert 6000 Samples.
        const float* mid = b + 24000 * 2;
        float earlyLow  = bin(mid,         1800, 110.0f, 48000.0f);
        float earlyHigh = bin(mid,         1800, 220.0f, 48000.0f);
        float lateLow   = bin(mid + 4200 * 2, 1800, 110.0f, 48000.0f);
        float lateHigh  = bin(mid + 4200 * 2, 1800, 220.0f, 48000.0f);
        chk(earlyLow > earlyHigh, "zu Beginn des Steps klingt noch die alte Note");
        chk(lateHigh > lateLow,   "am Ende des Steps ist die neue Note erreicht");
    }

    grp("Scenes und Morph");
    {
        const int N = 18000;
        static float a[24000 * 2], m[24000 * 2], z[24000 * 2];
        for (int k = 0; k < 3; k++) {
            e.prepare(48000.0f); setupBeat(e, 45);
            e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 30;
            e.P.pat[0].sc[0][0].set(SL("trk.flt.freq"), 30);     // Scene A: dumpf
            e.P.pat[0].sc[1][0].set(SL("trk.flt.freq"), 110);    // Scene B: offen
            e.morph = k * 0.5f;
            e.start(); e.render(k == 0 ? a : k == 1 ? m : z, N);
        }
        float ha = bin(a, N, 3000.0f, 48000.0f), hm = bin(m, N, 3000.0f, 48000.0f),
              hz_ = bin(z, N, 3000.0f, 48000.0f);
        chk(hz_ > ha * 4.0f, "Scene B klingt deutlich offener als Scene A");
        chk(hm > ha && hm < hz_, "die Mitte liegt zwischen beiden Scenes");
    }

    grp("Morph wirkt auch auf eine klingende Stimme");
    {
        const int N = 24000;
        static float b[24000 * 2];
        e.prepare(48000.0f); setupBeat(e, 45);
        Track& tr = e.P.pat[0].t[0];
        tr.p[SL("trk.len")] = 127; tr.p[SL("trk.amp.sus")] = 100; tr.p[SL("trk.amp.dec")] = 127;
        e.P.pat[0].sc[0][0].set(SL("trk.flt.freq"), 30);
        e.P.pat[0].sc[1][0].set(SL("trk.flt.freq"), 110);
        e.morph = 0.0f; e.start();
        e.render(b, 8000);                      // erst dumpf
        e.morph = 1.0f;
        e.render(b + 8000 * 2, 8000);           // mitten im Ton aufgezogen
        float vorher = bin(b + 3000 * 2, 4000, 3000.0f, 48000.0f);
        float nachher = bin(b + 11000 * 2, 4000, 3000.0f, 48000.0f);
        chk(nachher > vorher * 4.0f, "der Crossfader oeffnet den laufenden Ton");
        (void)N;
    }

    grp("Mute macht still, aber haelt den Lauf nicht an");
    {
        const int N = 48000;
        static float stumm[48000 * 2], leise[48000 * 2];
        for (int k = 0; k < 2; k++) {
            e.prepare(48000.0f);
            projectInit(e.P); e.P.pat[0].used = 1;
            Track& t0 = e.P.pat[0].t[0];        // wuerfelt
            t0.len = 16; t0.speed = 4; t0.p[SL("trk.note")] = 45;
            for (int s = 0; s < 16; s += 2) {
                t0.s[s].on = 1; t0.s[s].setLock(SL("trk.cond"), (int16_t)condProb(50));
            }
            Track& t1 = e.P.pat[0].t[1];        // folgt dem Wuerfel der Spur darueber
            t1.len = 16; t1.speed = 4; t1.p[SL("trk.note")] = 69;
            for (int s = 0; s < 16; s += 2) {
                t1.s[s].on = 1; t1.s[s].setLock(SL("trk.cond"), (int16_t)CND_NEI);
            }
            if (k == 0) e.mute[0] = true;       // stummgeschaltet
            else { e.mute[0] = false; t0.p[SL("trk.vol")] = 0; }   // oder nur ganz leise
            e.start(); e.render(k == 0 ? stumm : leise, N);
        }
        e.mute[0] = false;
        int bad = 0;
        for (int i = 0; i < N * 2; i++) if (stumm[i] != leise[i]) bad++;
        chk(bad == 0, "stumm und leise liefern dasselbe - die Bedingungen laufen weiter");
        chk(rms(stumm, N) > 0.005f, "die Nachbarspur klingt weiter");
    }

    grp("Step-Loop wiederholt das Fenster, die Uhr laeuft weiter");
    {
        const int N = 96000;                       // zwei Sekunden: Step 8 faellt erst dann an
        static float b[96000 * 2];
        e.prepare(48000.0f); setupBeat(e, 45);
        Track& tr = e.P.pat[0].t[0];
        tr.s[8].setLock(SL("trk.note"), 81);      // dieser Step darf nie zu hoeren sein
        e.seq.loopOn = true; e.seq.loopFrom = 0; e.seq.loopLen = 4;
        e.start(); e.render(b, N);
        static float ohne[96000 * 2];
        e.prepare(48000.0f); setupBeat(e, 45);
        e.P.pat[0].t[0].s[8].setLock(SL("trk.note"), 81);
        e.seq.loopOn = false;
        e.start(); e.render(ohne, N);
        // Gemessen im Fenster genau dieses Steps (Sample 48000 bis 54000), nicht ueber alles.
        chk(bin(b + 48000 * 2, 6000, 880.0f, 48000.0f)
                < bin(ohne + 48000 * 2, 6000, 880.0f, 48000.0f) * 0.25f,
            "der Step ausserhalb des Fensters klingt nicht mehr");
        chk(rms(b, N) > 0.01f, "das Fenster wiederholt sich hoerbar");
    }

    grp("Zweite und dritte Machine");
    {
        const int N = 24000;
        static float a[24000 * 2], b[24000 * 2];
        // FM: Verhaeltnis 2:1 - der Grundton bleibt stehen, es kommen Obertoene dazu
        e.prepare(48000.0f); setupBeat(e, 45);
        Track& t = e.P.pat[0].t[0];
        t.p[SL("trk.machine")] = 1;
        t.p[SL("trk.src.a")] = 24;                  // Verhaeltnis 2.00 : 1
        t.p[SL("trk.src.b")] = 0;                   // noch ohne Modulation
        t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100;
        t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 127;
        e.start(); e.render(a, N);
        chk(bin(a, N, 110.0f, 48000.0f) > bin(a, N, 220.0f, 48000.0f) * 4.0f,
            "FM ohne Index ist kein reiner Grundton");
        e.prepare(48000.0f); setupBeat(e, 45);
        Track& t2 = e.P.pat[0].t[0];
        t2.p[SL("trk.machine")] = 1;
        t2.p[SL("trk.src.a")] = 24; t2.p[SL("trk.src.b")] = 110;   // viel Modulation
        t2.p[SL("trk.flt.freq")] = 127; t2.p[SL("trk.amp.sus")] = 100;
        t2.p[SL("trk.amp.dec")] = 127; t2.p[SL("trk.len")] = 127;
        e.start(); e.render(b, N);
        chk(bin(b, N, 330.0f, 48000.0f) > bin(a, N, 330.0f, 48000.0f) * 5.0f,
            "der Index bringt keine Obertoene");

        // DRUM: die Tonhoehe faellt - genau das macht den Anschlag
        e.prepare(48000.0f); setupBeat(e, 33);
        Track& d = e.P.pat[0].t[0];
        d.p[SL("trk.machine")] = 2;
        d.p[SL("trk.src.a")] = 100;                 // kraeftiger Sweep
        d.p[SL("trk.src.b")] = 0;                   // kein Rauschen
        d.p[SL("trk.flt.freq")] = 127; d.p[SL("trk.amp.dec")] = 70;
        e.start(); e.render(a, N);
        float frueh = 0, spaet = 0;
        for (int k = 200; k < 2000; k++) frueh += fabsf(a[k * 2]);
        for (int k = 3000; k < 4800; k++) spaet += fabsf(a[k * 2]);
        chk(bin(a, 600, 400.0f, 48000.0f) > bin(a + 3000 * 2, 600, 400.0f, 48000.0f) * 2.0f,
            "die Tonhoehe faellt nicht");
        chk(frueh > 0.0f && spaet > 0.0f, "der Schlag klingt nicht");

        // Rauschen statt Ton
        e.prepare(48000.0f); setupBeat(e, 69);
        Track& h = e.P.pat[0].t[0];
        h.p[SL("trk.machine")] = 2;
        h.p[SL("trk.src.a")] = 0; h.p[SL("trk.src.b")] = 127;
        h.p[SL("trk.flt.freq")] = 127; h.p[SL("trk.amp.dec")] = 20;
        e.start(); e.render(b, N);
        float breit = bin(b, 2000, 3000.0f, 48000.0f), schmal = bin(b, 2000, 440.0f, 48000.0f);
        chk(breit > schmal * 0.2f, "Rauschen hat keine Hoehen");
        chk(sane(b, N * 2), "DRUM laeuft aus dem Ruder");
    }

    grp("Sende-Effekte");
    {
        const int N = 72000;                        // anderthalb Sekunden
        static float trocken[72000 * 2], nass[72000 * 2];
        for (int k = 0; k < 2; k++) {
            e.prepare(48000.0f);
            projectInit(e.P); e.P.pat[0].used = 1;
            Track& t = e.P.pat[0].t[0];
            t.len = 16; t.speed = 4;
            t.p[SL("trk.note")] = 57; t.p[SL("trk.amp.dec")] = 40;
            t.s[0].on = 1;                          // ein einziger Anschlag
            if (k == 1) t.p[SL("trk.send.a")] = 110;
            e.P.g[SL("fx.a.div")] = 4;              // 1/8 = 0,25 s bei 120 bpm
            e.P.g[SL("fx.a.fb")] = 90;
            e.start(); e.render(k == 0 ? trocken : nass, N);
        }
        // Nach einer halben Sekunde ist der Ton laengst vorbei - nur das Echo kann klingen.
        float spaetTrocken = rms(trocken + 30000 * 2, 8000);
        float spaetNass    = rms(nass + 30000 * 2, 8000);
        chk(spaetTrocken < 0.0005f, "ohne Sende klingt es nach dem Ton weiter");
        chk(spaetNass > spaetTrocken * 20.0f, "das Echo kommt nicht");
        chk(sane(nass, N * 2), "das Echo laeuft aus dem Ruder");

        static float hall[72000 * 2];
        e.prepare(48000.0f);
        projectInit(e.P); e.P.pat[0].used = 1;
        Track& t = e.P.pat[0].t[0];
        t.len = 16; t.speed = 4;
        t.p[SL("trk.note")] = 57; t.p[SL("trk.amp.dec")] = 40;
        t.p[SL("trk.send.b")] = 110;
        t.s[0].on = 1;
        e.P.g[SL("fx.b.dec")] = 120;
        e.start(); e.render(hall, N);
        chk(rms(hall + 20000 * 2, 4000) > 0.001f, "der Hall hat keine Fahne");
        chk(rms(hall + 20000 * 2, 4000) < rms(hall, 4000), "die Fahne wird nicht leiser");
        chk(sane(hall, N * 2), "der Hall laeuft aus dem Ruder");
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
