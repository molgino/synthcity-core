// Beweise fuer Meilenstein 1. Jede Pruefung steht fuer eine Zusage aus dem Konzept.
#include "../src/model.h"
#include "../src/seq.h"
#include "../src/queue.h"
#include "../src/sample.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>

static int pass = 0, fail = 0;
static const char* curCase = "";
#define CHECK(c, ...) do { if (c) pass++; else { fail++; printf("  FEHLER [%s] ", curCase); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Rec : Sink {
    struct R { int64_t at; int track, step, note, vel; EvKind kind; int off; };
    std::vector<R> r;
    int64_t base = 0;
    void ev(const Ev& e, int off) override { r.push_back({e.at, e.track, e.step, e.note, e.vel, e.kind, off}); }
};

static void demoPattern(Project& P) {
    projectInit(P);
    P.pat[0].used = 1;
    Track& tr = P.pat[0].t[0];
    tr.len = 16;
    for (int s = 0; s < 16; s += 4) tr.s[s].on = 1;
}

// 1 - Das Dokument: speichern, laden, Fremdes ueberlebt, Neues bekommt seinen Standard
static void testDoc() {
    curCase = "Dokument";
    static Project P; demoPattern(P);
    P.g[paramSlot(paramFind("bpm"))] = 137;
    Track& tr = P.pat[0].t[0];
    tr.p[paramSlot(paramFind("trk.flt.freq"))] = 90;
    tr.s[4].setLock(paramSlot(paramFind("trk.flt.freq")), 20, 1);
    tr.s[8].on = 2;

    static Doc d; projectToDoc(P, d);
    // eine ID, die diese Programmversion NICHT kennt - sie stammt aus einer spaeteren
    d.set("pat.0.trk.0.zukunft.regler", 42);
    CHECK(d.save("/tmp/sc-test.txt"), "speichern fehlgeschlagen");

    static Doc d2; CHECK(d2.load("/tmp/sc-test.txt"), "laden fehlgeschlagen");
    static Project Q; projectFromDoc(d2, Q);
    CHECK(Q.g[paramSlot(paramFind("bpm"))] == 137, "Tempo nicht wiederhergestellt (%d)", Q.g[paramSlot(paramFind("bpm"))]);
    CHECK(Q.pat[0].t[0].p[paramSlot(paramFind("trk.flt.freq"))] == 90, "Spurwert nicht wiederhergestellt");
    const Lock* l = Q.pat[0].t[0].s[4].findLock(paramSlot(paramFind("trk.flt.freq")));
    CHECK(l && l->val == 20 && l->slide == 1, "Lock mit Slide nicht wiederhergestellt");
    CHECK(Q.pat[0].t[0].s[8].on == 2, "trigless Step nicht wiederhergestellt");
    CHECK(Q.pat[0].t[0].s[0].on == 1 && Q.pat[0].t[0].s[1].on == 0, "Steps nicht wiederhergestellt");

    // Zurueckschreiben: die unbekannte ID ist noch da
    static Doc d3; projectToDoc(Q, d3);
    for (int i = 0; i < d2.n; i++) if (!d3.has(d2.e[i].k)) d3.set(d2.e[i].k, d2.e[i].v);   // was wir nicht kennen, bleibt
    CHECK(d3.get("pat.0.trk.0.zukunft.regler", -1) == 42, "unbekannte ID ueberlebt das Laden nicht");
}

// 2 - Ein Projekt laedt, NACHDEM ein Parameter dazugekommen ist: das Versprechen des Dokuments
static void testNeuerParameter() {
    curCase = "Projekt ueberlebt neue Parameter";
    static Project P; demoPattern(P);
    static Doc alt; projectToDoc(P, alt);
    alt.save("/tmp/sc-alt.txt");
    // Die "alte" Datei kennt trk.flt.reso nicht, weil es dort auf dem Standard stand.
    // Eine neue Version mit mehr Parametern laedt sie trotzdem - fehlende bekommen den Standard.
    static Doc d; d.load("/tmp/sc-alt.txt");
    static Project Q; projectFromDoc(d, Q);
    int slot = paramSlot(paramFind("trk.flt.reso"));
    CHECK(Q.pat[0].t[0].p[slot] == PARAMS[paramFind("trk.flt.reso")].def,
          "fehlender Parameter bekommt nicht seinen Standardwert (%d)", Q.pat[0].t[0].p[slot]);
    CHECK(!d.has("pat.0.trk.0.flt.reso"), "Standardwerte stehen unnoetig in der Datei");
}

// 3 - Der Scheduler: gleiche Events, egal wie der Host die Bloecke schneidet
static void renderBlocks(Project& P, int block, int frames, Rec& rec, double bpm = 120.0) {
    Seq s; s.sr = 48000; s.bpm = bpm; s.start();
    for (int done = 0; done < frames; done += block) {
        int n = frames - done < block ? frames - done : block;
        s.process(P, 0, n, rec);
    }
}
static void testBlockgroessen() {
    curCase = "Blockgroesse aendert nichts";
    static Project P; demoPattern(P);
    const int frames = 48000 * 4;
    Rec a, b, c;
    renderBlocks(P, 64, frames, a);
    renderBlocks(P, 1, frames, b);
    renderBlocks(P, 977, frames, c);
    CHECK(a.r.size() == 8, "4 s bei 120 BPM = 8 Trigs, nicht %zu", a.r.size());
    CHECK(a.r.size() == b.r.size() && a.r.size() == c.r.size(),
          "unterschiedlich viele Events: %zu / %zu / %zu", a.r.size(), b.r.size(), c.r.size());
    size_t n = a.r.size() < b.r.size() ? a.r.size() : b.r.size();
    bool same = true;
    for (size_t i = 0; i < n; i++) if (a.r[i].at != b.r[i].at || a.r[i].at != c.r[i].at) same = false;
    CHECK(same, "Events liegen bei anderer Blockgroesse woanders");
    // 120 BPM, 16tel = 0,125 s = 6000 Samples
    CHECK(a.r.size() >= 2 && a.r[1].at - a.r[0].at == 4 * 6000, "Abstand der Trigs falsch (%lld)",
          (long long)(a.r[1].at - a.r[0].at));
}

// 4 - Micro-Timing nach vorn: der Sonderfall von frueher gibt es nicht mehr
static void testMicro() {
    curCase = "Micro-Timing";
    static Project P; demoPattern(P);
    Track& tr = P.pat[0].t[0];
    int slot = paramSlot(paramFind("trk.micro"));
    tr.s[4].setLock(slot, 12);    // -50 % eines Steps
    tr.s[8].setLock(slot, 36);    // +50 %
    Rec r; renderBlocks(P, 128, 48000 * 2, r);
    CHECK(r.r.size() >= 3, "zu wenige Events");
    int64_t sp = 6000;
    CHECK(r.r[1].at == 4 * sp - sp / 2, "Step 4 nicht eine halbe Stepdauer FRUEHER (%lld)", (long long)r.r[1].at);
    CHECK(r.r[2].at == 8 * sp + sp / 2, "Step 8 nicht eine halbe Stepdauer spaeter (%lld)", (long long)r.r[2].at);
    for (size_t i = 0; i < r.r.size(); i++) CHECK(r.r[i].off >= 0, "negativer Versatz im Block");
}

// 5 - Conditions, Wahrscheinlichkeit als Condition, PRE sieht durch
static void testConditions() {
    curCase = "Conditions";
    {   // 1ST / !1ST ueber zwei Durchlaeufe
        static Project P; projectInit(P); P.pat[0].used = 1;
        Track& tr = P.pat[0].t[0]; tr.len = 4;
        tr.s[0].on = 1; tr.s[0].setLock(paramSlot(paramFind("trk.cond")), CND_1ST);
        tr.s[2].on = 1; tr.s[2].setLock(paramSlot(paramFind("trk.cond")), CND_N1ST);
        Rec r; renderBlocks(P, 256, 6000 * 8, r);
        int n1 = 0, n2 = 0;
        for (auto& e : r.r) { if (e.step == 0) n1++; if (e.step == 2) n2++; }
        CHECK(n1 == 1, "1ST spielt %d mal statt einmal", n1);
        CHECK(n2 == 1, "!1ST spielt %d mal statt einmal (zweiter Durchlauf)", n2);
    }
    {   // A:B - jeder zweite Durchlauf
        static Project P; projectInit(P); P.pat[0].used = 1;
        Track& tr = P.pat[0].t[0]; tr.len = 4;
        tr.s[0].on = 1; tr.s[0].setLock(paramSlot(paramFind("trk.cond")), condAB(1, 2, false));
        Rec r; renderBlocks(P, 256, 6000 * 16, r);
        CHECK(r.r.size() == 2, "1:2 spielt %zu mal statt zweimal in vier Durchlaeufen", r.r.size());
    }
    {   // Wahrscheinlichkeit IST eine Condition: PRE folgt ihr
        static Project P; projectInit(P); P.pat[0].used = 1;
        Track& tr = P.pat[0].t[0]; tr.len = 4;
        tr.s[0].on = 1; tr.s[0].setLock(paramSlot(paramFind("trk.cond")), condProb(50));
        tr.s[1].on = 1;                                                     // ohne Condition - PRE sieht hindurch
        tr.s[2].on = 1; tr.s[2].setLock(paramSlot(paramFind("trk.cond")), CND_PRE);
        Rec r; renderBlocks(P, 256, 6000 * 4 * 40, r);
        int a = 0, c = 0, both = 0, loops = 0;
        bool sawA = false;
        for (auto& e : r.r) {
            if (e.step == 0) { a++; sawA = true; }
            if (e.step == 2) { c++; if (sawA) both++; }
            if (e.step == 1) { if (!sawA) loops++; sawA = false; }
        }
        CHECK(a > 5 && a < 35, "50 %% Wahrscheinlichkeit trifft %d von 40 mal", a);
        CHECK(c == a, "PRE folgt dem Zufall nicht (%d Zufall, %d PRE)", a, c);
    }
}

// 6 - Polymetrik: zwei Spuren mit verschiedener Laenge laufen auseinander
static void testPolymetrik() {
    curCase = "Polymetrik";
    static Project P; projectInit(P); P.pat[0].used = 1;
    P.pat[0].t[0].len = 16; P.pat[0].t[0].s[0].on = 1;
    P.pat[0].t[1].len = 12; P.pat[0].t[1].s[0].on = 1;
    Rec r; renderBlocks(P, 512, 6000 * 48, r);
    int n0 = 0, n1 = 0;
    for (auto& e : r.r) (e.track == 0 ? n0 : n1)++;
    CHECK(n0 == 3, "16er-Spur spielt %d mal in 48 Steps", n0);
    CHECK(n1 == 4, "12er-Spur spielt %d mal in 48 Steps", n1);
}

// 7 - Ratchets: ein Step wird in gleiche Teile geteilt
static void testRatchet() {
    curCase = "Ratchets";
    static Project P; projectInit(P); P.pat[0].used = 1;
    Track& tr = P.pat[0].t[0]; tr.len = 4;
    tr.s[0].on = 1; tr.s[0].setLock(paramSlot(paramFind("trk.ratchet")), 3);
    Rec r; renderBlocks(P, 128, 6000 * 4, r);
    CHECK(r.r.size() == 3, "%zu Anschlaege statt 3", r.r.size());
    if (r.r.size() == 3) CHECK(r.r[1].at == 2000 && r.r[2].at == 4000, "Ratchets nicht gleich verteilt (%lld, %lld)",
                               (long long)r.r[1].at, (long long)r.r[2].at);
}

// 8 - Die Warteschlange: Reihenfolge bleibt, voll laeuft sie nicht ueber
// Scenes: zwei Werte-Ebenen je Spur, dazwischen faehrt der Morph-Regler.
static void testScenes() {
    curCase = "Scenes und Morph";
    const int sl = paramSlot(paramFind("trk.flt.freq"));
    static Project P; demoPattern(P);
    P.pat[0].sc[0][0].set(sl, 30);
    P.pat[0].sc[1][0].set(sl, 110);
    P.pat[0].t[0].p[sl] = 70;

    CHECK(sceneValue(P.pat[0], 0, sl, 0.0f) == 30.0f, "Morph 0 ist nicht Scene A");
    CHECK(sceneValue(P.pat[0], 0, sl, 1.0f) == 110.0f, "Morph 1 ist nicht Scene B");
    CHECK(sceneValue(P.pat[0], 0, sl, 0.5f) == 70.0f, "die Mitte liegt nicht dazwischen");
    // Spur 1 hat keine Scene - dort bleibt der Grundwert stehen
    CHECK(sceneValue(P.pat[0], 1, sl, 0.7f) == (float)P.pat[0].t[1].p[sl], "Spur ohne Scene wandert");
    // Nur EINE Seite belegt: dann wird gegen den Grundwert gefahren
    static Project R; demoPattern(R);
    R.pat[0].t[0].p[sl] = 60;
    R.pat[0].sc[1][0].set(sl, 120);
    CHECK(sceneValue(R.pat[0], 0, sl, 0.0f) == 60.0f, "halbe Scene: A ist nicht der Grundwert");
    CHECK(sceneValue(R.pat[0], 0, sl, 1.0f) == 120.0f, "halbe Scene: B fehlt");
    // Ein Wert laesst sich wieder entfernen - dann gilt dort der Grundwert.
    static Project S2; demoPattern(S2);
    S2.pat[0].t[0].p[sl] = 77;
    S2.pat[0].sc[0][0].set(sl, 10);
    CHECK(S2.pat[0].sc[0][0].clear(sl), "ein Scene-Wert laesst sich nicht entfernen");
    CHECK(!S2.pat[0].sc[0][0].clear(sl), "ein schon entfernter Wert wird nochmal entfernt");
    CHECK(sceneValue(S2.pat[0], 0, sl, 0.0f) == 77.0f, "nach dem Entfernen gilt nicht der Grundwert");
    // Ein Lock schlaegt die Scene
    P.pat[0].t[0].s[4].setLock(sl, 15);
    CHECK(effectiveF(P.pat[0], 0, 4, sl, 1.0f) == 15.0f, "Lock verliert gegen die Scene");

    static Doc d; projectToDoc(P, d);
    CHECK(d.save("/tmp/sc-scene.txt"), "speichern fehlgeschlagen");
    static Doc d2; CHECK(d2.load("/tmp/sc-scene.txt"), "laden fehlgeschlagen");
    static Project Q; projectFromDoc(d2, Q);
    const Lock* a = Q.pat[0].sc[0][0].find(sl);
    const Lock* b = Q.pat[0].sc[1][0].find(sl);
    CHECK(a && a->val == 30, "Scene A ueberlebt den Rundlauf nicht");
    CHECK(b && b->val == 110, "Scene B ueberlebt den Rundlauf nicht");
    CHECK(Q.pat[0].sc[0][1].n == 0, "eine Spur ohne Scene bekommt beim Laden Werte");
}

// Namen im Dokument: der Vorrat merkt sich, welche Samples ein Projekt verlangt.
static void testSampleDoc() {
    curCase = "Samples im Dokument";
    static Doc d; d.clear();
    d.set("bpm", 128);
    d.setStr("smp.0.name", "kick 909");
    CHECK(d.get("bpm", 0) == 128, "Zahl verloren");
    CHECK(!strcmp(d.getStr("smp.0.name", "?"), "kick 909"), "Name verloren");
    CHECK(!strcmp(d.getStr("gibtsnicht", "-"), "-"), "fehlender Name liefert keinen Standard");
    CHECK(d.isNumber("-12") && !d.isNumber("12a") && !d.isNumber(""), "Zahlentest falsch");
    CHECK(d.save("/tmp/sc-smp.txt"), "speichern fehlgeschlagen");
    static Doc d2; CHECK(d2.load("/tmp/sc-smp.txt"), "laden fehlgeschlagen");
    CHECK(d2.get("bpm", 0) == 128, "Zahl ueberlebt den Rundlauf nicht");
    CHECK(!strcmp(d2.getStr("smp.0.name", "?"), "kick 909"), "Name ueberlebt den Rundlauf nicht");

    static SamplePool pool;
    pool.clear();
    static float t[64];
    for (int i = 0; i < 64; i++) t[i] = 0.5f;
    CHECK(pool.set(2, t, 32, 44100.0f, "snare", false, 2), "Stereo-Sample abgelehnt");
    CHECK(pool.s[2].ch == 2 && pool.s[2].n == 32, "Stereo falsch abgelegt");
    static Doc d3; d3.clear();
    poolToDoc(pool, d3);
    CHECK(!strcmp(d3.getStr("smp.2.name", ""), "snare"), "Name nicht im Dokument");
    CHECK(d3.get("smp.2.rate", 0) == 44100 && d3.get("smp.2.ch", 0) == 2, "Daten nicht im Dokument");

    // Ein leerer Vorrat laedt das Projekt: der Platz wird zum Wunsch, nicht zum Klang.
    static SamplePool leer;
    leer.clear();
    poolFromDoc(d3, leer);
    CHECK(leer.wanted(2), "der verlangte Platz ist kein Wunsch");
    CHECK(leer.empty(2), "ein Wunsch gilt als belegt");
    CHECK(!strcmp(leer.s[2].name, "snare") && leer.s[2].ch == 2, "der Wunsch kennt den Namen nicht");
    CHECK(!leer.wanted(0), "ein unbenutzter Platz wird zum Wunsch");

    // Was schon da ist und denselben Namen traegt, bleibt liegen.
    poolFromDoc(d3, pool);
    CHECK(pool.s[2].n == 32 && !pool.wanted(2), "ein vorhandenes Sample wird weggeworfen");
}

// Tonart: einrasten und stufenweise gehen.
static void testScale() {
    curCase = "Tonart";
    const int DUR = 1, PMOLL = 9;
    CHECK(scaleQuant(60, 0, DUR) == 60, "C liegt nicht in C-Dur");
    CHECK(scaleQuant(61, 0, DUR) == 60, "C# rastet nicht auf C ein (Gleichstand: der tiefere)");
    CHECK(scaleQuant(66, 0, DUR) == 65, "F# rastet nicht auf F ein");
    CHECK(scaleQuant(61, 0, 0) == 61, "chromatisch rastet etwas ein");
    CHECK(scaleStep(64, 0, DUR, +1) == 65, "von E eine Stufe hoch ist nicht F");
    CHECK(scaleStep(60, 0, DUR, -1) == 59, "von C eine Stufe runter ist nicht H");
    CHECK(scaleStep(57, 9, PMOLL, +1) == 60, "A-Moll-Pentatonik: von A hoch ist nicht C");
    CHECK(scaleStep(60, 0, 0, +1) == 61, "chromatisch ist eine Stufe kein Halbton");
    // Jede Tonleiter: einrasten liefert immer einen Ton der Leiter, auch in anderen Tonarten
    bool alle = true;
    for (int sc = 0; sc < NSCALES; sc++)
        for (int root = 0; root < 12; root++)
            for (int n = 20; n < 110; n++)
                if (!scaleHas(scaleQuant(n, root, sc), root, sc)) alle = false;
    CHECK(alle, "einrasten liefert einen Ton ausserhalb der Leiter");
}

static void testQueue() {
    curCase = "Befehls-Warteschlange";
    Queue<8> q;
    for (int i = 0; i < 8; i++) CHECK(q.push({Cmd::PARAM, (int16_t)i, 0, 0, i * 10}), "push %d abgelehnt", i);
    CHECK(!q.push({Cmd::PARAM, 9, 0, 0, 0}), "volle Queue nimmt noch an");
    for (int i = 0; i < 8; i++) { Cmd c; CHECK(q.pop(c) && c.a == i && c.v == i * 10, "Reihenfolge bei %d", i); }
    Cmd c; CHECK(!q.pop(c), "leere Queue gibt etwas heraus");
}

int main() {
    testDoc();
    testNeuerParameter();
    testBlockgroessen();
    testMicro();
    testConditions();
    testPolymetrik();
    testRatchet();
    testScenes();
    testSampleDoc();
    testScale();
    testQueue();
    printf("%s: %d Pruefungen ok, %d Fehler\n", fail ? "FEHLGESCHLAGEN" : "OK", pass, fail);
    return fail ? 1 : 0;
}
