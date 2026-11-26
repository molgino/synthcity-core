// Machines: austauschbare Klangquellen hinter EINEM festen Satz Regler (Elektron-Prinzip).
// Eine Machine kennt nur ihren eigenen Zustand und die Spurparameter - nicht die Spur, nicht
// den Sequencer. Der Zustand liegt in einem festen Block, damit nie Speicher angefordert wird.
#pragma once
#include "dsp.h"
#include "param.h"
#include "sample.h"

enum { MACH_STATE = 256, MACH_CHUNK = 32 };

struct MState { alignas(16) unsigned char raw[MACH_STATE];
    uint32_t seed = 1;          // vom Anschlag gesetzt: Zufall, der trotzdem reproduzierbar ist
    uint8_t  step = 0;          // vom Anschlag gesetzt: auf welchem Step (fuer CHOP FOLLOW STEP)
    template <class T> T& as() { return *(T*)raw; }
    template <class T> const T& as() const { return *(const T*)raw; }
};

enum { MACH_KNOBS = 8 };
// Die acht Machine-Regler trk.src.a .. trk.src.h - ihre Bedeutung sagt die Machine.
inline int knobSlot(int k) {
    static int s[MACH_KNOBS] = {-1};
    if (s[0] < 0) {
        static const char* ids[MACH_KNOBS] = {"trk.src.a", "trk.src.b", "trk.src.c", "trk.src.d",
                                              "trk.src.e", "trk.src.f", "trk.src.g", "trk.src.h"};
        for (int k2 = 0; k2 < MACH_KNOBS; k2++) s[k2] = paramSlot(paramFind(ids[k2]));
    }
    return s[k < 0 ? 0 : (k >= MACH_KNOBS ? MACH_KNOBS - 1 : k)];
}
inline uint32_t machRng(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
inline float machNoise(uint32_t& s) { return (float)(int32_t)machRng(s) * (1.0f / 2147483648.0f); }
// Weiche Saettigung ohne tanhf: rational, bei |x| > 3 begrenzt.
inline float softSat(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct AdsrSlots {
    int attack, decay, sustain, release;
    void setup(Env& env, const float* p, float sr) const {
        env.setup(pTime(p[attack]), pTime(p[decay]), p[sustain] * 0.01f, pTime(p[release]), sr);
    }
};

inline AdsrSlots adsrSlots(const char* prefix) {
    const char* fields[] = {"atk", "dec", "sus", "rel"};
    int slots[4];
    for (int i = 0; i < 4; i++) {
        char id[32];
        snprintf(id, sizeof id, "%s.%s", prefix, fields[i]);
        slots[i] = paramSlot(paramFind(id));
    }
    return {slots[0], slots[1], slots[2], slots[3]};
}

struct Machine {
    virtual ~Machine() {}
    virtual const char* name() const = 0;
    // Anzeige fuer Regler k (0..7); "" = diese Machine braucht den Regler nicht.
    virtual const char* label(int k) const = 0;
    // Wie der Wert heisst, weiss nur die Machine: 64 ist bei der einen "kein Detune",
    // bei der anderen "Verhaeltnis 2:1". Vorgabe ist die nackte Zahl.
    virtual void text(int k, float v, char* out, int n) const {
        (void)k; snprintf(out, n, "%.0f", (double)v);
    }
    virtual void  start(MState& m, float hz, float vel, const float* p, float sr,
                        const SamplePool& pool) const = 0;
    // Fuellt n Samples (n <= MACH_CHUNK). Ein Aufruf je Teilblock, nicht je Sample.
    // bufR ist nur belegt, wenn stereo() wahr ist - sonst kostet Stereo jede Mono-Machine
    // einen zweiten Filter umsonst.
    virtual void  render(MState& m, const float* p, float hz, float* buf, float* bufR, int n,
                         float sr, const SamplePool& pool) const = 0;
    virtual void release(MState&) const {}
    virtual bool usesGate(const MState&) const { return false; }
    virtual bool  stereo(const float* p, const SamplePool& pool) const { (void)p; (void)pool; return false; }
    // Grundwert von Regler k - jede Machine sagt selbst, wo sie anfaengt.
    virtual int   def(int k) const { (void)k; return 64; }
    // Spielt die Machine ein Sample aus dem Vorrat? Nur dann braucht die Spur trk.smp.
    virtual bool  usesSample() const { return false; }
    // Hat Regler k feste Stufen (n > 0)? Dann ist Stufe i der Wert i * 128 / n + 64 / n.
    virtual int   steps(int k) const { (void)k; return 0; }
    const char* labelA() const { return label(0); }
    const char* labelB() const { return label(1); }
    int defA() const { return def(0); }
    int defB() const { return def(1); }
};
inline float knob(const float* p, int k) { return p[knobSlot(k)]; }

// --- POLY: eine analoge Stimme ----------------------------------------------------------------
// Wie eine analoge Schaltung: zwei Oszillatoren (bandbegrenzt, Saege bis Puls), ein Sub eine
// Oktave tiefer, Rauschen, und dazu das, was echte Schaltungen ausmacht - jede Stimme beginnt
// mit einer anderen Phase, die Stimmung wandert langsam (DRIFT), und vor dem Filter saettigt
// die Mischung (DRIVE). Das Filter ist das der Spur; der Typ LADDER ist der analoge 24-dB-Tiefpass.
struct MachPoly : Machine {
    struct St { Osc a, b, sub; float dr[2], drz[2]; int drn; uint32_t rng; };
    static_assert(sizeof(St) <= MACH_STATE, "Zustand passt nicht in den Block");

    const char* name() const override { return "POLY"; }
    const char* label(int k) const override {
        static const char* l[MACH_KNOBS] = {"DETUNE", "SHAPE", "PW", "OSC2", "SUB", "NOISE", "DRIFT", "DRIVE"};
        return l[k & 7];
    }
    int def(int k) const override {
        static const int d[MACH_KNOBS] = {64, 64, 0, 64, 0, 0, 24, 16};
        return d[k & 7];
    }
    struct OscSlots { int wave, level, tune, fine, width; };
    static OscSlots oscSlots(int number) {
        const char* names[] = {"wave", "level", "tune", "fine", "pw"};
        int slots[5];
        for (int i = 0; i < 5; i++) {
            char id[32];
            snprintf(id, sizeof id, "trk.poly%d.%s", number, names[i]);
            slots[i] = paramSlot(paramFind(id));
        }
        return {slots[0], slots[1], slots[2], slots[3], slots[4]};
    }
    // Welche Rechnung eine Wellenform braucht, steht je Block fest - im Sample-Takt bleibt
    // nur noch eine Weiche (gleiche Rechnung wie zuvor je Sample, bitgleiches Ergebnis).
    enum { W_MIX, W_SAW, W_PULSE, W_SINE };
    static int waveMode(int wave, float shape) {
        if (wave == 1 || (wave == 0 && shape == 0)) return W_SAW;
        if (wave == 2 || (wave == 0 && shape == 1)) return W_PULSE;
        if (wave == 3) return W_SINE;
        return W_MIX;
    }
    static float waveOf(const Osc& osc, int mode, float shape, float width) {
        switch (mode) {
        case W_SAW:   return osc.saw();
        case W_PULSE: return osc.pulse(width);
        case W_SINE:  return osc.sine();
        default: { float saw, pulse; osc.sawPulse(width, saw, pulse); return (1.0f - shape) * saw + shape * pulse; }
        }
    }
    static int osc2Semi(float v) { int s = (int)((v - 64.0f) * 24.0f / 63.0f + (v >= 64 ? 0.5f : -0.5f)); return s < -24 ? -24 : (s > 24 ? 24 : s); }
    void text(int k, float v, char* out, int n) const override {
        switch (k) {
        case 0: snprintf(out, n, "%+.0f ct", (double)(pBip(v) * 25.0f)); return;
        case 1: snprintf(out, n, v < 1 ? "SAW" : (v > 126 ? "PULSE" : "%.0f %% pulse"), (double)(v * 100.0f / 127.0f)); return;
        case 2: if (v < 0.5f) snprintf(out, n, "AUTO"); else snprintf(out, n, "%.0f %%", (double)(50.0f - 45.0f * pUni(v))); return;
        case 3: snprintf(out, n, "%+d semi", osc2Semi(v)); return;
        default: snprintf(out, n, "%.0f %%", (double)(v * 100.0f / 127.0f));
        }
    }
    void start(MState& m, float hz, float vel, const float* p, float sr,
               const SamplePool& pool) const override {
        St& s = m.as<St>();
        s.rng = m.seed | 1u;
        float drift = pUni(knob(p, 6));
        // Freie Phasen wie bei analogen Oszillatoren - ohne DRIFT bleibt es reproduzierbar.
        s.a.reset(drift > 0 ? (machRng(s.rng) >> 8) * (1.0f / 16777216.0f) : 0.0f);
        s.b.reset(drift > 0 ? (machRng(s.rng) >> 8) * (1.0f / 16777216.0f) : 0.25f);
        s.sub.reset(s.a.ph * 0.5f);
        for (int i = 0; i < 2; i++) { s.dr[i] = machNoise(s.rng) * drift; s.drz[i] = s.dr[i]; }
        s.drn = 0;
        (void)vel; (void)sr; (void)pool;
    }
    void render(MState& m, const float* p, float hz, float* buf, float* bufR, int n, float sr,
                const SamplePool& pool) const override {
        St& s = m.as<St>();
        float dtCt = pBip(knob(p, 0)) * 25.0f;            // bis +-25 Cent zwischen den Oszillatoren
        float shape = pUni(knob(p, 1));                    // 0 = Saege, 1 = Puls
        // Pulsbreite 50 % .. 5 %; PW 0 = AUTO: die Breite folgt SHAPE (so klang POLY immer)
        float pw = knob(p, 2);
        float w = 0.5f - 0.45f * (pw < 0.5f ? shape : pUni(pw));
        int semi2 = osc2Semi(knob(p, 3));
        float subL = pUni(knob(p, 4)), noise = pUni(knob(p, 5));
        float drift = pUni(knob(p, 6)), drive = pUni(knob(p, 7));
        // DRIFT: alle ~40 ms ein neues Ziel, langsam angesteuert - bis +-15 Cent je Oszillator.
        // Gezaehlt wird je Sample: so klingt es bei jeder Blockgroesse gleich.
        const int periode = (int)(sr * 0.04f);
        const float glatt = 1.0f / (sr * 0.03f);
        static const OscSlots a = oscSlots(1), b = oscSlots(2);
        const float h1 = hz * exp2f((100.0f * p[a.tune] + p[a.fine]) / 1200.0f);
        s.sub.setHz(h1 * 0.5f, sr);
        s.a.setHz(h1, sr);
        s.b.setHz(hz * exp2f((dtCt + 100.0f * (semi2 + p[b.tune]) + p[b.fine]) / 1200.0f), sr);
        const float w1 = p[a.width] ? p[a.width] * 0.01f : w, w2 = p[b.width] ? p[b.width] * 0.01f : w;
        const float lo1 = p[a.level] * 0.01f, lo2 = p[b.level] * 0.01f;
        const int wave1 = (int)p[a.wave], wave2 = (int)p[b.wave];
        const int mode1 = waveMode(wave1, shape), mode2 = waveMode(wave2, shape);
        const float incA = s.a.inc, incB = s.b.inc, incS = s.sub.inc;
        float g = 1.0f + drive * 5.0f, norm = 1.0f / (1.0f + 0.6f * drive);
        float mixN = 0.5f / (1.0f + 0.5f * subL + 0.4f * noise);
        for (int i = 0; i < n; i++) {
            if (drift > 0.0f) {
                if (++s.drn >= periode) { s.drn = 0; for (int k = 0; k < 2; k++) s.drz[k] = machNoise(s.rng) * drift; }
                for (int k = 0; k < 2; k++) s.dr[k] += (s.drz[k] - s.dr[k]) * glatt;
                float fa = 1.0f + s.dr[0] * 0.00867f, fb = 1.0f + s.dr[1] * 0.00867f;   // 15 Cent
                s.a.inc = incA * fa; s.b.inc = incB * fb; s.sub.inc = incS * fa;
            }
            // Was mit 0 gewichtet wird, muss nicht gerechnet werden (das Ergebnis ist bitgleich).
            const float x = lo1 ? lo1 * waveOf(s.a, mode1, shape, w1) : 0;
            const float y = lo2 ? lo2 * waveOf(s.b, mode2, shape, w2) : 0;
            float sb = subL != 0.0f ? s.sub.pulse(0.5f) : 0.0f;
            float nz = machNoise(s.rng);                 // Zufall laeuft immer weiter (DRIFT teilt ihn)
            float v = (x + y + subL * sb + noise * 0.8f * nz) * mixN;
            buf[i] = drive > 0.001f ? softSat(v * g) * norm : v;
            s.a.advance(); s.b.advance(); s.sub.advance();
        }
        (void)bufR; (void)pool;
    }
};

// --- FM: vier Operatoren, acht Algorithmen ---------------------------------------------------
// Operatoren C (Traeger, Verhaeltnis 1), A, B1 und B2 (B2 = B1 eine Oktave hoeher, mit
// Feedback). Phasenmodulation - die Tonhoehe bleibt stehen. Die Modulatoren klingen mit einer
// eigenen Huellkurve ab (MOD DECAY): hell im Anschlag, weich im Ausklang - das, was FM lebendig
// macht. Gerechnet wird in der Reihenfolge B2, B1, A, C: jeder kann nur von frueheren moduliert werden.
struct MachFm : Machine {
    struct St { float ph[4]; float out[4]; float fb; float menv; Env env[4]; unsigned gated; };
    static_assert(sizeof(St) <= MACH_STATE, "Zustand passt nicht in den Block");
    static const float* ratios() {
        static const float r[16] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f,
                                    5.0f, 6.0f, 7.0f, 8.0f, 1.41f, 2.73f, 5.19f, 9.42f};
        return r;
    }
    static int ratioIdx(float v) { int i = (int)(v * 16.0f / 128.0f); return i < 0 ? 0 : (i > 15 ? 15 : i); }
    // MOD DECAY: 0 = aus (Modulation bleibt stehen, wie frueher), sonst 10 ms .. 5 s
    static float modDecay(float v) { return 0.01f * powf(500.0f, (v - 1.0f) / 126.0f); }
    static int algo(float v) { int a = (int)(v * 8.0f / 128.0f); return a < 0 ? 0 : (a > 7 ? 7 : a); }
    enum { OC = 0, OA = 1, OB1 = 2, OB2 = 3 };
    struct OperatorSlots { int ratio, level, fine, enable; AdsrSlots adsr; };
    static OperatorSlots operatorSlots(int number) {
        char prefix[16], id[32];
        snprintf(prefix, sizeof prefix, "trk.fm%d", number);
        const char* fields[] = {"ratio", "level", "fine", "env"};
        int slots[4];
        for (int i = 0; i < 4; i++) {
            snprintf(id, sizeof id, "%s.%s", prefix, fields[i]);
            slots[i] = paramSlot(paramFind(id));
        }
        return {slots[0], slots[1], slots[2], slots[3], adsrSlots(prefix)};
    }
    static const OperatorSlots* operators() {
        static const OperatorSlots slots[] = {operatorSlots(1), operatorSlots(2), operatorSlots(3), operatorSlots(4)};
        return slots;
    }
    bool usesGate(const MState& m) const override { return m.as<St>().gated != 0; }
    void release(MState& m) const override {
        St& s = m.as<St>();
        for (int i = 0; i < 4; i++) if (s.gated & (1u << i)) s.env[i].release();
    }
    // je Algorithmus: wer moduliert wen (Bitmaske der Quellen je Ziel) und wer klingt
    struct Algo { uint8_t mod[4]; uint8_t car; const char* bild; };
    static const Algo& alg(int a) {
        static const Algo t[8] = {
            {{1 << OA, 1 << OB1, 1 << OB2, 0}, 1 << OC,                         "B2>B1>A>C"},
            {{(1 << OB1) | (1 << OA), 0, 1 << OB2, 0}, 1 << OC,                "B2>B1, B1+A>C"},
            {{1 << OA, 0, 1 << OB2, 0}, (1 << OC) | (1 << OB1),                "A>C + B2>B1"},
            {{1 << OA, (1 << OB1) | (1 << OB2), 0, 0}, 1 << OC,                "B1+B2>A>C"},
            {{1 << OA, 1 << OB1, 1 << OB2, 0}, (1 << OC) | (1 << OA),          "B2>B1>A>C, A out"},
            {{1 << OB1, 1 << OB2, 0, 0}, (1 << OC) | (1 << OA),                "B1>C + B2>A"},
            {{1 << OB2, 1 << OB2, 1 << OB2, 0}, (1 << OC) | (1 << OA) | (1 << OB1), "B2>C,A,B1"},
            {{0, 0, 0, 0}, 0x0F,                                               "C+A+B1+B2"}};
        return t[a & 7];
    }
    const char* name() const override { return "FM"; }
    int steps(int k) const override { return k == 2 ? 8 : (k == 0 || k == 5 ? 16 : 0); }
    const char* label(int k) const override {
        static const char* l[MACH_KNOBS] = {"RATIO A", "LEVEL A", "ALGO", "DETUNE", "LEVEL B", "RATIO B", "FEEDBACK", "MOD DECAY"};
        return l[k & 7];
    }
    int def(int k) const override {
        static const int d[MACH_KNOBS] = {24, 40, 0, 64, 40, 24, 0, 60};
        return d[k & 7];
    }
    void text(int k, float v, char* out, int n) const override {
        switch (k) {
        case 2: snprintf(out, n, "%d  %s", algo(v) + 1, alg(algo(v)).bild); return;
        case 0: case 5: snprintf(out, n, "%.2f : 1", (double)ratios()[ratioIdx(v)]); return;
        case 7: if (v < 0.5f) snprintf(out, n, "OFF"); else snprintf(out, n, "%.2f s", (double)modDecay(v)); return;
        case 3: snprintf(out, n, "%+.0f ct", (double)(pBip(v) * 30.0f)); return;
        default: snprintf(out, n, "%.0f %%", (double)(v * 100.0f / 127.0f));
        }
    }
    void start(MState& m, float hz, float vel, const float* p, float sr,
               const SamplePool& pool) const override {
        St& s = m.as<St>();
        for (int i = 0; i < 4; i++) { s.ph[i] = 0.0f; s.out[i] = 0.0f; }
        s.fb = 0.0f; s.menv = 1.0f; s.gated = 0;
        const OperatorSlots* op = operators();
        for (int i = 0; i < 4; i++) {
            if (p[op[i].enable] <= 0.5f) continue;
            s.gated |= 1u << i;
            s.env[i].kill();
            op[i].adsr.setup(s.env[i], p, sr);
            s.env[i].gate();
        }
        (void)hz; (void)vel; (void)pool;
    }
    void render(MState& m, const float* p, float hz, float* buf, float* bufR, int n, float sr,
                const SamplePool& pool) const override {
        St& s = m.as<St>();
        const Algo& a = alg(algo(knob(p, 2)));
        float rA = ratios()[ratioIdx(knob(p, 0))], rB = ratios()[ratioIdx(knob(p, 5))];
        float lA = pUni(knob(p, 1)), lB = pUni(knob(p, 4));
        float fbk = pUni(knob(p, 6)) * 0.35f;
        float dv = knob(p, 7);
        float mc = dv < 0.5f ? 1.0f : expf(-1.0f / (modDecay(dv) * sr));
        float det = exp2f(pBip(knob(p, 3)) * 30.0f / 1200.0f);
        float inc[4] = {hz / sr, hz * rA * det / sr, hz * rB / det / sr, hz * rB * 2.0f * det / sr};
        // Index als Modulator: bis ~3,5 Umdrehungen (unten wie die alte 2-Operator-FM); als Traeger: Lautstaerke
        float vol[4] = {1.0f, lA, lB, lB};
        const OperatorSlots* op = operators();
        for (int o = 0; o < 4; o++) {
            if (p[op[o].ratio] > 0) inc[o] = hz * p[op[o].ratio] * (0.01f / sr);
            if (p[op[o].fine] != 0) inc[o] *= exp2f(p[op[o].fine] / 1200.0f);
            if (inc[o] > 0.49f && (p[op[o].ratio] > 0 || p[op[o].fine] != 0)) inc[o] = 0.49f;
            if (p[op[o].level] >= 0) vol[o] = p[op[o].level] * 0.01f;
        }
        float idx[4] = {1.0f};
        for (int o = 1; o < 4; o++) idx[o] = vol[o] * 1.2f + vol[o] * vol[o] * vol[o] * 2.3f;
        int nCar = 0; for (int o = 0; o < 4; o++) if (a.car & (1 << o)) nCar++;
        float norm = 1.0f / (float)(nCar ? nCar : 1);
        static const int folge[4] = {OB2, OB1, OA, OC};
        for (int i = 0; i < n; i++) {
            s.menv *= mc;
            float me = dv < 0.5f ? 1.0f : 0.15f + 0.85f * s.menv;   // nie ganz weg: FM bleibt FM
            float mix = 0.0f;
            for (int f = 0; f < 4; f++) {
                int o = folge[f];
                float pm = 0.0f;
                for (int q = 0; q < 4; q++) if (a.mod[o] & (1 << q)) pm += s.out[q] * idx[q] * (s.gated & (1u << q) ? 1.0f : me);
                if (o == OB2) pm += s.fb * fbk;
                float ph = s.ph[o] + pm;
                ph -= floorf(ph);
                float y = sinf(6.2831853f * ph);
                if (s.gated & (1u << o)) y *= s.env[o].tick();
                s.out[o] = y;
                if (o == OB2) s.fb = 0.5f * (s.fb + y);
                if (a.car & (1 << o)) mix += y * vol[o];
                s.ph[o] += inc[o]; if (s.ph[o] >= 1.0f) s.ph[o] -= 1.0f;
            }
            buf[i] = mix * norm;
        }
        (void)bufR; (void)pool;
    }
};

// --- DRUM: Sinus mit fallender Tonhoehe plus Rauschen ------------------------------------------
// Das Grundrezept jeder Drum-Maschine: ein schneller Tonhoehenfall macht den Anschlag,
// Rauschen macht Snare und Hi-Hat. Die Huellkurve kommt wie ueberall von der Spur.
struct MachDrum : Machine {
    struct St { Osc osc; float env; uint32_t rng; float hp; };
    static_assert(sizeof(St) <= MACH_STATE, "Zustand passt nicht in den Block");

    int def(int k) const override { static const int d[MACH_KNOBS] = {96, 0, 0, 64, 0, 0, 0, 0}; return d[k & 7]; }
    // SWEEP T: 0 = 35 ms (so war es immer), hinauf bis 200 ms
    static float sweepT(float v) { return 0.035f * exp2f(pUni(v) * 2.5f); }
    const char* name() const override { return "DRUM"; }
    const char* label(int k) const override {
        static const char* l[MACH_KNOBS] = {"SWEEP", "NOISE", "SWEEP T", "", "DRIVE", "", "", ""};
        return l[k & 7];
    }
    void text(int k, float v, char* out, int n) const override {
        if (k == 0) snprintf(out, n, "%.0f semi", (double)(pUni(v) * 48.0f));
        else if (k == 2) snprintf(out, n, "%.0f ms", (double)(sweepT(v) * 1000.0f));
        else snprintf(out, n, "%.0f %%", (double)(v * 100.0f / 127.0f));
    }
    void start(MState& m, float hz, float vel, const float* p, float sr,
               const SamplePool& pool) const override {
        St& s = m.as<St>();
        s.osc.reset(0.0f);
        s.env = 1.0f; s.hp = 0.0f;
        s.rng = 0x1234567u + (uint32_t)(hz * 7.0f);
        (void)vel; (void)p; (void)sr; (void)pool;
    }
    void render(MState& m, const float* p, float hz, float* buf, float* bufR, int n, float sr,
                const SamplePool& pool) const override {
        St& s = m.as<St>();
        float sweep = pUni(knob(p, 0)) * 48.0f;        // Halbtoene nach oben beim Anschlag
        float mix = pUni(knob(p, 1));
        float c = expf(-1.0f / (sweepT(knob(p, 2)) * sr));
        float drive = pUni(knob(p, 4)), g = 1.0f + drive * 6.0f, norm = 1.0f / (1.0f + 0.5f * drive);
        for (int i = 0; i < n; i++) {
            s.env *= c;
            s.osc.setHz(hz * exp2f(sweep * s.env / 12.0f), sr);
            float nz = machNoise(s.rng);
            float x = (1.0f - mix) * s.osc.sine() + mix * nz * 0.7f;
            buf[i] = drive > 0.001f ? softSat(x * g) * norm : x;
            s.osc.advance();
        }
        (void)bufR; (void)pool;
    }
};

// --- SMPL: ein Sample abspielen --------------------------------------------------------------
// Die Tonhoehe entsteht aus der Abspielgeschwindigkeit, wie bei jedem Sampler. Der Platz
// steht in trk.smp und ist lockbar: jeder Step kann ein anderes Sample anschlagen.
// Das Tempo, das gerade gilt - fuer Machines, die im Takt laufen (Sampler-Warp). Die Engine
// setzt es je Block; eine Machine liest es nur.
inline float& machTempo() { static float t = 120.0f; return t; }

// --- SMPL: der Sampler -----------------------------------------------------------------------
// Abspielen mit Loop (wie im Editor, aus, vorwaerts, Ping-Pong), rueckwaerts, mit Laenge und
// grober Stimmung - und WARP: REPITCH laesst das Sample im Tempo laufen wie ein Band (die
// Tonhoehe geht mit), TIME dehnt es granular, so dass Tempo und Tonhoehe getrennt sind - die
// Note stimmt dann nur noch, die Dauer kommt aus Sample-BPM und Projekt-Tempo.
struct MachSmpl : Machine {
    struct St {
        double pos;                 // Lesekopf (Quelle, in Bildern)
        double gp[2];               // Startpunkt der beiden Koerner (TIME)
        float gph[2];               // Phase der Koerner 0..1
        int32_t at, s, e, la;       // Block-Lage, Bereich [s, e), Schleifenanfang
        int32_t wspan;              // worauf sich BEATS bezieht (ganzes Sample bei CHOP)
        int8_t dir;                 // +1 vorwaerts, -1 rueckwaerts
        uint8_t ch, loop, done;     // loop: 0 aus, 1 vorwaerts, 2 Ping-Pong
        uint8_t fadeIn;             // Start mitten im Sample: kurz einblenden
        uint16_t fin;               // Samples seit dem Start (fuer das Einblenden)
    };
    // Gegen Knackser: ein Start mitten in der Welle (START, Slice, rueckwaerts) springt sonst von 0
    // auf den Wert dort, und ein Ende (Slice, LENGTH) springt zurueck auf 0. Beides wird ueber
    // ~1 ms ein- bzw. ausgeblendet - kurz genug, dass kein Anschlag weicher klingt.
    enum { FADE = 48 };
    static float fade(St& st, double step) {
        float g = 1.0f;
        if (st.fadeIn && st.fin < FADE) { st.fin++; g = (float)st.fin * (1.0f / FADE); }
        if (!st.loop && step > 0.0) {
            double rem = st.dir > 0 ? ((double)(st.e - 1) - st.pos) / step : (st.pos - (double)st.s) / step;
            if (rem < (double)FADE) g *= rem <= 0.0 ? 0.0f : (float)(rem * (1.0 / FADE));
        }
        return g;
    }
    static_assert(sizeof(St) <= MACH_STATE, "Zustand passt nicht in den Block");
    enum { K_START, K_FINE, K_LOOP, K_TUNE, K_REV, K_WARP, K_BEATS, K_LEN };
    // Welcher Regler spielt welche Rolle? -1: gibt es bei dieser Machine nicht (Grundwert gilt).
    virtual int kn(int role) const { return role; }
    float kv(const float* p, int role, float dflt) const { int k = kn(role); return k < 0 ? dflt : knob(p, k); }

    // Alle Grundwerte so, dass ein altes Projekt klingt wie vorher.
    int def(int k) const override { static const int d[MACH_KNOBS] = {0, 64, 0, 64, 0, 0, 0, 0}; return d[k & 7]; }
    const char* name() const override { return "SAMPLE"; }
    bool usesSample() const override { return true; }
    const char* label(int k) const override {
        static const char* l[MACH_KNOBS] = {"START", "FINE", "LOOP", "TUNE", "REVERSE", "WARP", "", "LENGTH"};
        return l[k & 7];
    }
    // Regler mit festen Stufen: die Oberflaeche zeigt sie als Liste.
    int steps(int k) const override {
        switch (k) { case K_LOOP: return 4; case K_REV: return 2; case K_WARP: return 3; case K_BEATS: return 10; default: return 0; }
    }
    static int stufe(float v, int n) { int i = (int)(v * n / 128.0f); return i < 0 ? 0 : (i >= n ? n - 1 : i); }
    static float beatsOf(int i) { static const float b[10] = {0, 0.5f, 1, 2, 4, 8, 16, 32, 64, 128}; return b[i < 0 ? 0 : (i > 9 ? 9 : i)]; }
    static int tune(float v) { int t = (int)((v - 64.0f) * 24.0f / 63.0f + (v >= 64 ? 0.5f : -0.5f)); return t < -24 ? -24 : (t > 24 ? 24 : t); }
    void text(int k, float v, char* out, int n) const override {
        switch (k) {
        case K_START: snprintf(out, n, "%.0f %%", (double)(v * 100.0f / 127.0f)); return;
        case K_FINE:  snprintf(out, n, "%+.0f ct", (double)(pBip(v) * 100.0f)); return;
        case K_LOOP:  { static const char* t[4] = {"SLOT", "OFF", "FORWARD", "PING-PONG"}; snprintf(out, n, "%s", t[stufe(v, 4)]); return; }
        case K_TUNE:  snprintf(out, n, "%+d semi", tune(v)); return;
        case K_REV:   snprintf(out, n, "%s", stufe(v, 2) ? "ON" : "OFF"); return;
        case K_WARP:  { static const char* t[3] = {"OFF", "REPITCH", "TIME"}; snprintf(out, n, "%s", t[stufe(v, 3)]); return; }
        case K_BEATS: { int b = stufe(v, 10); if (!b) snprintf(out, n, "AUTO"); else if (b == 1) snprintf(out, n, "1/2 beat");
                        else snprintf(out, n, "%.0f beat%s", (double)beatsOf(b), b == 2 ? "" : "s"); return; }
        default:      if (v < 0.5f) snprintf(out, n, "FULL"); else snprintf(out, n, "%.0f %%", (double)(v * 100.0f / 127.0f)); return;
        }
    }
    static int slotBpm() { static const int s = paramSlot(paramFind("trk.sample.bpm")); return s; }
    static int slotSmp(){ static const int s = paramSlot(paramFind("trk.smp")); return s; }

    void start(MState& m, float hz, float vel, const float* p, float sr,
               const SamplePool& pool) const override {
        St& st = m.as<St>();
        int k = (int)(p[slotSmp()] + 0.5f);
        st.done = 1;
        if (pool.empty(k)) return;
        const SamplePool::Slot& sl = pool.s[k];
        int a = sl.a, e = sl.end();
        // START verschiebt innerhalb des Ausschnitts, LENGTH kuerzt ab dort (0 = bis zum Ende)
        int s0 = a + (int)(pUni(kv(p, K_START, 0)) * (e - a - 1));
        bereich(st, sl, p, s0, e, e - a);
        (void)vel; (void)hz; (void)sr;
    }
    // Den Bereich [s0, ende) einrichten: LENGTH, Loop, Richtung. span = worauf BEATS sich bezieht.
    void bereich(St& st, const SamplePool::Slot& sl, const float* p, int s0, int ende, int span) const {
        float lv = kv(p, K_LEN, 0);
        int e0 = lv < 0.5f ? ende : s0 + 2 + (int)(pUni(lv) * (ende - s0 - 2));
        if (e0 > ende) e0 = ende;
        if (e0 - s0 < 2) return;
        st.at = sl.at; st.ch = sl.ch; st.s = s0; st.e = e0; st.wspan = span > 2 ? span : e0 - s0;
        int lm = kn(K_LOOP) < 0 ? 1 : stufe(knob(p, kn(K_LOOP)), 4);
        st.loop = lm == 0 ? (sl.loop ? 1 : 0) : (lm == 1 ? 0 : (lm == 2 ? 1 : 2));
        int la = sl.loopStart();
        st.la = (lm == 0 && la >= s0 && la < e0 - 1) ? la : s0;
        st.dir = stufe(kv(p, K_REV, 0), 2) ? -1 : 1;
        st.pos = st.dir > 0 ? (double)s0 : (double)(e0 - 1);
        st.gp[0] = st.gp[1] = st.pos; st.gph[0] = 0.0f; st.gph[1] = 0.5f;
        st.fin = 0;
        st.fadeIn = (s0 != sl.a || st.dir < 0) ? 1 : 0;   // am Anfang des Samples beginnt es von selbst sauber
        st.done = 0;
    }
    bool stereo(const float* p, const SamplePool& pool) const override {
        int k = (int)(p[slotSmp()] + 0.5f);
        return !pool.empty(k) && pool.s[k].ch == 2;
    }
    // Ein Bild an einer gebrochenen Stelle lesen (linear), ausserhalb des Bereichs still.
    static void lese(const St& st, const SamplePool& pool, double pos, float& l, float& r) {
        if (pos < (double)st.s || pos > (double)(st.e - 1)) { l = r = 0.0f; return; }
        int i0 = (int)pos;
        float f = (float)(pos - (double)i0);
        int i1 = i0 + 1 < st.e ? i0 + 1 : i0;
        const int c = st.ch;
        int j0 = st.at + i0 * c, j1 = st.at + i1 * c;
        l = pool.data[j0] + (pool.data[j1] - pool.data[j0]) * f;
        r = c == 2 ? pool.data[j0 + 1] + (pool.data[j1 + 1] - pool.data[j0 + 1]) * f : l;
    }
    // WSOLA: ein neues Korn beginnt nicht stur am Lesekopf, sondern dort in seiner Naehe (+-8 ms),
    // wo das Signal am besten zu dem passt, was das andere Korn als Naechstes spielt. So loeschen
    // sich die beiden in der Ueberblendung nicht aus, und die Tonhoehe bleibt sauber.
    static double passend(const St& st, const SamplePool& pool, int g, float gLen, double pstep) {
        const int h = 1 - g;
        const double weiterH = st.gp[h] + (double)(st.gph[h] * gLen) * pstep * st.dir;
        const double rad = 0.008 * 48000.0;
        double best = st.pos, bestV = -1e30;
        float ref[32];                       // was das andere Korn spielt - fuer alle Kandidaten gleich
        for (int j = 0; j < 64; j += 2) { float b2; lese(st, pool, weiterH + j * pstep * st.dir, ref[j / 2], b2); }
        for (double c = st.pos - rad; c <= st.pos + rad; c += 2.0) {
            double v = 0.0;
            for (int j = 0; j < 64; j += 2) {
                float a, a2;
                lese(st, pool, c + j * pstep * st.dir, a, a2);
                v += (double)(a * ref[j / 2]);
            }
            if (v > bestV) { bestV = v; best = c; }
        }
        return best;
    }
    // Den Lesekopf um d weiterschieben - mit Loop, Ping-Pong und Ende.
    static void weiter(St& st, double d) {
        st.pos += d * st.dir;
        const double hi = (double)(st.e - 1), lo = st.dir > 0 || st.loop ? (double)st.la : (double)st.s;
        if (st.dir > 0 && st.pos >= hi) {
            if (st.loop == 1) st.pos = (double)st.la + (st.pos - hi);
            else if (st.loop == 2) { st.pos = hi - (st.pos - hi); st.dir = -1; }
            else st.done = 1;
        } else if (st.dir < 0 && st.pos <= lo) {
            if (st.loop == 1) st.pos = hi - (lo - st.pos);
            else if (st.loop == 2) { st.pos = lo + (lo - st.pos); st.dir = 1; }
            else st.done = 1;
        }
        if (st.pos < (double)st.s) st.pos = (double)st.s;
        if (st.pos > hi) st.pos = hi;
    }
    void render(MState& m, const float* p, float hz, float* buf, float* bufR, int n, float sr,
                const SamplePool& pool) const override {
        St& st = m.as<St>();
        if (st.done) {
            for (int i = 0; i < n; i++) { buf[i] = 0.0f; if (bufR) bufR[i] = 0.0f; }
            return;
        }
        int k = (int)(p[slotSmp()] + 0.5f);
        float rate = pool.empty(k) ? 48000.0f : pool.s[k].rate;
        // Note 60 spielt das Sample in seiner Aufnahmegeschwindigkeit ab; TUNE und FINE stimmen dazu.
        float noteR = (hz / noteHz(60.0f)) * exp2f((tune(kv(p, K_TUNE, 64)) * 100.0f + pBip(kv(p, K_FINE, 64)) * 100.0f) / 1200.0f);
        float base = rate / sr;
        int warp = stufe(kv(p, K_WARP, 0), 3);
        double zeit = 1.0;                      // Faktor, damit der Bereich BEATS im Tempo dauert
        if (warp) {
            const float bpm = machTempo() < 20.0f ? 20.0f : machTempo();
            const float source = p[slotBpm()];
            if (source >= SAMPLE_BPM_MIN) zeit = bpm / source;
            else if (source > SAMPLE_BPM_LEGACY) {
                const int estimated = pool.empty(k) ? 0 : pool.s[k].estimatedBpm;
                if (estimated > 0) zeit = bpm / estimated;
            } else {
                // Old project files retain their explicit or inferred beat duration.
                const float seconds = (float)st.wspan / rate;
                float beats = beatsOf(stufe(kv(p, K_BEATS, 0), 10));
                if (beats <= 0.0f) {
                    beats = 0.25f;
                    while (beats * 1.4142f < seconds * bpm / 60.0f && beats < 256.0f) beats *= 2.0f;
                }
                zeit = seconds / (beats * 60.0f / bpm);
            }
        }
        if (warp != 2) {                        // OFF oder REPITCH: ein Lesekopf, wie ein Band
            double step = (double)base * noteR * zeit;
            for (int i = 0; i < n; i++) {
                if (st.done) { buf[i] = 0.0f; if (bufR) bufR[i] = 0.0f; continue; }
                float l, r; lese(st, pool, st.pos, l, r);
                float g = fade(st, step);
                buf[i] = l * g; if (bufR) bufR[i] = r * g;
                weiter(st, step);
            }
            return;
        }
        // TIME: zwei Koerner zu je 70 ms, um eine halbe Laenge versetzt, mit Hann-Fenster - ihre
        // Summe ist immer 1. Der Lesekopf wandert im Tempo, jedes Korn liest in der Tonhoehe der Note.
        const float gLen = 0.07f * sr;
        const float dph = 1.0f / gLen;
        const double tstep = (double)base * zeit, pstep = (double)base * noteR;
        for (int i = 0; i < n; i++) {
            if (st.done) { buf[i] = 0.0f; if (bufR) bufR[i] = 0.0f; continue; }
            float l = 0.0f, r = 0.0f;
            for (int g = 0; g < 2; g++) {
                float w = 0.5f - 0.5f * cosf(6.2831853f * st.gph[g]);
                double at = st.gp[g] + (double)(st.gph[g] * gLen) * pstep * st.dir;
                float gl, gr; lese(st, pool, at, gl, gr);
                l += w * gl; r += w * gr;
                st.gph[g] += dph;
                if (st.gph[g] >= 1.0f) { st.gph[g] -= 1.0f; st.gp[g] = passend(st, pool, g, gLen, pstep); }
            }
            float fg = fade(st, tstep);
            buf[i] = l * fg; if (bufR) bufR[i] = r * fg;
            weiter(st, tstep);
        }
    }
};

// --- CHOP: das Sample in gleiche Stuecke schneiden -----------------------------------------
// SLICES Stuecke, SLICE waehlt eines (lockbar je Step) - oder FOLLOW: Step n spielt Stueck n,
// bzw. ein zufaelliges. Abgespielt wird wie bei SMPL; WARP und BEATS beziehen sich auf das
// ganze Sample, damit jedes Stueck im Tempo genau seinen Anteil dauert.
struct MachChop : MachSmpl {
    const bool legacy;
    explicit MachChop(bool legacyGrid = true) : legacy(legacyGrid) {}
    static int sliceSlot(bool legacy) {
        static const int slot = paramSlot(paramFind("trk.sample.slice"));
        return legacy ? knobSlot(K_FINE) : slot;
    }
    static int followSlot(bool legacy) {
        static const int slot = paramSlot(paramFind("trk.sample.follow"));
        return legacy ? knobSlot(K_LOOP) : slot;
    }
    enum { C_SLICES, C_SLICE, C_FOLLOW, C_TUNE, C_REV, C_WARP, C_BEATS, C_LEN };
    int kn(int role) const override {
        switch (role) {
        case K_TUNE: return C_TUNE; case K_REV: return C_REV; case K_WARP: return C_WARP;
        case K_BEATS: return C_BEATS; case K_LEN: return C_LEN; default: return -1;     // kein START/FINE/LOOP
        }
    }
    static int nSlices(float v) { static const int n[6] = {2, 4, 8, 16, 32, 64}; return n[stufe(v, 6)]; }
    const char* name() const override { return "SAMPLE"; }
    const char* label(int k) const override {
        static const char* l[MACH_KNOBS] = {"", "SLICE", "FOLLOW", "TUNE", "REVERSE", "WARP", "", "LENGTH"};
        if (!legacy && (k == C_SLICE || k == C_FOLLOW)) return "";
        return l[k & 7];
    }
    int def(int k) const override { static const int d[MACH_KNOBS] = {3 * 128 / 6 + 10, 0, 0, 64, 0, 0, 0, 0}; return d[k & 7]; }
    int steps(int k) const override {
        switch (k) { case C_SLICES: return 6; case C_FOLLOW: return 3; case C_REV: return 2; case C_WARP: return 3; case C_BEATS: return 10; default: return 0; }
    }
    void text(int k, float v, char* out, int n) const override {
        switch (k) {
        case C_SLICES: snprintf(out, n, "%d", nSlices(v)); return;
        case C_SLICE:  snprintf(out, n, "%d", (int)(v + 0.5f) + 1); return;
        case C_FOLLOW: { static const char* t[3] = {"OFF", "STEP", "RANDOM"}; snprintf(out, n, "%s", t[stufe(v, 3)]); return; }
        case C_TUNE:   MachSmpl::text(K_TUNE, v, out, n); return;
        case C_REV:    MachSmpl::text(K_REV, v, out, n); return;
        case C_WARP:   MachSmpl::text(K_WARP, v, out, n); return;
        case C_BEATS:  MachSmpl::text(K_BEATS, v, out, n); return;
        default:       MachSmpl::text(K_LEN, v, out, n); return;
        }
    }
    void start(MState& m, float hz, float vel, const float* p, float sr,
               const SamplePool& pool) const override {
        St& st = m.as<St>();
        int k = (int)(p[slotSmp()] + 0.5f);
        st.done = 1;
        if (pool.empty(k)) return;
        const SamplePool::Slot& sl = pool.s[k];
        int a = sl.a, e = sl.end(), len = e - a;
        const int grid = legacy ? nSlices(knob(p, C_SLICES)) : 1;
        const int ns = sl.sliceCount(grid);
        const int follow = legacy ? stufe(p[followSlot(true)], 3) : (int)(p[followSlot(false)] + 0.5f);
        int i = (int)(p[sliceSlot(legacy)] + 0.5f);
        if (follow == 1) i = m.step;
        else if (follow == 2) { uint32_t r = m.seed | 1u; i = (int)(machRng(r) % (uint32_t)ns); }
        i = ((i % ns) + ns) % ns;
        int s0 = sl.sliceBoundary(i, grid), e0 = sl.sliceBoundary(i + 1, grid);
        bereich(st, sl, p, s0, e0, len);
        (void)vel; (void)hz; (void)sr;
    }
};

// Register aller Machines. trk.machine ist der Index hier - die Reihenfolge darf nie wechseln.
inline const Machine* machineAt(int i) {
    static const MachPoly poly;
    static const MachFm   fm;
    static const MachDrum drum;
    static const MachSmpl smpl;
    static const MachChop legacyChop, slice(false);
    static const Machine* const all[] = { &poly, &fm, &drum, &smpl, &legacyChop, &slice };
    enum { N = (int)(sizeof all / sizeof *all) };
    if (i < 0 || i >= N) i = 0;
    return all[i];
}
inline int machineCount() { return 6; }

inline int sampleModeSlot() { static const int s = paramSlot(paramFind("trk.sample.mode")); return s; }
inline int machineSlot() { static const int s = paramSlot(paramFind("trk.machine")); return s; }

inline int trackMachine(const int16_t* params) {
    const int machine = params[machineSlot()];
    if (machine < 0 || machine > MACHINE_LEGACY_CHOP) return 0;
    return machine == MACHINE_SAMPLE && params[sampleModeSlot()] ? MACHINE_SLICE : machine;
}
inline bool slicePlayback(int machine) { return machine == MACHINE_SLICE || machine == MACHINE_LEGACY_CHOP; }

// Eine andere Machine waehlen heisst: ihre beiden Regler fangen bei IHREN Grundwerten an.
// Sonst steht der Sampler beim Umschalten mitten im Sample.
inline void machineSelect(int16_t* trackParams, int mach) {
    const int sM = machineSlot();
    const int previous = trackParams[sM];
    if (mach == MACHINE_SAMPLE && (previous == MACHINE_SAMPLE || previous == MACHINE_LEGACY_CHOP)) return;
    const Machine* m = machineAt(mach);
    trackParams[sM] = (int16_t)mach;
    if (mach == MACHINE_SAMPLE) {
        trackParams[sampleModeSlot()] = 0;
        trackParams[MachSmpl::slotBpm()] = SAMPLE_BPM_AUTO;
    }
    for (int k = 0; k < MACH_KNOBS; k++) trackParams[knobSlot(k)] = (int16_t)m->def(k);
}
