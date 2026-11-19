// Engine: verbindet Sequencer, Machines und Stimmen zu Klang.
// Drei Regeln gelten hier ueberall:
//   1. Im Render wird nichts angefordert, gesperrt oder geschrieben - nur gerechnet.
//   2. Ein Ereignis wirkt auf die SAMPLE genau: der Block wird an jeder Ereignis-Stelle geteilt.
//   3. Die Performance-Ebene (Mute, Fill, Step-Loop, Morph) aendert NIE die Daten.
#pragma once
#include "seq.h"
#include "machine.h"
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
    Ramp     rmp[NRAMP];
    int      nRamp = 0;
    MState   ms;
    Svf      flt;
    Env      env;
    float    panL = 0.7071f, panR = 0.7071f;
    float    base = 1000.0f, q = 0.7f, envAmt = 0.0f, gain = 1.0f;
    FltType  ft = FLT_LP;
    const Machine* mach = nullptr;

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
               int64_t lenSamples, float sr, uint64_t when) {
        mach = mc; track = (uint8_t)tr; step = (uint8_t)st; born = when;
        for (int i = 0; i < 64; i++) pf[i] = pp[i];
        nRamp = 0; vel = v; left = lenSamples; used = true; age = 0;
        const Slots& s = S();
        flt.reset();
        env.setup(pTime(pf[s.atk]), pTime(pf[s.dec]), pLevel(pf[s.sus]), pTime(pf[s.rel]), sr);
        env.kill(); env.gate();
        ctl(sr);
        mach->start(ms, hz, vel, pf, sr);
    }
    void release() { env.release(); left = -1; }

    void slide(int slot, float from, float to, int32_t samples) {
        if (samples < 1) samples = 1;
        pf[slot] = from;
        for (int i = 0; i < nRamp; i++)
            if (rmp[i].slot == slot) { rmp[i] = {(int16_t)slot, to, (to - from) / samples, samples}; return; }
        if (nRamp < NRAMP) rmp[nRamp++] = {(int16_t)slot, to, (to - from) / samples, samples};
    }

    // Steuerrate: hier werden Rampen bewegt und alle teuren Werte EINMAL gerechnet.
    void ctl(float sr) {
        for (int i = 0; i < nRamp; ) {
            Ramp& r = rmp[i];
            r.left -= CTL;
            if (r.left <= 0) { pf[r.slot] = r.to; rmp[i] = rmp[--nRamp]; continue; }
            pf[r.slot] += r.inc * CTL;
            i++;
        }
        const Slots& s = S();
        hz = noteHz(pf[s.note]);
        base = pFreq(pf[s.fFreq]);
        q = 0.7f + pUni(pf[s.fReso]) * 11.0f;
        envAmt = pBip(pf[s.fEnv]);
        ft = (FltType)(((int)pf[s.fType]) % FLT_N);
        gain = pLevel(pf[s.vol]) * vel;
        float a = 0.7853982f * (pBip(pf[s.pan]) + 1.0f);     // gleiche Leistung links wie rechts
        panL = cosf(a); panR = sinf(a);
    }

    // Drei Ziele: trocken, Sende A, Sende B. Die Sende-Anteile gehoeren zur Stimme,
    // damit ein P-Lock auf S.A auch wirklich nur diesen Anschlag weiterschickt.
    struct Mix { float *dL, *dR, *aL, *aR, *bL, *bR; };

    void run(const Mix& mx, int n, float sr) {
        if (!used) return;
        static const int slotSA = paramSlot(paramFind("trk.send.a"));
        static const int slotSB = paramSlot(paramFind("trk.send.b"));
        float sa = pLevel(pf[slotSA]), sb = pLevel(pf[slotSB]);
        float buf[MACH_CHUNK];
        int done = 0;
        while (done < n) {
            int c = n - done; if (c > MACH_CHUNK) c = MACH_CHUNK;
            mach->render(ms, pf, hz, buf, c, sr);
            for (int i = 0; i < c; i++) {
                if (left > 0) { if (--left == 0) release(); }
                float e = env.tick();
                // Gezaehlt wird an der STIMME, nicht am Block - sonst haengt der Klang davon ab,
                // wie der Host die Bloecke schneidet.
                if ((age & (CTL - 1)) == 0) {
                    ctl(sr);
                    flt.set(base * exp2f(envAmt * e * 6.0f), q, sr);   // bis zu sechs Oktaven
                }
                age++;
                float x = flt.run(buf[i], ft) * e * gain;
                float xl = x * panL, xr = x * panR;
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

    // --- Performance-Ebene: wirkt sofort, steht in keiner Datei ---------------------------
    bool  mute[NTRACKS] = {false};
    float morph = 0.0f;           // 0 = Scene A, 1 = Scene B
    float morphDone = -1.0f;
    // Fuer Slides: wo die Spur gerade steht. Nicht aus der klingenden Stimme gelesen - die
    // kann laengst verklungen sein, wenn der naechste Trig kommt.
    float trkVal[NTRACKS][64];

    Delay  delay;
    Reverb reverb;

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

    void prepare(float rate) {
        sr = rate; seq.sr = rate;
        for (int i = 0; i < NVOICES; i++) { v[i].used = false; v[i].env.kill(); }
        delay.reset(); reverb.reset();
        resetTrackValues();
        morphDone = -1.0f;
    }
    void start() { seq.bpm = P.g[paramSlot(paramFind("bpm"))]; seq.start(); }
    void stop()  { seq.stop(); for (int i = 0; i < NVOICES; i++) v[i].release(); }

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

    void fire(const Ev& e) {
        const Track& tr = P.pat[pat].t[e.track];
        float snap[64];
        snapshot(e.track, e.step, snap);
        int32_t glide = (int32_t)seq.stepLen(tr);

        if (e.kind == EV_LOCKS) {            // trigless: nur Werte, kein Anschlag
            for (int i = 0; i < NVOICES; i++) {
                if (!v[i].used || v[i].track != e.track) continue;
                for (int li = 0; li < tr.s[e.step].nLock; li++) {
                    const Lock& l = tr.s[e.step].lock[li];
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

        int mi = tr.p[paramSlot(paramFind("trk.machine"))];
        int i = pick();
        v[i].start(machineAt(mi), e.track, e.step, snap, e.vel / 127.0f, e.len, sr, clock++);
        for (int li = 0; li < tr.s[e.step].nLock; li++) {    // Slides: von dort, wo die Spur steht
            const Lock& l = tr.s[e.step].lock[li];
            if (l.slide) v[i].slide(l.slot, from[l.slot], (float)l.val, glide);
        }
        if (v[i].nRamp) v[i].ctl(sr);
    }

    // Der Morph-Regler wirkt auch auf klingende Stimmen - wie ein Crossfader an der Hardware.
    void applyMorph() {
        if (morph == morphDone) return;
        morphDone = morph;
        const Pattern& pt = P.pat[pat];
        for (int i = 0; i < NVOICES; i++) {
            if (!v[i].used) continue;
            int t = v[i].track;
            for (int k = 0; k < NSCENES; k++)
                for (int j = 0; j < pt.sc[k][t].n; j++) {
                    int sl = pt.sc[k][t].v[j].slot;
                    if (pt.t[t].s[v[i].step].findLock(sl)) continue;   // Lock schlaegt Scene
                    bool ramping = false;
                    for (int r = 0; r < v[i].nRamp; r++) if (v[i].rmp[r].slot == sl) ramping = true;
                    if (!ramping) v[i].pf[sl] = sceneValue(pt, t, sl, morph);
                }
        }
    }

    // out ist stereo-verschraenkt (L,R,L,R...) und wird ueberschrieben.
    void render(float* out, int frames) {
        for (int i = 0; i < frames * 2; i++) out[i] = 0.0f;
        nq = 0;
        seq.process(P, pat, frames, *this);
        applyMorph();
        float master = pLevel(P.g[paramSlot(paramFind("master.vol"))]);

        // Effekte: Einstellungen einmal je Block, nicht je Sample.
        static const int sDiv  = paramSlot(paramFind("fx.a.div"));
        static const int sFb   = paramSlot(paramFind("fx.a.fb"));
        static const int sTone = paramSlot(paramFind("fx.a.tone"));
        static const int sAVol = paramSlot(paramFind("fx.a.vol"));
        static const int sSize = paramSlot(paramFind("fx.b.size"));
        static const int sDec  = paramSlot(paramFind("fx.b.dec"));
        static const int sDamp = paramSlot(paramFind("fx.b.damp"));
        static const int sBVol = paramSlot(paramFind("fx.b.vol"));
        delay.setTime(P.g[sDiv], (float)seq.bpm, sr);
        reverb.setSize(pUni(P.g[sSize]), sr);
        float fb   = pUni(P.g[sFb]) * 0.95f;
        float tone = pUni(P.g[sTone]);
        float dec  = 0.3f + pUni(P.g[sDec]) * 0.69f;
        float damp = pUni(P.g[sDamp]);
        float aVol = pLevel(P.g[sAVol]), bVol = pLevel(P.g[sBVol]);

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
            for (int i = 0; i < NVOICES; i++) v[i].run(mx, c, sr);
            delay.run(bufA[0], bufA[1], bufD[0], bufD[1], c, fb, tone, aVol);
            reverb.run(bufB[0], bufB[1], bufD[0], bufD[1], c, dec, damp, bVol);
            for (int i = 0; i < c; i++) {
                out[(done + i) * 2]     = softClip(bufD[0][i] * master);
                out[(done + i) * 2 + 1] = softClip(bufD[1][i] * master);
            }
            done += c;
        }
    }
};
