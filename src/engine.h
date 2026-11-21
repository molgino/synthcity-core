// Engine: verbindet Sequencer, Machines und Stimmen zu Klang.
// Drei Regeln gelten hier ueberall:
//   1. Im Render wird nichts angefordert, gesperrt oder geschrieben - nur gerechnet.
//   2. Ein Ereignis wirkt auf die SAMPLE genau: der Block wird an jeder Ereignis-Stelle geteilt.
//   3. Die Performance-Ebene (Mute, Fill, Step-Loop, Morph) aendert NIE die Daten.
#pragma once
#include "seq.h"
#include "machine.h"
#include "mod.h"
#include "fx.h"

enum { NVOICES = 32, CTL = 16, NRAMP = 8 };   // CTL: Steuerrate in Samples (Zweierpotenz)

// Ein gleitender Wert: Slides machen aus der Stufe zwischen zwei Locks eine Rampe.
struct Ramp { int16_t slot; float to, inc; int32_t left; };

struct Voice {
    bool     used = false;
    uint8_t  track = 0;
    uint8_t  step = 0;
    uint64_t born = 0;          // zum Stehlen der aeltesten Stimme
    uint32_t age = 0;           // Samples seit dem Anschlag - gibt die Steuerrate vor
    float    hz = 0.0f, vel = 0.0f;
    int64_t  left = 0;          // Samples bis Note-Off
    float    pf[64];            // wirksame Werte DIESER Stimme (Lock -> Scene -> Grundwert)
    float    pm[64];            // dieselben, nach LFOs und Mod-Huellkurve - daraus klingt es
    ModState mod;
    double   spb = 24000.0;     // vor start() gesetzt: Samples je Viertel
    int64_t  t0 = -1;           // vor start() gesetzt: Transport-Position des Anschlags
    uint64_t locked = 0;        // welche Slots dieser Step festgelegt hat - sie folgen nicht mehr
    Ramp     rmp[NRAMP];
    int      nRamp = 0;
    MState   ms;
    Svf      flt, fltR;         // der zweite nur fuer Stereo-Quellen
    Ladder   lad, ladR;         // Filtertyp LADDER
    Svf      bwHp, bwLp, bwHpR, bwLpR;   // Base-Width hinter dem Filter
    bool     bwOn = false;
    Env      env;
    float    panL = 0.7071f, panR = 0.7071f;
    float    base = 1000.0f, q = 0.7f, envAmt = 0.0f, gain = 1.0f;
    const SamplePool* pool = nullptr;
    bool     inf = false;       // Laenge INF: klingt bis zum naechsten Anschlag der Spur
    bool     preview = false;   // vom Sample-Editor angeschlagen, gehoert keiner Spur
    FltType  ft = FLT_LP;
    const Machine* mach = nullptr;

    // Welche Regler waehrend eines Tons weiterwirken: alles, was als modulierbar
    // gekennzeichnet ist (Filter, Pegel, Panorama, Sends, die Regler der Machine).
    // Note, Velocity, Laenge und Bedingung gehoeren dagegen zum Anschlag und stehen fest.
    struct Live { int n; int slot[32]; };
    static const Live& liveSlots() {
        static Live L = {0, {0}};
        if (!L.n)
            for (int i = 0; i < NPARAMS; i++)
                if (PARAMS[i].scope == PS_TRACK && (PARAMS[i].flags & PF_MOD) && L.n < 32)
                    L.slot[L.n++] = paramSlot(i);
        return L;
    }

    // Slots einmalig nachschlagen - Namen sind die Wahrheit, Zahlen nur die Abkuerzung.
    struct Slots { int note, atk, dec, sus, rel, fFreq, fReso, fType, fEnv, vol, pan; };
    static const Slots& S() {
        static const Slots s = {
            paramSlot(paramFind("trk.note")),
            paramSlot(paramFind("trk.amp.atk")),  paramSlot(paramFind("trk.amp.dec")),
            paramSlot(paramFind("trk.amp.sus")),  paramSlot(paramFind("trk.amp.rel")),
            paramSlot(paramFind("trk.flt.freq")), paramSlot(paramFind("trk.flt.reso")),
            paramSlot(paramFind("trk.flt.type")), paramSlot(paramFind("trk.flt.env")),
            paramSlot(paramFind("trk.vol")),      paramSlot(paramFind("trk.pan"))};
        return s;
    }

    void start(const Machine* mc, int tr, int st, const float* pp, float v,
               int64_t lenSamples, float sr, uint64_t when, uint64_t lockMask, const float* live,
               const SamplePool& pl) {
        mach = mc; pool = &pl; preview = false; track = (uint8_t)tr; step = (uint8_t)st; born = when;
        for (int i = 0; i < 64; i++) pf[i] = pp[i];
        locked = lockMask;
        nRamp = 0; vel = v; left = lenSamples; used = true; age = 0;
        inf = lenSamples >= 2147483647;
        liveFollow(live);
        const Slots& s = S();
        flt.reset(); fltR.reset(); lad.reset(); ladR.reset();
        bwHp.reset(); bwLp.reset(); bwHpR.reset(); bwLpR.reset();
        env.setup(pTime(pf[s.atk]), pTime(pf[s.dec]), pLevel(pf[s.sus]), pTime(pf[s.rel]), sr);
        env.kill(); env.gate();
        mod.start(pf, spb, t0, (uint32_t)when);
        ctl(sr, live);
        ms.seed = (uint32_t)when * 2654435761u + 0x9E3779B9u;   // reproduzierbarer Zufall je Anschlag
        mach->start(ms, hz, vel, pm, sr, *pool);
    }
    void release() { env.release(); left = -1; }
    // Abschneiden in ein paar Millisekunden - fuer MONO, damit nicht zwei Toene klingen,
    // aber ohne Knacken.
    void cut(float sr) { env.cr = Env::coef(0.004f, sr); env.release(); left = -1; }

    void slide(int slot, float from, float to, int32_t samples) {
        if (samples < 1) samples = 1;
        pf[slot] = from;
        for (int i = 0; i < nRamp; i++)
            if (rmp[i].slot == slot) { rmp[i] = {(int16_t)slot, to, (to - from) / samples, samples}; return; }
        if (nRamp < NRAMP) rmp[nRamp++] = {(int16_t)slot, to, (to - from) / samples, samples};
    }

    // Ein Regler, der nicht auf diesem Step gelockt ist und gerade nicht gleitet, folgt der
    // Spur weiter - sonst waere jeder Griff waehrend eines Tons wirkungslos.
    void liveFollow(const float* live) {
        if (!live) return;
        const Live& L = liveSlots();
        for (int i = 0; i < L.n; i++) {
            int sl = L.slot[i];
            if (locked & (1ull << sl)) continue;
            bool ramping = false;
            for (int r = 0; r < nRamp; r++) if (rmp[r].slot == sl) { ramping = true; break; }
            if (!ramping) pf[sl] = live[sl];
        }
    }

    // Steuerrate: hier werden Rampen bewegt und alle teuren Werte EINMAL gerechnet.
    void ctl(float sr, const float* live) {
        liveFollow(live);
        for (int i = 0; i < nRamp; ) {
            Ramp& r = rmp[i];
            r.left -= CTL;
            if (r.left <= 0) { pf[r.slot] = r.to; rmp[i] = rmp[--nRamp]; continue; }
            pf[r.slot] += r.inc * CTL;
            i++;
        }
        for (int i = 0; i < 64; i++) pm[i] = pf[i];
        const Slots& s = S();
        if (mod.apply(pf, pm, CTL, sr))          // Amp-Huellkurve moduliert: Zeiten neu rechnen
            env.setup(pTime(pm[s.atk]), pTime(pm[s.dec]), pLevel(pm[s.sus]), pTime(pm[s.rel]), sr);
        hz = noteHz(pm[s.note]);
        base = pFreq(pm[s.fFreq]);
        q = 0.7f + pUni(pm[s.fReso]) * 11.0f;
        envAmt = pBip(pm[s.fEnv]);
        ft = (FltType)(((int)(pm[s.fType] + 0.5f)) % FLT_N);
        gain = pLevel(pm[s.vol]) * vel;
        {   // Base-Width: Hochpass bei BASE, Tiefpass WIDTH Oktaven darueber
            static const int sB = paramSlot(paramFind("trk.bw.base")), sW = paramSlot(paramFind("trk.bw.width"));
            float bb = pm[sB], bw = pm[sW];
            bwOn = bb > 0.5f || bw < 126.5f;
            if (bwOn) {
                float hp = bb > 0.5f ? pFreq(bb) : 10.0f;
                float lp = bw < 126.5f ? hp * exp2f(bw * 10.0f / 127.0f) : sr * 0.49f;
                bwHp.set(hp, 0.707f, sr); bwLp.set(lp, 0.707f, sr);
                if (true) { bwHpR = bwHp.coefOnly(bwHpR); bwLpR = bwLp.coefOnly(bwLpR); }
            }
        }
        float a = 0.7853982f * (pBip(pm[s.pan]) + 1.0f);     // gleiche Leistung links wie rechts
        panL = cosf(a); panR = sinf(a);
    }

    // Drei Ziele: trocken, Sende A, Sende B. Die Sende-Anteile gehoeren zur Stimme,
    // damit ein P-Lock auf S.A auch wirklich nur diesen Anschlag weiterschickt.
    struct Mix { float *dL, *dR, *aL, *aR, *bL, *bR; };

    void run(const Mix& mx, int n, float sr, const float* live) {
        if (!used) return;
        static const int slotSA = paramSlot(paramFind("trk.send.a"));
        static const int slotSB = paramSlot(paramFind("trk.send.b"));
        float sa = pLevel(pm[slotSA]), sb = pLevel(pm[slotSB]);
        float buf[MACH_CHUNK], bufR[MACH_CHUNK];
        const bool st = mach->stereo(pf, *pool);
        int done = 0;
        while (done < n) {
            int c = n - done; if (c > MACH_CHUNK) c = MACH_CHUNK;
            mach->render(ms, pm, hz, buf, st ? bufR : nullptr, c, sr, *pool);
            for (int i = 0; i < c; i++) {
                if (left > 0) { if (--left == 0) release(); }
                float e = env.tick();
                // Gezaehlt wird an der STIMME, nicht am Block - sonst haengt der Klang davon ab,
                // wie der Host die Bloecke schneidet.
                if ((age & (CTL - 1)) == 0) {
                    ctl(sr, live);
                    float fc = base * exp2f(envAmt * e * 6.0f);          // bis zu sechs Oktaven
                    if (ft == FLT_LADDER) { lad.set(fc, q, sr); ladR.G = lad.G; ladR.k = lad.k; }
                    else {
                        flt.set(fc, q, sr);
                        if (st) { fltR.a1 = flt.a1; fltR.a2 = flt.a2; fltR.a3 = flt.a3; fltR.k = flt.k; }
                    }
                }
                age++;
                float eg = e * gain;
                float xl, xr;
                if (st) {
                    float l = ft == FLT_LADDER ? lad.run(buf[i]) : flt.run(buf[i], ft);
                    float r = ft == FLT_LADDER ? ladR.run(bufR[i]) : fltR.run(bufR[i], ft);
                    if (bwOn) { l = bwLp.run(bwHp.run(l, FLT_HP), FLT_LP); r = bwLpR.run(bwHpR.run(r, FLT_HP), FLT_LP); }
                    xl = l * eg * panL; xr = r * eg * panR;
                } else {
                    float x = ft == FLT_LADDER ? lad.run(buf[i]) : flt.run(buf[i], ft);
                    if (bwOn) x = bwLp.run(bwHp.run(x, FLT_HP), FLT_LP);
                    x *= eg; xl = x * panL; xr = x * panR;
                }
                mx.dL[done + i] += xl;        mx.dR[done + i] += xr;
                mx.aL[done + i] += xl * sa;   mx.aR[done + i] += xr * sa;
                mx.bL[done + i] += xl * sb;   mx.bR[done + i] += xr * sb;
            }
            done += c;
            if (env.done()) { used = false; return; }
        }
    }
};

struct Engine : Sink {
    Project  P;
    Seq      seq;
    Voice    v[NVOICES];
    float    sr = 48000.0f;
    int      pat = 0;
    uint64_t clock = 0;
    int      fxStep = -1;       // zuletzt gespielter Step der FX-Spur (fuer die Anzeige)

    // --- Performance-Ebene: wirkt sofort, steht in keiner Datei ---------------------------
    bool  mute[NTRACKS] = {false};
    float morph = 0.0f;           // 0 = Scene A, 1 = Scene B
    float live[NTRACKS][64];      // was die Spur GERADE sagt - klingende Stimmen folgen dem
    // Fuer Slides: wo die Spur gerade steht. Nicht aus der klingenden Stimme gelesen - die
    // kann laengst verklungen sein, wenn der naechste Trig kommt.
    float trkVal[NTRACKS][64];

    Delay      delay;
    Reverb     reverb;
    SamplePool pool;

    // --- Aufnahme in einen Platz des Vorrats -----------------------------------------------
    // Aufgenommen wird, was hinten herauskommt. Am Geraet kommt spaeter der Eingang als
    // zweite Quelle dazu; der Weg hier bleibt derselbe.
    bool  recOn = false;
    int   recSlot = -1, recMax = 0, recCh = 2;

    // --- Pattern-Wechsel ---------------------------------------------------------------------
    // Gewaehlt wird sofort, gewechselt wird im Takt: alles andere klingt wie ein Versehen.
    int     patNext = -1;          // wartet auf den Wechsel

    int64_t switchAt = -1;         // genaue Sample-Position des Wechsels

    // --- Song --------------------------------------------------------------------------------
    // Der Song benutzt denselben Wechsel wie ein Tipp auf ein Pattern - nur entscheidet er
    // selbst, wann. Gewechselt wird erst nach allen Wiederholungen, und zwar ohne das Pattern
    // dazwischen neu anzufangen: eine Spur mit 12 Steps laeuft ueber die Wiederholungen
    // hinweg weiter gegen die 16.
    bool songOn = false;
    int  songPos = 0;              // welcher Eintrag gerade spielt
    bool songArmed = false;        // der anstehende Wechsel kommt vom Song

    Ev   q[256];
    int  qoff[256];
    int  nq = 0;
    // Arbeitspuffer gehoeren zur Engine, nicht auf den Stapel: im Browser sind davon
    // nur 64 KB da, und sechs Puffer zu 4096 Samples waeren mehr.
    float bufD[2][4096], bufA[2][4096], bufB[2][4096];

    Engine() { projectInit(P); resetTrackValues(); }

    void resetTrackValues() {
        for (int t = 0; t < NTRACKS; t++)
            for (int i = 0; i < 64; i++) trkVal[t][i] = (float)P.pat[pat].t[t].p[i];
    }

    // Vorbereiten heisst: alles steht still. Sonst laeuft die Uhr nach einem Neuaufbau weiter,
    // ohne dass jemand sie gestartet hat.
    void prepare(float rate) {
        sr = rate; seq.sr = rate;
        seq.stop();
        patNext = -1; switchAt = -1; songArmed = false;
        recStop();
        for (int i = 0; i < NVOICES; i++) { v[i].used = false; v[i].env.kill(); }
        clock = 0;                                   // Zufall der Stimmen (Drift) beginnt gleich
        delay.reset(); reverb.reset();
        resetTrackValues();
    }
    void start() {
        if (songOn) { seq.stop(); songStart(); }         // der Song faengt vorn an
        seq.bpm = effTempo(pat);
        seq.start();
    }

    // Platz reservieren und scharf stellen. Gibt die moegliche Laenge in Bildern zurueck, 0 = kein Platz.
    int recStart(int slot, float seconds, const char* name, int ch = 2) {
        recStop();
        int frames = (int)(seconds * sr);
        if (frames < 1) frames = 1;
        if (pool.reserve(slot, frames, sr, name, ch) < 0) return 0;
        recSlot = slot; recMax = frames; recCh = ch < 2 ? 1 : 2; recOn = true;
        return frames;
    }
    void recStop() { recOn = false; recSlot = -1; }
    int  recFrames() const { return (recSlot >= 0) ? pool.s[recSlot].n : 0; }
    void stop()  { seq.stop(); for (int i = 0; i < NVOICES; i++) v[i].release(); }

    // --- Tempo -------------------------------------------------------------------------------
    // Ein Tempowechsel im Lauf darf die Position nicht verschieben. Die Zeit eines Steps ist
    // Ursprung + k * Steplaenge; aendert sich die Steplaenge, liegen alle schon vergangenen k
    // ploetzlich woanders, und der Sequencer holt sie auf einen Schlag nach (ein Druck auf
    // BPM + feuerte sieben Anschlaege in einem Block). Darum wird der Ursprung so verschoben,
    // dass die AKTUELLE Position im Takt gleich bleibt - und alles schon Geplante mit.
    void setTempo(double bpm) {
        if (bpm < 2.5) bpm = 2.5;           // 20 bpm mal 1/8x
        if (bpm > 800.0) bpm = 800.0;       // 400 bpm mal 2x
        double alt = seq.bpm;
        if (bpm == alt) return;
        if (seq.running && alt > 0.0) {
            double f = alt / bpm;                              // neue Laenge je alter Laenge
            int64_t pos = seq.pos;
            seq.origin = pos - (int64_t)llround((double)(pos - seq.origin) * f);
            for (int i = 0; i < seq.nPend; i++)
                seq.pend[i].at = pos + (int64_t)llround((double)(seq.pend[i].at - pos) * f);
            if (switchAt > pos) switchAt = pos + (int64_t)llround((double)(switchAt - pos) * f);
        }
        seq.bpm = bpm;
    }
    // Teiler einer Spur aendern. Im Lauf wuerde die Spur sonst alle Steps nachholen, die
    // mit dem neuen Teiler "schon gewesen waeren" - darum setzt sie dort fort, wo sie auf
    // dem neuen Raster gerade steht.
    void setDiv(int t, int d) {
        if (t < 0 || t >= NTRACKS) return;
        if (d < 1) d = 1;
        if (d > 16) d = 16;
        Track& tr = P.pat[pat].t[t];
        tr.speed = (uint8_t)d;
        if (seq.running) {
            double sp = seq.stepLen(tr);
            double rel = (double)(seq.pos - seq.origin);
            int64_t k = (int64_t)(rel / sp);
            if ((double)k * sp < rel) k++;                   // aufrunden, ohne libm
            seq.nextK[t] = k < 0 ? 0 : k;
        }
    }

    // Das Tempo, das gerade gilt: das eigene des Patterns, sonst das des Projekts.
    // Das ist das MUSIKALISCHE Tempo - so steht es in der Anzeige, und daran haengt das Echo.
    double tempoOf(int p) const {
        int pb = (p >= 0 && p < NPATTERNS) ? P.pat[p].bpm : 0;
        return pb > 0 ? (double)pb : (double)P.g[paramSlot(paramFind("bpm"))];
    }
    // Swing, der gerade gilt: der eigene des Patterns, sonst der des Projekts (50..80 %).
    int swingOf(int p) const {
        int s = (p >= 0 && p < NPATTERNS) ? P.pat[p].swing : 0;
        if (!s) s = P.g[paramSlot(paramFind("swing"))];
        return s < 50 ? 50 : (s > 80 ? 80 : s);
    }
    double multOf(int p) const {
        int m = (p >= 0 && p < NPATTERNS) ? P.pat[p].mult : SPEED_1X;
        return SPEEDS[(m < 0 || m >= NSPEEDS) ? SPEED_1X : m].mult;
    }
    // Wie schnell der Sequencer laeuft: Tempo mal Geschwindigkeit des Patterns. Eine andere
    // Geschwindigkeit ist fuer den Sequencer dasselbe wie ein anderes Tempo - darum gilt
    // fuer sie auch dasselbe Umrechnen in setTempo, ohne Geknatter.
    double effTempo(int p) const { return tempoOf(p) * multOf(p); }

    // Laenge eines Wechsel-Takts: so viele Steps, wie das laufende Pattern als Wechsellaenge
    // angibt, gemessen in Sechzehnteln seiner eigenen Geschwindigkeit.
    double cycleLen() const {
        double sp16 = seq.sr * 60.0 / ((seq.bpm < 2.5 ? 2.5 : seq.bpm) * 4.0);
        int chg = P.pat[pat].chg;
        return sp16 * (chg < 1 ? 1 : chg);
    }

    // Song an. Steht die Uhr, beginnt Eintrag 1 sofort; laeuft sie, am naechsten Taktende -
    // ein Sprung mitten im Takt klaenge wie ein Fehler.
    void songStart() {
        if (!P.song.n) { songOn = false; return; }
        songOn = true;
        if (seq.running) {
            songPos = P.song.n - 1;                    // als ginge der letzte Eintrag gerade zu Ende
            patNext = P.song.e[0].pat;
            double cyc = cycleLen();
            int64_t k = (int64_t)((double)(seq.pos - seq.origin) / cyc) + 1;
            switchAt = seq.origin + (int64_t)llround((double)k * cyc);
            songArmed = true;
            return;
        }
        songPos = 0; songArmed = false;
        patNext = -1; switchAt = -1;
        pat = P.song.e[0].pat;
        seq.toPattern(seq.pos);
        seq.bpm = effTempo(pat);
        resetTrackValues();
    }
    void songStop() { songOn = false; if (songArmed) { patNext = -1; switchAt = -1; songArmed = false; } }

    // Den naechsten Wechsel des Songs einplanen: am Ende der letzten Wiederholung.
    void songPlan() {
        if (!songOn || !P.song.n || patNext >= 0 || !seq.running) return;
        if (songPos >= P.song.n) songPos = 0;
        int rep = P.song.e[songPos].rep < 1 ? 1 : P.song.e[songPos].rep;
        int nx = (songPos + 1) % P.song.n;
        patNext = P.song.e[nx].pat;
        switchAt = seq.origin + (int64_t)llround(cycleLen() * rep);
        if (switchAt <= seq.pos) switchAt = seq.pos;      // verspaetet: sofort
        songArmed = true;
    }

    // Vorhoeren im Sample-Editor: der Platz klingt so, wie er ist - offenes Filter, volle
    // Lautstaerke, Note 60 - unabhaengig davon, was eine Spur gerade eingestellt hat.
    // Alle Slots gelten als gelockt, damit keine Spur ihre Regler darueberlegt.
    void preview(int slot, float seconds = 6.0f) {
        previewStop();
        if (pool.empty(slot)) return;
        float snap[64];
        for (int i = 0; i < 64; i++) snap[i] = 0.0f;
        for (int i = 0; i < NPARAMS; i++)
            if (PARAMS[i].scope == PS_TRACK) snap[paramSlot(i)] = PARAMS[i].def;
        int16_t tmp[64];
        for (int i = 0; i < 64; i++) tmp[i] = (int16_t)snap[i];
        machineSelect(tmp, 3);
        snap[paramSlot(paramFind("trk.machine"))] = 3;
        snap[paramSlot(paramFind("trk.src.a"))] = tmp[paramSlot(paramFind("trk.src.a"))];
        snap[paramSlot(paramFind("trk.src.b"))] = tmp[paramSlot(paramFind("trk.src.b"))];
        snap[paramSlot(paramFind("trk.smp"))] = (float)slot;
        snap[paramSlot(paramFind("trk.note"))] = 60;
        snap[paramSlot(paramFind("trk.flt.freq"))] = 127;
        snap[paramSlot(paramFind("trk.amp.sus"))] = 100;
        snap[paramSlot(paramFind("trk.amp.dec"))] = 127;
        snap[paramSlot(paramFind("trk.amp.rel"))] = 20;
        snap[paramSlot(paramFind("trk.vol"))] = 100;
        snap[paramSlot(paramFind("trk.send.a"))] = 0;
        snap[paramSlot(paramFind("trk.send.b"))] = 0;
        int i = pick();
        v[i].start(machineAt(3), 0, 0, snap, 0.85f, (int64_t)(sr * seconds), sr, clock++,
                   ~0ull, live[0], pool);
        v[i].preview = true;
    }
    void previewStop() { for (int i = 0; i < NVOICES; i++) if (v[i].used && v[i].preview) v[i].release(); }
    bool previewing() const { for (int i = 0; i < NVOICES; i++) if (v[i].used && v[i].preview) return true; return false; }

    // Ein Pattern waehlen. Steht die Uhr, gilt es sofort; laeuft sie, am naechsten Taktende.
    // Wer von Hand waehlt, verlaesst den Song.
    void queuePattern(int p) {
        if (songOn) songStop();
        if (p < 0 || p >= NPATTERNS) return;
        if (!seq.running) {
            pat = p; patNext = -1; switchAt = -1;
            seq.toPattern(seq.pos);
            seq.bpm = effTempo(p);
            resetTrackValues();
            return;
        }
        patNext = p;
        double cyc = cycleLen();
        double rel = (double)(seq.pos - seq.origin);
        int64_t k = (int64_t)(rel / cyc) + 1;
        switchAt = seq.origin + (int64_t)llround((double)k * cyc);
    }
    int  patPlaying() const { return pat; }

    void ev(const Ev& e, int offset) override {
        if (nq < 256) { q[nq] = e; qoff[nq] = offset; nq++; }
    }

    int pick() {
        for (int i = 0; i < NVOICES; i++) if (!v[i].used) return i;
        int best = 0; uint64_t old = ~0ull;                  // sonst die aelteste stehlen
        for (int i = 0; i < NVOICES; i++) if (v[i].born < old) { old = v[i].born; best = i; }
        return best;
    }

    // Wirksame Werte eines Steps: Lock -> Morph zwischen den Scenes -> Grundwert.
    void snapshot(int t, int step, float* out) const {
        const Pattern& pt = P.pat[pat];
        for (int i = 0; i < 64; i++) out[i] = (float)pt.t[t].p[i];
        for (int i = 0; i < NPARAMS; i++) {
            if (PARAMS[i].scope != PS_TRACK) continue;
            int sl = paramSlot(i);
            out[sl] = effectiveF(pt, t, step, sl, morph);
        }
    }

    // Anschlag-Ziele der Modulation: der LFO-Wert im Moment des Trigs (siehe ModState::trigOffset).
    float trigMod(int track, int step, int slot, int64_t at) override {
        int d = modDestOfSlot(slot);
        if (d < 0 || !MOD_DESTS[d].trig) return 0.0f;
        float snap[64];
        snapshot(track, step, snap);
        double spb = sr * 60.0 / (tempoOf(pat) > 1 ? tempoOf(pat) : 120.0);
        return ModState::trigOffset(snap, d, at, spb, 0x51u + (uint32_t)track * 977u);
    }

    void fire(const Ev& e) {
        const Track& tr = P.pat[pat].t[e.track];
        float snap[64];
        snapshot(e.track, e.step, snap);
        int32_t glide = (int32_t)seq.stepLen(tr);
        if (e.kind == EV_TRIG) {
            static const char* const beimAnschlag[] = {"trk.smp", "trk.quant", "trk.nt2", "trk.nt3", "trk.nt4"};
            double spb = sr * 60.0 / (tempoOf(pat) > 1 ? tempoOf(pat) : 120.0);
            for (const char* id : beimAnschlag) {
                int pi = paramFind(id), sl = paramSlot(pi);
                float o = ModState::trigOffset(snap, modDestOfSlot(sl), e.at, spb, 0x51u + (uint32_t)e.track * 977u);
                if (o == 0.0f) continue;
                float v = (float)(int)(snap[sl] + o + (snap[sl] + o < 0 ? -0.5f : 0.5f));
                snap[sl] = v < PARAMS[pi].lo ? PARAMS[pi].lo : (v > PARAMS[pi].hi ? PARAMS[pi].hi : v);
            }
        }

        if (e.kind == EV_LOCKS) {            // trigless: nur Werte, kein Anschlag
            for (int i = 0; i < NVOICES; i++) {
                if (!v[i].used || v[i].track != e.track) continue;
                for (int li = 0; li < tr.s[e.step].nLock; li++) {
                    const Lock& l = tr.s[e.step].lock[li];
                    // Auch ein trigless gesetzter Wert ist festgelegt - sonst holt ihn die
                    // Spur beim naechsten Steuertakt wieder zurueck.
                    v[i].locked |= 1ull << l.slot;
                    if (l.slide) v[i].slide(l.slot, trkVal[e.track][l.slot], (float)l.val, glide);
                    else         v[i].pf[l.slot] = (float)l.val;
                }
            }
            for (int i = 0; i < 64; i++) trkVal[e.track][i] = snap[i];
            return;
        }
        float from[64];
        for (int i = 0; i < 64; i++) { from[i] = trkVal[e.track][i]; trkVal[e.track][i] = snap[i]; }
        if (mute[e.track]) return;           // stumm heisst: kein Klang, der Lauf geht weiter

        uint64_t lockMask = 0;
        for (int li = 0; li < tr.s[e.step].nLock; li++) lockMask |= 1ull << tr.s[e.step].lock[li].slot;
        // Tonart: folgt die Spur der Skala, rastet die Note auf den naechsten Ton der Leiter ein.
        static const int sNote = paramSlot(paramFind("trk.note"));
        static const int sQuant = paramSlot(paramFind("trk.quant"));
        const Pattern& ptq = P.pat[pat];
        const bool rastet = ptq.scale > 0 && snap[sQuant] > 0.5f;
        if (rastet) snap[sNote] = (float)scaleQuant((int)(snap[sNote] + 0.5f), ptq.root, ptq.scale);
        int mi = tr.p[paramSlot(paramFind("trk.machine"))];
        static const int sMode = paramSlot(paramFind("trk.vmode"));
        static const int sNt[3] = {paramSlot(paramFind("trk.nt2")), paramSlot(paramFind("trk.nt3")),
                                   paramSlot(paramFind("trk.nt4"))};
        const int mode = (int)(snap[sMode] + 0.5f);          // 0 POLY, 1 MONO, 2 LEGATO

        // LEGATO: klingt die Spur noch, wird der Ton nur umgestimmt - die Huellkurve laeuft
        // weiter, kein neuer Anschlag. Ein kurzes Gleiten verbindet die beiden Noten.
        if (mode == 2) {
            for (int k = 0; k < NVOICES; k++) {
                Voice& w = v[k];
                if (!w.used || w.preview || w.track != e.track || w.env.st == Env::REL) continue;
                w.slide(sNote, w.pf[sNote], snap[sNote], (int32_t)(sr * 0.03f));
                w.left = e.len;
                w.inf = e.len >= 2147483647;
                w.step = e.step;
                for (int k2 = k + 1; k2 < NVOICES; k2++)       // was sonst noch klingt, endet
                    if (v[k2].used && !v[k2].preview && v[k2].track == e.track) v[k2].cut(sr);
                return;
            }
        }
        // INF-Noten der Spur enden mit diesem Anschlag (in jedem Modus).
        for (int k = 0; k < NVOICES; k++)
            if (v[k].used && v[k].inf && !v[k].preview && v[k].track == e.track && v[k].env.st != Env::REL)
                { v[k].release(); v[k].inf = false; }
        // MONO und LEGATO: eine Stimme je Spur - die vorige wird abgeschnitten.
        if (mode >= 1)
            for (int k = 0; k < NVOICES; k++)
                if (v[k].used && !v[k].preview && v[k].track == e.track) v[k].cut(sr);

        // Akkord: die Grundnote und bis zu drei weitere im Abstand NT2..NT4. Nur im POLY-Modus.
        // Folgt die Spur einer Tonart, zaehlen die Abstaende in STUFEN der Leiter: NT2 +2 ist die
        // Terz der Tonart, +4 die Quinte - so bleibt jeder Akkord leitereigen, auf jedem Grundton.
        float noten[4];
        int nNoten = 0;
        noten[nNoten++] = snap[sNote];
        if (mode == 0)
            for (int c = 0; c < 3; c++) {
                int o = (int)(snap[sNt[c]] + 0.5f);
                if (o <= 0) continue;
                int nt = rastet ? scaleDegrees((int)(snap[sNote] + 0.5f), o - 25, ptq.root, ptq.scale)
                                : (int)(snap[sNote] + 0.5f) + (o - 25);
                if (nt < 0 || nt > 127) continue;
                noten[nNoten++] = (float)nt;
            }
        for (int c = 0; c < nNoten; c++) {
            snap[sNote] = noten[c];
            int i = pick();
            v[i].spb = sr * 60.0 / (tempoOf(pat) > 1 ? tempoOf(pat) : 120.0);
            v[i].t0 = e.at;
            v[i].start(machineAt(mi), e.track, e.step, snap, e.vel / 127.0f, e.len, sr, clock++,
                       lockMask, live[e.track], pool);
            for (int li = 0; li < tr.s[e.step].nLock; li++) {    // Slides: von dort, wo die Spur steht
                const Lock& l = tr.s[e.step].lock[li];
                if (!l.slide) continue;
                float ziel = (float)l.val;
                float von = from[l.slot];
                if (l.slot == sNote) {                           // Akkordnoten gleiten parallel mit
                    if (rastet) ziel = (float)scaleQuant(l.val, ptq.root, ptq.scale);
                    ziel += noten[c] - noten[0];
                    von += noten[c] - noten[0];
                }
                v[i].slide(l.slot, von, ziel, glide);
            }
            if (v[i].nRamp) v[i].ctl(sr, live[e.track]);
        }
        snap[sNote] = noten[0];
    }

    // Was die Spuren gerade sagen - einmal je Block, nicht je Stimme. Darueber wirken
    // sowohl ein gedrehter Regler als auch der Morph-Regler auf schon klingende Toene.
    void updateLive() {
        const Pattern& pt = P.pat[pat];
        const Voice::Live& L = Voice::liveSlots();
        for (int t = 0; t < NTRACKS; t++)
            for (int i = 0; i < L.n; i++) live[t][L.slot[i]] = sceneValue(pt, t, L.slot[i], morph);
    }

    // Mitschreiben, was gerade herauskommt - in den reservierten Bereich, ohne Umweg.
    void recWrite(const float* stereoOut, int n) {
        SamplePool::Slot& sl = pool.s[recSlot];
        for (int i = 0; i < n && sl.n < recMax; i++) {
            float l = stereoOut[i * 2], r = stereoOut[i * 2 + 1];
            if (recCh == 2) { pool.data[sl.at + sl.n * 2] = l; pool.data[sl.at + sl.n * 2 + 1] = r; }
            else            { pool.data[sl.at + sl.n] = 0.5f * (l + r); }
            sl.n++;
        }
        if (sl.n >= recMax) recStop();          // die Laenge stand vorher fest
    }

    // out ist stereo-verschraenkt (L,R,L,R...) und wird ueberschrieben.
    // Steht ein Pattern-Wechsel an, wird der Block an GENAU der Stelle geteilt - sonst haenge
    // der Wechsel davon ab, wie der Host schneidet.
    void render(float* out, int frames) {
        int done = 0;
        songPlan();
        while (done < frames) {
            int c = frames - done;
            if (patNext >= 0 && switchAt > seq.pos && switchAt - seq.pos < (int64_t)c)
                c = (int)(switchAt - seq.pos);
            renderPart(out + done * 2, c);
            done += c;
            if (patNext >= 0 && seq.pos >= switchAt) {
                pat = patNext; patNext = -1; switchAt = -1;
                seq.toPattern(seq.pos);
                seq.bpm = effTempo(pat);              // Ursprung ist frisch - kein Umrechnen noetig
                resetTrackValues();
                if (songArmed) {
                    songArmed = false;
                    songPos = P.song.n ? (songPos + 1) % P.song.n : 0;
                    songPlan();                          // gleich den naechsten einplanen
                }
            }
        }
    }

    // Welcher Step der FX-Spur gilt gerade? -1: keiner (gestoppt) - dann gelten die Grundwerte.
    int fxStepNow() const {
        if (!seq.running) return -1;
        const Track& f = P.pat[pat].fx;
        double sp = seq.stepLen(f);
        if (sp < 1.0 || seq.pos < seq.origin) return -1;
        int64_t k = (int64_t)((double)(seq.pos - seq.origin) / sp);
        int len = f.len < 1 ? 1 : f.len;
        return (int)(k % len);
    }
    // Projektwert, wie er JETZT klingt: Lock der FX-Spur, sonst der Grundwert.
    int16_t gNow(int slot, int fxStep) const {
        if (fxStep >= 0) {
            const Lock* l = P.pat[pat].fx.s[fxStep].findLock(slot);
            if (l) return l->val;
        }
        return P.g[slot];
    }

    void renderPart(float* out, int frames) {
        for (int i = 0; i < frames * 2; i++) out[i] = 0.0f;
        nq = 0;
        const int fs = fxStepNow();                   // am Blockanfang - Effekte sind traege genug
        fxStep = fs;
        seq.swing = swingOf(pat) / 100.0;
        seq.process(P, pat, frames, *this);
        updateLive();
        float master = pLevel(gNow(paramSlot(paramFind("master.vol")), fs));

        // Effekte: Einstellungen einmal je Block, nicht je Sample.
        static const int sDiv  = paramSlot(paramFind("fx.a.div"));
        static const int sFb   = paramSlot(paramFind("fx.a.fb"));
        static const int sTone = paramSlot(paramFind("fx.a.tone"));
        static const int sAVol = paramSlot(paramFind("fx.a.vol"));
        static const int sSize = paramSlot(paramFind("fx.b.size"));
        static const int sDec  = paramSlot(paramFind("fx.b.dec"));
        static const int sDamp = paramSlot(paramFind("fx.b.damp"));
        static const int sBVol = paramSlot(paramFind("fx.b.vol"));
        delay.setTime(gNow(sDiv, fs), (float)tempoOf(pat), sr);   // Echo am musikalischen Tempo
        reverb.setSize(pUni(gNow(sSize, fs)), sr);
        float fb   = pUni(gNow(sFb, fs)) * 0.95f;
        float tone = pUni(gNow(sTone, fs));
        float dec  = pUni(gNow(sDec, fs));
        float damp = pUni(gNow(sDamp, fs));
        float aVol = pLevel(gNow(sAVol, fs)), bVol = pLevel(gNow(sBVol, fs));

        int done = 0, ei = 0;
        while (done < frames) {
            int next = frames;
            while (ei < nq && qoff[ei] <= done) { fire(q[ei]); ei++; }
            if (ei < nq && qoff[ei] < next) next = qoff[ei];
            int c = next - done; if (c > 4096) c = 4096; if (c <= 0) c = 1;
            for (int i = 0; i < c; i++) {
                bufD[0][i] = bufD[1][i] = 0.0f;
                bufA[0][i] = bufA[1][i] = 0.0f;
                bufB[0][i] = bufB[1][i] = 0.0f;
            }
            Voice::Mix mx{bufD[0], bufD[1], bufA[0], bufA[1], bufB[0], bufB[1]};
            for (int i = 0; i < NVOICES; i++) v[i].run(mx, c, sr, live[v[i].track]);
            delay.run(bufA[0], bufA[1], bufD[0], bufD[1], c, fb, tone, aVol);
            reverb.run(bufB[0], bufB[1], bufD[0], bufD[1], c, dec, damp, bVol);
            for (int i = 0; i < c; i++) {
                out[(done + i) * 2]     = softClip(bufD[0][i] * master);
                out[(done + i) * 2 + 1] = softClip(bufD[1][i] * master);
            }
            if (recOn) recWrite(out + done * 2, c);
            done += c;
        }
    }
};
