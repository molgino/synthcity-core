// Engine: verbindet Sequencer, Machines und Stimmen zu Klang.
// Zwei Regeln gelten hier ueberall:
//   1. Im Render wird nichts angefordert, gesperrt oder geschrieben - nur gerechnet.
//   2. Ein Ereignis wirkt auf die SAMPLE genau: der Block wird an jeder Ereignis-Stelle geteilt.
#pragma once
#include "seq.h"
#include "machine.h"

enum { NVOICES = 32, CTL = 16 };   // CTL: Steuerrate in Samples (Zweierpotenz), 3 kHz bei 48 kHz

struct Voice {
    bool     used = false;
    uint8_t  track = 0;
    uint8_t  step = 0;
    uint64_t born = 0;          // zum Stehlen der aeltesten Stimme
    uint32_t age = 0;           // Samples seit dem Anschlag - gibt die Steuerrate vor
    float    hz = 0.0f, vel = 0.0f;
    int64_t  left = 0;          // Samples bis Note-Off
    int16_t  p[64];             // Schnappschuss der wirksamen Werte BEIM ANSCHLAG (P-Locks)
    MState   ms;
    Svf      flt;
    Env      env;
    float    panL = 0.7071f, panR = 0.7071f;
    const Machine* mach = nullptr;

    // Slots einmalig nachschlagen - Namen sind die Wahrheit, Zahlen nur die Abkuerzung.
    struct Slots { int atk, dec, sus, rel, fFreq, fReso, fType, fEnv, vol, pan; };
    static const Slots& S() {
        static const Slots s = {
            paramSlot(paramFind("trk.amp.atk")),  paramSlot(paramFind("trk.amp.dec")),
            paramSlot(paramFind("trk.amp.sus")),  paramSlot(paramFind("trk.amp.rel")),
            paramSlot(paramFind("trk.flt.freq")), paramSlot(paramFind("trk.flt.reso")),
            paramSlot(paramFind("trk.flt.type")), paramSlot(paramFind("trk.flt.env")),
            paramSlot(paramFind("trk.vol")),      paramSlot(paramFind("trk.pan"))};
        return s;
    }

    void start(const Machine* mc, int tr, int st, const int16_t* pp, float h, float v,
               int64_t lenSamples, float sr, uint64_t when) {
        mach = mc; track = (uint8_t)tr; step = (uint8_t)st; born = when;
        for (int i = 0; i < 64; i++) p[i] = pp[i];
        hz = h; vel = v; left = lenSamples; used = true; age = 0;
        const Slots& s = S();
        flt.reset();
        env.setup(pTime(p[s.atk]), pTime(p[s.dec]), pLevel(p[s.sus]), pTime(p[s.rel]), sr);
        env.kill(); env.gate();
        float pan = pBip(p[s.pan]);                    // gleiche Leistung links wie rechts
        float a = 0.7853982f * (pan + 1.0f);
        panL = cosf(a); panR = sinf(a);
        mach->start(ms, hz, vel, p, sr);
    }
    void release() { env.release(); left = -1; }

    void run(float* outL, float* outR, int n, float sr) {
        if (!used) return;
        const Slots& s = S();
        float base = pFreq(p[s.fFreq]);
        float envAmt = pBip(p[s.fEnv]);
        float q = 0.7f + pUni(p[s.fReso]) * 11.0f;
        FltType ft = (FltType)(p[s.fType] % FLT_N);
        float gain = pLevel(p[s.vol]) * vel;
        float buf[MACH_CHUNK];
        int done = 0;
        while (done < n) {
            int c = n - done; if (c > MACH_CHUNK) c = MACH_CHUNK;
            mach->render(ms, p, hz, buf, c, sr);
            for (int i = 0; i < c; i++) {
                if (left > 0) { if (--left == 0) release(); }
                float e = env.tick();
                // Steuerrate: die Filterkoeffizienten werden alle CTL Samples neu gerechnet.
                // Gezaehlt wird an der STIMME, nicht am Block - sonst haengt der Klang davon ab,
                // wie der Host die Bloecke schneidet.
                if ((age & (CTL - 1)) == 0)
                    flt.set(base * exp2f(envAmt * e * 6.0f), q, sr);   // bis zu sechs Oktaven
                age++;
                float x = flt.run(buf[i], ft) * e * gain;
                outL[done + i] += x * panL;
                outR[done + i] += x * panR;
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

    Ev   q[256];
    int  qoff[256];
    int  nq = 0;

    Engine() { projectInit(P); }

    void prepare(float rate) {
        sr = rate; seq.sr = rate;
        for (int i = 0; i < NVOICES; i++) { v[i].used = false; v[i].env.kill(); }
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

    void fire(const Ev& e) {
        const Track& tr = P.pat[pat].t[e.track];
        int16_t snap[64];
        for (int i = 0; i < 64; i++) snap[i] = tr.p[i];
        for (int i = 0; i < NPARAMS; i++) {                  // P-Locks dieses Steps einrechnen
            if (PARAMS[i].scope != PS_TRACK) continue;
            int sl = paramSlot(i);
            const Lock* l = tr.s[e.step].findLock(sl);
            if (l) snap[sl] = l->val;
        }
        if (e.kind == EV_LOCKS) {                            // trigless: nur Werte, kein Anschlag
            for (int i = 0; i < NVOICES; i++)
                if (v[i].used && v[i].track == e.track)
                    for (int k = 0; k < 64; k++) v[i].p[k] = snap[k];
            return;
        }
        int mi = tr.p[paramSlot(paramFind("trk.machine"))];
        int i = pick();
        v[i].start(machineAt(mi), e.track, e.step, snap, noteHz((float)e.note),
                   e.vel / 127.0f, e.len, sr, clock++);
    }

    // out ist stereo-verschraenkt (L,R,L,R...) und wird ueberschrieben.
    void render(float* out, int frames) {
        for (int i = 0; i < frames * 2; i++) out[i] = 0.0f;
        nq = 0;
        seq.process(P, pat, frames, *this);
        float master = pLevel(P.g[paramSlot(paramFind("master.vol"))]);

        float L[4096], R[4096];
        int done = 0, ei = 0;
        while (done < frames) {
            int next = frames;
            while (ei < nq && qoff[ei] <= done) { fire(q[ei]); ei++; }
            if (ei < nq && qoff[ei] < next) next = qoff[ei];
            int c = next - done; if (c > 4096) c = 4096; if (c <= 0) c = 1;
            for (int i = 0; i < c; i++) { L[i] = 0.0f; R[i] = 0.0f; }
            for (int i = 0; i < NVOICES; i++) v[i].run(L, R, c, sr);
            for (int i = 0; i < c; i++) {
                out[(done + i) * 2]     = softClip(L[i] * master);
                out[(done + i) * 2 + 1] = softClip(R[i] * master);
            }
            done += c;
        }
    }
};
