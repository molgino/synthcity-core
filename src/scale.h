// Tonart und Geschwindigkeit eines Patterns.
//
// Tonart: ein Grundton und eine Tonleiter. Noten einer Spur, die "Skala folgen" hat, rasten
// beim Anschlag auf den naechsten Ton der Leiter ein - und "eine Note hoeher" heisst dann
// "die naechste Stufe", nicht "der naechste Halbton".
//
// Geschwindigkeit: ein Multiplikator wie bei Elektron (1/8x bis 2x) - das Pattern laeuft
// schneller oder langsamer, ohne dass sich das Tempo des Projekts aendert.
#pragma once
#include <stdint.h>

struct ScaleDef { const char* name; uint16_t mask; };   // Bit i = Halbton i ueber dem Grundton
static const ScaleDef SCALES[] = {
    {"CHROMATISCH",  0xFFF},
    {"DUR",          0xAB5},   // 0 2 4 5 7 9 11
    {"MOLL",         0x5AD},   // 0 2 3 5 7 8 10
    {"HARM. MOLL",   0x9AD},   // 0 2 3 5 7 8 11
    {"DORISCH",      0x6AD},   // 0 2 3 5 7 9 10
    {"PHRYGISCH",    0x5AB},   // 0 1 3 5 7 8 10
    {"LYDISCH",      0xAD5},   // 0 2 4 6 7 9 11
    {"MIXOLYDISCH",  0x6B5},   // 0 2 4 5 7 9 10
    {"PENT. DUR",    0x295},   // 0 2 4 7 9
    {"PENT. MOLL",   0x4A9},   // 0 3 5 7 10
    {"BLUES",        0x4E9},   // 0 3 5 6 7 10
};
enum { NSCALES = (int)(sizeof SCALES / sizeof *SCALES) };
static const char* const NOTE_NAMES[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

inline bool scaleHas(int note, int root, int scale) {
    if (scale <= 0 || scale >= NSCALES) return true;
    int d = ((note - root) % 12 + 12) % 12;
    return (SCALES[scale].mask >> d) & 1;
}
// Naechster Ton der Leiter; bei Gleichstand der tiefere - so bleibt eine Melodie eher unten.
inline int scaleQuant(int note, int root, int scale) {
    if (scale <= 0 || scale >= NSCALES) return note;
    for (int r = 0; r <= 6; r++) {
        if (scaleHas(note - r, root, scale)) return note - r;
        if (scaleHas(note + r, root, scale)) return note + r;
    }
    return note;
}
// Eine Stufe nach oben (dir = +1) oder unten (dir = -1).
inline int scaleStep(int note, int root, int scale, int dir) {
    int n = scaleQuant(note, root, scale);
    if (scale <= 0 || scale >= NSCALES) return n + dir;
    do { n += dir; } while (!scaleHas(n, root, scale) && n > -12 && n < 140);
    return n;
}

// Geschwindigkeit eines Patterns - Index 4 ist 1x.
struct SpeedDef { const char* name; double mult; };
static const SpeedDef SPEEDS[] = {
    {"1/8x", 0.125}, {"1/4x", 0.25}, {"1/2x", 0.5}, {"3/4x", 0.75},
    {"1x", 1.0}, {"3/2x", 1.5}, {"2x", 2.0},
};
enum { NSPEEDS = (int)(sizeof SPEEDS / sizeof *SPEEDS), SPEED_1X = 4 };

// Teiler einer Spur: wie viele Steps in eine Viertelnote gehen.
struct DivDef { const char* name; uint8_t div; };
static const DivDef DIVS[] = {
    {"1/4", 1}, {"1/8", 2}, {"1/8T", 3}, {"1/16", 4}, {"1/16T", 6}, {"1/32", 8},
};
enum { NDIVS = (int)(sizeof DIVS / sizeof *DIVS) };
