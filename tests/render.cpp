// Klang-Tests. Gemessen wird am ERGEBNIS (Pegel, Frequenz, Gleichheit), nicht an internen
// Zwischenwerten - so bleiben die Tests gueltig, wenn die Umsetzung sich aendert.
#include "../src/engine.h"
#include "../src/text.h"
#include "../src/wav.h"
#include <stdio.h>
#include <math.h>
#include <initializer_list>
#include <utility>

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
        e.P.pat[0].t[0].p[SL("trk.len")] = 98;         // lange Note, sonst greift der Anstieg nicht
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
        tr.p[SL("trk.len")] = 98; tr.p[SL("trk.amp.sus")] = 100; tr.p[SL("trk.amp.dec")] = 127;
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

    grp("Ein gedrehter Regler wirkt auf den klingenden Ton");
    {
        const int N = 24000;
        static float b[24000 * 2];
        e.prepare(48000.0f); setupBeat(e, 45);
        Track& t = e.P.pat[0].t[0];
        t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100;
        t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 98;
        e.start();
        e.render(b, 12000);
        e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 30;        // mitten im Ton zudrehen
        e.render(b + 12000 * 2, 12000);
        float offen = bin(b + 2000 * 2, 8000, 4000.0f, 48000.0f);
        float zu    = bin(b + 14000 * 2, 8000, 4000.0f, 48000.0f);
        chk(zu < offen * 0.2f, "der Filterregler bleibt am laufenden Ton wirkungslos");

        // Gegenprobe: was dieser Step gelockt hat, bleibt stehen - sonst waeren P-Locks wertlos.
        static float c[24000 * 2];
        e.prepare(48000.0f); setupBeat(e, 45);
        Track& t2 = e.P.pat[0].t[0];
        t2.p[SL("trk.flt.freq")] = 127; t2.p[SL("trk.amp.sus")] = 100;
        t2.p[SL("trk.amp.dec")] = 127; t2.p[SL("trk.len")] = 98;
        t2.s[0].setLock(SL("trk.flt.freq"), 127);
        e.start();
        e.render(c, 12000);
        e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 30;
        e.render(c + 12000 * 2, 12000);
        float vorher  = bin(c + 2000 * 2, 8000, 4000.0f, 48000.0f);
        float nachher = bin(c + 14000 * 2, 8000, 4000.0f, 48000.0f);
        chk(nachher > vorher * 0.4f, "der P-Lock laesst sich vom Spurregler wegdrehen");

        // Ein trigless gesetzter Wert gilt weiter, auch wenn die Spur etwas anderes sagt.
        static float tl[24000 * 2];
        e.prepare(48000.0f); setupBeat(e, 45);
        Track& t4 = e.P.pat[0].t[0];
        t4.p[SL("trk.flt.freq")] = 127; t4.p[SL("trk.amp.sus")] = 100;
        t4.p[SL("trk.amp.dec")] = 127; t4.p[SL("trk.len")] = 98;
        t4.s[1].on = 2;                                   // trigless: nur Werte, kein Anschlag
        t4.s[1].setLock(SL("trk.flt.freq"), 30);
        e.start();
        e.render(tl, 24000);
        chk(bin(tl + 1000 * 2, 4000, 4000.0f, 48000.0f)
                > bin(tl + 10000 * 2, 4000, 4000.0f, 48000.0f) * 4.0f,
            "der trigless gesetzte Wert haelt nicht");

        // Und die Note bleibt die Note: sie gehoert zum Anschlag, nicht zum Dauerzustand.
        static float d[24000 * 2];
        e.prepare(48000.0f); setupBeat(e, 45);
        Track& t3 = e.P.pat[0].t[0];
        t3.p[SL("trk.flt.freq")] = 127; t3.p[SL("trk.amp.sus")] = 100;
        t3.p[SL("trk.amp.dec")] = 127; t3.p[SL("trk.len")] = 98;
        e.start();
        e.render(d, 6000);
        e.P.pat[0].t[0].p[SL("trk.note")] = 69;
        e.render(d + 6000 * 2, 6000);
        chk(bin(d + 7000 * 2, 4000, 110.0f, 48000.0f) > bin(d + 7000 * 2, 4000, 440.0f, 48000.0f),
            "der klingende Ton springt auf die neue Note");
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
        t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 98;
        e.start(); e.render(a, N);
        chk(bin(a, N, 110.0f, 48000.0f) > bin(a, N, 220.0f, 48000.0f) * 4.0f,
            "FM ohne Index ist kein reiner Grundton");
        e.prepare(48000.0f); setupBeat(e, 45);
        Track& t2 = e.P.pat[0].t[0];
        t2.p[SL("trk.machine")] = 1;
        t2.p[SL("trk.src.a")] = 24; t2.p[SL("trk.src.b")] = 110;   // viel Modulation
        t2.p[SL("trk.flt.freq")] = 127; t2.p[SL("trk.amp.sus")] = 100;
        t2.p[SL("trk.amp.dec")] = 127; t2.p[SL("trk.len")] = 98;
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

    grp("Sampler");
    {
        const int N = 24000;
        static float b[24000 * 2];
        // Ein Sample bauen: eine Sekunde 440 Hz. Note 60 muss es unveraendert abspielen.
        static float sine[48000];
        for (int i = 0; i < 48000; i++) sine[i] = sinf(6.2831853f * 440.0f * i / 48000.0f);

        e.prepare(48000.0f);
        chk(e.pool.set(0, sine, 48000, 48000.0f, "sinus", true), "Sample laesst sich nicht ablegen");
        chk(e.pool.s[0].n == 48000 && !e.pool.empty(0), "Sample liegt nicht im Vorrat");
        chk(e.pool.empty(1), "ein leerer Platz gilt als belegt");

        setupBeat(e, 60);
        Track& t = e.P.pat[0].t[0];
        t.p[SL("trk.machine")] = 3;                       // SMPL
        t.p[SL("trk.smp")] = 0;
        t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100;
        t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 98;
        e.start(); e.render(b, N);
        chk(bin(b, N, 440.0f, 48000.0f) > bin(b, N, 880.0f, 48000.0f) * 8.0f,
            "Note 60 spielt das Sample nicht in seiner Geschwindigkeit");

        e.prepare(48000.0f);
        e.pool.set(0, sine, 48000, 48000.0f, "sinus", true);
        setupBeat(e, 72);                                  // eine Oktave hoeher
        Track& t2 = e.P.pat[0].t[0];
        machineSelect(t2.p, 3); t2.p[SL("trk.smp")] = 0;
        t2.p[SL("trk.flt.freq")] = 127; t2.p[SL("trk.amp.sus")] = 100;
        t2.p[SL("trk.amp.dec")] = 127; t2.p[SL("trk.len")] = 98;
        e.start(); e.render(b, N);
        chk(bin(b, N, 880.0f, 48000.0f) > bin(b, N, 440.0f, 48000.0f) * 8.0f,
            "eine Oktave hoeher wird nicht doppelt so schnell abgespielt");

        // Ein leerer Platz macht Stille, keinen Absturz
        e.prepare(48000.0f);
        setupBeat(e, 60);
        Track& t3 = e.P.pat[0].t[0];
        machineSelect(t3.p, 3); t3.p[SL("trk.smp")] = 7;
        e.start(); e.render(b, N);
        chk(rms(b, N) == 0.0f, "ein leerer Platz klingt");

        // Der Platz ist lockbar: jeder Step darf ein anderes Sample anschlagen
        static float lo[48000], hi[48000];
        for (int i = 0; i < 48000; i++) {
            lo[i] = sinf(6.2831853f * 220.0f * i / 48000.0f);
            hi[i] = sinf(6.2831853f * 1760.0f * i / 48000.0f);
        }
        static float c[96000 * 2];
        e.prepare(48000.0f);
        e.pool.set(0, lo, 48000, 48000.0f, "tief", true);
        e.pool.set(1, hi, 48000, 48000.0f, "hoch", true);
        setupBeat(e, 60);
        Track& t4 = e.P.pat[0].t[0];
        machineSelect(t4.p, 3); t4.p[SL("trk.smp")] = 0;
        t4.p[SL("trk.flt.freq")] = 127; t4.p[SL("trk.amp.dec")] = 90;
        t4.s[4].setLock(SL("trk.smp"), 1);
        e.start(); e.render(c, 48000);
        float ersterTief = bin(c, 5000, 220.0f, 48000.0f);
        float zweiterHoch = bin(c + 24000 * 2, 5000, 1760.0f, 48000.0f);
        float zweiterTief = bin(c + 24000 * 2, 5000, 220.0f, 48000.0f);
        chk(ersterTief > 0.005f, "der erste Step spielt das tiefe Sample nicht");
        chk(zweiterHoch > zweiterTief * 4.0f, "der gelockte Step spielt nicht das andere Sample");
        chk(sane(c, 48000 * 2), "der Sampler laeuft aus dem Ruder");
    }

    grp("Stereo-Samples");
    {
        const int N = 24000;
        static float b[24000 * 2];
        static float st[48000 * 2];                     // links 220 Hz, rechts 880 Hz
        for (int i = 0; i < 48000; i++) {
            st[i * 2]     = sinf(6.2831853f * 220.0f * i / 48000.0f);
            st[i * 2 + 1] = sinf(6.2831853f * 880.0f * i / 48000.0f);
        }
        e.prepare(48000.0f);
        chk(e.pool.set(0, st, 48000, 48000.0f, "stereo", true, 2), "Stereo-Sample abgelehnt");
        chk(e.pool.s[0].ch == 2 && e.pool.s[0].n == 48000, "Stereo falsch abgelegt");
        setupBeat(e, 60);
        Track& t = e.P.pat[0].t[0];
        machineSelect(t.p, 3); t.p[SL("trk.smp")] = 0;
        t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100;
        t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 98;
        e.start(); e.render(b, N);
        float l220 = bin(b, N, 220.0f, 48000.0f),     l880 = bin(b, N, 880.0f, 48000.0f);
        float r220 = bin(b + 1, N, 220.0f, 48000.0f), r880 = bin(b + 1, N, 880.0f, 48000.0f);
        chk(l220 > l880 * 4.0f, "links klingt nicht die linke Seite");
        chk(r880 > r220 * 4.0f, "rechts klingt nicht die rechte Seite");
    }

    grp("Aufnahme in den Vorrat");
    {
        const int N = 48000;
        static float b[48000 * 2];
        e.prepare(48000.0f); setupBeat(e, 57);
        e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 127;
        e.start();
        chk(e.recStart(5, 0.5f, "mitschnitt", 2) == 24000, "Aufnahme laesst sich nicht scharf stellen");
        chk(e.recOn, "Aufnahme nicht scharf");
        e.render(b, N);                                  // eine Sekunde - die Aufnahme endet nach 0,5
        chk(!e.recOn, "die Aufnahme endet nicht von selbst");
        chk(e.pool.s[5].n == 24000, "die Aufnahme ist nicht eine halbe Sekunde lang");
        chk(e.pool.s[5].ch == 2, "die Aufnahme ist nicht stereo");

        // Das Aufgenommene muss klingen wie das, was gespielt wurde.
        float rmsAuf = 0;
        for (int i = 0; i < 24000; i++) {
            float x = e.pool.data[e.pool.s[5].at + i * 2];
            rmsAuf += x * x;
        }
        rmsAuf = sqrtf(rmsAuf / 24000.0f);
        chk(rmsAuf > 0.01f, "die Aufnahme ist still");

        // Und sie laesst sich sofort wieder abspielen.
        static float c[24000 * 2];
        e.prepare(48000.0f);
        setupBeat(e, 60);
        Track& t = e.P.pat[0].t[0];
        machineSelect(t.p, 3); t.p[SL("trk.smp")] = 5;      // setzt START auf den Anfang
        chk(t.p[SL("trk.src.a")] == 0, "SMPL startet nicht am Anfang des Samples");
        t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.dec")] = 110;
        e.start(); e.render(c, 24000);
        chk(rms(c, 24000) > 0.005f, "das Aufgenommene spielt nicht");
        chk(sane(c, 24000 * 2), "die Aufnahme laeuft aus dem Ruder");
    }

    grp("Sample-Ausschnitt und Schleife");
    {
        // Eine Sekunde: erste Haelfte 220 Hz, zweite Haelfte 880 Hz.
        static float z[48000];
        for (int i = 0; i < 48000; i++)
            z[i] = sinf(6.2831853f * (i < 24000 ? 220.0f : 880.0f) * i / 48000.0f);
        auto spiel = [&](int a, int b, int la, bool loop, int start, float* out, int N) {
            e.prepare(48000.0f);
            e.pool.clear();
            e.pool.set(0, z, 48000, 48000.0f, "zwei", false);
            e.pool.setTrim(0, a, b, la, loop);
            setupBeat(e, 60);
            Track& t = e.P.pat[0].t[0];
            machineSelect(t.p, 3); t.p[SL("trk.smp")] = 0;
            t.p[SL("trk.src.a")] = (int16_t)start;
            t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100;
            t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 98;
            for (int s = 1; s < 16; s++) t.s[s].on = 0;           // nur ein Anschlag
            e.start(); e.render(out, N);
        };
        static float o[48000 * 2];
        spiel(24000, 0, 0, false, 0, o, 12000);
        chk(bin(o, 12000, 880.0f, 48000.0f) > bin(o, 12000, 220.0f, 48000.0f) * 6.0f,
            "der Ausschnitt beginnt nicht bei a");

        spiel(0, 24000, 0, false, 0, o, 36000);
        chk(rms(o + 26000 * 2, 8000) < 0.001f, "ohne Schleife klingt es ueber b hinaus");
        chk(bin(o, 20000, 220.0f, 48000.0f) > bin(o, 20000, 880.0f, 48000.0f) * 6.0f,
            "vor b klingt nicht die erste Haelfte");

        // Schleife ueber die zweite Haelfte: es klingt ueber das Sample-Ende (48000) hinaus,
        // und zwar 880 Hz. Gemessen vor dem Ende der Note (LEN endet bei 63500).
        static float lo[96000 * 2];
        spiel(0, 0, 24000, true, 0, lo, 96000);
        chk(rms(lo + 50000 * 2, 8000) > 0.05f, "die Schleife haelt den Ton nicht");
        chk(bin(lo + 50000 * 2, 8000, 880.0f, 48000.0f) > bin(lo + 50000 * 2, 8000, 220.0f, 48000.0f) * 6.0f,
            "die Schleife springt nicht an la");
        // Gegenprobe ohne Schleife: an derselben Stelle ist es still.
        spiel(0, 0, 24000, false, 0, lo, 96000);
        chk(rms(lo + 50000 * 2, 8000) < 0.001f, "ohne Schleife klingt es ueber das Ende hinaus");

        // START verschiebt INNERHALB des Ausschnitts: Ausschnitt = zweite Haelfte, START ~ Mitte davon.
        spiel(24000, 0, 0, false, 64, o, 6000);
        chk(rms(o, 6000) > 0.05f, "START zeigt aus dem Ausschnitt hinaus");

        // Vorhoeren: klingt bei stehender Uhr, und die Einstellungen einer Spur stoeren nicht.
        e.prepare(48000.0f);
        e.pool.clear(); e.pool.set(0, z, 48000, 48000.0f, "zwei", false);
        projectInit(e.P);
        e.P.pat[0].t[0].p[SL("trk.flt.freq")] = 5;          // Spur 1 fast zu - darf nicht stoeren
        e.P.pat[0].t[0].p[SL("trk.vol")] = 0;
        e.resetTrackValues();
        e.preview(0);
        chk(e.previewing(), "Vorhoeren startet nicht");
        e.render(o, 12000);
        chk(rms(o, 12000) > 0.05f, "Vorhoeren klingt nicht (oder die Spur legt sich darueber)");
        e.previewStop();
        e.render(o, 12000);
        chk(rms(o + 6000 * 2, 6000) < 0.001f, "Vorhoeren laesst sich nicht anhalten");

        // Nulldurchgang: der gefundene Punkt liegt auf einem Wechsel von <=0 nach >0.
        e.pool.clear(); e.pool.set(0, z, 48000, 48000.0f, "zwei", false);
        int32_t zc = e.pool.zeroCross(0, 1000);
        chk(zc > 0 && z[zc - 1] <= 0.0f && z[zc] > 0.0f, "kein Nulldurchgang gefunden");
        chk(abs(zc - 1000) < 220, "der Nulldurchgang liegt nicht in der Naehe");

        // Ausschnitt im Dokument, und er wartet auf den Klang, wenn das Sample erst spaeter kommt.
        e.pool.setTrim(0, 1000, 30000, 5000, true);
        static Doc d; d.clear(); poolToDoc(e.pool, d);
        static SamplePool leer; leer.clear(); poolFromDoc(d, leer);
        chk(leer.wanted(0) && leer.s[0].a == 1000 && leer.s[0].b == 30000 && leer.s[0].la == 5000,
            "der Ausschnitt steht nicht im Dokument");
        leer.set(0, z, 48000, 48000.0f, "zwei", false);
        chk(leer.s[0].a == 1000 && leer.s[0].end() == 30000 && leer.s[0].loopStart() == 5000 && leer.s[0].loop,
            "der nachgereichte Klang verliert den Ausschnitt");
        // Unsinnige Punkte werden zurechtgerueckt statt zu stoeren.
        leer.setTrim(0, 40000, 100, 999999, false);
        chk(leer.s[0].a < leer.s[0].end() && leer.s[0].loopStart() >= leer.s[0].a, "unsinnige Punkte bleiben stehen");
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

        // Gemeldet: DAMP 127 machte den Hall stumm, TONE 127 das Echo. Beides darf nie sein -
        // und eine laengere Abklingzeit muss laenger klingen.
        auto fahne = [&](int dec, int damp) {
            e.prepare(48000.0f);
            projectInit(e.P); e.P.pat[0].used = 1;
            Track& h = e.P.pat[0].t[0];
            h.p[SL("trk.note")] = 57; h.p[SL("trk.amp.dec")] = 40; h.p[SL("trk.send.b")] = 110;
            h.s[0].on = 1;
            e.P.g[SL("fx.b.dec")] = (int16_t)dec; e.P.g[SL("fx.b.damp")] = (int16_t)damp;
            e.start(); e.render(hall, 72000);
            return rms(hall + 60000 * 2, 8000);
        };
        chk(fahne(90, 127) > 0.002f, "DAMP 127 macht den Hall stumm");
        chk(fahne(110, 40) > fahne(50, 40) * 4.0f, "mehr DECAY klingt nicht laenger");
        {
            e.prepare(48000.0f);
            projectInit(e.P); e.P.pat[0].used = 1;
            Track& h = e.P.pat[0].t[0];
            h.p[SL("trk.note")] = 57; h.p[SL("trk.amp.dec")] = 30; h.p[SL("trk.send.a")] = 110;
            h.s[0].on = 1;
            e.P.g[SL("fx.a.tone")] = 127; e.P.g[SL("fx.a.fb")] = 80;
            e.start(); e.render(hall, 48000);
            chk(rms(hall + 30000 * 2, 8000) > 0.002f, "TONE 127 macht das Echo stumm");
        }
        chk(sane(hall, N * 2), "der Hall laeuft aus dem Ruder");
    }

    grp("Pattern wechseln");
    {
        // Pattern 0: tiefer Dauerton. Pattern 1: hoher Dauerton. Der Wechsel muss genau
        // am Taktende liegen - 16 Sechzehntel bei 120 bpm sind 96000 Samples.
        auto bau = [&](Engine& en) {
            projectInit(en.P);
            for (int p = 0; p < 2; p++) {
                en.P.pat[p].used = 1;
                Track& tr = en.P.pat[p].t[0];
                tr.len = 16; tr.speed = 4;
                tr.p[SL("trk.note")] = p == 0 ? 45 : 69;      // 110 Hz bzw. 440 Hz
                tr.p[SL("trk.flt.freq")] = 127;
                tr.p[SL("trk.amp.sus")] = 100; tr.p[SL("trk.amp.dec")] = 127;
                tr.p[SL("trk.len")] = 98;
                tr.s[0].on = 1;
            }
            en.pat = 0;
        };
        const int N = 144000;                                 // drei Sekunden
        static float b[144000 * 2];
        e.prepare(48000.0f); bau(e);
        e.start();
        e.queuePattern(1);
        chk(e.patNext == 1 && e.pat == 0, "der Wechsel gilt sofort statt im Takt");
        chk(e.switchAt == 96000, "der Wechsel liegt nicht am Taktende");
        e.render(b, N);
        chk(e.pat == 1 && e.patNext < 0, "es wurde nicht gewechselt");
        // Vor dem Wechsel der tiefe Grundton, danach der hohe - gemessen an derselben
        // Frequenz vor und nach der Grenze, nicht an Nachbarfrequenzen.
        float tiefVor  = bin(b + 40000 * 2, 20000, 110.0f, 48000.0f);
        float tiefNach = bin(b + 110000 * 2, 20000, 110.0f, 48000.0f);
        float hochVor  = bin(b + 40000 * 2, 20000, 440.0f, 48000.0f);
        float hochNach = bin(b + 110000 * 2, 20000, 440.0f, 48000.0f);
        // 110 Hz hat bei 440 Hz eine Oberwelle - entscheidend ist, welcher Ton in SEINEM
        // Fenster fuehrt, nicht der Vergleich derselben Frequenz ueber die Grenze hinweg.
        chk(tiefVor > hochVor, "vor dem Wechsel fuehrt nicht der tiefe Grundton");
        chk(hochNach > tiefNach * 10.0f, "nach dem Wechsel fuehrt nicht der hohe Grundton");
        chk(tiefNach < tiefVor * 0.05f, "der tiefe Grundton klingt nach dem Wechsel weiter");

        // Und der Wechsel haengt nicht davon ab, wie der Host schneidet.
        static float a2[144000 * 2], c2[144000 * 2];
        for (int k = 0; k < 2; k++) {
            float* dst = k == 0 ? a2 : c2;
            int blk = k == 0 ? 64 : 977;
            e.prepare(48000.0f); bau(e);
            e.start(); e.queuePattern(1);
            int done = 0;
            while (done < N) { int n = blk; if (done + n > N) n = N - done; e.render(dst + done * 2, n); done += n; }
        }
        int bad = 0;
        for (int i = 0; i < N * 2; i++) if (a2[i] != c2[i]) bad++;
        chk(bad == 0, "der Wechsel faellt je nach Blockgroesse anders aus");

        // Ohne laufende Uhr gilt die Wahl sofort.
        e.prepare(48000.0f); bau(e);
        e.queuePattern(1);
        chk(e.pat == 1 && e.patNext < 0, "bei stehender Uhr wird nicht sofort gewechselt");
    }

    grp("Pattern kopieren und leeren");
    {
        e.prepare(48000.0f);
        projectInit(e.P);
        e.P.pat[0].used = 1;
        Track& t = e.P.pat[0].t[0];
        t.p[SL("trk.note")] = 50;
        t.s[3].on = 1;
        t.s[3].setLock(SL("trk.flt.freq"), 42);
        e.P.pat[0].sc[1][0].set(SL("trk.vol"), 90);

        patternCopy(e.P, 0, 5);
        chk(e.P.pat[5].used == 1, "die Kopie gilt als unbenutzt");
        chk(e.P.pat[5].t[0].s[3].on == 1, "der Step fehlt in der Kopie");
        const Lock* l = e.P.pat[5].t[0].s[3].findLock(SL("trk.flt.freq"));
        chk(l && l->val == 42, "der Lock fehlt in der Kopie");
        chk(e.P.pat[5].sc[1][0].find(SL("trk.vol")) != nullptr, "die Scene fehlt in der Kopie");
        chk(e.P.pat[5].t[0].p[SL("trk.note")] == 50, "der Spurwert fehlt in der Kopie");

        patternClear(e.P, 5);
        chk(e.P.pat[5].used == 0 && e.P.pat[5].t[0].s[3].on == 0, "das Leeren laesst Steps stehen");
        chk(e.P.pat[5].t[0].p[SL("trk.note")] == 60, "das Leeren setzt die Werte nicht zurueck");
        chk(e.P.pat[0].t[0].s[3].on == 1, "das Leeren trifft das falsche Pattern");
    }

    grp("Song: eine Kette aus Patterns");
    {
        // Pattern 0 zweimal, Pattern 1 einmal, dann wieder von vorn.
        // Ein Takt sind bei 120 bpm 96000 Samples.
        auto bau = [&](Engine& en) {
            projectInit(en.P);
            for (int p = 0; p < 2; p++) {
                en.P.pat[p].used = 1;
                Track& tr = en.P.pat[p].t[0];
                tr.p[SL("trk.note")] = p == 0 ? 45 : 69;
                tr.s[0].on = 1;
            }
            en.P.song.n = 2;
            en.P.song.e[0] = {0, 2};
            en.P.song.e[1] = {1, 1};
            en.pat = 0;
        };
        e.prepare(48000.0f); bau(e);
        e.songStart();
        e.start();
        chk(e.songOn && e.pat == 0 && e.songPos == 0, "der Song beginnt nicht bei Eintrag 1");

        // In Bloecken rendern und mitschreiben, wann welches Pattern lief.
        static float b[1000 * 2];
        int64_t wechsel[8]; int pats[8], nw = 0;
        int vorher = e.pat;
        int64_t ursprungNachTakt1 = -1;
        for (int k = 0; k < 400; k++) {                    // 400 000 Samples
            e.render(b, 1000);
            if (e.seq.pos == 97000) ursprungNachTakt1 = e.seq.origin;
            if (e.pat != vorher && nw < 8) { wechsel[nw] = e.seq.origin; pats[nw] = e.pat; nw++; vorher = e.pat; }
        }
        chk(nw >= 2, "der Song wechselt nicht");
        chk(nw >= 1 && wechsel[0] == 192000 && pats[0] == 1,
            "nach zwei Takten Pattern 0 kommt nicht genau bei 192000 Pattern 1");
        chk(nw >= 2 && wechsel[1] == 288000 && pats[1] == 0,
            "nach einem Takt Pattern 1 geht es nicht genau bei 288000 von vorn los");
        // Zwischen den Wiederholungen wird das Pattern NICHT neu begonnen - sonst bricht
        // jede Spur mit anderer Laenge (12 gegen 16) bei jeder Wiederholung ab.
        chk(ursprungNachTakt1 == 0, "die Wiederholung beginnt das Pattern neu");

        // Unabhaengig davon, wie der Host schneidet.
        static float a2[300000 * 2], c2[300000 * 2];
        for (int k = 0; k < 2; k++) {
            float* dst = k == 0 ? a2 : c2;
            int blk = k == 0 ? 64 : 977;
            e.prepare(48000.0f); bau(e);
            e.songStart(); e.start();
            int done = 0;
            while (done < 300000) { int n = blk; if (done + n > 300000) n = 300000 - done; e.render(dst + done * 2, n); done += n; }
        }
        int bad = 0;
        for (int i = 0; i < 300000 * 2; i++) if (a2[i] != c2[i]) bad++;
        chk(bad == 0, "der Song faellt je nach Blockgroesse anders aus");

        // Song einschalten, waehrend es laeuft: erst am Taktende, nicht mittendrin.
        e.prepare(48000.0f); bau(e);
        e.start();
        for (int k = 0; k < 30; k++) e.render(b, 1000);    // 30 000 Samples in den Takt hinein
        e.pat = 1; e.seq.toPattern(e.seq.pos);             // gerade laeuft ein anderes Pattern
        e.songStart();
        chk(e.songOn && e.pat == 1, "der Song springt sofort statt am Taktende");
        chk(e.switchAt == 30000 + 96000, "der Song beginnt nicht am naechsten Taktende");
        for (int k = 0; k < 100; k++) e.render(b, 1000);
        chk(e.pat == 0 && e.songPos == 0, "nach dem Taktende spielt nicht Eintrag 1");

        // Wer von Hand ein Pattern waehlt, verlaesst den Song.
        e.prepare(48000.0f); bau(e);
        e.songStart(); e.start();
        e.render(b, 1000);
        e.queuePattern(1);
        chk(!e.songOn, "ein Tipp auf ein Pattern laesst den Song weiterlaufen");

        // Der Song steht im Dokument.
        static Doc d; d.clear();
        projectToDoc(e.P, d);
        static Project Q; projectFromDoc(d, Q);
        chk(Q.song.n == 2 && Q.song.e[0].pat == 0 && Q.song.e[0].rep == 2
            && Q.song.e[1].pat == 1 && Q.song.e[1].rep == 1, "der Song ueberlebt das Dokument nicht");
        e.pat = 0;
    }

    grp("Tempo aendern, waehrend es laeuft");
    {
        // Jede Sechzehntel ein Anschlag. Ein Druck auf BPM + darf nicht nachholen, was
        // bei neuem Tempo "schon haette sein muessen" - vorher kamen sieben auf einmal.
        auto zaehle = [&](int bloecke) { uint64_t c0 = e.clock; static float bb[512 * 2];
                                         for (int k = 0; k < bloecke; k++) e.render(bb, 512);
                                         return (int)(e.clock - c0); };
        e.prepare(48000.0f);
        projectInit(e.P); e.P.pat[0].used = 1; e.pat = 0;
        for (int s = 0; s < 16; s++) e.P.pat[0].t[0].s[s].on = 1;
        e.start();
        zaehle(200);
        e.setTempo(180.0);
        chk(zaehle(1) <= 1, "ein Tempowechsel feuert mehrere Anschlaege auf einmal");
        // Danach im neuen Tempo: 180 bpm sind 12 Sechzehntel je Sekunde.
        int n = zaehle(94);                                   // ~1 s
        chk(n >= 11 && n <= 13, "nach dem Wechsel laeuft es nicht im neuen Tempo");
        e.setTempo(90.0);
        chk(zaehle(1) <= 1, "langsamer werden feuert mehrere Anschlaege auf einmal");
        n = zaehle(94);
        chk(n >= 5 && n <= 7, "nach dem Bremsen laeuft es nicht im neuen Tempo");

        // Ein Pattern mit eigenem Tempo: es gilt ab dem Wechsel, danach wieder das Projekt.
        e.prepare(48000.0f);
        projectInit(e.P);
        for (int p = 0; p < 2; p++) { e.P.pat[p].used = 1; e.P.pat[p].t[0].s[0].on = 1; }
        e.P.pat[1].bpm = 150;
        e.pat = 0;
        e.start();
        chk(e.seq.bpm == 120.0, "Pattern 0 laeuft nicht im Projekttempo");
        e.queuePattern(1);
        static float bb[1000 * 2];
        for (int k = 0; k < 100; k++) e.render(bb, 1000);    // ueber den Wechsel bei 96000
        chk(e.pat == 1 && e.seq.bpm == 150.0, "das eigene Tempo von Pattern 1 gilt nicht");
        e.queuePattern(0);
        for (int k = 0; k < 100; k++) e.render(bb, 1000);
        chk(e.pat == 0 && e.seq.bpm == 120.0, "zurueck bei Pattern 0 gilt nicht wieder das Projekttempo");

        static Doc d; d.clear();
        projectToDoc(e.P, d);
        static Project Q; projectFromDoc(d, Q);
        chk(Q.pat[1].bpm == 150 && Q.pat[0].bpm == 0, "das Pattern-Tempo ueberlebt das Dokument nicht");
        e.pat = 0;
    }

    grp("Skala: Geschwindigkeit, Wechsellaenge, Teiler, Tonart");
    {
        auto zaehle = [&](int bloecke) { uint64_t c0 = e.clock; static float bb[512 * 2];
                                         for (int k = 0; k < bloecke; k++) e.render(bb, 512);
                                         return (int)(e.clock - c0); };
        // 2x: doppelt so viele Anschlaege je Sekunde
        e.prepare(48000.0f);
        projectInit(e.P); e.P.pat[0].used = 1; e.pat = 0;
        for (int s = 0; s < 16; s++) e.P.pat[0].t[0].s[s].on = 1;
        e.P.pat[0].mult = 6;                                  // 2x
        e.start();
        zaehle(10);
        int n2 = zaehle(94);                                  // ~1 s
        chk(n2 >= 15 && n2 <= 17, "2x laeuft nicht doppelt so schnell");

        // Umschalten im Lauf: kein Nachholen
        e.P.pat[0].mult = SPEED_1X; e.setTempo(e.effTempo(0));
        chk(zaehle(1) <= 1, "Geschwindigkeit umschalten feuert mehrere Anschlaege");
        int n1 = zaehle(94);
        chk(n1 >= 7 && n1 <= 9, "nach dem Umschalten nicht wieder 1x");

        // Teiler einer Spur im Lauf: kein Nachholen, danach das neue Raster
        e.setDiv(0, 8);                                       // 1/32
        chk(zaehle(1) <= 1, "Teiler umschalten feuert mehrere Anschlaege");
        int n32 = zaehle(94);
        chk(n32 >= 15 && n32 <= 17, "1/32 laeuft nicht doppelt so dicht wie 1/16");

        // Wechsellaenge 8: ein angewaehltes Pattern kommt nach 8 Sechzehnteln
        e.prepare(48000.0f);
        projectInit(e.P);
        for (int p = 0; p < 2; p++) { e.P.pat[p].used = 1; e.P.pat[p].t[0].s[0].on = 1; }
        e.P.pat[0].chg = 8;
        e.pat = 0; e.start();
        e.queuePattern(1);
        chk(e.switchAt == 48000, "Wechsellaenge 8 wechselt nicht nach 8 Sechzehnteln");

        // Tonart: C-Dur, die Spur spielt C# - es klingt C. Ohne SKALA bleibt es C#.
        const int N = 24000;
        static float a[24000 * 2];
        for (int k = 0; k < 2; k++) {
            e.prepare(48000.0f); setupBeat(e, 61);            // C#4
            e.P.pat[0].scale = 1; e.P.pat[0].root = 0;        // C-Dur
            Track& t = e.P.pat[0].t[0];
            t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100;
            t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 98;
            t.p[SL("trk.quant")] = (int16_t)(k == 0 ? 1 : 0);
            e.start(); e.render(a, N);
            float c = bin(a, N, 261.63f, 48000.0f), cis = bin(a, N, 277.18f, 48000.0f);
            if (k == 0) chk(c > cis * 4.0f, "in C-Dur rastet C# nicht auf C ein");
            else        chk(cis > c * 4.0f, "ohne SKALA wird trotzdem eingerastet");
        }

        // Im Dokument
        e.P.pat[3].used = 1; e.P.pat[3].mult = 2; e.P.pat[3].chg = 12;
        e.P.pat[3].root = 9; e.P.pat[3].scale = 9;
        static Doc d; d.clear(); projectToDoc(e.P, d);
        static Project Q; projectFromDoc(d, Q);
        chk(Q.pat[3].mult == 2 && Q.pat[3].chg == 12 && Q.pat[3].root == 9 && Q.pat[3].scale == 9,
            "die Skala ueberlebt das Dokument nicht");
        e.pat = 0;
    }

    grp("Akkorde und Stimmen");
    {
        auto stimmen = [&](int track) {
            int n = 0;
            for (int i = 0; i < NVOICES; i++)
                if (e.v[i].used && !e.v[i].preview && e.v[i].track == track && e.v[i].env.st != Env::REL) n++;
            return n;
        };
        auto halte = [&](int note) {                        // Grundton lang und offen
            e.prepare(48000.0f); setupBeat(e, note);
            Track& t = e.P.pat[0].t[0];
            t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100;
            t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 98; t.p[SL("trk.amp.rel")] = 90;
            return &t;
        };
        const int N = 12000;
        static float a[24000 * 2];

        // C-Dur-Dreiklang: C4 + 4 + 7
        Track* t = halte(60);
        t->p[SL("trk.nt2")] = 25 + 4; t->p[SL("trk.nt3")] = 25 + 7;
        e.start(); e.render(a, N);
        chk(stimmen(0) == 3, "ein Akkord aus drei Noten startet nicht drei Stimmen");
        float c = bin(a, N, 261.63f, 48000.0f), ee = bin(a, N, 329.63f, 48000.0f),
              g = bin(a, N, 392.00f, 48000.0f), dis = bin(a, N, 311.13f, 48000.0f);
        chk(ee > dis * 4.0f && g > dis * 4.0f && c > dis * 4.0f, "die Akkordnoten C E G klingen nicht");

        // Leitereigen: in einer Tonart zaehlen NT2..NT4 in Stufen. D in C-Dur, +2 und +4 Stufen
        // = F und A (d-Moll) - nicht F# und A wie ein Dur-Akkord in Halbtoenen.
        t = halte(62);
        e.P.pat[0].scale = 1; e.P.pat[0].root = 0;
        t->p[SL("trk.quant")] = 1; t->p[SL("trk.nt2")] = 25 + 2; t->p[SL("trk.nt3")] = 25 + 4;
        e.start(); e.render(a, N);
        chk(bin(a, N, 349.23f, 48000.0f) > bin(a, N, 369.99f, 48000.0f) * 4.0f,
            "in C-Dur ist +2 Stufen ueber D nicht F");
        chk(bin(a, N, 440.0f, 48000.0f) > bin(a, N, 415.3f, 48000.0f) * 4.0f, "in C-Dur ist +4 Stufen ueber D nicht A");
        // Auf B: +2/+4 Stufen = D und F (vermindert), ganz ohne Kreuz
        t = halte(59);
        e.P.pat[0].scale = 1; e.P.pat[0].root = 0;
        t->p[SL("trk.quant")] = 1; t->p[SL("trk.nt2")] = 25 + 2; t->p[SL("trk.nt3")] = 25 + 4;
        e.start(); e.render(a, N);
        chk(bin(a, N, 349.23f, 48000.0f) > bin(a, N, 369.99f, 48000.0f) * 4.0f, "auf B ist +4 Stufen nicht F");
        // Ohne KEY bleiben es Halbtoene
        t = halte(62);
        e.P.pat[0].scale = 1; t->p[SL("trk.quant")] = 0; t->p[SL("trk.nt2")] = 25 + 4;
        e.start(); e.render(a, N);
        chk(bin(a, N, 369.99f, 48000.0f) > bin(a, N, 349.23f, 48000.0f) * 4.0f, "ohne KEY ist +4 nicht vier Halbtoene");
        e.P.pat[0].scale = 0;

        // Akkord als Lock: nur Step 0 hat eine zweite Note
        t = halte(60);
        t->s[0].setLock(SL("trk.nt2"), 25 + 12);
        e.start(); e.render(a, N);
        chk(stimmen(0) == 2, "ein gelockter Akkord spielt nicht");
        e.render(a, 24000);                                  // weiter bis hinter Step 4
        int s4 = 0;
        for (int i = 0; i < NVOICES; i++) if (e.v[i].used && e.v[i].track == 0 && e.v[i].step == 4) s4++;
        chk(s4 == 1, "der Akkord-Lock haengt am naechsten Step noch");

        // POLY: lange Noten ueberlappen. MONO: immer nur eine - auch mit Akkord.
        for (int mode = 0; mode < 2; mode++) {
            t = halte(60);
            t->p[SL("trk.vmode")] = (int16_t)mode;
            if (mode == 1) t->p[SL("trk.nt2")] = 25 + 7;
            e.start(); e.render(a, 24000); e.render(a, 2000);   // hinter den zweiten Anschlag
            int n = 0;
            for (int i = 0; i < NVOICES; i++) if (e.v[i].used && e.v[i].track == 0) n++;
            if (mode == 0) chk(n >= 2, "POLY: lange Noten ueberlappen nicht");
            else           chk(n == 1, "MONO: es klingt mehr als eine Stimme");
        }

        // LEGATO: der zweite Anschlag stimmt nur um, die Huellkurve laeuft weiter.
        t = halte(60);
        t->p[SL("trk.vmode")] = 2;
        t->s[4].setLock(SL("trk.note"), 72);
        e.start(); e.render(a, 23000);
        uint32_t alt = 0; for (int i = 0; i < NVOICES; i++) if (e.v[i].used) alt = e.v[i].age;
        e.render(a, 4000);
        int n = 0; uint32_t age = 0; float note = 0;
        for (int i = 0; i < NVOICES; i++)
            if (e.v[i].used) { n++; age = e.v[i].age; note = e.v[i].pf[SL("trk.note")]; }
        chk(n == 1, "LEGATO: es klingt mehr als eine Stimme");
        chk(age > alt, "LEGATO: die Huellkurve wurde neu angeschlagen");
        chk(note > 71.5f && note < 72.5f, "LEGATO: der Ton wurde nicht umgestimmt");
        e.render(a, 12000);
        chk(bin(a, 12000, 523.25f, 48000.0f) > bin(a, 12000, 261.63f, 48000.0f) * 4.0f,
            "LEGATO: die neue Note klingt nicht");

        // Im Dokument
        t = halte(60);
        t->p[SL("trk.vmode")] = 1; t->s[3].setLock(SL("trk.nt3"), 32);
        static Doc d; d.clear(); projectToDoc(e.P, d);
        static Project Q; projectFromDoc(d, Q);
        chk(Q.pat[0].t[0].p[SL("trk.vmode")] == 1 && Q.pat[0].t[0].s[3].nLock == 1,
            "Akkord und Stimmenmodus ueberleben das Dokument nicht");
    }

    grp("Swing: global und je Pattern");
    {
        // Wann beginnt Step 2 (Index 1)? Aus dem Alter seiner Stimme zurueckgerechnet.
        auto start1 = [&](int gsw, int psw) {
            e.prepare(48000.0f); setupBeat(e, 60);
            Track& t = e.P.pat[0].t[0];
            for (int s = 0; s < 4; s++) t.s[s].on = 1;
            t.p[SL("trk.amp.rel")] = 90; t.p[SL("trk.len")] = 98;
            e.P.g[SL("swing")] = (int16_t)gsw;
            e.P.pat[0].swing = (uint8_t)psw;
            e.start();
            static float a[11000 * 2];
            e.render(a, 11000);
            for (int i = 0; i < NVOICES; i++)
                if (e.v[i].used && e.v[i].step == 1) return 11000 - (int)e.v[i].age;
            return -1;
        };
        int gerade = start1(50, 0), drittel = start1(67, 0), eigen = start1(67, 75);
        chk(gerade >= 5990 && gerade <= 6010, "ohne Swing kommt Step 2 nicht nach einem Step");
        chk(drittel >= 8030 && drittel <= 8090, "Swing 67 % schiebt Step 2 nicht um ein Drittel");
        chk(eigen >= 8990 && eigen <= 9010, "der Swing des Patterns gilt nicht vor dem des Projekts");
        e.P.pat[0].swing = 0;
        e.P.g[SL("swing")] = 50;

        e.P.pat[5].used = 1; e.P.pat[5].swing = 62;
        static Doc d; d.clear(); projectToDoc(e.P, d);
        static Project Q; projectFromDoc(d, Q);
        chk(Q.pat[5].swing == 62 && Q.pat[0].swing == 0, "der Swing des Patterns ueberlebt das Dokument nicht");
        // Ein Pattern mit Trigs, das niemand als benutzt markiert hat, geht trotzdem nicht verloren
        e.P.pat[9].used = 0; e.P.pat[9].t[3].s[4].on = 1;
        d.clear(); projectToDoc(e.P, d); projectFromDoc(d, Q);
        chk(Q.pat[9].used == 1 && Q.pat[9].t[3].s[4].on == 1, "ein Pattern mit Trigs fehlt im Dokument");
    }

    grp("Notenlaenge bis INF");
    {
        char tx[32];
        const ParamDef& ld = PARAMS[paramFind("trk.len")];
        paramText(ld, 12, tx, sizeof tx);  chk(!strcmp(tx, "1.00 Steps"), "12 ist nicht ein Step");
        paramText(ld, 97, tx, sizeof tx);  chk(!strcmp(tx, "9 Steps"), "97 ist nicht 9 Steps");
        paramText(ld, 126, tx, sizeof tx); chk(!strcmp(tx, "256 Steps"), "126 ist nicht 256 Steps");
        paramText(ld, 127, tx, sizeof tx); chk(!strcmp(tx, "INF"), "127 ist nicht INF");
        // INF: die Note haelt ueber viele Steps und endet erst mit dem naechsten Anschlag der Spur
        e.prepare(48000.0f); setupBeat(e, 60);
        Track& t = e.P.pat[0].t[0];
        for (int s = 0; s < 16; s++) t.s[s].on = 0;
        t.s[0].on = 1; t.s[12].on = 1;
        t.p[SL("trk.len")] = LEN_INF; t.p[SL("trk.amp.sus")] = 100; t.p[SL("trk.amp.rel")] = 60;
        e.start();
        static float a[72000 * 2];
        e.render(a, 70000);                                   // fast 12 Steps
        int an = 0; for (int i = 0; i < NVOICES; i++) if (e.v[i].used && e.v[i].env.st != Env::REL) an++;
        chk(an == 1, "die INF-Note klingt nach 11 Steps nicht mehr");
        e.render(a, 4000);                                    // hinter Step 13
        int alt = 0, neu = 0;
        for (int i = 0; i < NVOICES; i++) if (e.v[i].used) {
            if (e.v[i].step == 0 && e.v[i].env.st == Env::REL) alt++;
            if (e.v[i].step == 12 && e.v[i].env.st != Env::REL) neu++;
        }
        chk(alt == 1 && neu == 1, "der naechste Anschlag beendet die INF-Note nicht");
    }

    grp("Modulation: LFOs und Mod-Huellkurve");
    {
        static float a[48000 * 2];
        auto lang = [&](int note) {
            e.prepare(48000.0f); setupBeat(e, note);
            Track& t = e.P.pat[0].t[0];
            t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100;
            t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 98;
            return &t;
        };
        // LFO1: Rechteck auf PITCH, +50 % = +12 Halbtoene (bei PITCH ist ein Schritt ein halber Halbton), eine Viertel lang, TRIG
        Track* t = lang(60);
        t->p[SL("trk.lfo1.wave")] = LFO_SQR; t->p[SL("trk.lfo1.rate")] = 7;      // 1/4
        t->p[SL("trk.lfo1.depth")] = 64 + 24; t->p[SL("trk.lfo1.dest")] = 1;     // PITCH: ein Schritt = halber Halbton
        t->p[SL("trk.lfo1.mode")] = LFO_TRIG;
        e.start(); e.render(a, 24000);
        float hoch1 = bin(a + 2000 * 2, 8000, 523.25f, 48000.0f), tief1 = bin(a + 2000 * 2, 8000, 130.81f, 48000.0f);
        float hoch2 = bin(a + 14000 * 2, 8000, 523.25f, 48000.0f), tief2 = bin(a + 14000 * 2, 8000, 130.81f, 48000.0f);
        chk(hoch1 > tief1 * 4.0f, "LFO-Rechteck: die erste Haelfte liegt nicht eine Oktave hoeher");
        chk(tief2 > tief1 * 4.0f, "LFO-Rechteck: die zweite Haelfte liegt nicht eine Oktave tiefer");   // 523 Hz ist dort nur Oberton
        (void)hoch2;
        // Ohne Ziel bewegt sich nichts
        t = lang(60);
        t->p[SL("trk.lfo1.wave")] = LFO_SQR; t->p[SL("trk.lfo1.depth")] = 127;
        e.start(); e.render(a, 24000);
        chk(bin(a + 14000 * 2, 8000, 261.63f, 48000.0f) > bin(a + 14000 * 2, 8000, 130.81f, 48000.0f) * 4.0f,
            "ein LFO ohne Ziel veraendert den Klang");
        // FREE: die Phase haengt am Takt. Rate 1/2 = zwei Viertel; der Anschlag auf Viertel 2
        // beginnt mitten im Zyklus - also in der tiefen Haelfte.
        t = lang(60);
        t->p[SL("trk.lfo1.wave")] = LFO_SQR; t->p[SL("trk.lfo1.rate")] = 5;      // 1/2
        t->p[SL("trk.lfo1.depth")] = 64 + 24; t->p[SL("trk.lfo1.dest")] = 1;
        t->p[SL("trk.lfo1.mode")] = LFO_FREE;
        e.start(); e.render(a, 24000); e.render(a, 12000);
        chk(bin(a + 2000 * 2, 8000, 130.81f, 48000.0f) > bin(a + 2000 * 2, 8000, 523.25f, 48000.0f) * 4.0f,
            "FREE-LFO: die Phase laeuft nicht am Takt");
        // Mod-Huellkurve auf VOLUME, -100 %: am Anfang still, mit dem Abklingen lauter
        t = lang(60);
        t->p[SL("trk.menv.dest")] = 7; t->p[SL("trk.menv.depth")] = 1;           // VOLUME, -100 %
        t->p[SL("trk.menv.atk")] = 0; t->p[SL("trk.menv.dec")] = 70;
        e.start(); e.render(a, 24000);
        chk(rms(a + 200 * 2, 1500) * 4.0f < rms(a + 18000 * 2, 4000), "die Mod-Huellkurve wirkt nicht auf VOLUME");
        // LFO2 auf CUTOFF
        t = lang(48);
        t->p[SL("trk.flt.freq")] = 60;
        t->p[SL("trk.lfo2.wave")] = LFO_SQR; t->p[SL("trk.lfo2.rate")] = 7;
        t->p[SL("trk.lfo2.depth")] = 127; t->p[SL("trk.lfo2.dest")] = 4; t->p[SL("trk.lfo2.mode")] = LFO_TRIG;
        e.start(); e.render(a, 24000);
        chk(bin(a + 2000 * 2, 8000, 3000.0f, 48000.0f) > bin(a + 14000 * 2, 8000, 3000.0f, 48000.0f) * 4.0f,
            "LFO2 auf CUTOFF oeffnet das Filter nicht");
        // Text
        char tx[32];
        paramText(PARAMS[paramFind("trk.lfo1.rate")], 7, tx, sizeof tx); chk(!strcmp(tx, "1/4"), "Rate 7 ist nicht 1/4");
        paramText(PARAMS[paramFind("trk.lfo1.dest")], 4, tx, sizeof tx); chk(!strcmp(tx, "CUTOFF"), "Ziel 4 ist nicht CUTOFF");
        chk(enumCount(PARAMS[paramFind("trk.lfo1.dest")]) == MOD_NDEST, "die Zielliste hat nicht alle Ziele");

        // Alle lockbaren Werte sind Ziele - auch die, die nur beim Anschlag zaehlen
        {
            int fehlt = 0;
            for (int i = 0; i < NPARAMS; i++) {
                const ParamDef& d = PARAMS[i];
                if (d.scope != PS_TRACK || !(d.flags & PF_LOCK) || !strcmp(d.id, "trk.cond")) continue;
                if (!strncmp(d.id, "trk.lfo", 7) || !strncmp(d.id, "trk.menv", 8)) continue;
                if (modDestOfSlot(paramSlot(i)) < 0) { fehlt++; printf("    kein Ziel: %s\n", d.id); }
            }
            chk(fehlt == 0, "nicht jeder lockbare Wert ist ein Mod-Ziel");
            chk(PARAMS[paramFind("trk.lfo1.dest")].hi == MOD_NDEST - 1, "die DEST-Tabelle passt nicht zur Zielliste");
        }
        auto ziel = [](const char* name) { for (int d = 0; d < MOD_NDEST; d++) if (!strcmp(MOD_DESTS[d].name, name)) return d; return -1; };
        // VELOCITY beim Anschlag: Rechteck TRIG steht bei +1, -100 % -> Velocity 0 -> still
        t = lang(60);
        t->p[SL("trk.lfo1.wave")] = LFO_SQR; t->p[SL("trk.lfo1.mode")] = LFO_TRIG;
        t->p[SL("trk.lfo1.depth")] = 1; t->p[SL("trk.lfo1.dest")] = (int16_t)ziel("VELOCITY");
        e.start(); e.render(a, 12000);
        chk(rms(a, 12000) < 0.0005f, "LFO auf VELOCITY macht den Anschlag nicht leise");
        // PROB, FREE am Takt: Rechteck 1/2, -100 %: Viertel 1 faellt aus, Viertel 2 spielt
        t = lang(60);
        t->p[SL("trk.lfo1.wave")] = LFO_SQR; t->p[SL("trk.lfo1.mode")] = LFO_FREE; t->p[SL("trk.lfo1.rate")] = 5;
        t->p[SL("trk.lfo1.depth")] = 1; t->p[SL("trk.lfo1.dest")] = (int16_t)ziel("PROB");
        t->p[SL("trk.amp.dec")] = 30; t->p[SL("trk.amp.sus")] = 0;
        e.start(); e.render(a, 24000);
        float erste = rms(a, 20000);
        e.render(a, 20000);
        chk(erste < 0.0005f && rms(a, 20000) > 0.01f, "LFO auf PROB: FREE am Takt laesst nicht genau jeden zweiten Trig aus");
        // NT2 beim Anschlag: aus einem Ton wird eine Quinte
        t = lang(60);
        t->p[SL("trk.lfo1.wave")] = LFO_SQR; t->p[SL("trk.lfo1.mode")] = LFO_TRIG;
        t->p[SL("trk.lfo1.depth")] = 105; t->p[SL("trk.lfo1.dest")] = (int16_t)ziel("NT2");
        e.start(); e.render(a, 4000);
        { int n = 0; for (int i = 0; i < NVOICES; i++) if (e.v[i].used) n++;
          chk(n == 2, "LFO auf NT2 macht keinen Zweiklang"); }
        chk(bin(a + 1000 * 2, 3000, 392.0f, 48000.0f) > 0.01f, "die Quinte aus dem LFO klingt nicht");
        // AMP SUS waehrend der Note: ohne Modulation verklingt sie, mit +100 % haelt sie
        float ohne = 0;
        for (int k = 0; k < 2; k++) {
            t = lang(60);
            t->p[SL("trk.amp.dec")] = 30; t->p[SL("trk.amp.sus")] = 0;
            if (k) { t->p[SL("trk.lfo1.wave")] = LFO_SQR; t->p[SL("trk.lfo1.mode")] = LFO_TRIG;
                     t->p[SL("trk.lfo1.depth")] = 127; t->p[SL("trk.lfo1.dest")] = (int16_t)ziel("AMP SUS"); }
            e.start(); e.render(a, 10000);
            float r = rms(a + 8000 * 2, 2000);
            if (!k) ohne = r; else chk(r > ohne * 10.0f + 0.01f, "LFO auf AMP SUS haelt die Note nicht");
        }
    }

    grp("FX-Spur: Locks auf Projekt-Effekte je Step");
    {
        e.prepare(48000.0f); setupBeat(e, 60);
        Track& t = e.P.pat[0].t[0];
        for (int s = 0; s < 16; s++) t.s[s].on = 1;
        t.p[SL("trk.amp.sus")] = 100; t.p[SL("trk.len")] = 12;
        static const int gMaster = paramSlot(paramFind("master.vol"));
        Track& f = e.P.pat[0].fx;
        for (int s = 4; s < 8; s++) { f.s[s].on = 2; f.s[s].setLock(gMaster, 0); }
        e.start();
        static float a[96000 * 2];
        int done = 0; while (done < 96000) { int n = 96000 - done < 512 ? 96000 - done : 512; e.render(a + done * 2, n); done += n; }
        float vor = rms(a + 7000 * 2, 10000), still = rms(a + 31000 * 2, 10000), nach = rms(a + 55000 * 2, 10000);
        chk(vor > 0.01f, "vor dem Lock klingt nichts");
        chk(still < vor * 0.01f, "der Master-Lock der FX-Spur macht Steps 5-8 nicht still");
        chk(nach > vor * 0.5f, "nach dem Lock kehrt der Grundwert nicht zurueck");
        // Im Dokument, und Pattern kopieren nimmt die FX-Spur mit
        e.P.pat[0].fx.len = 32;
        static Doc d; d.clear(); projectToDoc(e.P, d);
        static Project Q; projectFromDoc(d, Q);
        chk(Q.pat[0].fx.len == 32 && Q.pat[0].fx.s[5].on == 2 && Q.pat[0].fx.s[5].findLock(gMaster)
            && Q.pat[0].fx.s[5].findLock(gMaster)->val == 0, "die FX-Spur ueberlebt das Dokument nicht");
        patternCopy(e.P, 0, 7);
        chk(e.P.pat[7].fx.s[6].findLock(gMaster) != nullptr, "Pattern kopieren vergisst die FX-Spur");
        patternClear(e.P, 7);
        chk(e.P.pat[7].fx.s[6].nLock == 0 && e.P.pat[7].fx.len == 16, "Pattern leeren laesst die FX-Spur stehen");
        e.P.pat[0].fx.len = 16;
    }

    grp("Machines mit acht Reglern, Ladder, Base-Width");
    {
        static float a[24000 * 2], b2[24000 * 2];
        const int N = 24000;
        auto ton = [&](int mach, int note, std::initializer_list<std::pair<const char*, int>> werte, float* out) {
            e.prepare(48000.0f); setupBeat(e, note);
            Track& t = e.P.pat[0].t[0];
            machineSelect(t.p, mach);
            for (int k = 0; k < MACH_KNOBS; k++) t.p[knobSlot(k)] = (int16_t)PARAMS[paramOfSlot(PS_TRACK, knobSlot(k))].def;
            t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100; t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 98;
            for (auto& w : werte) t.p[SL(w.first)] = (int16_t)w.second;
            e.start(); e.render(out, N);
        };
        // POLY: SUB legt eine Oktave darunter, OSC2 +12 eine darueber
        ton(0, 57, {}, a); ton(0, 57, {{"trk.src.e", 127}}, b2);
        chk(bin(b2, N, 110.0f, 48000.0f) > bin(a, N, 110.0f, 48000.0f) * 8.0f + 0.005f, "POLY SUB bringt keine Unteroktave");
        ton(0, 57, {{"trk.src.d", 64 + 32}}, b2);
        chk(bin(b2, N, 440.0f, 48000.0f) > bin(a, N, 440.0f, 48000.0f) * 1.5f, "POLY OSC2 +12 bringt keine Oktave darueber");
        // NOISE: breites Rauschen zwischen den Obertoenen
        ton(0, 57, {{"trk.src.f", 127}}, b2);
        chk(bin(b2, N, 5115.0f, 48000.0f) > bin(a, N, 5115.0f, 48000.0f) * 3.0f, "POLY NOISE rauscht nicht");
        // DRIFT ist Zufall - aber reproduzierbar: zweimal gleich, bei jeder Blockgroesse
        {
            ton(0, 57, {{"trk.src.g", 127}, {"trk.src.h", 90}}, a);
            e.prepare(48000.0f); setupBeat(e, 57);
            Track& t = e.P.pat[0].t[0];
            t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100; t.p[SL("trk.amp.dec")] = 127; t.p[SL("trk.len")] = 98;
            t.p[SL("trk.src.g")] = 127; t.p[SL("trk.src.h")] = 90;
            for (int k = 2; k < MACH_KNOBS; k++) if (k != 6 && k != 7) t.p[knobSlot(k)] = (int16_t)PARAMS[paramOfSlot(PS_TRACK, knobSlot(k))].def;
            e.start();
            int done = 0; while (done < N) { int n = N - done < 333 ? N - done : 333; e.render(b2 + done * 2, n); done += n; }
            int bad = 0; for (int i = 0; i < N * 2; i++) if (a[i] != b2[i]) bad++;
            chk(bad == 0, "DRIFT/DRIVE haengen von der Blockgroesse oder vom Zufall ab");
            chk(sane(a, N * 2), "DRIVE laeuft aus dem Ruder");
        }
        // FM: Algorithmus 8 ist additiv - LEVEL B laesst B1 (RATIO B 3:1) direkt klingen
        ton(1, 57, {{"trk.src.c", 7 * 16 + 1}, {"trk.src.e", 0}, {"trk.src.f", 5 * 8}}, a);
        ton(1, 57, {{"trk.src.c", 7 * 16 + 1}, {"trk.src.e", 127}, {"trk.src.f", 5 * 8}}, b2);
        chk(bin(b2, N, 660.0f, 48000.0f) > bin(a, N, 660.0f, 48000.0f) * 8.0f + 0.01f, "FM Algorithmus 8: B1 klingt nicht");
        // MOD DECAY: der Anschlag ist heller als der Ausklang - spaeter steckt mehr im Grundton
        ton(1, 45, {{"trk.src.a", 24}, {"trk.src.b", 100}, {"trk.src.h", 40}}, a);
        {
            float frueh = bin(a + 500 * 2, 3000, 110.0f, 48000.0f) / (rms(a + 500 * 2, 3000) + 1e-9f);
            float spaet = bin(a + 18000 * 2, 3000, 110.0f, 48000.0f) / (rms(a + 18000 * 2, 3000) + 1e-9f);
            chk(spaet > frueh * 1.5f, "FM MOD DECAY: der Anschlag ist nicht heller als der Ausklang");
        }
        // FEEDBACK bringt Obertoene in B2 (Algorithmus 8, nur B2 hoerbar ueber LEVEL B)
        ton(1, 45, {{"trk.src.c", 7 * 16 + 1}, {"trk.src.e", 127}, {"trk.src.g", 0}}, a);
        ton(1, 45, {{"trk.src.c", 7 * 16 + 1}, {"trk.src.e", 127}, {"trk.src.g", 127}}, b2);
        chk(bin(b2, N, 220.0f * 3, 48000.0f) > bin(a, N, 220.0f * 3, 48000.0f) * 3.0f + 0.002f, "FM FEEDBACK bringt keine Obertoene");
        // LADDER: zu ist dunkler als offen, und stabil auch mit voller Resonanz
        ton(0, 45, {{"trk.flt.type", FLT_LADDER}, {"trk.flt.freq", 127}}, a);
        ton(0, 45, {{"trk.flt.type", FLT_LADDER}, {"trk.flt.freq", 40}}, b2);
        chk(bin(b2, N, 2970.0f, 48000.0f) * 10.0f < bin(a, N, 2970.0f, 48000.0f), "LADDER filtert nicht");
        ton(0, 45, {{"trk.flt.type", FLT_LADDER}, {"trk.flt.freq", 70}, {"trk.flt.reso", 127}}, b2);
        chk(sane(b2, N * 2) && rms(b2, N) > 0.005f, "LADDER mit voller Resonanz ist kaputt oder stumm");
        // BASE-WIDTH: BASE nimmt den Grundton weg, WIDTH die Hoehen
        ton(0, 45, {}, a);
        ton(0, 45, {{"trk.bw.base", 60}}, b2);
        chk(bin(b2, N, 110.0f, 48000.0f) * 8.0f < bin(a, N, 110.0f, 48000.0f), "BASE schneidet den Bass nicht");
        ton(0, 45, {{"trk.bw.base", 20}, {"trk.bw.width", 30}}, b2);
        chk(bin(b2, N, 4070.0f, 48000.0f) * 8.0f < bin(a, N, 4070.0f, 48000.0f) + 1e-6f, "WIDTH schneidet die Hoehen nicht");
    }

    grp("Sampler: Loop, Reverse, Tune, Warp (Repitch und Time)");
    {
        // Sample: 0,5 s 440 Hz, dann 0,5 s 880 Hz
        static float smp[48000];
        for (int i = 0; i < 48000; i++) smp[i] = sinf(6.2831853f * (i < 24000 ? 440.0f : 880.0f) * i / 48000.0f) * 0.8f;
        static float b[72000 * 2];
        auto spiel = [&](std::initializer_list<std::pair<int, int>> knoepfe, int frames) {
            e.prepare(48000.0f);
            e.pool.set(0, smp, 48000, 48000.0f, "zwei", false);
            setupBeat(e, 60);
            Track& t = e.P.pat[0].t[0];
            for (int st = 1; st < 16; st++) t.s[st].on = 0;
            machineSelect(t.p, 3); t.p[SL("trk.smp")] = 0;
            t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100; t.p[SL("trk.amp.dec")] = 127;
            t.p[SL("trk.len")] = 107;                         // 22 Steps: laenger als alles hier
            for (auto& kv : knoepfe) t.p[knobSlot(kv.first)] = (int16_t)kv.second;
            e.P.g[SL("bpm")] = 120;
            e.start(); e.render(b, frames);
        };
        auto f = [&](int von, float hz) { return bin(b + von * 2, 6000, hz, 48000.0f); };
        auto stufe = [](int i, int n) { return i * 128 / n + 64 / n; };
        // vorwaerts: erst 440; REVERSE: erst 880
        spiel({}, 30000);
        chk(f(2000, 440) > f(2000, 880) * 8.0f, "vorwaerts beginnt das Sample nicht vorne");
        spiel({{4, stufe(1, 2)}}, 30000);
        chk(f(2000, 880) > f(2000, 440) * 8.0f, "REVERSE spielt nicht rueckwaerts");
        // TUNE +12: eine Oktave hoeher
        spiel({{3, 64 + 32}}, 12000);
        chk(f(2000, 880) > f(2000, 440) * 8.0f, "TUNE +12 stimmt nicht eine Oktave hoeher");
        // Ohne Loop ist nach 1 s Schluss, PING-PONG laeuft weiter
        spiel({{2, stufe(1, 4)}}, 72000);
        chk(rms(b + 56000 * 2, 8000) < 0.001f, "LOOP OFF spielt ueber das Ende hinaus");
        spiel({{2, stufe(3, 4)}}, 72000);
        chk(rms(b + 56000 * 2, 8000) > 0.05f, "PING-PONG haelt nicht an");
        chk(f(50000, 880) > f(50000, 440) * 4.0f, "PING-PONG laeuft nach dem Ende nicht rueckwaerts durch 880 Hz");
        // WARP: das 1-s-Sample soll 4 Beats dauern (2 s bei 120 bpm) - halbe Geschwindigkeit
        spiel({{5, stufe(1, 3)}, {6, stufe(4, 10)}}, 30000);       // REPITCH: Band - halbe Tonhoehe
        chk(f(2000, 220) > f(2000, 440) * 8.0f, "WARP REPITCH: halbe Geschwindigkeit klingt nicht eine Oktave tiefer");
        spiel({{5, stufe(2, 3)}, {6, stufe(4, 10)}}, 72000);       // TIME: Tonhoehe bleibt
        chk(f(2000, 440) > f(2000, 220) * 8.0f, "WARP TIME aendert die Tonhoehe");
        chk(f(36000, 440) > f(36000, 880) * 4.0f, "WARP TIME: nach 0,75 s ist das Sample nicht erst bei einem Viertel (440 Hz)");
        chk(f(60000, 880) > f(60000, 440) * 4.0f, "WARP TIME: nach 1,25 s ist das Sample nicht in der zweiten Haelfte (880 Hz)");
        chk(sane(b, 72000 * 2), "WARP TIME laeuft aus dem Ruder");
        // TIME mit 1 Beat: doppelt so schnell, Tonhoehe bleibt, nach 0,5 s ist Schluss
        spiel({{5, stufe(2, 3)}, {6, stufe(2, 10)}}, 48000);
        chk(f(2000, 440) > f(2000, 880) * 4.0f, "WARP TIME schnell: die Tonhoehe springt");
        chk(rms(b + 30000 * 2, 8000) < 0.01f, "WARP TIME schnell: das Sample dauert nicht nur einen Beat");
        // AUTO bei 120 bpm: 1 s = 2 Beats - REPITCH laesst es unveraendert
        spiel({{5, stufe(1, 3)}}, 12000);
        chk(f(2000, 440) > f(2000, 220) * 8.0f, "BEATS AUTO trifft die Laenge nicht");
    }

    grp("CHOP: Stuecke, FOLLOW STEP, Warp");
    {
        // vier Viertel: 220, 330, 440, 660 Hz, je 0,25 s
        static float smp[48000];
        const float fq[4] = {220, 330, 440, 660};
        for (int i = 0; i < 48000; i++) smp[i] = sinf(6.2831853f * fq[i / 12000] * i / 48000.0f) * 0.8f;
        static float b[48000 * 2];
        auto stufe = [](int i, int n) { return i * 128 / n + 64 / n; };
        auto spiel = [&](std::initializer_list<std::pair<int, int>> knoepfe, int frames, bool alleSteps, int noteLen = 12) {
            e.prepare(48000.0f);
            e.pool.set(0, smp, 48000, 48000.0f, "vier", false);
            setupBeat(e, 60);
            Track& t = e.P.pat[0].t[0];
            for (int st = 0; st < 16; st++) t.s[st].on = alleSteps || st == 0;
            machineSelect(t.p, 4); t.p[SL("trk.smp")] = 0;
            t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.amp.sus")] = 100; t.p[SL("trk.amp.dec")] = 127;
            t.p[SL("trk.len")] = (int16_t)noteLen; t.p[SL("trk.amp.rel")] = 0;
            for (auto& kv : knoepfe) t.p[knobSlot(kv.first)] = (int16_t)kv.second;
            e.start(); e.render(b, frames);
        };
        auto f = [&](int von, int n, float hz) { return bin(b + von * 2, n, hz, 48000.0f); };
        chk(!strcmp(machineAt(4)->name(), "CHOP"), "Machine 4 heisst nicht CHOP");
        // 4 Stuecke, Stueck 3 (Index 2) = 440 Hz
        spiel({{0, stufe(1, 6)}, {1, 2}}, 6000, false);
        chk(f(500, 4000, 440) > f(500, 4000, 220) * 8.0f && f(500, 4000, 440) > f(500, 4000, 660) * 8.0f, "SLICE 3 von 4 spielt nicht das dritte Stueck");
        // Ein Stueck endet nach seinem Viertel - es laeuft nicht ins naechste
        spiel({{0, stufe(1, 6)}, {1, 0}}, 18000, false);
        chk(rms(b + 13000 * 2, 4000) < 0.001f, "ein Stueck spielt ueber sein Ende hinaus");
        // FOLLOW STEP: Step n spielt Stueck n (4 Stuecke, Steps zu 6000 Samples)
        spiel({{0, stufe(1, 6)}, {2, stufe(1, 3)}}, 24000, true);
        chk(f(6500, 4000, 330) > f(6500, 4000, 220) * 8.0f, "FOLLOW STEP: Step 2 spielt nicht Stueck 2");
        chk(f(18500, 4000, 660) > f(18500, 4000, 440) * 8.0f, "FOLLOW STEP: Step 4 spielt nicht Stueck 4");
        // WARP TIME: das ganze Sample = 4 Beats (2 s) -> jedes Stueck dauert 0,5 s, Tonhoehe bleibt
        spiel({{0, stufe(1, 6)}, {1, 0}, {5, stufe(2, 3)}, {6, stufe(4, 10)}}, 30000, false, 98);
        chk(f(14000, 6000, 220) > f(14000, 6000, 330) * 8.0f, "CHOP WARP TIME: das erste Stueck dauert nicht laenger");
        chk(sane(b, 30000 * 2), "CHOP laeuft aus dem Ruder");
        char tx[32];
        machineAt(4)->text(1, 4, tx, sizeof tx); chk(!strcmp(tx, "5"), "SLICE 4 heisst nicht 5");
        machineAt(4)->text(0, stufe(3, 6), tx, sizeof tx); chk(!strcmp(tx, "16"), "SLICES Stufe 4 ist nicht 16");
    }

    grp("Song-Zeilen: Laenge, Start, Mutes, Tempo, Scene, LOOP, HALT");
    {
        // 120 bpm: ein Sechzehntel = 6000 Samples.
        e.prepare(48000.0f);
        for (int t = 0; t < NTRACKS; t++) e.mute[t] = false;   // fruehere Gruppen haben gemutet
        projectInit(e.P);
        for (int p = 0; p < 2; p++) {
            e.P.pat[p].used = 1;
            for (int t = 0; t < 2; t++) {
                Track& tr = e.P.pat[p].t[t];
                tr.p[SL("trk.note")] = (int16_t)(p == 0 ? 45 : 69);
                for (int st = 0; st < 16; st++) tr.s[st].on = 1;
                tr.p[SL("trk.len")] = 6;
            }
        }
        Song& sg = e.P.song;
        sg.n = 4;
        sg.e[0] = SongStep(); sg.e[0].pat = 0; sg.e[0].rep = 2; sg.e[0].len = 4;
        sg.e[1] = SongStep(); sg.e[1].pat = 1; sg.e[1].len = 8; sg.e[1].start = 4; sg.e[1].mute = 1 << 1; sg.e[1].morph = 101;
        sg.e[2] = SongStep(); sg.e[2].type = SONG_LOOP; sg.e[2].pat = 0; sg.e[2].rep = 1;
        sg.e[3] = SongStep(); sg.e[3].type = SONG_HALT;
        e.pat = 0;
        e.songStart(); e.start();
        static float b[1000 * 2];
        int64_t wechsel[8]; int zeile[8], nw = 0, vorher = e.songPos;
        int spur1InZeile1 = 0, spur0InZeile1 = 0;
        float morphInZeile1 = -1;
        int64_t haltPos = -1;
        for (int k = 0; k < 260; k++) {
            e.render(b, 1000);
            if (e.songPos != vorher && nw < 8) { wechsel[nw] = e.seq.pos; zeile[nw] = e.songPos; nw++; vorher = e.songPos; }
            if (e.songOn && e.songPos == 1) {
                morphInZeile1 = e.morph;
                for (int i = 0; i < NVOICES; i++) if (e.v[i].used && e.v[i].age <= 1000) {
                    if (e.v[i].track == 1) spur1InZeile1++; else spur0InZeile1++;
                }
            }
            if (!e.seq.running && haltPos < 0) haltPos = e.seq.pos;
        }
        chk(nw >= 1 && zeile[0] == 1 && wechsel[0] == 48000, "Zeile 1 (4 Steps x 2) wechselt nicht bei 48000");
        chk(nw >= 2 && zeile[1] == 0 && wechsel[1] == 96000, "Zeile 2 (Start 4, 8 Steps) endet nicht bei 96000 - oder LOOP springt nicht");
        chk(nw >= 3 && zeile[2] == 1 && wechsel[2] == 144000, "nach dem Sprung kommt Zeile 2 nicht wieder");
        chk(nw >= 4 && zeile[3] == 3, "nach dem LOOP kommt nicht HALT");
        chk(haltPos == 192000, "HALT haelt nicht genau am Ende an");
        chk(spur1InZeile1 == 0 && spur0InZeile1 > 0, "die Mutes der Zeile wirken nicht (oder muten alles)");
        chk(morphInZeile1 > 0.99f, "die Zeile stellt die Scene nicht auf B");
        // Start-Step: Zeile 2 beginnt bei Step 5 (Index 4) - der erste Anschlag dort ist Step 4
        e.prepare(48000.0f); e.songStart(); e.start();
        int ersterStep = -1;
        for (int k = 0; k < 60 && ersterStep < 0; k++) {
            e.render(b, 1000);
            if (e.songPos == 1) for (int i = 0; i < NVOICES; i++) if (e.v[i].used && e.v[i].track == 0 && e.v[i].age <= 1000) ersterStep = e.v[i].step;
        }
        chk(ersterStep == 4, "Zeile mit START 4 beginnt nicht bei Step 5");
        // Tempo der Zeile
        sg.n = 1; sg.e[0] = SongStep(); sg.e[0].pat = 0; sg.e[0].bpm = 90;
        e.prepare(48000.0f); e.songStart(); e.start();
        chk((int)(e.seq.bpm + 0.5) == 90 && (int)(e.tempoOf(e.pat) + 0.5) == 90, "das Tempo der Zeile gilt nicht");
        e.songStop();
        chk((int)(e.tempoOf(e.pat) + 0.5) == 120, "nach dem Song bleibt das Zeilentempo haengen");
        // Im Dokument
        sg.n = 2; sg.e[1] = SongStep(); sg.e[1].type = SONG_LOOP; sg.e[1].pat = 0; sg.e[1].rep = 0;
        sg.e[0].len = 12; sg.e[0].start = 3; sg.e[0].mute = 0x81; sg.e[0].morph = 51;
        static Doc d; d.clear(); projectToDoc(e.P, d);
        static Project Q; projectFromDoc(d, Q);
        chk(Q.song.n == 2 && Q.song.e[0].len == 12 && Q.song.e[0].start == 3 && Q.song.e[0].bpm == 90
            && Q.song.e[0].mute == 0x81 && Q.song.e[0].morph == 51 && Q.song.e[1].type == SONG_LOOP && Q.song.e[1].rep == 0,
            "die Song-Zeilen ueberleben das Dokument nicht");
        e.P.song.n = 0;
    }

    grp("Sampler ohne Knacksen: Slice-Start und -Ende werden ein- und ausgeblendet");
    {
        // Gleichspannung als Sample: jeder Sprung im Ausgang waere ein Knacks
        static float dc[48000];
        for (int i = 0; i < 48000; i++) dc[i] = 0.5f;
        e.prepare(48000.0f); e.pat = 0;
        projectInit(e.P); e.P.pat[0].used = 1;
        e.pool.clear(); e.pool.set(5, dc, 48000, 48000.0f, "dc");
        Track& t = e.P.pat[0].t[0];
        machineSelect(t.p, 4);                         // CHOP
        t.len = 16; t.speed = 4;
        t.p[SL("trk.smp")] = 5;
        t.p[SL("trk.src.a")] = 3 * 128 / 6 + 10;       // 16 Stuecke (je 62,5 ms, kuerzer als ein Step)
        t.p[SL("trk.src.b")] = 5;                      // Stueck 6: beginnt mitten im Sample
        t.p[SL("trk.amp.atk")] = 0; t.p[SL("trk.amp.sus")] = 127; t.p[SL("trk.amp.rel")] = 60;
        t.p[SL("trk.flt.freq")] = 127; t.p[SL("trk.send.a")] = 0; t.p[SL("trk.send.b")] = 0;
        t.s[0].on = 1;
        e.start();
        static float b[48000 * 2];
        e.render(b, 24000);
        float jump = 0.0f, peak = 0.0f;
        for (int i = 1; i < 24000; i++) {
            float d = fabsf(b[i * 2] - b[(i - 1) * 2]);
            if (d > jump) jump = d;
            if (fabsf(b[i * 2]) > peak) peak = fabsf(b[i * 2]);
        }
        printf("  Spitze %.3f, groesster Sprung zwischen zwei Samples %.4f\n", peak, jump);
        chk(peak > 0.1f, "Stueck klingt");
        chk(jump < peak * 0.05f, "kein Sprung am Anfang oder Ende des Stuecks (< 5 % der Spitze)");
        e.stop(); e.pool.clear();
    }

    grp("Referenz-Aufnahme");
    {
        const int N = 96000;                         // zwei Sekunden
        static float b[96000 * 2];
        e.prepare(48000.0f);
        e.pat = 0;                      // frueher Test liess ein anderes Pattern stehen
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
        (void)0;
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
