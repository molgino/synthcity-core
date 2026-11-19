// Machines: austauschbare Klangquellen hinter EINEM festen Satz Regler (Elektron-Prinzip).
// Eine Machine kennt nur ihren eigenen Zustand und die Spurparameter - nicht die Spur, nicht
// den Sequencer. Der Zustand liegt in einem festen Block, damit nie Speicher angefordert wird.
#pragma once
#include "dsp.h"
#include "param.h"

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
    virtual void  start(MState& m, float hz, float vel, const float* p, float sr) const = 0;
    // Fuellt n Samples (n <= MACH_CHUNK). Ein Aufruf je Teilblock, nicht je Sample.
    virtual void  render(MState& m, const float* p, float hz, float* buf, int n, float sr) const = 0;
};

// --- POLY: zwei verstimmte Oszillatoren, stufenlos von Saege nach Rechteck ---------------------
struct MachPoly : Machine {
    struct St { Osc a, b; float width; };
    static_assert(sizeof(St) <= MACH_STATE, "Zustand passt nicht in den Block");

    const char* name()   const override { return "POLY"; }
    const char* labelA() const override { return "DTUN"; }
    const char* labelB() const override { return "SHAPE"; }

    static int slotA() { static const int s = paramSlot(paramFind("trk.src.a")); return s; }
    static int slotB() { static const int s = paramSlot(paramFind("trk.src.b")); return s; }

    void start(MState& m, float hz, float vel, const float* p, float sr) const override {
        St& s = m.as<St>();
        s.a.reset(0.0f);
        s.b.reset(0.25f);           // feste Phasenlage: gleicher Anschlag klingt immer gleich.
                                    // NICHT 0.5: bei gleicher Frequenz loeschte das die Grundwelle aus.
        s.width = 0.5f;
        (void)vel; (void)p; (void)hz; (void)sr;
    }
    void render(MState& m, const float* p, float hz, float* buf, int n, float sr) const override {
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

// Register aller Machines. trk.machine ist der Index hier - die Reihenfolge darf nie wechseln.
inline const Machine* machineAt(int i) {
    static const MachPoly poly;
    static const Machine* const all[] = { &poly };
    enum { N = (int)(sizeof all / sizeof *all) };
    if (i < 0 || i >= N) i = 0;
    return all[i];
}
inline int machineCount() { return 1; }
