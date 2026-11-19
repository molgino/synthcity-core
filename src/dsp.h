// DSP-Bausteine. Alles ohne Zuweisung von Speicher, alles zustandsbehaftet in kleinen structs.
// Hier steht NUR Signalverarbeitung - kein Wissen ueber Sequencer, Parameter oder Dateien.
#pragma once
#include <math.h>
#include <stdint.h>

// --- Skalen: aus 0..127 werden musikalisch sinnvolle Groessen -------------------------------
// Die Tabelle in param.h kennt nur ganze Zahlen; die Bedeutung bekommen sie erst hier.
inline float pBip(int v)   { return (v - 64) / 63.0f; }                     // -1 .. +1, Mitte 64
inline float pUni(int v)   { return v / 127.0f; }                           // 0 .. 1
inline float pTime(int v)  { return 0.001f * powf(10.0f, 4.0f * v / 127.0f); } // 1 ms .. 10 s
inline float pFreq(int v)  { return 20.0f * powf(900.0f, v / 127.0f); }     // 20 Hz .. 18 kHz
inline float dbToLin(float db) { return db <= -60.0f ? 0.0f : powf(10.0f, db / 20.0f); }
// 100 = 0 dB (Einheitsverstaerkung), 0 = Stille, 127 = +6 dB.
inline float pLevel(int v) {
    if (v <= 0) return 0.0f;
    float db = v <= 100 ? (v - 100) * (60.0f / 99.0f) : (v - 100) * (6.0f / 27.0f);
    return dbToLin(db);
}
inline float noteHz(float n) { return 440.0f * powf(2.0f, (n - 69.0f) / 12.0f); }

// Winzige Werte kosten auf manchen Kernen sehr viel Zeit - sie werden hart auf null gesetzt.
inline float undenorm(float x) { return (x > -1e-20f && x < 1e-20f) ? 0.0f : x; }

// Weiche Begrenzung am Ausgang: bis -3 dB voellig unveraendert, darueber sanft bis genau 1.0.
// Hardware klingt an der Grenze gnaedig; hartes Abschneiden erzeugt scharfe Obertoene.
inline float softClip(float x) {
    float a = x < 0.0f ? -x : x;
    if (a <= 0.7f) return x;
    float y = 0.7f + 0.3f * tanhf((a - 0.7f) / 0.3f);
    return x < 0.0f ? -y : y;
}

// --- Oszillator mit PolyBLEP ------------------------------------------------------------------
// Ein naiver Sprung erzeugt Aliasing bis hinunter in den hoerbaren Bereich. PolyBLEP glaettet
// genau die zwei Samples um jeden Sprung und ist dabei billig genug fuer viele Stimmen.
struct Osc {
    float ph = 0.0f, inc = 0.0f;
    void setHz(float hz, float sr) { inc = hz / sr; if (inc > 0.49f) inc = 0.49f; }
    void reset(float p = 0.0f) { ph = p; }
    static float blep(float t, float dt) {
        if (t < dt)          { t /= dt;      return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt)   { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    void advance() { ph += inc; if (ph >= 1.0f) ph -= 1.0f; }
    float saw() const  { return 2.0f * ph - 1.0f - blep(ph, inc); }
    float pulse(float width) const {   // width 0.02 .. 0.98
        float p2 = ph + (1.0f - width); if (p2 >= 1.0f) p2 -= 1.0f;
        return (ph < width ? 1.0f : -1.0f) - blep(ph, inc) + blep(p2, inc);
    }
    float sine() const { return sinf(6.2831853f * ph); }
};

// --- Filter: TPT/ZDF-State-Variable (Zavalishin) -----------------------------------------------
// Bleibt auch bei schneller Modulation der Eckfrequenz stabil und liefert LP/BP/HP gleichzeitig.
enum FltType : uint8_t { FLT_LP = 0, FLT_BP, FLT_HP, FLT_NOTCH, FLT_N };
// Die Koeffizienten werden NICHT je Sample berechnet (tanf ist teuer), sondern auf der
// Steuerrate - set() aufrufen, dann viele Samples lang run().
struct Svf {
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
    void reset() { ic1 = ic2 = 0.0f; }
    void set(float fc, float q, float sr) {
        if (fc < 20.0f) fc = 20.0f;
        float nyq = sr * 0.49f; if (fc > nyq) fc = nyq;
        float g = tanf(3.14159265f * fc / sr);
        k = 1.0f / (q < 0.5f ? 0.5f : q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float run(float x, FltType type) {
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = undenorm(2.0f * v1 - ic1);
        ic2 = undenorm(2.0f * v2 - ic2);
        float hp = x - k * v1 - v2;
        switch (type) {
        case FLT_LP:    return v2;
        case FLT_BP:    return v1;
        case FLT_HP:    return hp;
        default:        return hp + v2;         // Notch
        }
    }
};

// --- Huellkurve ------------------------------------------------------------------------------
// Analoge Bauart: jedes Segment laeuft auf ein Ziel zu (einpolig). Das klingt natuerlicher als
// gerade Rampen und braucht pro Sample nur eine Multiplikation.
struct Env {
    enum Stage : uint8_t { OFF, ATK, DEC, SUS, REL };
    Stage st = OFF;
    float v = 0.0f, ca = 0.0f, cd = 0.0f, cr = 0.0f, sus = 0.0f;
    static float coef(float sec, float sr) {
        if (sec < 0.0002f) sec = 0.0002f;
        return expf(-1.0f / (sec * sr));
    }
    void setup(float a, float d, float s, float r, float sr) {
        ca = coef(a * 0.4f, sr); cd = coef(d * 0.4f, sr); cr = coef(r * 0.4f, sr); sus = s;
    }
    void gate()    { st = ATK; }
    void release() { if (st != OFF) st = REL; }
    void kill()    { st = OFF; v = 0.0f; }
    bool done() const { return st == OFF; }
    float tick() {
        switch (st) {
        case ATK: v = 1.3f + (v - 1.3f) * ca; if (v >= 1.0f) { v = 1.0f; st = DEC; } break;
        case DEC: v = sus + (v - sus) * cd;   if (v <= sus + 0.0005f) { v = sus; st = SUS; } break;
        case SUS: v = sus; break;
        case REL: v *= cr; if (v < 0.0001f) { v = 0.0f; st = OFF; } break;
        default:  v = 0.0f;
        }
        return v;
    }
};

// --- Glaettung fuer Parameter, die sich waehrend eines Tons aendern ---------------------------
struct Smooth {
    float v = 0.0f, c = 0.0f;
    void setup(float ms, float sr) { c = expf(-1.0f / (0.001f * ms * sr)); }
    void jump(float x) { v = x; }
    float run(float x) { v = x + (v - x) * c; return undenorm(v); }
};
