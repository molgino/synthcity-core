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
#include "insert.h"
#include "master.h"

enum { NVOICES = 32, CTL = 16, NRAMP = 8 };   // CTL: Steuerrate in Samples (Zweierpotenz)

// Ein gleitender Wert: Slides machen aus der Stufe zwischen zwei Locks eine Rampe.
struct Ramp { int16_t slot; float to, inc; int32_t left; };

struct VoiceLocks {
    uint64_t bits[(VOICE_PARAM_CAP + 63) / 64] = {};
    void set(int slot) { bits[slot / 64] |= 1ull << (slot % 64); }
    bool has(int slot) const { return (bits[slot / 64] & (1ull << (slot % 64))) != 0; }
    static VoiceLocks all() {
        VoiceLocks result;
        for (auto& word : result.bits) word = ~0ull;
        return result;
    }
};

struct Voice {
    bool     used = false;
    uint8_t  track = 0;
    uint8_t  step = 0;
    uint64_t born = 0;          // zum Stehlen der aeltesten Stimme
    uint32_t age = 0;           // Samples seit dem Anschlag - gibt die Steuerrate vor
    float    hz = 0.0f, vel = 0.0f;
    int64_t  left = 0;          // Samples bis Note-Off
    float    pf[VOICE_PARAM_CAP];            // wirksame Werte DIESER Stimme (Lock -> Scene -> Grundwert)
    float    pm[VOICE_PARAM_CAP];            // dieselben, nach LFOs und Mod-Huellkurve - daraus klingt es
    ModState mod;
    double   spb = 24000.0;     // vor start() gesetzt: Samples je Viertel
    int64_t modTime = -1;
    int64_t  t0 = -1;           // vor start() gesetzt: Transport-Position des Anschlags
    VoiceLocks locked;        // welche Slots dieser Step festgelegt hat - sie folgen nicht mehr
    Ramp     rmp[NRAMP];
    int      nRamp = 0;
    MState   ms;
    Svf      flt, fltR;         // der zweite nur fuer Stereo-Quellen
    Ladder   lad, ladR;         // Filtertyp LADDER
    Svf      bwHp, bwLp, bwHpR, bwLpR;   // Base-Width hinter dem Filter
    bool     bwOn = false;
    Env      env, filterEnv;
    bool     filterAdsr = false;
    int      machineIndex = 0;
    float    panL = 0.7071f, panR = 0.7071f;
    float    base = 1000.0f, q = 0.7f, envAmt = 0.0f, gain = 1.0f;
    const SamplePool* pool = nullptr;
    bool     inf = false;       // Laenge INF: klingt bis zum naechsten Anschlag der Spur
    bool     preview = false;   // vom Sample-Editor angeschlagen, gehoert keiner Spur
    FltType  ft = FLT_LP;
    const Machine* mach = nullptr;
    // Zwischenspeicher der Steuerrate: teure Funktionen (pow, tan, sin/cos) nur, wenn sich ihr
    // Eingang geaendert hat. Gleicher Eingang, gleiches Ergebnis - der Klang bleibt bitgleich.
    struct Memo { float in = -1e30f, out = 0.0f; };
    Memo mHz, mBase, mGain, mReso, mEnv;
    float cPanIn = -1e30f, cFc = -1e30f, cQ = -1e30f, cBb = -1e30f, cBw = -1e30f, cSr = 0.0f;
    int   cFt = -1;

    // Welche Regler waehrend eines Tons weiterwirken: alles, was als modulierbar
    // gekennzeichnet ist (Filter, Pegel, Panorama, Sends, die Regler der Machine).
    // Note, Velocity, Laenge und Bedingung gehoeren dagegen zum Anschlag und stehen fest.
    struct Live { int n; int slot[VOICE_PARAM_CAP]; int machine[VOICE_PARAM_CAP]; };
    static const Live& liveSlots() {
        static Live L = {};
        if (!L.n)
            for (int i = 0; i < NPARAMS; i++)
                if (PARAMS[i].scope == PS_TRACK && (PARAMS[i].flags & (PF_MOD | PF_LIVE))) {
                    L.slot[L.n] = paramSlot(i);
                    L.machine[L.n++] = !(PARAMS[i].flags & PF_LIVE) ? -1 : sourceParamVisible(PARAMS[i].id, 0) ? 0 : 1;
                }
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
               int64_t lenSamples, float sr, uint64_t when, VoiceLocks lockMask, const float* live,
               const SamplePool& pl) {
        mHz = Memo(); mBase = Memo(); mGain = Memo(); cPanIn = cFc = cQ = cBb = cBw = -1e30f; cFt = -1; cSr = 0.0f;
        mach = mc; pool = &pl;
        static const int sMachine = paramSlot(paramFind("trk.machine"));
        machineIndex = (int)pp[sMachine]; preview = false; track = (uint8_t)tr; step = (uint8_t)st; born = when;
        for (int i = 0; i < VOICE_PARAM_CAP; i++) pf[i] = pp[i];
        locked = lockMask;
        nRamp = 0; vel = v; left = lenSamples; used = true; age = 0;
        inf = lenSamples >= 2147483647;
        liveFollow(live);
        const Slots& s = S();
        flt.reset(); fltR.reset(); lad.reset(); ladR.reset();
        bwHp.reset(); bwLp.reset(); bwHpR.reset(); bwLpR.reset();
        env.setup(pTime(pf[s.atk]), pTime(pf[s.dec]), pLevel(pf[s.sus]), pTime(pf[s.rel]), sr);
        env.kill(); env.gate();
        static const int sMode = paramSlot(paramFind("trk.flt.envmode"));
        filterAdsr = pf[sMode] > 0.5f;
        if (filterAdsr) {
            static const AdsrSlots filter = adsrSlots("trk.flt");
            filter.setup(filterEnv, pf, sr);
            filterEnv.kill();
            filterEnv.gate();
        }
        mod.start(pf, spb, t0, (uint32_t)when, 0x51u + (uint32_t)tr * 977u, modTime);
        ctl(sr, live);
        ms.seed = (uint32_t)when * 2654435761u + 0x9E3779B9u;   // reproduzierbarer Zufall je Anschlag
        ms.step = (uint8_t)st;
        mach->start(ms, hz, vel, pm, sr, *pool);
    }
    void release() {
        env.release();
        if (filterAdsr) filterEnv.release();
        if (mach) mach->release(ms);
        left = -1;
    }
    // Abschneiden in ein paar Millisekunden - fuer MONO, damit nicht zwei Toene klingen,
    // aber ohne Knacken.
    void cut(float sr) { env.cr = Env::coef(0.004f, sr); release(); }

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
            if (L.machine[i] >= 0 && L.machine[i] != machineIndex) continue;
            if (locked.has(sl)) continue;
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
        memcpy(pm, pf, sizeof pm);
        const Slots& s = S();
        if (mod.apply(pf, pm, CTL, sr))          // Amp-Huellkurve moduliert: Zeiten neu rechnen
            env.setup(pTime(pm[s.atk]), pTime(pm[s.dec]), pLevel(pm[s.sus]), pTime(pm[s.rel]), sr);
        if (pm[s.note] != mHz.in)    { mHz.in = pm[s.note];    mHz.out = noteHz(mHz.in); }
        if (pm[s.fFreq] != mBase.in) { mBase.in = pm[s.fFreq]; mBase.out = pFreq(mBase.in); }
        if (pm[s.vol] != mGain.in)   { mGain.in = pm[s.vol];   mGain.out = pLevel(mGain.in); }
        hz = mHz.out;
        base = mBase.out;
        q = 0.7f + pUni(pm[s.fReso]) * 11.0f;
        envAmt = pBip(pm[s.fEnv]);
        ft = (FltType)(((int)(pm[s.fType] + 0.5f)) % FLT_N);
        gain = mGain.out * vel;
        {   // Base-Width: Hochpass bei BASE, Tiefpass WIDTH Oktaven darueber
            static const int sB = paramSlot(paramFind("trk.bw.base")), sW = paramSlot(paramFind("trk.bw.width"));
            float bb = pm[sB], bw = pm[sW];
            bwOn = bb > 0.5f || bw < 126.5f;
            if (bwOn && (bb != cBb || bw != cBw || sr != cSr)) {
                cBb = bb; cBw = bw; cSr = sr;
                float hp = bb > 0.5f ? pFreq(bb) : 10.0f;
                float lp = bw < 126.5f ? hp * exp2f(bw * 10.0f / 127.0f) : sr * 0.49f;
                bwHp.set(hp, 0.707f, sr); bwLp.set(lp, 0.707f, sr);
                if (true) { bwHpR = bwHp.coefOnly(bwHpR); bwLpR = bwLp.coefOnly(bwLpR); }
            }
        }
        if (pm[s.pan] != cPanIn) {
            cPanIn = pm[s.pan];
            float a = 0.7853982f * (pBip(pm[s.pan]) + 1.0f);     // gleiche Leistung links wie rechts
            panL = cosf(a); panR = sinf(a);
        }
    }

    // Drei Ziele: trocken, Sende A, Sende B. Die Sende-Anteile gehoeren zur Stimme,
    // damit ein P-Lock auf S.A auch wirklich nur diesen Anschlag weiterschickt.
    struct Mix { float *dL, *dR, *aL, *aR, *bL, *bR; };

    void run(const Mix& mx, int n, float sr, const float* live) {
        if (!used) return;
        static const int slotSA = paramSlot(paramFind("trk.send.a"));
        static const int slotSB = paramSlot(paramFind("trk.send.b"));
        float sa = pLevel(pm[slotSA]), sb = pLevel(pm[slotSB]);
        float buf[MACH_CHUNK], bufR[MACH_CHUNK], oL[MACH_CHUNK], oR[MACH_CHUNK];
        const bool st = mach->stereo(pf, *pool);
        const bool aligned = ModState::extended(pf, 0) || ModState::extended(pf, 1) || pf[modSlots().eMode] > 0.5f;
        const bool machineEnv = mach->usesGate(ms);
        int done = 0;
        while (done < n) {
            if (machineEnv && left == 1) release();
            int limit = aligned ? CTL - (age & (CTL - 1)) : MACH_CHUNK;
            int c = n - done; if (c > limit) c = limit;
            if (machineEnv && left > 1 && c >= left) c = (int)left - 1;
            if (!aligned) mach->render(ms, pm, hz, buf, st ? bufR : nullptr, c, sr, *pool);
            for (int i = 0; i < c; i++) {
                if (left > 0) { if (--left == 0) release(); }
                float e = env.tick();
                const float fe = filterAdsr ? filterEnv.tick() : e;
                // Gezaehlt wird an der STIMME, nicht am Block - sonst haengt der Klang davon ab,
                // wie der Host die Bloecke schneidet.
                if ((age & (CTL - 1)) == 0) {
                    ctl(sr, live);
                    float fc = envAmt != 0.0f ? base * exp2f(envAmt * fe * 6.0f) : base;   // bis zu sechs Oktaven
                    // Koeffizienten (tanf) nur, wenn sich Eckfrequenz, Resonanz oder Typ geaendert haben
                    if (fc != cFc || q != cQ || (int)ft != cFt || sr != cSr) {
                        cFc = fc; cQ = q; cFt = (int)ft; cSr = sr;
                        if (ft == FLT_LADDER) { lad.set(fc, q, sr); ladR.G = lad.G; ladR.k = lad.k; }
                        else {
                            flt.set(fc, q, sr);
                            if (st) { fltR.a1 = flt.a1; fltR.a2 = flt.a2; fltR.a3 = flt.a3; fltR.k = flt.k; }
                        }
                    }
                }
                // For the new modes, render only after the current voice control tick.
                // Pitch and machine knobs then hold for the same samples in every host block.
                if (aligned && i == 0) {
                    mach->render(ms, pm, hz, buf, st ? bufR : nullptr, c, sr, *pool);
                    sa = pLevel(pm[slotSA]); sb = pLevel(pm[slotSB]);
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
                oL[i] = xl; oR[i] = xr;
            }
            // Mischen: Sende-Wege nur, wenn sie offen sind (x * 0 dazuzaehlen aendert nichts)
            float* dL = mx.dL + done; float* dR = mx.dR + done;
            for (int i = 0; i < c; i++) { dL[i] += oL[i]; dR[i] += oR[i]; }
            if (sa != 0.0f) { float* aL = mx.aL + done; float* aR = mx.aR + done; for (int i = 0; i < c; i++) { aL[i] += oL[i] * sa; aR[i] += oR[i] * sa; } }
            if (sb != 0.0f) { float* bL = mx.bL + done; float* bR = mx.bR + done; for (int i = 0; i < c; i++) { bL[i] += oL[i] * sb; bR[i] += oR[i] * sb; } }
            done += c;
            if (env.done()) { used = false; return; }
        }
    }
};

// Ein zweiter Rechenkern fuer die Stimmen (Geraet): die Engine teilt die Stimmen fest in gerade
// und ungerade; die ungeraden rechnet - wenn es ihn gibt - der Helfer, sonst sie selbst. Beide
// Haelften landen in eigenen Puffern und werden danach addiert: das Ergebnis haengt also nicht
// davon ab, ob ein Helfer da ist, wie schnell er ist oder wie die Faeden laufen.
struct Engine;
struct VoiceHelper {
    virtual void begin() = 0;      // ungerade Stimmen anstossen (Engine::runHalf(1))
    virtual void wait() = 0;       // warten, bis sie fertig sind
    virtual ~VoiceHelper() {}
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
    float live[NTRACKS][VOICE_PARAM_CAP];      // was die Spur GERADE sagt - klingende Stimmen folgen dem
    // Fuer Slides: wo die Spur gerade steht. Nicht aus der klingenden Stimme gelesen - die
    // kann laengst verklungen sein, wenn der naechste Trig kommt.
    float trkVal[NTRACKS][VOICE_PARAM_CAP];

    Delay      delay;
    Reverb     reverb;
    SamplePool pool;

    // --- Aufnahme in einen Platz des Vorrats -----------------------------------------------
    // Aufnahmequellen: Master, trockene Spur oder externer Stereo-Eingang.
    bool  recOn = false;
    enum { REC_MASTER = -1, REC_INPUT = -2 };
    int   recSlot = -1, recMax = 0, recCh = 2, recSource = REC_MASTER, recDelay = 0;
    int recTrigTrack = -1, recTrigStep = -1, recTrigPattern = -1, recInputLead = 0;
    float recStereo[4096 * 2];

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
    int  songPos = 0;              // welche Zeile gerade spielt
    bool songArmed = false;        // der anstehende Wechsel kommt vom Song
    int  songNext = -1;            // die eingeplante naechste Zeile
    int  songBpm = 0;              // Tempo der Zeile (0 = keins)
    uint16_t songMute = 0;         // Mutes der Zeile - zusaetzlich zu den eigenen
    int8_t loopLeft[NSONG];        // LOOP-Zeilen: wie oft noch springen (-1 = frisch)

    Ev   q[256];
    int  qoff[256];
    int  nq = 0;
    // Arbeitspuffer gehoeren zur Engine, nicht auf den Stapel: im Browser sind davon
    // nur 64 KB da, und sechs Puffer zu 4096 Samples waeren mehr.
    float bufD[2][4096], bufA[2][4096], bufB[2][4096];
    float bufD2[2][4096], bufA2[2][4096], bufB2[2][4096];   // die ungerade Haelfte der Stimmen
    StereoInsert inserts[NTRACKS][INSERT_STAGES];
    MasterChain mastering;
    bool masteringExport = false;
    int64_t sampleClock = 0, modulationOffset = 0;
    float trackDry[2][NTRACKS][2][4096];
    unsigned trackCapture = 0;
    VoiceHelper* helper = nullptr;
    int jobC = 0;                  // Laenge des laufenden Teilblocks
    // Eine Haelfte der Stimmen rechnen (0 = gerade in bufD/A/B, 1 = ungerade in bufD2/A2/B2).
    void runHalf(int parity, int c) {
        Voice::Mix mx = parity ? Voice::Mix{bufD2[0], bufD2[1], bufA2[0], bufA2[1], bufB2[0], bufB2[1]}
                               : Voice::Mix{bufD[0], bufD[1], bufA[0], bufA[1], bufB[0], bufB[1]};
        if (!trackCapture) {
            for (int i = parity; i < NVOICES; i += 2) v[i].run(mx, c, sr, live[v[i].track]);
            return;
        }
        for (int track = 0; track < NTRACKS; track++) {
            if (!(trackCapture & (1u << track))) continue;
            memset(trackDry[parity][track][0], 0, sizeof(float) * c);
            memset(trackDry[parity][track][1], 0, sizeof(float) * c);
        }
        for (int i = parity; i < NVOICES; i += 2) {
            Voice& voice = v[i];
            Voice::Mix target = mx;
            if (!voice.preview && (trackCapture & (1u << voice.track))) {
                target.dL = trackDry[parity][voice.track][0];
                target.dR = trackDry[parity][voice.track][1];
            }
            voice.run(target, c, sr, live[voice.track]);
        }
    }
    void resetInserts() {
        for (auto& track : inserts) for (StereoInsert& stage : track) stage.reset();
    }
    void configureInserts() {
        const Pattern& pattern = P.pat[pat];
        for (int t = 0; t < NTRACKS; t++)
            for (int slot = 0; slot < INSERT_STAGES; slot++)
                inserts[t][slot].configure(pattern.t[t].p + insertParamSlot(slot), sr);
    }
    unsigned trackCaptureMask() const {
        unsigned mask = recOn && recSource >= 0 ? 1u << recSource : 0;
        for (int t = 0; t < NTRACKS; t++)
            for (const StereoInsert& stage : inserts[t])
                if (stage.processing()) mask |= (1u << t) | stage.sources();
        return mask;
    }
    void mixInserts(int frames) {
        // Keep every sidechain source intact until all tracks have processed. Sidechains
        // tap the dry bus, so an A->B->A assignment cannot form a feedback loop.
        for (int t = 0; t < NTRACKS; t++) {
            if (!(trackCapture & (1u << t))) continue;
            for (int i = 0; i < frames; i++) {
                trackDry[0][t][0][i] += trackDry[1][t][0][i];
                trackDry[0][t][1][i] += trackDry[1][t][1][i];
            }
        }
        for (int t = 0; t < NTRACKS; t++) {
            if (!(trackCapture & (1u << t))) continue;
            bool recording = recOn && recSource == t;
            StereoInsert* slots = inserts[t];
            bool processFirst = slots[0].processing(), processSecond = slots[1].processing();
            for (int i = 0; i < frames; i++) {
                float l = trackDry[0][t][0][i], r = trackDry[0][t][1][i];
                auto insert = [&](StereoInsert& stage) {
                    stage.advance();
                    int source = stage.active.source - 1;
                    stage.process(l, r, source >= 0 ? trackDry[0][source][0][i] : 0.0f,
                                         source >= 0 ? trackDry[0][source][1][i] : 0.0f);
                };
                if (processFirst) insert(slots[0]);
                if (processSecond) insert(slots[1]);
                bufD[0][i] += l;
                bufD[1][i] += r;
                if (recording) { recStereo[2 * i] = l; recStereo[2 * i + 1] = r; }
            }
        }
    }
    // Faule Tabellen einmal im Hauptfaden fuellen, bevor ein zweiter Faden Stimmen rechnet.
    static void warmTables() {
        float lo, hi;
        knobSlot(0); modDestOfSlot(0); modDestRange(1, lo, hi); modSlots();
        Voice::liveSlots(); Voice::S(); insertParamSlot(0); StereoChorus::warm(); MasterChain::slots();
        for (int i = 0; i < machineCount(); i++) machineAt(i);
        for (int i = 0; i < MasterEq::BANDS; i++) MasterEq::slots(i);
        MachFm::operators();
        FxControls::layout();
    }

    Engine() { projectInit(P); resetTrackValues(); for (int i = 0; i < NSONG; i++) loopLeft[i] = -1; }

    void resetTrackValues() {
        for (int t = 0; t < NTRACKS; t++)
            for (int i = 0; i < VOICE_PARAM_CAP; i++) trkVal[t][i] = (float)P.pat[pat].t[t].p[i];
    }

    // Vorbereiten heisst: alles steht still. Sonst laeuft die Uhr nach einem Neuaufbau weiter,
    // ohne dass jemand sie gestartet hat.
    void prepare(float rate) {
        warmTables();
        sr = rate; seq.sr = rate;
        seq.stop();
        patNext = -1; switchAt = -1; songArmed = false;
        recStop();
        for (int i = 0; i < NVOICES; i++) { v[i].used = false; v[i].env.kill(); }
        sampleClock = modulationOffset = 0;
        clock = 0;                                   // Zufall der Stimmen (Drift) beginnt gleich
        delay.reset(); reverb.reset(); resetInserts(); mastering.reset();
        resetTrackValues();
    }
    void start() {
        if (songOn) { seq.stop(); songStart(); }         // der Song faengt vorn an
        seq.bpm = effTempo(pat);
        seq.start();
        modulationOffset = sampleClock - seq.pos;
        if (songOn) enterRow(songPos);                   // Start-Step der Zeile nach dem Reset
    }

    // Platz reservieren und scharf stellen. Gibt die moegliche Laenge in Bildern zurueck, 0 = kein Platz.
    int recStart(int slot, float seconds, const char* name, int ch = 2,
                 int source = REC_MASTER, bool nextBar = false) {
        if (recOn || source < REC_INPUT || source >= NTRACKS || !pool.empty(slot) || pool.wanted(slot)) return 0;
        int channels = ch < 2 ? 1 : 2;
        int capacity = pool.freeFrames() / channels;
        if (capacity < 1) return 0;
        int frames = seconds <= 0.0f ? capacity : (int)(seconds * sr);
        if (frames < 1) frames = 1;
        if (pool.reserve(slot, frames, sr, name, ch) < 0) return 0;
        recSlot = slot; recMax = frames; recCh = channels; recSource = source; recOn = true;
        recDelay = 0;
        recTrigTrack = -1;
        if (nextBar && seq.running) {
            const double bar = 4.0 * 60.0 * sr / tempoOf(pat);
            const int64_t rel = seq.pos - seq.origin;
            const int64_t k = (int64_t)(rel / bar) + 1;
            recDelay = (int)(llround(k * bar) - rel);
        }
        return frames;
    }
    void recArmTrig(int track, int step) {
        recTrigTrack = track;
        recTrigStep = step;
        recTrigPattern = pat;
    }
    bool recWaiting() const { return recTrigTrack >= 0 || recDelay > 0; }
    void recStop() {
        if (recOn) {
            auto& slot = pool.s[recSlot];
            if (pool.used == slot.at + recMax * recCh) pool.used = slot.at + slot.n * recCh;
            if (!slot.n) slot = SamplePool::Slot{};
        }
        recOn = false;
        recSlot = -1;
        recTrigTrack = -1;
        recDelay = 0;
    }
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
        if (extBpm > 0.0) return extBpm;                       // externe MIDI-Clock fuehrt
        if (songOn && songBpm > 0) return (double)songBpm;     // die Song-Zeile bestimmt das Tempo
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

    // Laenge einer Song-Zeile in Sechzehnteln (ein Durchlauf).
    int rowLen(int i) const {
        const SongStep& r = P.song.e[i];
        int l = r.len ? r.len : P.pat[r.pat].chg;
        return l < 1 ? 1 : l;
    }
    // Welche Zeile kommt nach 'von'? LOOP-Zeilen springen (und zaehlen mit), HALT bleibt stehen.
    int songResolve(int von) {
        int i = von + 1;
        for (int g = 0; g < NSONG * 4; g++) {
            if (i >= P.song.n || i < 0) i = 0;
            const SongStep& r = P.song.e[i];
            if (r.type != SONG_LOOP) return i;
            if (r.rep == 0) { i = r.pat; continue; }             // fuer immer
            if (loopLeft[i] < 0) loopLeft[i] = (int8_t)r.rep;
            if (loopLeft[i] > 0) { loopLeft[i]--; i = r.pat; continue; }
            loopLeft[i] = -1; i++;                               // fertig gesprungen: weiter
        }
        return -1;                                               // nur Spruenge, nichts zu spielen
    }
    // Eine Zeile beginnt (das Pattern steht schon): Tempo, Start-Step, Mutes, Scene.
    void enterRow(int i) {
        const SongStep& r = P.song.e[i];
        songBpm = r.bpm;
        songMute = r.mute;
        if (r.morph) morph = (float)(r.morph - 1) / 100.0f;
        seq.bpm = effTempo(pat);
        if (r.start) {                                           // ab Step 'start': Ursprung zurueck
            double sp16 = seq.sr * 60.0 / ((seq.bpm < 2.5 ? 2.5 : seq.bpm) * 4.0);
            seq.origin = seq.pos - (int64_t)llround(r.start * sp16);
            for (int t = 0; t < NTRACKS; t++) {
                double sp = seq.stepLen(P.pat[pat].t[t]);
                double rel = (double)(seq.pos - seq.origin);
                int64_t k = (int64_t)(rel / sp);
                if ((double)k * sp < rel) k++;
                seq.nextK[t] = k;
            }
        }
    }

    // Song an. Steht die Uhr, beginnt die erste Zeile sofort; laeuft sie, am naechsten Taktende -
    // ein Sprung mitten im Takt klaenge wie ein Fehler.
    void songStart(int ab = -1) {
        for (int i = 0; i < NSONG; i++) loopLeft[i] = -1;
        if (!P.song.n) { songOn = false; return; }
        int erste = (ab >= 0 && ab < P.song.n) ? songResolve(ab - 1) : songResolve(-1);
        if (erste < 0 || P.song.e[erste].type == SONG_HALT) { songOn = false; return; }
        songOn = true;
        if (seq.running) {
            songNext = erste;
            patNext = P.song.e[erste].pat;
            double cyc = cycleLen();
            int64_t k = (int64_t)((double)(seq.pos - seq.origin) / cyc) + 1;
            switchAt = seq.origin + (int64_t)llround((double)k * cyc);
            songArmed = true;
            return;
        }
        songPos = erste; songArmed = false;
        patNext = -1; switchAt = -1;
        pat = P.song.e[erste].pat;
        seq.toPattern(seq.pos);
        enterRow(erste);
        resetTrackValues();
    }
    void songStop() {
        songOn = false; songBpm = 0; songMute = 0;
        if (songArmed) { patNext = -1; switchAt = -1; songArmed = false; }
    }

    // Den naechsten Wechsel des Songs einplanen: nach (Start + Laenge x Wiederholungen) Steps.
    void songPlan() {
        if (!songOn || !P.song.n || patNext >= 0 || !seq.running) return;
        if (songPos >= P.song.n) songPos = 0;
        const SongStep& r = P.song.e[songPos];
        int rep = r.rep < 1 ? 1 : r.rep;
        int nx = songResolve(songPos);
        if (nx < 0) { songStop(); return; }
        songNext = nx;
        patNext = P.song.e[nx].type == SONG_HALT ? pat : P.song.e[nx].pat;
        double sp16 = seq.sr * 60.0 / ((seq.bpm < 2.5 ? 2.5 : seq.bpm) * 4.0);
        switchAt = seq.origin + (int64_t)llround(sp16 * (double)(r.start + rowLen(songPos) * rep));
        if (switchAt <= seq.pos) switchAt = seq.pos;      // verspaetet: sofort
        songArmed = true;
    }

    // Vorhoeren im Sample-Editor: der Platz klingt so, wie er ist - offenes Filter, volle
    // Lautstaerke, Note 60 - unabhaengig davon, was eine Spur gerade eingestellt hat.
    // Alle Slots gelten als gelockt, damit keine Spur ihre Regler darueberlegt.
    void preview(int slot, float seconds = 6.0f, int slice = -1) {
        previewStop();
        if (pool.empty(slot)) return;
        float snap[VOICE_PARAM_CAP];
        for (int i = 0; i < VOICE_PARAM_CAP; i++) snap[i] = 0.0f;
        for (int i = 0; i < NPARAMS; i++)
            if (PARAMS[i].scope == PS_TRACK && !isInsertParam(i)) snap[paramSlot(i)] = PARAMS[i].def;
        int16_t tmp[VOICE_PARAM_CAP];
        for (int i = 0; i < VOICE_PARAM_CAP; i++) tmp[i] = (int16_t)snap[i];
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
        int machine = slice < 0 ? 3 : 4;
        if (slice >= 0) {
            for (int k = 0; k < MACH_KNOBS; k++) snap[knobSlot(k)] = (float)machineAt(machine)->def(k);
            snap[knobSlot(MachChop::C_SLICE)] = (float)slice;

        }
        v[i].modTime = sampleClock;
        v[i].start(machineAt(machine), 0, 0, snap, 0.85f, (int64_t)(sr * seconds), sr, clock++,
                   VoiceLocks::all(), live[0], pool);
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
        // Alle belegt: zuerst die leiseste, die schon ausklingt - das Abschneiden hoert man dort
        // am wenigsten. Erst wenn keine ausklingt, die aelteste.
        int rel = -1; float lo = 1e30f;
        for (int i = 0; i < NVOICES; i++)
            if (!v[i].preview && v[i].env.st == Env::REL && v[i].env.v < lo) { lo = v[i].env.v; rel = i; }
        if (rel >= 0) return rel;
        int best = 0; uint64_t old = ~0ull;
        for (int i = 0; i < NVOICES; i++) if (v[i].born < old) { old = v[i].born; best = i; }
        return best;
    }

    // Wirksame Werte eines Steps: Lock -> Morph zwischen den Scenes -> Grundwert.
    void snapshot(int t, int step, float* out) const {
        const Pattern& pt = P.pat[pat];
        for (int i = 0; i < VOICE_PARAM_CAP; i++) out[i] = (float)pt.t[t].p[i];
        for (int sl = 0; sl < insertParamSlot(0); sl++)
            out[sl] = effectiveF(pt, t, step, sl, morph);
    }

    // Anschlag-Ziele der Modulation: der LFO-Wert im Moment des Trigs (siehe ModState::trigOffset).
    int64_t modTimeFor(int64_t at) const {
        if (!seq.running) return sampleClock;
        int64_t t = at + modulationOffset;
        return t < 0 ? 0 : t;
    }
    float trigMod(int track, int step, int slot, int64_t at) override {
        int d = modDestOfSlot(slot);
        if (d < 0 || !MOD_DESTS[d].trig) return 0.0f;
        float snap[VOICE_PARAM_CAP];
        snapshot(track, step, snap);
        double spb = sr * 60.0 / (tempoOf(pat) > 1 ? tempoOf(pat) : 120.0);
        return ModState::trigOffset(snap, d, at, spb, 0x51u + (uint32_t)track * 977u, sr, modTimeFor(at));
    }

    // Geraet: Noten nach aussen (MIDI) und Tempo von aussen (MIDI-Clock). Beides bleibt im
    // Browser und im Plugin ungenutzt (nullptr / 0).
    void (*noteOut)(void* user, int track, int note, int vel, int64_t at, int32_t len) = nullptr;
    void* noteUser = nullptr;
    double extBpm = 0.0;

    void fire(const Ev& e) {
        const Track& tr = P.pat[pat].t[e.track];
        float snap[VOICE_PARAM_CAP];
        snapshot(e.track, e.step, snap);
        int32_t glide = (int32_t)seq.stepLen(tr);
        if (e.kind == EV_TRIG) {
            static const char* const beimAnschlag[] = {"trk.smp", "trk.quant", "trk.nt2", "trk.nt3", "trk.nt4"};
            double spb = sr * 60.0 / (tempoOf(pat) > 1 ? tempoOf(pat) : 120.0);
            auto applyTrigParam = [&](int pi, int dest) {
                const int sl = paramSlot(pi);
                const float o = ModState::trigOffset(snap, dest, e.at, spb, 0x51u + (uint32_t)e.track * 977u, sr, modTimeFor(e.at));
                if (o == 0.0f) return;
                const float value = snap[sl] + o;
                snap[sl] = (float)clampParam(pi, (int)(value + (value < 0 ? -0.5f : 0.5f)));
            };
            for (const char* id : beimAnschlag) {
                const int pi = paramFind(id);
                applyTrigParam(pi, modDestOfSlot(paramSlot(pi)));
            }
            const ModSlots& mods = modSlots();
            const int chosen[] = {(int)snap[mods.dest[0]], (int)snap[mods.dest[1]], (int)snap[mods.eDest]};
            for (int i = 0; i < 3; i++) {
                const int dest = chosen[i], sl = modDestSlot(dest);
                if (sl < 0 || !MOD_DESTS[dest].trig) continue;
                if ((i > 0 && dest == chosen[0]) || (i > 1 && dest == chosen[1])) continue;
                const int pi = paramOfSlot(PS_TRACK, sl);
                if (isExtendedVoiceParam(pi)) applyTrigParam(pi, dest);
            }
        }

        if (e.kind == EV_LOCKS) {            // trigless: nur Werte, kein Anschlag
            for (int i = 0; i < NVOICES; i++) {
                if (!v[i].used || v[i].track != e.track) continue;
                for (int li = 0; li < tr.s[e.step].nLock; li++) {
                    const Lock& l = tr.s[e.step].lock[li];
                    // Auch ein trigless gesetzter Wert ist festgelegt - sonst holt ihn die
                    // Spur beim naechsten Steuertakt wieder zurueck.
                    v[i].locked.set(l.slot);
                    if (l.slide) v[i].slide(l.slot, trkVal[e.track][l.slot], (float)l.val, glide);
                    else         v[i].pf[l.slot] = (float)l.val;
                }
            }
            for (int i = 0; i < VOICE_PARAM_CAP; i++) trkVal[e.track][i] = snap[i];
            return;
        }
        float from[VOICE_PARAM_CAP];
        for (int i = 0; i < VOICE_PARAM_CAP; i++) { from[i] = trkVal[e.track][i]; trkVal[e.track][i] = snap[i]; }
        if (mute[e.track] || (songOn && (songMute >> e.track) & 1)) return;   // stumm: kein Klang, der Lauf geht weiter

        VoiceLocks lockMask;
        for (int li = 0; li < tr.s[e.step].nLock; li++) lockMask.set(tr.s[e.step].lock[li].slot);
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
                if (o <= CHORD_OFF) continue;
                int nt = rastet ? scaleDegrees((int)(snap[sNote] + 0.5f), o - CHORD_UNISON, ptq.root, ptq.scale)
                                : (int)(snap[sNote] + 0.5f) + (o - CHORD_UNISON);
                if (nt < 0 || nt > 127) continue;
                noten[nNoten++] = (float)nt;
            }
        if (noteOut)
            for (int c = 0; c < nNoten; c++) noteOut(noteUser, e.track, (int)(noten[c] + 0.5f), e.vel, e.at, e.len);
        for (int c = 0; c < nNoten; c++) {
            snap[sNote] = noten[c];
            int i = pick();
            v[i].spb = sr * 60.0 / (tempoOf(pat) > 1 ? tempoOf(pat) : 120.0);
            v[i].t0 = e.at;
            v[i].modTime = modTimeFor(e.at);
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
        if (recTrigTrack >= 0) return;
        int skip = recDelay < n ? recDelay : n;
        recDelay -= skip;
        SamplePool::Slot& sl = pool.s[recSlot];
        for (int i = skip; i < n && sl.n < recMax; i++) {
            float l = stereoOut[i * 2], r = stereoOut[i * 2 + 1];
            if (recCh == 2) { pool.data[sl.at + sl.n * 2] = l; pool.data[sl.at + sl.n * 2 + 1] = r; }
            else            { pool.data[sl.at + sl.n] = 0.5f * (l + r); }
            sl.n++;
        }
        if (sl.n >= recMax) recStop();          // die Laenge stand vorher fest
    }

    // Nach einem HALT: den Rest des Blocks mit gestoppter Uhr rechnen (Stimmen klingen aus).
    void renderRest(float* out, int done, int frames) {
        while (done < frames) { int c = frames - done; renderPart(out + done * 2, c); done += c; }
    }
    // out ist stereo-verschraenkt (L,R,L,R...) und wird ueberschrieben.
    // Steht ein Pattern-Wechsel an, wird der Block an GENAU der Stelle geteilt - sonst haenge
    // der Wechsel davon ab, wie der Host schneidet.
    void render(float* out, int frames) {
        recInputLead = 0;
        int done = 0;
        songPlan();
        while (done < frames) {
            int c = frames - done;
            if (patNext >= 0 && switchAt > seq.pos && switchAt - seq.pos < (int64_t)c)
                c = (int)(switchAt - seq.pos);
            renderPart(out + done * 2, c);
            done += c;
            if (patNext >= 0 && seq.pos >= switchAt) {
                if (songArmed && songNext >= 0 && songNext < P.song.n && P.song.e[songNext].type == SONG_HALT) {
                    // HALT: der Song haelt an - Stimmen klingen aus, die Uhr steht
                    patNext = -1; switchAt = -1; songArmed = false;
                    songPos = songNext; songStop(); stop();
                    renderRest(out, done, frames);
                    return;
                }
                pat = patNext; patNext = -1; switchAt = -1;
                seq.toPattern(seq.pos);
                seq.bpm = effTempo(pat);              // Ursprung ist frisch - kein Umrechnen noetig
                if (songArmed) {
                    songArmed = false;
                    songPos = songNext >= 0 ? songNext : 0;
                    enterRow(songPos);
                    resetTrackValues();
                    songPlan();                          // gleich den naechsten einplanen
                } else resetTrackValues();
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
        machTempo() = (float)effTempo(pat);            // fuer den Sampler-Warp
        modulationOffset = sampleClock - seq.pos;
        seq.process(P, pat, frames, *this);
        updateLive();
        configureInserts();
        mastering.configure(P.g, sr);
        float master = pLevel(gNow(paramSlot(paramFind("master.vol")), fs));

        FxControls fx;
        fx.read([&](int slot) { return gNow(slot, fs); });
        const float fxTempo = (float)effTempo(pat);

        int done = 0, ei = 0;
        while (done < frames) {
            int next = frames;
            while (ei < nq && qoff[ei] <= done) {
                const Ev& e = q[ei++];
                if (recOn && recTrigTrack == e.track && recTrigStep == e.step
                    && recTrigPattern == pat && e.kind == EV_TRIG) {
                    recTrigTrack = -1;
                    if (recSource == REC_INPUT) recDelay = recInputLead;
                }
                fire(e);
            }
            if (ei < nq && qoff[ei] < next) next = qoff[ei];
            int c = next - done; if (c > 4096) c = 4096; if (c <= 0) c = 1;
            for (int i = 0; i < c; i++) {
                bufD[0][i] = bufD[1][i] = 0.0f;
                bufA[0][i] = bufA[1][i] = 0.0f;
                bufB[0][i] = bufB[1][i] = 0.0f;
                bufD2[0][i] = bufD2[1][i] = 0.0f;
                bufA2[0][i] = bufA2[1][i] = 0.0f;
                bufB2[0][i] = bufB2[1][i] = 0.0f;
            }
            trackCapture = trackCaptureMask();
            jobC = c;
            if (helper) helper->begin();
            runHalf(0, c);
            if (helper) helper->wait(); else runHalf(1, c);
            for (int i = 0; i < c; i++) {
                bufD[0][i] += bufD2[0][i]; bufD[1][i] += bufD2[1][i];
                bufA[0][i] += bufA2[0][i]; bufA[1][i] += bufA2[1][i];
                bufB[0][i] += bufB2[0][i]; bufB[1][i] += bufB2[1][i];
            }
            if (trackCapture) mixInserts(c);
            for (int offset = 0; offset < c;) {
                using F = FxControls;
                constexpr int quantum = 64;
                const int64_t time = sampleClock + done + offset;
                int length = c - offset;
                if (fx.active) {
                    const int until = quantum - (int)(time % quantum);
                    if (length > until) length = until;
                }
                int settings[F::FIELDS];
                fx.values(settings, time - time % quantum, sr, fxTempo);
                delay.configure(settings, fxTempo, sr);
                reverb.configure(settings, sr);
                delay.run(bufA[0] + offset, bufA[1] + offset, bufD[0] + offset, bufD[1] + offset, length,
                          pUni(settings[F::D_FB]) * 0.95f, pUni(settings[F::D_TONE]), pLevel(settings[F::D_VOL]));
                reverb.run(bufB[0] + offset, bufB[1] + offset, bufD[0] + offset, bufD[1] + offset, length,
                           pUni(settings[F::R_DAMP]), pLevel(settings[F::R_VOL]));
                offset += length;
            }
            for (int i = 0; i < c; i++) {
                float l = bufD[0][i] * master, r = bufD[1][i] * master;
                if (mastering.enabled) mastering.run(l, r, masteringExport);
                else { l = softClip(l); r = softClip(r); }
                out[(done + i) * 2] = l;
                out[(done + i) * 2 + 1] = r;
            }
            if (recOn && recSource == REC_INPUT && recTrigTrack >= 0) recInputLead += c;
            if (recOn && recSource == REC_MASTER) recWrite(out + done * 2, c);
            else if (recOn && recSource >= 0) recWrite(recStereo, c);
            done += c;
        }
        sampleClock += frames;
    }
};
