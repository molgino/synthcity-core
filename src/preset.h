#pragma once
// Klaenge und Kits: der Klang einer Spur (alle Spurparameter) bzw. die Klaenge aller acht
// Spuren eines Patterns, als Textdokument wie das Projekt. Gespeichert wird nach Parameter-ID
// und nur, was vom Standard abweicht - ein Preset aus einer alten Version laedt also auch
// dann, wenn Parameter dazugekommen oder weggefallen sind. Steps, Locks, Laenge und Tempo der
// Spur gehoeren zum Pattern und bleiben beim Laden, wie sie sind.
//
// Spielt die Machine ein Sample, steht dessen Platz mit im Preset (Name, Slices, Loop,
// Ausschnitt). Beim Laden bekommt die Spur den Platz, auf dem dieses Sample schon liegt,
// sonst den ersten freien als Wunsch - die Huelle laedt ihn dann wie beim Projekt nach.
#include "engine.h"

enum { SOUND_OK = 1, SOUND_SAMPLE_WANTED = 2, SOUND_NO_SLOT = 3 };
static_assert(SMP_SLOTS <= 64, "taken ist eine 64-Bit-Maske");

inline void soundToDoc(const Track& tr, const SamplePool& pool, Doc& d, const char* pre) {
    char k[DOC_KEY];
    static const int tempo = paramFind("trk.sample.bpm"), machineSlot = paramSlot(paramFind("trk.machine"));
    static const int smpSlot = paramSlot(paramFind("trk.smp"));
    const int machine = tr.p[machineSlot];
    for (int i = 0; i < NPARAMS; i++) {
        if (PARAMS[i].scope != PS_TRACK) continue;
        const int slot = paramSlot(i);
        if (tr.p[slot] == trackDocDefault(i)) continue;
        if (i == tempo && tr.p[slot] == SAMPLE_BPM_AUTO && machine != MACHINE_SAMPLE && machine != MACHINE_LEGACY_CHOP) continue;
        snprintf(k, sizeof k, "%s%s", pre, PARAMS[i].id);
        d.set(k, tr.p[slot]);
    }
    const int s = tr.p[smpSlot];
    if (machine >= 0 && machine < machineCount() && machineAt(machine)->usesSample()
        && s >= 0 && s < SMP_SLOTS && pool.s[s].name[0]) {
        snprintf(k, sizeof k, "%ssmp", pre);
        slotToDoc(pool, s, d, k);
    }
}

// taken: Plaetze, die ein Kit in diesem Durchgang schon vergeben hat (nicht doppelt belegen)
inline int soundFromDoc(const Doc& d, const char* pre, Track& tr, SamplePool& pool, uint64_t& taken) {
    char k[DOC_KEY];
    static const int tempo = paramFind("trk.sample.bpm"), machineSlot = paramSlot(paramFind("trk.machine"));
    static const int smpSlot = paramSlot(paramFind("trk.smp"));
    const int oldSample = tr.p[smpSlot];
    for (int i = 0; i < NPARAMS; i++) {
        if (PARAMS[i].scope != PS_TRACK) continue;
        snprintf(k, sizeof k, "%s%s", pre, PARAMS[i].id);
        int v = d.get(k, trackDocDefault(i));
        if (i == tempo && !d.has(k) && tr.p[machineSlot] != MACHINE_SAMPLE && tr.p[machineSlot] != MACHINE_LEGACY_CHOP)
            v = SAMPLE_BPM_AUTO;
        if (isInsertParam(i) || isExtendedVoiceParam(i)) v = clampParam(i, v);   // wie beim Projekt
        tr.p[paramSlot(i)] = (int16_t)v;
    }
    char base[DOC_KEY]; snprintf(base, sizeof base, "%ssmp", pre);
    snprintf(k, sizeof k, "%s.name", base);
    const char* name = d.getStr(k, "");
    if (!name[0]) { tr.p[smpSlot] = (int16_t)oldSample; return SOUND_OK; }
    for (int i = 0; i < SMP_SLOTS; i++)                        // liegt schon da (oder ist gewuenscht)
        if (pool.s[i].name[0] && !strcmp(pool.s[i].name, name)) {
            tr.p[smpSlot] = (int16_t)i; taken |= 1ull << i;
            return pool.s[i].n > 0 ? SOUND_OK : SOUND_SAMPLE_WANTED;
        }
    for (int i = 0; i < SMP_SLOTS; i++)                        // erster freier Platz
        if (pool.s[i].n <= 0 && !pool.s[i].name[0] && !(taken >> i & 1)) {
            slotFromDoc(d, base, pool, i);
            tr.p[smpSlot] = (int16_t)i; taken |= 1ull << i;
            return SOUND_SAMPLE_WANTED;
        }
    tr.p[smpSlot] = (int16_t)oldSample;                        // alles voll: Spur behaelt ihren Platz
    return SOUND_NO_SLOT;
}

inline void kitToDoc(const Pattern& pt, const SamplePool& pool, Doc& d) {
    d.setStr("kind", "kit");
    char pre[16];
    for (int t = 0; t < NTRACKS; t++) { snprintf(pre, sizeof pre, "kit.%d.", t); soundToDoc(pt.t[t], pool, d, pre); }
}
// Rueckgabe: schlechtester Zustand aller Spuren (SOUND_*)
inline int kitFromDoc(const Doc& d, Pattern& pt, SamplePool& pool) {
    char pre[16];
    uint64_t taken = 0;
    int worst = SOUND_OK;
    for (int t = 0; t < NTRACKS; t++) {
        snprintf(pre, sizeof pre, "kit.%d.", t);
        int r = soundFromDoc(d, pre, pt.t[t], pool, taken);
        if (r > worst) worst = r;
    }
    return worst;
}
