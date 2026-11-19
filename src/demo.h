// Ein Pattern zum Anfangen. Es steht EINMAL hier und wird von der Oberflaeche, vom
// Demo-Werkzeug und spaeter vom Geraet benutzt - zwei Fassungen liefen sonst auseinander.
#pragma once
#include "engine.h"

inline void demoProject(Engine& e) {
    projectInit(e.P);
    const int bpm = paramSlot(paramFind("bpm"));
    e.P.g[bpm] = 124;
    e.seq.bpm = 124;
    // Platz nach oben lassen: die weiche Begrenzung ist eine Notbremse, keine Lautstaerke.
    e.P.g[paramSlot(paramFind("master.vol"))] = 90;
    e.P.g[paramSlot(paramFind("fx.a.div"))] = 4;        // Echo auf Achteln
    e.P.g[paramSlot(paramFind("fx.a.fb"))] = 72;
    e.P.g[paramSlot(paramFind("fx.b.size"))] = 86;
    e.P.g[paramSlot(paramFind("fx.b.dec"))] = 92;
    e.P.pat[0].used = 1;

    const int MACH = paramSlot(paramFind("trk.machine")), NOTE = paramSlot(paramFind("trk.note"));
    const int SRCA = paramSlot(paramFind("trk.src.a")),   SRCB = paramSlot(paramFind("trk.src.b"));
    const int FREQ = paramSlot(paramFind("trk.flt.freq")),RESO = paramSlot(paramFind("trk.flt.reso"));
    const int FENV = paramSlot(paramFind("trk.flt.env")), ATK  = paramSlot(paramFind("trk.amp.atk"));
    const int DEC  = paramSlot(paramFind("trk.amp.dec")), SUS  = paramSlot(paramFind("trk.amp.sus"));
    const int REL  = paramSlot(paramFind("trk.amp.rel")), VOL  = paramSlot(paramFind("trk.vol"));
    const int PAN  = paramSlot(paramFind("trk.pan")),     LEN  = paramSlot(paramFind("trk.len"));
    const int SA   = paramSlot(paramFind("trk.send.a")),  SB   = paramSlot(paramFind("trk.send.b"));
    const int COND = paramSlot(paramFind("trk.cond")),    RTRG = paramSlot(paramFind("trk.ratchet"));
    Pattern& pt = e.P.pat[0];

    Track& kick = pt.t[0];                               // Spur 1: Bassdrum
    kick.p[MACH] = 2; kick.p[NOTE] = 29; kick.p[SRCA] = 96; kick.p[SRCB] = 6;
    kick.p[DEC] = 58; kick.p[FREQ] = 90; kick.p[VOL] = 112;
    for (int s = 0; s < 16; s += 4) kick.s[s].on = 1;
    kick.s[14].on = 1; kick.s[14].setLock(NOTE, 26);

    Track& snare = pt.t[1];                              // Spur 2: Snare mit Hall
    snare.p[MACH] = 2; snare.p[NOTE] = 50; snare.p[SRCA] = 40; snare.p[SRCB] = 92;
    snare.p[DEC] = 42; snare.p[FREQ] = 104; snare.p[VOL] = 96; snare.p[SB] = 74;
    snare.s[4].on = 1; snare.s[12].on = 1;
    snare.s[7].on = 1; snare.s[7].setLock(COND, (int16_t)condProb(45));

    Track& hat = pt.t[2];                                // Spur 3: Hi-Hat, Sechzehntel
    hat.p[MACH] = 2; hat.p[NOTE] = 80; hat.p[SRCA] = 0; hat.p[SRCB] = 127;
    hat.p[DEC] = 16; hat.p[FREQ] = 118; hat.p[VOL] = 76; hat.p[PAN] = 76;
    for (int s = 0; s < 16; s += 2) hat.s[s].on = 1;
    hat.s[10].setLock(RTRG, 3);
    hat.s[6].setLock(DEC, 34);

    Track& bass = pt.t[3];                               // Spur 4: Bass, gleitend
    bass.p[MACH] = 0; bass.p[NOTE] = 33; bass.p[SRCA] = 70; bass.p[SRCB] = 30;
    bass.p[FREQ] = 50; bass.p[RESO] = 72; bass.p[FENV] = 100; bass.p[DEC] = 50;
    bass.p[VOL] = 104;
    for (int s = 0; s < 16; s += 4) bass.s[s].on = 1;
    bass.s[8].setLock(NOTE, 45);
    bass.s[12].on = 1; bass.s[12].setLock(NOTE, 40, 1);  // Slide hinauf

    Track& lead = pt.t[4];                               // Spur 5: FM-Melodie, 12 Steps
    lead.len = 12;
    lead.p[MACH] = 1; lead.p[NOTE] = 69; lead.p[SRCA] = 24; lead.p[SRCB] = 58;
    lead.p[DEC] = 46; lead.p[FREQ] = 100; lead.p[VOL] = 82; lead.p[PAN] = 48;
    lead.p[SA] = 86; lead.p[SB] = 40;                    // ins Echo und in den Hall
    const int ns[12] = {69, 0, 72, 0, 0, 76, 0, 69, 0, 0, 64, 0};
    for (int s = 0; s < 12; s++)
        if (ns[s]) { lead.s[s].on = 1; lead.s[s].setLock(NOTE, (int16_t)ns[s]); }
    lead.s[5].setLock(paramSlot(paramFind("trk.prob")), 70);

    Track& pad = pt.t[5];                                // Spur 6: Flaeche, nur jede zweite Runde
    pad.speed = 2;
    pad.p[MACH] = 0; pad.p[NOTE] = 57; pad.p[SRCA] = 78; pad.p[SRCB] = 18;
    pad.p[ATK] = 74; pad.p[SUS] = 92; pad.p[REL] = 86; pad.p[LEN] = 100;
    pad.p[FREQ] = 66; pad.p[VOL] = 74; pad.p[PAN] = 82; pad.p[SB] = 90;
    for (int s = 0; s < 16; s += 8) { pad.s[s].on = 1; pad.s[s].setLock(COND, (int16_t)condAB(2, 2, false)); }
    pad.s[8].setLock(NOTE, 60);

    pt.sc[0][3].set(FREQ, 42);                           // Scene A: Bass dumpf
    pt.sc[1][3].set(FREQ, 92);                           // Scene B: Bass offen
    pt.sc[0][4].set(VOL, 60);                            // Scene A: Melodie leise
    pt.sc[1][4].set(VOL, 96);
    e.resetTrackValues();
}
