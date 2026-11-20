// Machines: austauschbare Klangquellen hinter EINEM festen Satz Regler (Elektron-Prinzip).
// Eine Machine kennt nur ihren eigenen Zustand und die Spurparameter - nicht die Spur, nicht
// den Sequencer. Der Zustand liegt in einem festen Block, damit nie Speicher angefordert wird.
#pragma once
#include "dsp.h"
#include "param.h"
#include "sample.h"

enum { MACH_STATE = 256, MACH_CHUNK = 32 };

struct MState { alignas(16) unsigned char raw[MACH_STATE];
    template <class T> T& as() { return *(T*)raw; }
    template <class T> const T& as() const { return *(const T*)raw; }
};

struct Machine {
    virtual ~Machine() {}
    virtual const char* name()   const = 0;
    virtual const char* labelA() const = 0;      // Anzeige fuer trk.src.a
    virtual const char* labelB() const = 0;      // Anzeige fuer trk.src.b
    // Wie der Wert heisst, weiss nur die Machine: 64 ist bei der einen "kein Detune",
    // bei der anderen "Verhaeltnis 2:1". Vorgabe ist die nackte Zahl.
    virtual void text(int which, float v, char* out, int n) const {
        (void)which; snprintf(out, n, "%.0f", (double)v);
    }
    virtual void  start(MState& m, float hz, float vel, const float* p, float sr,
                        const SamplePool& pool) const = 0;
    // Fuellt n Samples (n <= MACH_CHUNK). Ein Aufruf je Teilblock, nicht je Sample.
    // bufR ist nur belegt, wenn stereo() wahr ist - sonst kostet Stereo jede Mono-Machine
    // einen zweiten Filter umsonst.
    virtual void  render(MState& m, const float* p, float hz, float* buf, float* bufR, int n,
                         float sr, const SamplePool& pool) const = 0;
    virtual bool  stereo(const float* p, const SamplePool& pool) const { (void)p; (void)pool; return false; }
    // Grundwerte der beiden eigenen Regler. 64 ist bei POLY "kein Detune", beim Sampler
    // waere es die Mitte des Samples - jede Machine muss das selbst sagen.
    virtual int   defA() const { return 64; }
    virtual int   defB() const { return 64; }
    // Spielt die Machine ein Sample aus dem Vorrat? Nur dann braucht die Spur trk.smp.
    virtual bool  usesSample() const { return false; }
};

// --- POLY: zwei verstimmte Oszillatoren, stufenlos von Saege nach Rechteck ---------------------
struct MachPoly : Machine {
    struct St { Osc a, b; float width; };
    static_assert(sizeof(St) <= MACH_STATE, "Zustand passt nicht in den Block");

    const char* name()   const override { return "POLY"; }
    const char* labelA() const override { return "DTUN"; }
    const char* labelB() const override { return "SHAPE"; }
    void text(int which, float v, char* out, int n) const override {
        if (which == 0) snprintf(out, n, "%+.2f Halbt.", (double)(pBip(v) * 0.25f));
        else            snprintf(out, n, "%.0f %%", (double)(v * 100.0f / 127.0f));
    }

    static int slotA() { static const int s = paramSlot(paramFind("trk.src.a")); return s; }
    static int slotB() { static const int s = paramSlot(paramFind("trk.src.b")); return s; }

    void start(MState& m, float hz, float vel, const float* p, float sr,
               const SamplePool& pool) const override {
        St& s = m.as<St>();
        s.a.reset(0.0f);
        s.b.reset(0.25f);           // feste Phasenlage: gleicher Anschlag klingt immer gleich.
                                    // NICHT 0.5: bei gleicher Frequenz loeschte das die Grundwelle aus.
        s.width = 0.5f;
        (void)vel; (void)p; (void)hz; (void)sr;
    }
    void render(MState& m, const float* p, float hz, float* buf, float* bufR, int n, float sr,
                const SamplePool& pool) const override {
        St& s = m.as<St>();
        float dt = pBip(p[slotA()]) * 0.25f;          // bis zu einem Viertelton Verstimmung
        float shape = pUni(p[slotB()]);               // 0 = Saege, 1 = schmaler Puls
        float w = 0.5f - 0.45f * shape;
        s.a.setHz(hz, sr);
        s.b.setHz(hz * powf(2.0f, dt / 12.0f), sr);
        for (int i = 0; i < n; i++) {
            float x = (1.0f - shape) * s.a.saw() + shape * s.a.pulse(w);
            float y = (1.0f - shape) * s.b.saw() + shape * s.b.pulse(w);
            buf[i] = 0.5f * (x + y);
            s.a.advance(); s.b.advance();
        }
    }
};

// --- FM: zwei Operatoren, Phasenmodulation ----------------------------------------------------
// Phasenmodulation statt echter Frequenzmodulation - die Tonhoehe bleibt dabei stehen.
// Ganzzahlige Verhaeltnisse klingen harmonisch, krumme glockig und metallisch.
struct MachFm : Machine {
    struct St { Osc car, mod; float z; };
    static_assert(sizeof(St) <= MACH_STATE, "Zustand passt nicht in den Block");
    static const float* ratios() {
        static const float r[16] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f,
                                    5.0f, 6.0f, 7.0f, 8.0f, 1.41f, 2.73f, 5.19f, 9.42f};
        return r;
    }
    static int ratioIdx(float v) { int i = (int)(v * 16.0f / 128.0f); return i < 0 ? 0 : (i > 15 ? 15 : i); }

    int defA() const override { return 24; }     // Verhaeltnis 2:1
    int defB() const override { return 40; }
    const char* name()   const override { return "FM"; }
    const char* labelA() const override { return "RATIO"; }
    const char* labelB() const override { return "INDEX"; }
    void text(int which, float v, char* out, int n) const override {
        if (which == 0) {
            float r = ratios()[ratioIdx(v)];
            snprintf(out, n, "%.2f : 1", (double)r);
        } else snprintf(out, n, "%.0f %%", (double)(v * 100.0f / 127.0f));
    }
    static int slotA() { static const int s = paramSlot(paramFind("trk.src.a")); return s; }
    static int slotB() { static const int s = paramSlot(paramFind("trk.src.b")); return s; }

    void start(MState& m, float hz, float vel, const float* p, float sr,
               const SamplePool& pool) const override {
        St& s = m.as<St>();
        s.car.reset(0.0f); s.mod.reset(0.0f); s.z = 0.0f;
        (void)hz; (void)vel; (void)p; (void)sr;
    }
    void render(MState& m, const float* p, float hz, float* buf, float* bufR, int n, float sr,
                const SamplePool& pool) const override {
        St& s = m.as<St>();
        float r = ratios()[ratioIdx(p[slotA()])];
        float idx = pUni(p[slotB()]) * 8.0f;
        s.car.setHz(hz, sr);
        s.mod.setHz(hz * r, sr);
        for (int i = 0; i < n; i++) {
            float mo = s.mod.sine();
            float ph = s.car.ph + idx * mo * 0.15f;
            ph -= (float)(int)ph;
            if (ph < 0.0f) ph += 1.0f;
            buf[i] = sinf(6.2831853f * ph);
            s.car.advance(); s.mod.advance();
        }
    }
};

// --- DRUM: Sinus mit fallender Tonhoehe plus Rauschen ------------------------------------------
// Das Grundrezept jeder Drum-Maschine: ein schneller Tonhoehenfall macht den Anschlag,
// Rauschen macht Snare und Hi-Hat. Die Huellkurve kommt wie ueberall von der Spur.
struct MachDrum : Machine {
    struct St { Osc osc; float env; uint32_t rng; };
    static_assert(sizeof(St) <= MACH_STATE, "Zustand passt nicht in den Block");

    int defA() const override { return 96; }     // kraeftiger Sweep
    int defB() const override { return 0; }
    const char* name()   const override { return "DRUM"; }
    const char* labelA() const override { return "SWEEP"; }
    const char* labelB() const override { return "RAUSCH"; }
    void text(int which, float v, char* out, int n) const override {
        if (which == 0) snprintf(out, n, "%.0f Halbt.", (double)(pUni(v) * 48.0f));
        else            snprintf(out, n, "%.0f %%", (double)(v * 100.0f / 127.0f));
    }
    static int slotA() { static const int s = paramSlot(paramFind("trk.src.a")); return s; }
    static int slotB() { static const int s = paramSlot(paramFind("trk.src.b")); return s; }

    void start(MState& m, float hz, float vel, const float* p, float sr,
               const SamplePool& pool) const override {
        St& s = m.as<St>();
        s.osc.reset(0.0f);
        s.env = 1.0f;
        s.rng = 0x1234567u + (uint32_t)(hz * 7.0f);
        (void)vel; (void)p; (void)sr;
    }
    void render(MState& m, const float* p, float hz, float* buf, float* bufR, int n, float sr,
                const SamplePool& pool) const override {
        St& s = m.as<St>();
        float sweep = pUni(p[slotA()]) * 48.0f;        // Halbtoene nach oben beim Anschlag
        float mix = pUni(p[slotB()]);
        float c = expf(-1.0f / (0.035f * sr));         // Fall in rund 35 ms
        for (int i = 0; i < n; i++) {
            s.env *= c;
            s.osc.setHz(hz * exp2f(sweep * s.env / 12.0f), sr);
            s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
            float nz = (float)(int32_t)s.rng * (1.0f / 2147483648.0f);
            buf[i] = (1.0f - mix) * s.osc.sine() + mix * nz * 0.7f;
            s.osc.advance();
        }
    }
};

// --- SMPL: ein Sample abspielen --------------------------------------------------------------
// Die Tonhoehe entsteht aus der Abspielgeschwindigkeit, wie bei jedem Sampler. Der Platz
// steht in trk.smp und ist lockbar: jeder Step kann ein anderes Sample anschlagen.
struct MachSmpl : Machine {
    struct St { double pos; int32_t at, n, e, la; uint8_t loop, ch; float step; };
    static_assert(sizeof(St) <= MACH_STATE, "Zustand passt nicht in den Block");

    int defA() const override { return 0; }      // am Anfang des Samples
    int defB() const override { return 64; }
    const char* name()   const override { return "SMPL"; }
    bool usesSample() const override { return true; }
    const char* labelA() const override { return "START"; }
    const char* labelB() const override { return "FEIN"; }
    void text(int which, float v, char* out, int n) const override {
        if (which == 0) snprintf(out, n, "%.0f %%", (double)(v * 100.0f / 127.0f));
        else            snprintf(out, n, "%+.0f ct", (double)(pBip(v) * 100.0f));
    }
    static int slotA()  { static const int s = paramSlot(paramFind("trk.src.a")); return s; }
    static int slotB()  { static const int s = paramSlot(paramFind("trk.src.b")); return s; }
    static int slotSmp(){ static const int s = paramSlot(paramFind("trk.smp")); return s; }

    void start(MState& m, float hz, float vel, const float* p, float sr,
               const SamplePool& pool) const override {
        St& st = m.as<St>();
        int k = (int)(p[slotSmp()] + 0.5f);
        if (pool.empty(k)) { st.n = 0; st.pos = 0; return; }
        const SamplePool::Slot& sl = pool.s[k];
        st.at = sl.at; st.n = sl.n; st.loop = sl.loop; st.ch = sl.ch;
        st.e = sl.end(); st.la = sl.loopStart();
        // START verschiebt innerhalb des Ausschnitts, nicht im ganzen Sample.
        st.pos = (double)sl.a + (double)(int)(pUni(p[slotA()]) * (st.e - sl.a - 1));
        (void)vel; (void)hz; (void)sr;
    }
    bool stereo(const float* p, const SamplePool& pool) const override {
        int k = (int)(p[slotSmp()] + 0.5f);
        return !pool.empty(k) && pool.s[k].ch == 2;
    }
    void render(MState& m, const float* p, float hz, float* buf, float* bufR, int n, float sr,
                const SamplePool& pool) const override {
        St& st = m.as<St>();
        if (st.n <= 0) {
            for (int i = 0; i < n; i++) { buf[i] = 0.0f; if (bufR) bufR[i] = 0.0f; }
            return;
        }
        int k = (int)(p[slotSmp()] + 0.5f);
        float rate = pool.empty(k) ? 48000.0f : pool.s[k].rate;
        // Note 60 spielt das Sample in seiner Aufnahmegeschwindigkeit ab.
        float cents = pBip(p[slotB()]) * 100.0f;
        float step = (hz / noteHz(60.0f)) * (rate / sr) * exp2f(cents / 1200.0f);
        const int c = st.ch;
        const double ende = (double)(st.e - 1);
        for (int i = 0; i < n; i++) {
            if (st.pos >= ende) {
                if (!st.loop || st.e - st.la < 2) { buf[i] = 0.0f; if (bufR) bufR[i] = 0.0f; continue; }
                st.pos = (double)st.la + (st.pos - ende);     // zurueck an den Schleifenanfang
            }
            int i0 = (int)st.pos;
            float f = (float)(st.pos - (double)i0);
            int j0 = st.at + i0 * c, j1 = st.at + (i0 + 1 < st.e ? i0 + 1 : i0) * c;
            buf[i] = pool.data[j0] + (pool.data[j1] - pool.data[j0]) * f;   // lineare Interpolation
            if (bufR)
                bufR[i] = c == 2 ? pool.data[j0 + 1] + (pool.data[j1 + 1] - pool.data[j0 + 1]) * f
                                 : buf[i];
            st.pos += step;
        }
    }
};

// Register aller Machines. trk.machine ist der Index hier - die Reihenfolge darf nie wechseln.
inline const Machine* machineAt(int i) {
    static const MachPoly poly;
    static const MachFm   fm;
    static const MachDrum drum;
    static const MachSmpl smpl;
    static const Machine* const all[] = { &poly, &fm, &drum, &smpl };
    enum { N = (int)(sizeof all / sizeof *all) };
    if (i < 0 || i >= N) i = 0;
    return all[i];
}
inline int machineCount() { return 4; }

// Eine andere Machine waehlen heisst: ihre beiden Regler fangen bei IHREN Grundwerten an.
// Sonst steht der Sampler beim Umschalten mitten im Sample.
inline void machineSelect(int16_t* trackParams, int mach) {
    static const int sM = paramSlot(paramFind("trk.machine"));
    static const int sA = paramSlot(paramFind("trk.src.a"));
    static const int sB = paramSlot(paramFind("trk.src.b"));
    const Machine* m = machineAt(mach);
    trackParams[sM] = (int16_t)mach;
    trackParams[sA] = (int16_t)m->defA();
    trackParams[sB] = (int16_t)m->defB();
}
