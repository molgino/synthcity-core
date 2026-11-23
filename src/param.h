// Parameter werden GENAU EINMAL deklariert. Aus dieser Tabelle entstehen spaeter Oberflaeche,
// Plugin-Parameterliste, MIDI-Learn, Hilfe und die Datei - nie dieselbe Angabe an zwei Stellen.
// Ein neuer Parameter ist eine Zeile hier; er wird dann automatisch gespeichert, geladen und
// mit seinem Standardwert vorbelegt (siehe tests/unit.cpp "Projekt ueberlebt neue Parameter").
#pragma once
#include <stdint.h>
#include <string.h>

// FNV-1a: der Name steht als Text in der Datei, die Zahl ist nur die Abkuerzung zur Laufzeit.
constexpr uint32_t pidHash(const char* s, uint32_t h = 2166136261u) {
    return *s ? pidHash(s + 1, (uint32_t)((h ^ (uint8_t)*s) * 16777619u)) : h;
}

enum PScope : uint8_t { PS_GLOBAL, PS_TRACK };
enum PKind  : uint8_t { PK_NUM, PK_BIP, PK_ONOFF, PK_NOTE, PK_LEVEL, PK_TIME, PK_ENUM, PK_COND };
enum : uint8_t { PF_LOCK = 1, PF_MOD = 2, PF_LIVE = 4 };   // je Step lockbar / als Modulationsziel erlaubt

struct ParamDef {
    const char* id;      // stabile ID - steht so in der Datei, wird nie umbenannt
    const char* name;    // Anzeige
    PScope scope;
    PKind kind;
    int16_t lo, hi, def;
    const char* unit;
    uint8_t flags;
};

enum { VOICE_PARAM_CAP = 112, TRACK_PARAM_CAP = 160, GLOBAL_PARAM_CAP = 128, INSERT_STAGES = 2 };
enum InsertType { INS_OFF, INS_CRUSH, INS_COMPRESS, INS_CHORUS, INS_CLIP, INS_TYPE_COUNT };
enum ClipShape { CLIP_HARD, CLIP_CUBIC, CLIP_TANH, CLIP_ATAN, CLIP_SHAPES };
enum EqShape { EQ_OFF, EQ_BELL, EQ_LOW_SHELF, EQ_HIGH_SHELF, EQ_LOW_PASS, EQ_LOW_PASS_24,
               EQ_HIGH_PASS, EQ_HIGH_PASS_24, EQ_NOTCH, EQ_BAND_PASS, EQ_SHAPES };
enum DelayType { DLY_PING_PONG, DLY_STEREO, DLY_TAPE, DLY_TYPES };
enum DelayTiming { DLY_LINKED, DLY_DUAL, DLY_FREE, DLY_TIMINGS };
enum ReverbType { REV_STUDIO, REV_ROOM, REV_PLATE, REV_HALL, REV_TYPES };
enum { FX_LFO_DEST_MAX = 21 };
enum InsertField {
    INS_TYPE, INS_MIX, INS_TRIM, INS_RATE, INS_BITS, INS_DRIVE, INS_TONE, INS_PREFILTER,
    INS_THRESHOLD, INS_RATIO, INS_ATTACK, INS_RELEASE, INS_KNEE, INS_SOURCE, INS_SC_HPF,
    INS_CH_MODE, INS_CH_RATE, INS_CH_DEPTH, INS_CH_WIDTH, INS_CLIP_SHAPE, INS_CLIP_CEIL, INS_DETECT, INS_AUTO_RELEASE, INSERT_FIELDS
};

#define INSERT_PARAMS(id) \
    {id ".type",      "TYPE",       PS_TRACK, PK_ENUM,   0, INS_TYPE_COUNT - 1, 0, "", 0}, \
    {id ".mix",       "MIX",        PS_TRACK, PK_NUM,    0, 100, 100, "%", 0}, \
    {id ".trim",      "OUTPUT",     PS_TRACK, PK_NUM,  -24,  24,   0, "dB", 0}, \
    {id ".rate",      "RATE",       PS_TRACK, PK_NUM,    0, 127,   0, "", 0}, \
    {id ".bits",      "BITS",       PS_TRACK, PK_NUM,    0,  16,   0, "", 0}, \
    {id ".drive",     "DRIVE",      PS_TRACK, PK_NUM,    0,  30,   0, "dB", 0}, \
    {id ".tone",      "TONE",       PS_TRACK, PK_NUM,  500, 20000, 20000, "Hz", 0}, \
    {id ".prefilter", "PRE FILTER", PS_TRACK, PK_ONOFF,  0,   1,   1, "", 0}, \
    {id ".threshold", "THRESHOLD",  PS_TRACK, PK_NUM,  -60,   0, -18, "dB", 0}, \
    {id ".ratio",     "RATIO",      PS_TRACK, PK_NUM,    1,  20,   4, ":1", 0}, \
    {id ".attack",    "ATTACK",     PS_TRACK, PK_NUM,    1, 100,  10, "ms", 0}, \
    {id ".release",   "RELEASE",    PS_TRACK, PK_NUM,   10, 1000, 120, "ms", 0}, \
    {id ".knee",      "KNEE",       PS_TRACK, PK_NUM,    0,  24,   6, "dB", 0}, \
    {id ".source",    "SIDECHAIN",  PS_TRACK, PK_ENUM,   0,   8,   0, "", 0}, \
    {id ".sc.hpf",    "SC HPF",     PS_TRACK, PK_NUM,    0, 500,   0, "Hz", 0}, \
    {id ".ch.mode",   "CHARACTER",  PS_TRACK, PK_ENUM,   0,   1,   0, "", 0}, \
    {id ".ch.rate",   "RATE",       PS_TRACK, PK_NUM,    5, 500,  45, "", 0}, \
    {id ".ch.depth",  "DEPTH",      PS_TRACK, PK_NUM,    0, 100,  55, "%", 0}, \
    {id ".ch.width",  "WIDTH",      PS_TRACK, PK_NUM,    0, 100, 100, "%", 0}, \
    {id ".clip.shape", "SHAPE",     PS_TRACK, PK_ENUM, 0, CLIP_SHAPES - 1, 0, "", 0}, \
    {id ".clip.ceil",  "CEILING",   PS_TRACK, PK_NUM, -24, 0, 0, "dB", 0}, \
    {id ".detect",     "DETECTOR",  PS_TRACK, PK_ENUM, 0, 1, 0, "", 0}, \
    {id ".auto",       "AUTO REL",  PS_TRACK, PK_ONOFF, 0, 1, 0, "", 0}

#define EQ_PARAMS(id, n, hz) \
    {id ".shape", "B" n " SHAPE", PS_GLOBAL, PK_ENUM, 0, EQ_SHAPES - 1, EQ_OFF, "", 0}, \
    {id ".freq",  "B" n " FREQ",  PS_GLOBAL, PK_NUM, 20, 20000, hz, "Hz", 0}, \
    {id ".gain",  "B" n " GAIN",  PS_GLOBAL, PK_NUM, -12, 12, 0, "dB", 0}, \
    {id ".q",     "B" n " Q",     PS_GLOBAL, PK_NUM, 10, 1000, 71, "", 0}

#define OSC_PARAMS(id, n) \
    {id ".wave", "OSC " n " WAVE", PS_TRACK, PK_ENUM, 0, 3, 0, "", PF_LOCK | PF_LIVE}, \
    {id ".level", "OSC " n " VOL", PS_TRACK, PK_NUM, 0, 100, 100, "%", PF_LOCK | PF_LIVE}, \
    {id ".tune", "OSC " n " TUNE", PS_TRACK, PK_NUM, -24, 24, 0, "st", PF_LOCK | PF_LIVE}, \
    {id ".fine", "OSC " n " FINE", PS_TRACK, PK_NUM, -100, 100, 0, "ct", PF_LOCK | PF_LIVE}, \
    {id ".pw",   "OSC " n " PW",   PS_TRACK, PK_NUM, 0, 95, 0, "%", PF_LOCK | PF_LIVE}

#define FM_PARAMS(id, name) \
    {id ".ratio", name " RATIO", PS_TRACK, PK_NUM, 0, 1600, 0, "", PF_LOCK | PF_LIVE}, \
    {id ".level", name " LEVEL", PS_TRACK, PK_NUM, -1, 100, -1, "%", PF_LOCK | PF_LIVE}, \
    {id ".fine",  name " FINE", PS_TRACK, PK_NUM, -100, 100, 0, "ct", PF_LOCK | PF_LIVE}, \
    {id ".env",   name " ENV", PS_TRACK, PK_ONOFF, 0, 1, 0, "", PF_LOCK}, \
    {id ".atk",   name " ATK", PS_TRACK, PK_TIME, 0, 127, 0, "s", PF_LOCK}, \
    {id ".dec",   name " DEC", PS_TRACK, PK_TIME, 0, 127, 72, "s", PF_LOCK}, \
    {id ".sus",   name " SUS", PS_TRACK, PK_NUM, 0, 100, 60, "%", PF_LOCK}, \
    {id ".rel",   name " REL", PS_TRACK, PK_TIME, 0, 127, 45, "s", PF_LOCK}


#define FX_LFO_PARAMS(id, name) \
    {id ".wave", name " WAVE", PS_GLOBAL, PK_ENUM, 0, 6, 0, "", 0}, \
    {id ".sync", name " SYNC", PS_GLOBAL, PK_ONOFF, 0, 1, 1, "", 0}, \
    {id ".rate", name " RATE", PS_GLOBAL, PK_ENUM, 0, 14, 7, "", 0}, \
    {id ".hz", name " HZ", PS_GLOBAL, PK_NUM, 1, 2000, 100, "", 0}, \
    {id ".depth", name " DEPTH", PS_GLOBAL, PK_NUM, -100, 100, 0, "%", 0}, \
    {id ".phase", name " PHASE", PS_GLOBAL, PK_NUM, 0, 127, 0, "", 0}, \
    {id ".dest", name " DEST", PS_GLOBAL, PK_ENUM, 0, FX_LFO_DEST_MAX, 0, "", 0}

// Reihenfolge ist Anzeigereihenfolge; sie darf sich aendern, ohne Dateien zu brechen.
static constexpr ParamDef PARAMS[] = {
    // --- global ---
    {"bpm",          "TEMPO",  PS_GLOBAL, PK_NUM,    30, 300, 120, "BPM", 0},
    {"swing",        "SWING",  PS_GLOBAL, PK_NUM,    50,  80,  50, "%",   0},
    {"master.vol",   "MASTER", PS_GLOBAL, PK_LEVEL,   0, 127, 100, "dB",  0},
    // --- Sende-Effekte: einmal gerechnet fuer alle Spuren ---
    {"fx.a.div",     "DLY TIME", PS_GLOBAL, PK_ENUM,    0,   8,   4, "",    0},
    {"fx.a.fb",      "DLY FB", PS_GLOBAL, PK_NUM,     0, 127,  60, "",    0},
    {"fx.a.tone",    "DLY TONE", PS_GLOBAL, PK_NUM,     0, 127,  70, "",    0},
    {"fx.a.vol",     "DLY VOL", PS_GLOBAL, PK_LEVEL,   0, 127, 100, "dB",  0},
    {"fx.b.size",    "REV SIZE", PS_GLOBAL, PK_NUM,     0, 127,  70, "",    0},
    {"fx.b.dec",     "REV DECAY", PS_GLOBAL, PK_NUM,     0, 127,  80, "",    0},
    {"fx.b.damp",    "REV DAMP", PS_GLOBAL, PK_NUM,     0, 127,  60, "",    0},
    {"fx.b.vol",     "REV VOL", PS_GLOBAL, PK_LEVEL,   0, 127, 100, "dB",  0},
    // --- Spur: Anschlag ---
    {"trk.note",     "NOTE",   PS_TRACK,  PK_NOTE,    0, 127,  60, "",    PF_LOCK},
    {"trk.vel",      "VEL",    PS_TRACK,  PK_NUM,     0, 127, 100, "",    PF_LOCK},
    {"trk.len",      "LEN",    PS_TRACK,  PK_TIME,    1, 127,  12, "",    PF_LOCK},
    {"trk.prob",     "PROB",   PS_TRACK,  PK_NUM,     0, 100, 100, "%",   PF_LOCK},
    {"trk.cond",     "COND",   PS_TRACK,  PK_COND,    0, 255,   0, "",    PF_LOCK},
    {"trk.micro",    "MTIM",   PS_TRACK,  PK_BIP,     0,  48,  24, "",    PF_LOCK},
    {"trk.ratchet",  "RTRG",   PS_TRACK,  PK_NUM,     1,   8,   1, "",    PF_LOCK},
    {"trk.quant",    "KEY",    PS_TRACK,  PK_ONOFF,   0,   1,   1, "",    PF_LOCK},
    // --- Spur: Akkord und Stimmen ---
    // Akkordnoten als Abstand zur NOTE in Halbtoenen: 0 = aus, 1..49 = -24..+24 (25 = gleiche Note).
    {"trk.nt2",      "NT2",    PS_TRACK,  PK_NUM,     0,  49,   0, "",    PF_LOCK},
    {"trk.nt3",      "NT3",    PS_TRACK,  PK_NUM,     0,  49,   0, "",    PF_LOCK},
    {"trk.nt4",      "NT4",    PS_TRACK,  PK_NUM,     0,  49,   0, "",    PF_LOCK},
    {"trk.vmode",    "VOICES",PS_TRACK,  PK_ENUM,    0,   2,   0, "",    0},
    // --- Spur: Quelle ---
    {"trk.machine",  "ENGINE", PS_TRACK,  PK_ENUM,    0,  15,   0, "",    0},
    {"trk.smp",      "SAMPLE", PS_TRACK,  PK_ENUM,    0,  63,   0, "",    PF_LOCK},
    {"trk.src.a",    "SRC A", PS_TRACK, PK_BIP,    0, 127,  64, "",    PF_LOCK | PF_MOD},
    {"trk.src.b",    "SRC B", PS_TRACK, PK_NUM,    0, 127,  64, "",    PF_LOCK | PF_MOD},
    {"trk.src.c",    "SRC C",  PS_TRACK,  PK_NUM,     0, 127,    0, "",    PF_LOCK | PF_MOD},
    {"trk.src.d",    "SRC D",  PS_TRACK,  PK_NUM,     0, 127,   64, "",    PF_LOCK | PF_MOD},
    {"trk.src.e",    "SRC E",  PS_TRACK,  PK_NUM,     0, 127,    0, "",    PF_LOCK | PF_MOD},
    {"trk.src.f",    "SRC F",  PS_TRACK,  PK_NUM,     0, 127,    0, "",    PF_LOCK | PF_MOD},
    {"trk.src.g",    "SRC G",  PS_TRACK,  PK_NUM,     0, 127,    0, "",    PF_LOCK | PF_MOD},
    {"trk.src.h",    "SRC H",  PS_TRACK,  PK_NUM,     0, 127,    0, "",    PF_LOCK | PF_MOD},
    // --- Spur: Filter ---
    {"trk.flt.freq", "FREQ",   PS_TRACK,  PK_NUM,     0, 127, 127, "Hz",  PF_LOCK | PF_MOD},
    {"trk.flt.reso", "RESO",   PS_TRACK,  PK_NUM,     0, 127,   0, "",    PF_LOCK | PF_MOD},
    {"trk.flt.type", "TYPE",   PS_TRACK,  PK_ENUM,    0,  10,   0, "",    PF_LOCK},
    {"trk.flt.env",  "ENV",    PS_TRACK,  PK_BIP,     0, 127,  64, "",    PF_LOCK | PF_MOD},
    // Base-Width: ein Hochpass (BASE) und ein Tiefpass WIDTH Oktaven darueber, hinter dem Filter.
    {"trk.bw.base",  "BASE",   PS_TRACK,  PK_NUM,     0, 127,   0, "Hz",  PF_LOCK | PF_MOD},
    {"trk.bw.width", "WIDTH",  PS_TRACK,  PK_NUM,     0, 127, 127, "",    PF_LOCK | PF_MOD},
    // --- Spur: Huellkurve und Ausgang ---
    {"trk.amp.atk",  "ATK",    PS_TRACK,  PK_TIME,    0, 127,   0, "s",   PF_LOCK | PF_MOD},
    {"trk.amp.dec",  "DEC",    PS_TRACK,  PK_TIME,    0, 127,  71, "s",   PF_LOCK | PF_MOD},
    {"trk.amp.sus",  "SUS",    PS_TRACK,  PK_LEVEL,   0, 127,   0, "dB",  PF_LOCK | PF_MOD},
    {"trk.amp.rel",  "REL",    PS_TRACK,  PK_TIME,    0, 127,  40, "s",   PF_LOCK | PF_MOD},
    {"trk.vol",      "VOL",    PS_TRACK,  PK_LEVEL,   0, 127, 100, "dB",  PF_LOCK | PF_MOD},
    {"trk.pan",      "PAN",    PS_TRACK,  PK_BIP,     0, 127,  64, "",    PF_LOCK | PF_MOD},
    {"trk.send.a",   "SEND A",    PS_TRACK,  PK_LEVEL,   0, 127,   0, "dB",  PF_LOCK | PF_MOD},
    {"trk.send.b",   "SEND B",    PS_TRACK,  PK_LEVEL,   0, 127,   0, "dB",  PF_LOCK | PF_MOD},
    // --- Spur: Modulation - zwei LFOs und eine Mod-Huellkurve, je mit einem Ziel ---
    {"trk.lfo1.wave",  "LFO1 WAVE",  PS_TRACK, PK_ENUM,    0,   6,   0, "",    PF_LOCK},
    {"trk.lfo1.rate",  "LFO1 RATE",  PS_TRACK, PK_ENUM,    0,  14,   7, "",    PF_LOCK | PF_MOD},
    {"trk.lfo1.depth", "LFO1 DEPTH", PS_TRACK, PK_BIP,     0, 127,  64, "",    PF_LOCK | PF_MOD},
    {"trk.lfo1.dest",  "LFO1 DEST",  PS_TRACK, PK_ENUM,    0,  80,   0, "",    PF_LOCK},
    {"trk.lfo1.mode",  "LFO1 MODE",  PS_TRACK, PK_ENUM,    0,   4,   0, "",    0},
    {"trk.lfo2.wave",  "LFO2 WAVE",  PS_TRACK, PK_ENUM,    0,   6,   1, "",    PF_LOCK},
    {"trk.lfo2.rate",  "LFO2 RATE",  PS_TRACK, PK_ENUM,    0,  14,   4, "",    PF_LOCK | PF_MOD},
    {"trk.lfo2.depth", "LFO2 DEPTH", PS_TRACK, PK_BIP,     0, 127,  64, "",    PF_LOCK | PF_MOD},
    {"trk.lfo2.dest",  "LFO2 DEST",  PS_TRACK, PK_ENUM,    0,  80,   0, "",    PF_LOCK},
    {"trk.lfo2.mode",  "LFO2 MODE",  PS_TRACK, PK_ENUM,    0,   4,   0, "",    0},
    {"trk.menv.atk",   "MENV ATK",   PS_TRACK, PK_TIME,    0, 127,   0, "s",   PF_LOCK},
    {"trk.menv.dec",   "MENV DEC",   PS_TRACK, PK_TIME,    0, 127,  60, "s",   PF_LOCK},
    {"trk.menv.depth", "MENV DEPTH", PS_TRACK, PK_BIP,     0, 127,  64, "",    PF_LOCK | PF_MOD},
    {"trk.menv.dest",  "MENV DEST",  PS_TRACK, PK_ENUM,    0,  80,   0, "",    PF_LOCK},
    {"trk.lfo1.spd", "LFO1 SPEED", PS_TRACK, PK_NUM, -65, 63, -65, "", PF_LOCK},
    {"trk.lfo1.mult", "LFO1 MULT", PS_TRACK, PK_ENUM, 0, 23, 4, "", PF_LOCK},
    {"trk.lfo1.fade", "LFO1 FADE", PS_TRACK, PK_NUM, -64, 63, 0, "", PF_LOCK},
    {"trk.lfo1.phase", "LFO1 PHASE", PS_TRACK, PK_NUM, 0, 127, 0, "", PF_LOCK},
    {"trk.lfo2.spd", "LFO2 SPEED", PS_TRACK, PK_NUM, -65, 63, -65, "", PF_LOCK},
    {"trk.lfo2.mult", "LFO2 MULT", PS_TRACK, PK_ENUM, 0, 23, 4, "", PF_LOCK},
    {"trk.lfo2.fade", "LFO2 FADE", PS_TRACK, PK_NUM, -64, 63, 0, "", PF_LOCK},
    {"trk.lfo2.phase", "LFO2 PHASE", PS_TRACK, PK_NUM, 0, 127, 0, "", PF_LOCK},
    {"trk.menv.mode", "MENV MODE", PS_TRACK, PK_ENUM, 0, 1, 0, "", PF_LOCK},
    {"master.on", "CHAIN", PS_GLOBAL, PK_ONOFF, 0, 1, 0, "", 0},
    {"master.trim", "TRIM", PS_GLOBAL, PK_NUM, -24, 24, 0, "dB", 0},
    {"master.low", "LOW EQ", PS_GLOBAL, PK_NUM, -12, 12, 0, "dB", 0},
    {"master.mid", "MID EQ", PS_GLOBAL, PK_NUM, -12, 12, 0, "dB", 0},
    {"master.freq", "MID FREQ", PS_GLOBAL, PK_NUM, 20, 20000, 1000, "Hz", 0},
    {"master.high", "HIGH EQ", PS_GLOBAL, PK_NUM, -12, 12, 0, "dB", 0},
    {"master.threshold", "THRESHOLD", PS_GLOBAL, PK_NUM, -60, 0, -18, "dB", 0},
    {"master.ratio", "RATIO", PS_GLOBAL, PK_NUM, 1, 8, 1, ":1", 0},
    {"master.attack", "ATTACK", PS_GLOBAL, PK_NUM, 1, 100, 20, "ms", 0},
    {"master.release", "RELEASE", PS_GLOBAL, PK_NUM, 10, 1000, 150, "ms", 0},
    {"master.makeup", "MAKEUP", PS_GLOBAL, PK_NUM, -12, 18, 0, "dB", 0},
    {"master.clip", "CLIP", PS_GLOBAL, PK_NUM, 0, 18, 0, "dB", 0},
    {"master.limit", "LIMITER", PS_GLOBAL, PK_ONOFF, 0, 1, 1, "", 0},
    {"master.ceiling", "CEILING", PS_GLOBAL, PK_NUM, -60, -1, -10, "", 0},
    {"master.ab", "A/B DRY", PS_GLOBAL, PK_ONOFF, 0, 1, 0, "", 0},
    {"master.eq1.shape", "B1 SHAPE", PS_GLOBAL, PK_ENUM, 0, EQ_SHAPES - 1, EQ_LOW_SHELF, "", 0},
    {"master.eq1.freq", "B1 FREQ", PS_GLOBAL, PK_NUM, 20, 20000, 100, "Hz", 0},
    {"master.eq1.q", "B1 Q", PS_GLOBAL, PK_NUM, 10, 1000, 71, "", 0},
    EQ_PARAMS("master.eq2", "2", 180),
    EQ_PARAMS("master.eq3", "3", 450),
    {"master.eq4.shape", "B4 SHAPE", PS_GLOBAL, PK_ENUM, 0, EQ_SHAPES - 1, EQ_BELL, "", 0},
    {"master.eq4.q", "B4 Q", PS_GLOBAL, PK_NUM, 10, 1000, 71, "", 0},
    EQ_PARAMS("master.eq5", "5", 2000),
    EQ_PARAMS("master.eq6", "6", 3500),
    EQ_PARAMS("master.eq7", "7", 6000),
    {"master.eq8.shape", "B8 SHAPE", PS_GLOBAL, PK_ENUM, 0, EQ_SHAPES - 1, EQ_HIGH_SHELF, "", 0},
    {"master.eq8.freq", "B8 FREQ", PS_GLOBAL, PK_NUM, 20, 20000, 9000, "Hz", 0},
    {"master.eq8.q", "B8 Q", PS_GLOBAL, PK_NUM, 10, 1000, 71, "", 0},
    {"master.knee", "KNEE", PS_GLOBAL, PK_NUM, 0, 24, 6, "dB", 0},
    {"master.detect", "DETECTOR", PS_GLOBAL, PK_ENUM, 0, 1, 0, "", 0},
    {"master.auto", "AUTO REL", PS_GLOBAL, PK_ONOFF, 0, 1, 0, "", 0},
    {"master.comp_mix", "COMP MIX", PS_GLOBAL, PK_NUM, 0, 100, 100, "%", 0},
    {"master.limrel", "LIM RELEASE", PS_GLOBAL, PK_NUM, 10, 1000, 80, "ms", 0},
    {"fx.a.mode", "DLY MODE", PS_GLOBAL, PK_ENUM, 0, DLY_TYPES - 1, DLY_PING_PONG, "", 0},
    {"fx.a.timing", "DLY SYNC", PS_GLOBAL, PK_ENUM, 0, DLY_TIMINGS - 1, DLY_LINKED, "", 0},
    {"fx.a.divr", "TIME R", PS_GLOBAL, PK_ENUM, 0, 8, 4, "", 0},
    {"fx.a.msl", "TIME L FREE", PS_GLOBAL, PK_NUM, 1, 2000, 250, "ms", 0},
    {"fx.a.msr", "TIME R FREE", PS_GLOBAL, PK_NUM, 1, 2000, 375, "ms", 0},
    {"fx.a.base", "DLY BASE", PS_GLOBAL, PK_NUM, 0, 127, 0, "Hz", 0},
    {"fx.a.width", "DLY WIDTH", PS_GLOBAL, PK_NUM, 0, 127, 127, "", 0},
    {"fx.a.wow", "TAPE WOW", PS_GLOBAL, PK_NUM, 0, 100, 25, "%", 0},
    {"fx.a.drive", "TAPE DRIVE", PS_GLOBAL, PK_NUM, 0, 18, 3, "dB", 0},
    {"fx.b.mode", "REV MODE", PS_GLOBAL, PK_ENUM, 0, REV_TYPES - 1, REV_STUDIO, "", 0},
    {"fx.b.pre", "REV PREDELAY", PS_GLOBAL, PK_NUM, 0, 250, 0, "ms", 0},
    {"fx.b.hpf", "REV IN HPF", PS_GLOBAL, PK_NUM, 0, 2500, 0, "Hz", 0},
    {"fx.b.lpf", "REV IN LPF", PS_GLOBAL, PK_NUM, 500, 20000, 20000, "Hz", 0},
    {"fx.b.low", "REV LOW SHELF", PS_GLOBAL, PK_NUM, -12, 12, 0, "dB", 0},
    {"fx.b.lowhz", "REV LOW FREQ", PS_GLOBAL, PK_NUM, 40, 1000, 200, "Hz", 0},
    {"fx.b.high", "REV HIGH SHELF", PS_GLOBAL, PK_NUM, -12, 12, 0, "dB", 0},
    {"fx.b.highhz", "REV HIGH FREQ", PS_GLOBAL, PK_NUM, 1500, 16000, 6000, "Hz", 0},
    {"fx.b.width", "REV WIDTH", PS_GLOBAL, PK_NUM, 0, 100, 100, "%", 0},
    FX_LFO_PARAMS("fx.lfo1", "FX LFO1"),
    FX_LFO_PARAMS("fx.lfo2", "FX LFO2"),
    {"trk.flt.envmode", "ENV SOURCE", PS_TRACK, PK_ENUM, 0, 1, 0, "", PF_LOCK},
    {"trk.flt.atk", "FILT ATK", PS_TRACK, PK_TIME, 0, 127, 0, "s", PF_LOCK},
    {"trk.flt.dec", "FILT DEC", PS_TRACK, PK_TIME, 0, 127, 71, "s", PF_LOCK},
    {"trk.flt.sus", "FILT SUS", PS_TRACK, PK_NUM, 0, 100, 0, "%", PF_LOCK},
    {"trk.flt.rel", "FILT REL", PS_TRACK, PK_TIME, 0, 127, 40, "s", PF_LOCK},
    OSC_PARAMS("trk.poly1", "1"),
    OSC_PARAMS("trk.poly2", "2"),
    FM_PARAMS("trk.fm1", "C"),
    FM_PARAMS("trk.fm2", "A"),
    FM_PARAMS("trk.fm3", "B1"),
    FM_PARAMS("trk.fm4", "B2"),
    INSERT_PARAMS("trk.ins1"),
    INSERT_PARAMS("trk.ins2"),
};
#undef INSERT_PARAMS
#undef EQ_PARAMS
#undef OSC_PARAMS
#undef FM_PARAMS
#undef FX_LFO_PARAMS
enum { NPARAMS = (int)(sizeof PARAMS / sizeof *PARAMS) };

enum { INSERT_FIRST = NPARAMS - INSERT_STAGES * INSERT_FIELDS };
inline bool isInsertParam(int index) { return index >= INSERT_FIRST && index < NPARAMS; }
inline int insertStage(int index) { return (index - INSERT_FIRST) / INSERT_FIELDS; }
inline int insertField(int index) { return (index - INSERT_FIRST) % INSERT_FIELDS; }
inline bool insertFieldVisible(int type, int field) {
    if (field == INS_TYPE) return true;
    if (type == INS_CRUSH) return field <= INS_PREFILTER;
    if (type == INS_COMPRESS) return field <= INS_TRIM || (field >= INS_THRESHOLD && field <= INS_SC_HPF)
                                  || field == INS_DETECT || field == INS_AUTO_RELEASE;
    if (type == INS_CLIP) return field <= INS_TRIM || field == INS_DRIVE || field == INS_CLIP_SHAPE || field == INS_CLIP_CEIL;
    return type == INS_CHORUS && (field <= INS_TRIM || (field >= INS_CH_MODE && field <= INS_CH_WIDTH));
}
// Index innerhalb des Gueltigkeitsbereichs (global bzw. Spur) - so liegen die Werte im Modell.
struct ParamLayout {
    int slot[NPARAMS] = {}, track[TRACK_PARAM_CAP] = {}, global[GLOBAL_PARAM_CAP] = {};
    int nTrack = 0, nGlobal = 0;
    constexpr ParamLayout() {
        for (int i = 0; i < NPARAMS; i++) {
            if (PARAMS[i].scope == PS_TRACK) { slot[i] = nTrack; track[nTrack++] = i; }
            else { slot[i] = nGlobal; global[nGlobal++] = i; }
        }
    }
};
static constexpr ParamLayout PARAM_LAYOUT;
inline int paramSlot(int i) { return i >= 0 && i < NPARAMS ? PARAM_LAYOUT.slot[i] : -1; }
inline int insertParamSlot(int stage) {
    const static int first = paramSlot(INSERT_FIRST);
    return first + stage * INSERT_FIELDS;
}
inline int paramCount(PScope s) { return s == PS_TRACK ? PARAM_LAYOUT.nTrack : PARAM_LAYOUT.nGlobal; }
constexpr bool trackParamLayoutFits() {
    int voice = 0, track = 0, global = 0;
    for (int i = 0; i < NPARAMS; i++) {
        if (PARAMS[i].scope != PS_TRACK) { global++; continue; }
        track++;
        if (i < INSERT_FIRST) voice++;
    }
    return voice <= VOICE_PARAM_CAP && track <= TRACK_PARAM_CAP && global <= GLOBAL_PARAM_CAP;
}
static_assert(trackParamLayoutFits(), "Track parameter storage overflow");
inline int paramFind(const char* id) {
    for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].id, id)) return i;
    return -1;
}
// Spurparameter nach Slot (die Reihenfolge der Tabelle)
inline int paramOfSlot(PScope s, int slot) {
    if (slot < 0 || slot >= paramCount(s)) return -1;
    return s == PS_TRACK ? PARAM_LAYOUT.track[slot] : PARAM_LAYOUT.global[slot];
}

inline int clampParam(int p, int v) { return v < PARAMS[p].lo ? PARAMS[p].lo : v > PARAMS[p].hi ? PARAMS[p].hi : v; }
inline bool isExtendedVoiceParam(int p) {
    static const int first = paramFind("trk.flt.envmode");
    return p >= first && p < INSERT_FIRST;
}

inline bool sourceParamVisible(const char* id, int machine) {
    if (!strncmp(id, "trk.poly", 8)) return machine == 0;
    if (!strncmp(id, "trk.fm", 6)) return machine == 1;
    return true;
}

// --- Notenlaenge (trk.len) -------------------------------------------------------------------
// 1..96 in Zwoelfteln eines Steps (fein, bis 8 Steps), 97..126 grob bis 256 Steps, 127 = INF:
// die Note klingt, bis die Spur das naechste Mal anschlaegt.
static const int LEN_INF = 127;
static const int LEN_GROB[30] = {9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 26, 28, 32, 36, 40,
                                 44, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128, 256};
inline double lenSteps(int v) {
    if (v <= 96) return v / 12.0;
    if (v >= LEN_INF) return -1.0;
    return (double)LEN_GROB[v - 97];
}

