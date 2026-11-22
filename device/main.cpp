// SYNTH CITY CORE - die Geraete-Huelle fuer den R36S (640x480, Gamepad).
// Der Kern ist derselbe wie im Browser und im Plugin: diese Datei bindet die Bruecke
// (web/bridge.cpp) direkt ein und spricht NUR ueber deren sc*-Funktionen mit ihm.
// Hier steht nur, was das Geraet anders macht: Bild (SDL-Fenster oder DRM), Tasten,
// Ton (SDL/ALSA), Dateien (Projekte, Samples, Export).
//
// Faeden: Der Audio-Callback rechnet den Kern. Alles, was die Oberflaeche am Modell
// aendert, geschieht unter SDL_LockAudioDevice - kurz, einmal pro Bild fuer die Eingabe.
// Gezeichnet wird ohne Sperre (es wird nur gelesen).
#include "../web/bridge.cpp"
#include "../src/wav.h"
#include "font.h"
#include "input.h"
#include "drm.h"
#include "midi.h"
#include "usbaudio.h"
#include "web.h"
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
int runDhcpd(const char* ifname);   // dhcpd.cpp: Helfer fuer den PC-Modus (pc-modus.sh startet "groovebox --dhcpd usb0")
#include <SDL.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef SC_NO_LIBSTDCXX
// Ohne libstdc++ gebaut (R36S-Cross-Build): die zwei Dinge, die der Compiler trotzdem erwartet.
extern "C" void __cxa_pure_virtual() { abort(); }
void operator delete(void* p) noexcept { free(p); }
void operator delete(void* p, unsigned long) noexcept { free(p); }
#endif

// ============================================================================================
// Bild
// ============================================================================================
enum { SW = 640, SH = 480 };
static uint32_t FB[SW * SH];

enum : uint32_t {
    C_BG = 0x0e0f12, C_PANEL = 0x16181d, C_SEL = 0x252a35, C_LINE = 0x343945, C_TXT = 0xdde1e8,
    C_DIM = 0x7d8390, C_FAINT = 0x4a4f5a, C_LOCK = 0xffb020, C_PLAY = 0x4fd6a0, C_REC = 0xff4d5e,
    C_SCENE = 0xb68cff
};
static const uint32_t HUE[9] = {0x5ec8ff, 0xffb020, 0xff4d9d, 0x4fd6a0, 0xb68cff, 0xff7a5c, 0x8ad94f, 0x5cc9c9, 0xc9d1d6};

static inline uint32_t mix(uint32_t a, uint32_t b, int t) {   // t 0..256: 0 = a
    uint32_t rb = ((a & 0xff00ff) * (256 - t) + (b & 0xff00ff) * t) >> 8;
    uint32_t g  = ((a & 0x00ff00) * (256 - t) + (b & 0x00ff00) * t) >> 8;
    return (rb & 0xff00ff) | (g & 0x00ff00);
}
static void fill(int x, int y, int w, int h, uint32_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SW) w = SW - x;
    if (y + h > SH) h = SH - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) { uint32_t* r = FB + (y + j) * SW + x; for (int i = 0; i < w; i++) r[i] = c; }
}
static void shade(int x, int y, int w, int h, uint32_t c, int t) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SW) w = SW - x;
    if (y + h > SH) h = SH - y;
    for (int j = 0; j < h; j++) { uint32_t* r = FB + (y + j) * SW + x; for (int i = 0; i < w; i++) r[i] = mix(r[i], c, t); }
}
static void frame(int x, int y, int w, int h, uint32_t c) {
    fill(x, y, w, 1, c); fill(x, y + h - 1, w, 1, c); fill(x, y, 1, h, c); fill(x + w - 1, y, 1, h, c);
}
// Text mit der Bitmap-Schrift (8x16, Graustufen = Deckkraft). Nur ASCII - der Kern liefert nur ASCII.
static int text(int x, int y, const char* s, uint32_t c, int maxCh = 999) {
    int x0 = x;
    for (int n = 0; *s && n < maxCh; s++, n++, x += FONT_W) {
        unsigned ch = (uint8_t)*s;
        if (ch < 32 || ch > 126) ch = '?';
        if (ch == ' ' || x + FONT_W <= 0 || x >= SW) continue;
        const uint8_t* g = FONT_DATA[ch - 32];
        for (int gy = 0; gy < FONT_H; gy++) {
            int py = y + gy;
            if (py < 0 || py >= SH) continue;
            uint32_t* row = FB + py * SW;
            for (int gx = 0; gx < FONT_W; gx++) {
                int px = x + gx;
                unsigned a = g[gy * FONT_W + gx];
                if (!a || px < 0 || px >= SW) continue;
                row[px] = a >= 250 ? c : mix(row[px], c, (int)(a + (a >> 7)));
            }
        }
    }
    return x - x0;
}
static int textR(int xr, int y, const char* s, uint32_t c) {   // rechtsbuendig bis xr
    int n = (int)strlen(s);
    return text(xr - n * FONT_W, y, s, c);
}
static int textC(int xc, int y, const char* s, uint32_t c) {
    int n = (int)strlen(s);
    return text(xc - n * FONT_W / 2, y, s, c);
}
static void triRight(int x, int y, int h, uint32_t c) {        // Abspiel-Dreieck
    for (int j = 0; j < h; j++) { int w = j < h / 2 ? j : h - 1 - j; fill(x, y + j, w + 1, 1, c); }
}
static bool saveBmp(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    uint32_t rowB = SW * 3, size = 54 + rowB * SH;
    uint8_t h[54] = {'B', 'M'};
    memcpy(h + 2, &size, 4); uint32_t off = 54; memcpy(h + 10, &off, 4);
    uint32_t hs = 40; memcpy(h + 14, &hs, 4); int32_t w = SW, hh = SH; memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
    uint16_t pl = 1, bpp = 24; memcpy(h + 26, &pl, 2); memcpy(h + 28, &bpp, 2);
    fwrite(h, 1, 54, f);
    static uint8_t line[SW * 3];
    for (int y = SH - 1; y >= 0; y--) {
        for (int x = 0; x < SW; x++) { uint32_t c = FB[y * SW + x]; line[x * 3] = c & 255; line[x * 3 + 1] = (c >> 8) & 255; line[x * 3 + 2] = (c >> 16) & 255; }
        fwrite(line, 1, rowB, f);
    }
    fclose(f);
    return true;
}

// ============================================================================================
// Dateien
// ============================================================================================
static char gBase[512] = "./";
static char gPrjName[64] = "song01";
static int  gDispOff = 0;                 // Byte-Versatz fuers DRM-Panel (wie im alten SYNTH CITY)
// Geraete-Einstellungen (SYSTEM-Ansicht), in synthcity.cfg
struct DevCfg {
    int usbMode = 0;        // 0 HOST (Geraete am R36S), 1 PC (MIDI + Netz), 2 PC + Audio-Interface
    int usbVol = 100;       // Lautstaerke zum PC in %
    int pcIn = 0;           // was der PC abspielt, in den Ausgang mischen
    int midiIn = 1;         // Noten Kanal 1-8 spielen Spur 1-8, Program Change waehlt das Pattern
    int clockIn = 0;        // externe MIDI-Clock fuehrt Tempo und Start/Stop
    int clockOut = 1;       // MIDI-Clock + Start/Stop senden
    int notesOut = 0;       // Trigs als Noten senden (Spur n = Kanal n)
} gCfg;

static void lockA();
static bool gLearnReq = false;
static void unlockA();
static void pathOf(char* out, size_t n, const char* rel) { snprintf(out, n, "%s%s", gBase, rel); }
static void mkdirs() {
    const char* d[] = {"projects", "samples", "samples/export"};
    for (const char* s : d) { char p[700]; pathOf(p, sizeof p, s); mkdir(p, 0755); }
}
static void settingsLoad() {
    char p[700]; pathOf(p, sizeof p, "synthcity.cfg");
    FILE* f = fopen(p, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char k[64], v[128];
        if (sscanf(line, "%63s %127[^\n]", k, v) != 2) continue;
        if (!strcmp(k, "project")) snprintf(gPrjName, sizeof gPrjName, "%s", v);
        else if (!strcmp(k, "display_offset")) gDispOff = atoi(v);
        else if (!strcmp(k, "usb_mode")) gCfg.usbMode = atoi(v) < 0 ? 0 : atoi(v) > 2 ? 2 : atoi(v);
        else if (!strcmp(k, "usb_volume")) gCfg.usbVol = atoi(v);
        else if (!strcmp(k, "pc_in")) gCfg.pcIn = atoi(v) != 0;
        else if (!strcmp(k, "midi_in")) gCfg.midiIn = atoi(v) != 0;
        else if (!strcmp(k, "clock_in")) gCfg.clockIn = atoi(v) != 0;
        else if (!strcmp(k, "clock_out")) gCfg.clockOut = atoi(v) != 0;
        else if (!strcmp(k, "notes_out")) gCfg.notesOut = atoi(v) != 0;
    }
    fclose(f);
}
static void settingsSave() {
    char p[700]; pathOf(p, sizeof p, "synthcity.cfg");
    FILE* f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "project %s\ndisplay_offset %d\nusb_mode %d\nusb_volume %d\npc_in %d\nmidi_in %d\nclock_in %d\nclock_out %d\nnotes_out %d\n",
            gPrjName, gDispOff, gCfg.usbMode, gCfg.usbVol, gCfg.pcIn, gCfg.midiIn, gCfg.clockIn, gCfg.clockOut, gCfg.notesOut);
    fclose(f);
}

// --- WAV lesen: 8/16/24/32 Bit PCM und 32 Bit Fliesskomma, mono oder stereo ------------------
static float* wavRead(const char* path, int* frames, int* ch, float* rate) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return nullptr; }
    int fmt = 0, nch = 0, bits = 0; uint32_t sr = 0;
    float* out = nullptr;
    for (;;) {
        uint8_t c[8];
        if (fread(c, 1, 8, f) != 8) break;
        uint32_t len; memcpy(&len, c + 4, 4);
        if (!memcmp(c, "fmt ", 4)) {
            uint8_t b[40] = {0};
            uint32_t r = len < 40 ? len : 40;
            if (fread(b, 1, r, f) != r) break;
            if (len > r) fseek(f, (long)(len - r), SEEK_CUR);
            memcpy(&fmt, b, 2); fmt &= 0xffff;
            nch = b[2] | (b[3] << 8);
            memcpy(&sr, b + 4, 4);
            bits = b[14] | (b[15] << 8);
            if (fmt == 0xFFFE && r >= 26) fmt = b[24] | (b[25] << 8);   // WAVE_FORMAT_EXTENSIBLE
        } else if (!memcmp(c, "data", 4)) {
            if (!nch || !bits || (fmt != 1 && fmt != 3)) break;
            int bps = bits / 8;
            long n = (long)len / (bps * nch);
            int keep = nch >= 2 ? 2 : 1;
            if (n <= 0) break;
            out = (float*)malloc((size_t)n * keep * sizeof(float));
            uint8_t* raw = (uint8_t*)malloc((size_t)len);
            if (!out || !raw) { free(out); free(raw); out = nullptr; break; }
            n = (long)fread(raw, 1, len, f) / (bps * nch);
            for (long i = 0; i < n; i++)
                for (int k = 0; k < keep; k++) {
                    const uint8_t* s = raw + ((size_t)i * nch + k) * bps;
                    float v = 0;
                    if (fmt == 3 && bits == 32) memcpy(&v, s, 4);
                    else if (bits == 8)  v = (s[0] - 128) / 128.0f;
                    else if (bits == 16) v = (int16_t)(s[0] | (s[1] << 8)) / 32768.0f;
                    else if (bits == 24) v = (int32_t)((uint32_t)(s[0] << 8 | s[1] << 16 | s[2] << 24)) / 2147483648.0f;
                    else if (bits == 32) { int32_t x; memcpy(&x, s, 4); v = x / 2147483648.0f; }
                    out[i * keep + k] = v;
                }
            free(raw);
            *frames = (int)n; *ch = keep; *rate = (float)sr;
            break;
        } else fseek(f, (long)(len + (len & 1)), SEEK_CUR);
    }
    fclose(f);
    return out;
}

// --- Sample-Dateien: alle WAVs unter samples/ (mit Unterordnern) ---------------------------
enum { MAX_FILES = 4096 };
struct FileEnt { char rel[200]; };
static FileEnt gFiles[MAX_FILES];
static int gNFiles = 0;
static bool hasWav(const char* n) {
    size_t l = strlen(n);
    return l > 4 && (!strcasecmp(n + l - 4, ".wav"));
}
static void scanDir(const char* rel, int depth) {
    char p[700]; snprintf(p, sizeof p, "%ssamples/%s", gBase, rel);
    DIR* d = opendir(p);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) && gNFiles < MAX_FILES) {
        if (e->d_name[0] == '.') continue;
        char r[400]; snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        char full[800]; snprintf(full, sizeof full, "%ssamples/%s", gBase, r);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < 6 && strcmp(e->d_name, "export")) scanDir(r, depth + 1); }
        else if (hasWav(e->d_name) && strlen(r) < sizeof gFiles[0].rel) snprintf(gFiles[gNFiles++].rel, sizeof gFiles[0].rel, "%s", r);
    }
    closedir(d);
}
static int cmpFile(const void* a, const void* b) { return strcasecmp(((const FileEnt*)a)->rel, ((const FileEnt*)b)->rel); }
static void scanSamples() {
    gNFiles = 0;
    scanDir("", 0);
    qsort(gFiles, gNFiles, sizeof gFiles[0], cmpFile);
}
// Name im Projekt = Dateiname ohne Ordner und ohne .wav (so wie ihn auch der Browser vergibt)
static void smpNameOf(const char* rel, char* out, int n) {
    const char* b = rel;
    for (const char* c = rel; *c; c++) if (*c == '/') b = c + 1;
    snprintf(out, n, "%s", b);
    int l = (int)strlen(out);
    if (l > 4 && !strcasecmp(out + l - 4, ".wav")) out[l - 4] = 0;
}

static bool poolLoadFile(int slot, const char* rel);
// Der Vorrat waechst nur nach vorn. Ist er voll, werden die belegten Plaetze dicht neu gepackt.
static void poolCompact() {
    struct Keep { float* d; int n, ch, loop, a, b, la; float rate; char name[SMP_NAME]; };
    static Keep k[SMP_SLOTS];
    for (int i = 0; i < SMP_SLOTS; i++) {
        k[i].d = nullptr;
        int n = scSmpFrames(i);
        if (n <= 0) { snprintf(k[i].name, SMP_NAME, "%s", scSmpName(i)); continue; }
        k[i].n = n; k[i].ch = scSmpCh(i); k[i].rate = scSmpRate(i); k[i].loop = scSmpLoop(i);
        k[i].a = scSmpA(i); k[i].b = scSmpB(i); k[i].la = scSmpLA(i);
        snprintf(k[i].name, SMP_NAME, "%s", scSmpName(i));
        k[i].d = (float*)malloc((size_t)n * k[i].ch * sizeof(float));
        if (k[i].d) memcpy(k[i].d, scSmpData(i), (size_t)n * k[i].ch * sizeof(float));
    }
    scSmpClear();
    for (int i = 0; i < SMP_SLOTS; i++) {
        if (!k[i].d) { if (k[i].name[0]) snprintf(gEng.pool.s[i].name, SMP_NAME, "%s", k[i].name); continue; }
        scSmpSet(i, k[i].d, k[i].n, k[i].rate, k[i].name, k[i].loop, k[i].ch);
        scSmpSetTrim(i, k[i].a, k[i].b, k[i].la, k[i].loop);
        free(k[i].d);
    }
}
static bool poolLoadFile(int slot, const char* rel) {
    char full[800]; snprintf(full, sizeof full, "%ssamples/%s", gBase, rel);
    int n = 0, ch = 1; float rate = 48000;
    float* d = wavRead(full, &n, &ch, &rate);
    if (!d) return false;
    if (n > SMP_POOL / ch) n = SMP_POOL / ch;
    char nm[SMP_NAME]; smpNameOf(rel, nm, sizeof nm);
    bool ok = false;
    lockA();
    gEng.pool.s[slot].n = 0;                       // der alte Inhalt zaehlt nicht mehr
    if (scSmpFree() < n * ch) poolCompact();
    ok = scSmpSet(slot, d, n, rate, nm, 0, ch) != 0;
    unlockA();
    free(d);
    return ok;
}
// Nach dem Laden eines Projekts: jeder Platz, der einen Namen kennt, aber leer ist, sucht seine Datei.
static int poolResolveWanted() {
    int got = 0;
    scanSamples();
    for (int i = 0; i < SMP_SLOTS; i++) {
        if (!scSmpWanted(i)) continue;
        char want[SMP_NAME]; snprintf(want, sizeof want, "%s", scSmpName(i));
        for (int f = 0; f < gNFiles; f++) {
            char nm[SMP_NAME]; smpNameOf(gFiles[f].rel, nm, sizeof nm);
            char nmx[SMP_NAME]; { const char* b = gFiles[f].rel; for (const char* c = b; *c; c++) if (*c == '/') b = c + 1; snprintf(nmx, sizeof nmx, "%s", b); }
            if (!strcmp(nm, want) || !strcmp(nmx, want)) { if (poolLoadFile(i, gFiles[f].rel)) got++; break; }
        }
    }
    return got;
}

// --- Projekt: das Dokument des Kerns als Textdatei ----------------------------------------
static void prjPath(const char* name, char* out, size_t n) { snprintf(out, n, "%sprojects/%s.sc", gBase, name); }
static bool prjSave() {
    char p[800], tmp[820]; prjPath(gPrjName, p, sizeof p);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    lockA();
    const char* doc = scSave();
    FILE* f = fopen(tmp, "w");
    bool ok = f && fputs("# synthcity 1\n", f) >= 0 && fputs(doc, f) >= 0;
    unlockA();
    if (f) ok = (fclose(f) == 0) && ok;
    if (ok) ok = rename(tmp, p) == 0;     // erst ganz schreiben, dann ersetzen: ein Absturz verliert nichts
    if (ok) sync();
    return ok;
}
static bool prjLoad(const char* name) {
    char p[800]; prjPath(name, p, sizeof p);
    FILE* f = fopen(p, "r");
    if (!f) return false;
    static char buf[DOC_MAX * 48];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    lockA();
    scStop();
    scSmpClear();
    scLoad(buf);
    unlockA();
    poolResolveWanted();
    return true;
}
static bool prjExists(const char* name) { char p[800]; prjPath(name, p, sizeof p); struct stat st; return stat(p, &st) == 0; }

// ============================================================================================
// Ton
// ============================================================================================
static SDL_AudioDeviceID gAudio = 0;
static int  gRate = 48000;
static volatile bool gExporting = false;
static volatile float gPeak = 0;
static volatile float gCpu = 0;           // Anteil der Pufferzeit, den der Kern zum Rechnen braucht

// --- MIDI im Audio-Faden: was hereinkommt, wirkt vor dem naechsten Block; was hinausgeht, bekommt
// eine Uhrzeit (Blockbeginn + ein Puffer Vorlauf), der MIDI-Faden schickt es dann puenktlich.
static double  gCbTime = 0;               // midiNow() zu Beginn des Blocks
static int     gCbFrames = 256;
static int64_t gBlockPos = 0;             // Sequencer-Position zu Beginn des Blocks
static int64_t gClock = 0;                // eigene Sample-Uhr (laeuft auch im Stand)
static bool    gFromMidi = false;         // Noten von aussen nicht wieder hinausschicken
static volatile int gMidiInSeen = 0;      // fuer die Anzeige
struct PendOff { int64_t at; uint8_t st, note; };
static PendOff gOffs[128]; static int gNOffs = 0;
static double dueAt(int64_t clockSample) { return gCbTime + (double)(gCbFrames + (clockSample - gClock)) / gRate; }
static void noteOutCb(void*, int track, int note, int vel, int64_t at, int32_t len) {
    if (!gCfg.notesOut || gFromMidi || note < 0 || note > 127) return;
    int64_t c = gClock + (at - gBlockPos > 0 ? at - gBlockPos : 0);
    MidiMsg on = {(uint8_t)(0x90 | track), (uint8_t)note, (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel), 0, dueAt(c)};
    midiOutPush(on);
    if (gNOffs < 128) gOffs[gNOffs++] = {c + (len >= 2147483647 || len <= 0 ? gRate / 2 : len), (uint8_t)(0x80 | track), (uint8_t)note};
}
static void flushOffs(int64_t until) {   // faellige Note-Offs senden (alle, wenn until < 0)
    for (int i = 0; i < gNOffs;) {
        if (until >= 0 && gOffs[i].at >= until) { i++; continue; }
        MidiMsg m = {gOffs[i].st, gOffs[i].note, 0, 0, until < 0 ? midiNow() : dueAt(gOffs[i].at)};
        midiOutPush(m);
        gOffs[i] = gOffs[--gNOffs];
    }
}
static double gClkPhase = 0;              // MIDI-Clock-Ausgang: Samples bis zum naechsten Tick
static bool   gWasRunning = false;
static double gInLast = 0, gInSum = 0; static int gInN = 0;   // MIDI-Clock-Eingang: Abstand der Ticks
static void midiInHandle(const MidiMsg& m) {
    uint8_t st = m.st, ty = st & 0xF0, ch = st & 0x0F;
    gMidiInSeen++;
    if (gCfg.clockIn) {
        if (st == 0xF8) {
            double t = m.due > 0 ? m.due : midiNow();
            if (gInLast > 0) {
                double d = t - gInLast;
                if (d > 0.004 && d < 0.2) { gInSum += d; gInN++; }
                if (gInN >= 24) {   // ein Viertel gemessen: Tempo glatt nachfuehren
                    double bpm = 60.0 / (gInSum / gInN * 24.0);
                    gEng.extBpm = gEng.extBpm > 0 ? gEng.extBpm * 0.5 + bpm * 0.5 : bpm;
                    gEng.setTempo(gEng.effTempo(gEng.pat));
                    gInSum = 0; gInN = 0;
                }
            }
            gInLast = t;
            return;
        }
        if (st == 0xFA) { gInLast = 0; gInSum = 0; gInN = 0; gEng.start(); return; }
        if (st == 0xFB) { gEng.start(); return; }
        if (st == 0xFC) { gEng.stop(); return; }
    }
    if (!gCfg.midiIn) return;
    if (ty == 0x90 && m.d2 > 0 && ch < NTRACKS) { gFromMidi = true; scPlayNote(ch, m.d1, m.d2); gFromMidi = false; }
    else if (ty == 0xC0) { int p = m.d1 % NPATTERNS; if (gEng.seq.running) gEng.queuePattern(p); else scSetPattern(p); }
}
static void midiBlock(int frames) {
    MidiMsg mm;
    while (midiInPop(mm)) midiInHandle(mm);
    if (gCfg.clockIn && gEng.extBpm > 0 && gInLast > 0 && midiNow() - gInLast > 2.0) {   // Clock weg: eigenes Tempo
        gEng.extBpm = 0; gEng.setTempo(gEng.effTempo(gEng.pat)); gInLast = 0;
    }
    if (!gCfg.clockIn && gEng.extBpm > 0) { gEng.extBpm = 0; gEng.setTempo(gEng.effTempo(gEng.pat)); }
    bool run = gEng.seq.running;
    if (gCfg.clockOut) {
        if (run && !gWasRunning) { MidiMsg s = {0xFA, 0, 0, 0, dueAt(gClock)}; midiOutPush(s); gClkPhase = 0; }
        if (!run && gWasRunning) { MidiMsg s = {0xFC, 0, 0, 0, dueAt(gClock)}; midiOutPush(s); }
        if (run) {
            double bpm = gEng.tempoOf(gEng.pat);
            double per = gRate * 60.0 / ((bpm > 1 ? bpm : 120.0) * 24.0);
            while (gClkPhase < frames) {
                MidiMsg c = {0xF8, 0, 0, 0, dueAt(gClock + (int64_t)gClkPhase)};
                midiOutPush(c);
                gClkPhase += per;
            }
            gClkPhase -= frames;
        }
    }
    if (!run && gWasRunning) flushOffs(-1);
    gWasRunning = run;
}

static void audioCb(void*, Uint8* stream, int len) {
    float* out = (float*)stream;
    int frames = len / (int)(2 * sizeof(float));
    if (gExporting) { memset(stream, 0, (size_t)len); return; }
    uint64_t t0 = SDL_GetPerformanceCounter();
    gCbTime = midiNow(); gCbFrames = frames; gBlockPos = gEng.seq.pos;
    midiBlock(frames);
    scRender(out, frames);
    flushOffs(gClock + frames);
    gClock += frames;
    usbAudioPush(out, frames);                                 // parallel an den PC (falls aktiv)
    pcInMix(out, frames, gCfg.pcIn ? 1.0f : 0.0f);             // PC-Ton dazu (nach dem Senden: kein Echo)
    double used = (double)(SDL_GetPerformanceCounter() - t0) / SDL_GetPerformanceFrequency();
    float load = (float)(used * gRate / frames);
    gCpu = load > gCpu ? load : gCpu * 0.98f + load * 0.02f;
    float pk = 0;
    for (int i = 0; i < frames * 2; i++) { float a = fabsf(out[i]); if (a > pk) pk = a; }
    if (pk > gPeak) gPeak = pk;
}
static bool openAudio(int bufFrames) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000; want.format = AUDIO_F32SYS; want.channels = 2;
    want.samples = (Uint16)bufFrames; want.callback = audioCb;
    gAudio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!gAudio) { printf("Audio: %s\n", SDL_GetError()); return false; }
    gRate = have.freq;
    printf("Audio: %d Hz, Puffer %d Frames (%.1f ms)\n", have.freq, have.samples, have.samples * 1000.0 / have.freq);
    return true;
}
// Sperre um alles, was das Modell aendert. Ohne Ton (Tests, Bilder) geht es ohne.
static void lockA()   { if (gAudio) SDL_LockAudioDevice(gAudio); }
static void unlockA() { if (gAudio) SDL_UnlockAudioDevice(gAudio); }

// ============================================================================================
// Oberflaeche: Zustand
// ============================================================================================
enum { V_TRACK, V_SONG, V_PROJECT, V_SYSTEM, V_N };
static const char* VIEWNAME[V_N] = {"TRACK", "SONG", "PROJ", "SYSTEM"};
enum { TAB_TRIG, TAB_CHRD, TAB_SRC, TAB_FLTR, TAB_AMP, TAB_MIX, TAB_MOD, TAB_N };
static const char* TABNAME[TAB_N] = {"TRIG", "CHRD", "SRC", "FLTR", "AMP", "MIX", "MOD"};
enum { M_NONE, M_MENU, M_SAMPLES, M_HELP, M_KEYS };
static const int FXT = NTRACKS;   // die FX-Spur des Patterns

struct UI {
    int view = V_TRACK, sel = 0, tab = TAB_TRIG;
    int rowSel[TAB_N + 1] = {0};      // gewaehlte Zeile je Reiter (+ FX-Spur)
    int cur = 0;                      // Step-Cursor
    bool aHeld = false, aEdited = false;
    int modal = M_NONE, menuSel = 0, smpSel = 0, smpScroll = 0;
    int songRow = 0, songField = 0;
    int prjRow = 0, sysRow = 0;
    int keyRow = 0, keyCol = 0, keyMode = 0; char keyBuf[32] = "";   // Bildschirmtastatur
    char toast[96] = ""; uint32_t toastUntil = 0;
    bool dirty = false; uint32_t editAt = 0;
    bool quit = false;
    uint32_t now = 0;
    int listScroll = 0;
    int scene = -1;                   // -1 = aus, 0/1 = Werte gehen in Scene A/B (Crossfader)
} U;
static Input IN;

static void toast(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void toast(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(U.toast, sizeof U.toast, fmt, ap);
    va_end(ap);
    U.toastUntil = U.now + 1800;
}
static void touched() { U.dirty = true; U.editAt = U.now; }

// --- Parameter, wie die Web-Oberflaeche sie ordnet -----------------------------------------
static int P_MACHINE, P_SMP, P_NOTE, P_QUANT;
static int pIndex(const char* id) { return paramFind(id); }
static int slotOf(int p) { return paramSlot(p); }
static int tabOf(const ParamDef& d) {
    if (strncmp(d.id, "trk.", 4)) return -1;
    char key[16]; int i = 0;
    for (const char* c = d.id + 4; *c && *c != '.' && i < 15; c++) key[i++] = *c;
    key[i] = 0;
    static const struct { const char* k; int t; } M[] = {
        {"note", TAB_TRIG}, {"vel", TAB_TRIG}, {"len", TAB_TRIG}, {"prob", TAB_TRIG}, {"cond", TAB_TRIG},
        {"micro", TAB_TRIG}, {"ratchet", TAB_TRIG}, {"quant", TAB_TRIG},
        {"nt2", TAB_CHRD}, {"nt3", TAB_CHRD}, {"nt4", TAB_CHRD}, {"vmode", TAB_CHRD},
        {"machine", TAB_SRC}, {"smp", TAB_SRC}, {"src", TAB_SRC}, {"flt", TAB_FLTR}, {"bw", TAB_FLTR},
        {"amp", TAB_AMP}, {"vol", TAB_MIX}, {"pan", TAB_MIX}, {"send", TAB_MIX},
        {"lfo1", TAB_MOD}, {"lfo2", TAB_MOD}, {"menv", TAB_MOD}};
    for (auto& m : M) if (!strcmp(m.k, key)) return m.t;
    return TAB_MIX;
}
static int machOf(int t) { return t >= FXT ? 0 : scGet(t, slotOf(P_MACHINE)); }
static int srcKnob(const ParamDef& d) {   // trk.src.a..h -> 0..7, sonst -1
    if (strncmp(d.id, "trk.src.", 8) || !d.id[8] || d.id[9]) return -1;
    return d.id[8] - 'a';
}
// Zeilen der aktuellen Liste: Parameterindex, oder -1-k = Ueberschrift k
enum { HDR_LFO1 = -1, HDR_LFO2 = -2, HDR_MENV = -3, HDR_DELAY = -4, HDR_REVERB = -5, HDR_MASTER = -6 };
static int gRows[96], gNRows = 0;
static void buildRows() {
    gNRows = 0;
    if (U.sel == FXT) {
        int last = 0;
        for (int pass = 0; pass < 3; pass++)
            for (int i = 0; i < NPARAMS; i++) {
                if (!fxLockable(i)) continue;
                const char* id = PARAMS[i].id;
                int k = !strncmp(id, "fx.a", 4) ? 0 : !strncmp(id, "fx.b", 4) ? 1 : 2;
                if (k != pass) continue;
                int h = k == 0 ? HDR_DELAY : k == 1 ? HDR_REVERB : HDR_MASTER;
                if (h != last) { gRows[gNRows++] = h; last = h; }
                gRows[gNRows++] = i;
            }
        return;
    }
    bool smp = scMachUsesSmp(machOf(U.sel)) != 0;
    int lastHdr = 0;
    for (int i = 0; i < NPARAMS && gNRows < 94; i++) {
        const ParamDef& d = PARAMS[i];
        if (d.scope != PS_TRACK || tabOf(d) != U.tab) continue;
        if (i == P_SMP && !smp) continue;
        int k = srcKnob(d);
        if (k >= 0 && !scMachLabel(machOf(U.sel), k)[0]) continue;
        if (U.tab == TAB_MOD) {
            int h = !strncmp(d.id, "trk.lfo1", 8) ? HDR_LFO1 : !strncmp(d.id, "trk.lfo2", 8) ? HDR_LFO2 : HDR_MENV;
            if (h != lastHdr) { gRows[gNRows++] = h; lastHdr = h; }
        }
        gRows[gNRows++] = i;
    }
}
static const char* hdrName(int h) {
    switch (h) { case HDR_LFO1: return "LFO 1"; case HDR_LFO2: return "LFO 2"; case HDR_MENV: return "MOD ENVELOPE";
                 case HDR_DELAY: return "DELAY (SEND A)"; case HDR_REVERB: return "REVERB (SEND B)"; default: return "MASTER"; }
}
static int& rowSelRef() { return U.rowSel[U.sel == FXT ? TAB_N : U.tab]; }
// gewaehlte Zeile: immer ein Parameter, nie eine Ueberschrift
static int curParam() {
    if (!gNRows) return -1;
    int& r = rowSelRef();
    if (r >= gNRows) r = gNRows - 1;
    if (r < 0) r = 0;
    while (r < gNRows - 1 && gRows[r] < 0) r++;
    return gRows[r] >= 0 ? gRows[r] : -1;
}
static void moveRow(int d) {
    int& r = rowSelRef();
    int n = r;
    for (;;) {
        n += d;
        if (n < 0 || n >= gNRows) return;
        if (gRows[n] >= 0) { r = n; return; }
    }
}
static const char* rowName(int p) {
    static char b[32];
    const ParamDef& d = PARAMS[p];
    static const struct { const char* id; const char* n; } FXN[] = {
        {"fx.a.div", "TIME"}, {"fx.a.fb", "FEEDBACK"}, {"fx.a.tone", "TONE"}, {"fx.a.vol", "LEVEL"},
        {"fx.b.size", "SIZE"}, {"fx.b.dec", "DECAY"}, {"fx.b.damp", "DAMPING"}, {"fx.b.vol", "LEVEL"},
        {"master.vol", "VOLUME"}};
    for (auto& f : FXN) if (!strcmp(f.id, d.id)) return f.n;
    int k = srcKnob(d);
    if (k >= 0) return scMachLabel(machOf(U.sel), k);
    if (!strncmp(d.id, "trk.lfo", 7) || !strncmp(d.id, "trk.menv.", 9)) {
        const char* s = d.name;
        while (*s && *s != ' ') s++;
        snprintf(b, sizeof b, "%s", *s ? s + 1 : d.name);
        return b;
    }
    return d.name;
}
static bool lockable(int p) {
    if (p < 0) return false;
    if (U.sel == FXT) return fxLockable(p);
    return (PARAMS[p].flags & PF_LOCK) != 0;
}
static int baseValue(int p) { return PARAMS[p].scope == PS_GLOBAL ? scGet(-1, slotOf(p)) : scGet(U.sel, slotOf(p)); }
static int sceneValue(int p) {   // -32768 = die Scene laesst den Regler in Ruhe
    if (U.scene < 0 || U.sel >= FXT || PARAMS[p].scope != PS_TRACK) return -32768;
    return scSceneGet(U.scene, U.sel, slotOf(p));
}
static int lockValue(int p, int step) {   // -32768 = kein Lock
    if (!lockable(p)) return -32768;
    return scLockGet(U.sel, step, slotOf(p));
}
// Text eines Werts - mit den Sonderfaellen der Web-Oberflaeche (Sample-Name, Akkordstufen, Tonart)
static const char* valueText(int p, int v) {
    static char b[96];
    const ParamDef& d = PARAMS[p];
    if (p == P_SMP) {
        if (scSmpFrames(v)) snprintf(b, sizeof b, "%d %s", v + 1, scSmpName(v));
        else if (scSmpName(v)[0]) snprintf(b, sizeof b, "%d %s (missing)", v + 1, scSmpName(v));
        else snprintf(b, sizeof b, "%d empty", v + 1);
        return b;
    }
    int pat = scPattern();
    bool key = U.sel < FXT && scPatScale(pat) > 0 && scGet(U.sel, slotOf(P_QUANT)) > 0;
    if (key && (!strcmp(d.id, "trk.nt2") || !strcmp(d.id, "trk.nt3") || !strcmp(d.id, "trk.nt4"))) {
        if (v <= 0) return "OFF";
        int deg = v - 25, a = deg < 0 ? -deg : deg, n = scScaleSize();
        int target = scScaleDeg(baseValue(P_NOTE), deg);
        static const char* ST[7] = {"root", "2nd", "3rd", "4th", "5th", "6th", "7th"};
        char nm[24];
        if (a % n == 0) snprintf(nm, sizeof nm, a ? "%d oct" : "unison", a / n);
        else if (n == 7) snprintf(nm, sizeof nm, "%s", ST[a % 7]);
        else snprintf(nm, sizeof nm, "%d deg", a);
        snprintf(b, sizeof b, "%c%d %s %s", deg > 0 ? '+' : deg < 0 ? '-' : ' ', a, nm, scParamText(P_NOTE, (float)target, -1));
        return b;
    }
    if (p == P_NOTE && key && !scScaleHas(v)) {
        char a[32]; snprintf(a, sizeof a, "%s", scParamText(p, (float)v, -1));
        snprintf(b, sizeof b, "%s > %s", a, scParamText(p, (float)scScaleQuant(v), -1));
        return b;
    }
    snprintf(b, sizeof b, "%s", scParamText(p, (float)v, U.sel < FXT ? machOf(U.sel) : -1));
    return b;
}

// --- Werte aendern ------------------------------------------------------------------------
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int stepValue(int p, int v, int dir, bool coarse) {
    const ParamDef& d = PARAMS[p];
    int n = scEnumCount(p);
    if (n > 0) {                                   // Auswahl: in Listenreihenfolge, grob = naechste Gruppe
        int idx = 0;
        for (int i = 0; i < n; i++) if (scEnumValue(p, i) == v) { idx = i; break; }
        if (coarse) {
            const char* g0 = scEnumGroup(p, idx);
            int j = idx;
            do j += dir; while (j > 0 && j < n - 1 && !strcmp(scEnumGroup(p, j), g0));
            idx = j;
        } else idx += dir;
        return scEnumValue(p, clampi(idx, 0, n - 1));
    }
    int k = srcKnob(d);
    int st = (k >= 0 && U.sel < FXT) ? scMachSteps(machOf(U.sel), k) : 0;
    if (st > 0) {                                  // Machine-Regler mit festen Stufen
        int s = clampi(v * st / 128 + dir, 0, st - 1);
        return s * 128 / st + 64 / st;
    }
    int span = d.hi - d.lo;
    int step = coarse ? (span > 32 ? span / 16 : 4) : 1;
    return clampi(v + dir * step, d.lo, d.hi);
}
static void editParam(int p, int dir, bool coarse) {
    if (p < 0) return;
    const ParamDef& d = PARAMS[p];
    bool asLock = U.aHeld && lockable(p);
    bool asScene = !asLock && U.scene >= 0 && U.sel < FXT && d.scope == PS_TRACK && p != P_MACHINE;
    int lv = asLock ? lockValue(p, U.cur) : asScene ? sceneValue(p) : -32768;
    int v = lv != -32768 ? lv : baseValue(p);
    int nv = stepValue(p, v, dir, coarse);
    if (asScene) {
        scSceneSet(U.scene, U.sel, slotOf(p), nv);
    } else if (asLock) {
        if (U.sel < FXT && !scStepGet(U.sel, U.cur)) scStepSet(U.sel, U.cur, 2);   // trigless: nur Werte
        scLockSet(U.sel, U.cur, slotOf(p), nv, 0);
        U.aEdited = true;
    } else if (p == P_MACHINE) {
        if (nv != v) scSetMachine(U.sel, nv);
    } else if (d.scope == PS_GLOBAL) scSet(-1, slotOf(p), nv);
    else scSet(U.sel, slotOf(p), nv);
    touched();
}

// --- Steps ---------------------------------------------------------------------------------
static int trackLen() { return scTrackLen(U.sel); }
static void toggleTrig() {
    int s = U.cur;
    if (U.sel == FXT) { toast("FX track: hold A and change a value to lock it"); return; }
    int was = scStepGet(U.sel, s);
    scStepSet(U.sel, s, was == 1 ? 0 : 1);
    touched();
}
static void clearStepLocks() {
    int s = U.cur;
    for (int i = scNLocks(U.sel, s) - 1; i >= 0; i--) scLockClear(U.sel, s, scLockSlot(U.sel, s, i));
    if (U.sel < FXT && scStepGet(U.sel, s) == 2) scStepSet(U.sel, s, 0);
    touched();
    toast("Step %d: locks cleared", s + 1);
}

// --- Menue (SELECT) -------------------------------------------------------------------------
enum { MI_SAVE, MI_COPYPAT, MI_PASTEPAT, MI_CLEARPAT, MI_COPYTRK, MI_PASTETRIG, MI_PASTEALL, MI_CLEARTRIG,
       MI_COPYSTEP, MI_PASTESTEP, MI_LOADSMP, MI_LEN_DN, MI_LEN_UP, MI_SCENE_A, MI_SCENE_B, MI_SCENE_CLR, MI_HELP, MI_N };
static const char* MENU[MI_N] = {"Save project", "Copy pattern", "Paste pattern", "Clear pattern",
    "Copy track", "Paste trigs", "Paste track (with sound)", "Clear trigs", "Copy step", "Paste step",
    "Load sample into slot...", "Track length -16", "Track length +16",
    "Edit scene A (on/off)", "Edit scene B (on/off)", "Clear the scene being edited", "Help"};
static void openSamples();
static void menuDo(int i) {
    int p = scPattern();
    U.modal = M_NONE;
    switch (i) {
    case MI_SAVE: toast(prjSave() ? "Saved %s" : "Could not save %s", gPrjName); U.dirty = false; return;
    case MI_COPYPAT: scClipPat(p); toast("Pattern %02d copied", p + 1); return;
    case MI_PASTEPAT: if (!scHasClipPat()) { toast("Nothing copied"); return; } scPastePat(p); toast("Pasted into pattern %02d", p + 1); break;
    case MI_CLEARPAT: scPatClear(p); toast("Pattern %02d cleared", p + 1); break;
    case MI_COPYTRK: scClipTrack(U.sel); toast("Track copied"); return;
    case MI_PASTETRIG: if (!scHasClipTrack()) { toast("Nothing copied"); return; } scPasteTrack(U.sel, 0); toast("Trigs pasted"); break;
    case MI_PASTEALL: if (!scHasClipTrack()) { toast("Nothing copied"); return; } scPasteTrack(U.sel, 1); toast("Track pasted"); break;
    case MI_CLEARTRIG: scClearTrigs(U.sel); toast("Trigs cleared"); break;
    case MI_COPYSTEP: scClipStep(U.sel, U.cur); toast("Step %d copied", U.cur + 1); return;
    case MI_PASTESTEP: if (!scHasClipStep()) { toast("Nothing copied"); return; } scPasteStep(U.sel, U.cur); toast("Pasted on step %d", U.cur + 1); break;
    case MI_LOADSMP: openSamples(); return;
    case MI_LEN_DN: scSetTrackLen(U.sel, trackLen() - 16 < 1 ? 1 : trackLen() - 16); toast("Length %d", trackLen()); break;
    case MI_LEN_UP: scSetTrackLen(U.sel, trackLen() + 16); toast("Length %d", trackLen()); break;
    case MI_SCENE_A: case MI_SCENE_B: {
        int sc = i - MI_SCENE_A;
        U.scene = U.scene == sc ? -1 : sc;
        toast(U.scene < 0 ? "Scene edit off" : "Editing scene %c: X/Y now set scene values", 'A' + sc);
        return;
    }
    case MI_SCENE_CLR:
        if (U.scene < 0) { toast("Choose scene A or B first"); return; }
        scSceneClearAll(U.scene); toast("Scene %c cleared", 'A' + U.scene); break;
    case MI_HELP: U.modal = M_HELP; return;
    }
    touched();
}

// --- Sample-Auswahl -----------------------------------------------------------------------
static int smpTargetSlot() { return U.sel < FXT ? scGet(U.sel, slotOf(P_SMP)) : 0; }
// Ordner-Explorer: die Dateiliste ist flach (relative Pfade), die Ansicht zeigt einen Ordner:
// "..", dann die Unterordner, dann die WAVs darin - wie die Webseite.
static char gSmpDir[200] = "";
struct SmpEnt { int kind; int file; char name[96]; int count; };   // kind 0 = "..", 1 = Ordner, 2 = Datei
static SmpEnt gSmpView[MAX_FILES + 64];
static int gNSmpView = 0;
static int cmpEnt(const void* a, const void* b) {
    const SmpEnt* x = (const SmpEnt*)a; const SmpEnt* y = (const SmpEnt*)b;
    if (x->kind != y->kind) return x->kind - y->kind;
    return strcasecmp(x->name, y->name);
}
static void buildSmpView() {
    gNSmpView = 0;
    size_t dl = strlen(gSmpDir);
    if (dl) { gSmpView[gNSmpView++] = {0, -1, "..", 0}; }
    for (int i = 0; i < gNFiles; i++) {
        const char* r = gFiles[i].rel;
        if (dl && (strncmp(r, gSmpDir, dl) || r[dl] != '/')) continue;
        const char* rest = dl ? r + dl + 1 : r;
        const char* sl = strchr(rest, '/');
        if (!sl) {
            if (gNSmpView < MAX_FILES + 63) { SmpEnt& e = gSmpView[gNSmpView++]; e.kind = 2; e.file = i; e.count = 0; snprintf(e.name, sizeof e.name, "%s", rest); }
            continue;
        }
        char sub[96]; snprintf(sub, sizeof sub, "%.*s", (int)(sl - rest), rest);
        int k = -1;
        for (int j = 0; j < gNSmpView; j++) if (gSmpView[j].kind == 1 && !strcmp(gSmpView[j].name, sub)) { k = j; break; }
        if (k < 0 && gNSmpView < MAX_FILES + 63) { k = gNSmpView++; gSmpView[k].kind = 1; gSmpView[k].file = -1; gSmpView[k].count = 0; snprintf(gSmpView[k].name, sizeof gSmpView[k].name, "%s", sub); }
        if (k >= 0) gSmpView[k].count++;
    }
    int skip = dl ? 1 : 0;
    qsort(gSmpView + skip, gNSmpView - skip, sizeof gSmpView[0], cmpEnt);
    if (U.smpSel >= gNSmpView) U.smpSel = gNSmpView ? gNSmpView - 1 : 0;
}
static void smpEnter(const char* sub) {
    size_t dl = strlen(gSmpDir);
    char n[200]; snprintf(n, sizeof n, "%s%s%s", gSmpDir, dl ? "/" : "", sub);
    snprintf(gSmpDir, sizeof gSmpDir, "%s", n);
    U.smpSel = 0; U.smpScroll = 0;
    buildSmpView();
    if (gNSmpView > 1) U.smpSel = 1;   // erste Zeile unter ".."
}
static void smpLeave() {
    if (!gSmpDir[0]) return;
    char was[96]; const char* sl = strrchr(gSmpDir, '/');
    snprintf(was, sizeof was, "%s", sl ? sl + 1 : gSmpDir);
    if (sl) gSmpDir[sl - gSmpDir] = 0; else gSmpDir[0] = 0;
    buildSmpView();
    U.smpSel = 0;
    for (int i = 0; i < gNSmpView; i++) if (gSmpView[i].kind == 1 && !strcmp(gSmpView[i].name, was)) U.smpSel = i;
}
static void openSamples() {
    scanSamples();
    // Ordner gibt es nicht mehr (per Web geloescht)? Dann nach oben.
    while (gSmpDir[0]) {
        size_t dl = strlen(gSmpDir); bool any = false;
        for (int i = 0; i < gNFiles && !any; i++) any = !strncmp(gFiles[i].rel, gSmpDir, dl) && gFiles[i].rel[dl] == '/';
        if (any) break;
        char* sl = strrchr(gSmpDir, '/'); if (sl) *sl = 0; else gSmpDir[0] = 0;
    }
    buildSmpView();
    U.modal = M_SAMPLES;
    if (!gNFiles) toast("No .wav files in %ssamples/ - upload them in the browser", gBase);
}

// --- Song ----------------------------------------------------------------------------------
enum { SF_TYPE, SF_PAT, SF_REP, SF_LEN, SF_START, SF_BPM, SF_SCENE, SF_MUTE0, SF_N = SF_MUTE0 + NTRACKS };
static void songEdit(int dir, bool coarse) {
    int i = U.songRow;
    if (i >= scSongN()) return;
    int m = coarse ? 10 : 1;
    switch (U.songField) {
    case SF_TYPE:  scSongSetF(i, 2, clampi(scSongGet(i, 2) + dir, 0, 2)); break;
    case SF_PAT:   scSongSetF(i, 0, scSongGet(i, 0) + dir); break;
    case SF_REP:   scSongSetF(i, 1, scSongGet(i, 1) + dir * m); break;
    case SF_LEN:   scSongSetF(i, 3, clampi(scSongGet(i, 3) + dir * (coarse ? 16 : 1), 0, NSTEPS)); break;
    case SF_START: scSongSetF(i, 4, clampi(scSongGet(i, 4) + dir * (coarse ? 16 : 1), 0, NSTEPS - 1)); break;
    case SF_BPM: {
        int b = scSongGet(i, 5);
        if (!b) b = dir > 0 ? scTempoNow() : 0;
        else b += dir * m;
        scSongSetF(i, 5, b < 30 ? 0 : b);
        break;
    }
    case SF_SCENE: scSongSetF(i, 7, clampi(scSongGet(i, 7) + dir * (coarse ? 25 : 1), 0, 101)); break;
    default: {
        int t = U.songField - SF_MUTE0;
        scSongSetF(i, 6, scSongGet(i, 6) ^ (1 << t));
        break;
    }
    }
    touched();
}

// --- Export: Song als 24-Bit-WAV in samples/export/ ---------------------------------------
// Gerechnet wird in Happen pro Bild, damit die Anzeige den Fortschritt zeigt und B abbricht.
// Waehrenddessen gibt der Audio-Callback Stille aus und fasst den Kern nicht an.
static struct { FILE* f = nullptr; int64_t frames = 0; int total = 0; bool on = false, wasRunning = false; char rel[200]; char full[900]; } gExp;
static void exportHeader(FILE* f, uint32_t frames) {
    uint32_t data = frames * 6, v; uint16_t w;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); w = 1; fwrite(&w, 2, 1, f); w = 2; fwrite(&w, 2, 1, f);
    v = (uint32_t)gRate; fwrite(&v, 4, 1, f); v = (uint32_t)gRate * 6; fwrite(&v, 4, 1, f);
    w = 6; fwrite(&w, 2, 1, f); w = 24; fwrite(&w, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}
static bool exportBegin() {
    if (!scSongN()) { toast("Song is empty - add rows in SONG"); return false; }
    for (int k = 1; k < 1000; k++) {
        snprintf(gExp.rel, sizeof gExp.rel, "export/%s-%03d.wav", gPrjName, k);
        snprintf(gExp.full, sizeof gExp.full, "%ssamples/%s", gBase, gExp.rel);
        struct stat st; if (stat(gExp.full, &st) != 0) break;
    }
    gExp.f = fopen(gExp.full, "wb");
    if (!gExp.f) { toast("Cannot write %s", gExp.rel); return false; }
    exportHeader(gExp.f, 0);
    lockA();
    gExp.wasRunning = scIsRunning();
    gExporting = true;
    gExp.total = scExportBegin(0, scSongN() - 1, 4, 600);
    unlockA();
    gExp.frames = 0;
    gExp.on = true;
    return true;
}
static void exportFinish(bool cancel) {
    lockA();
    scExportEnd();
    gExporting = false;
    if (gExp.wasRunning) scStart();
    unlockA();
    exportHeader(gExp.f, (uint32_t)gExp.frames);
    fclose(gExp.f);
    gExp.f = nullptr;
    gExp.on = false;
    if (cancel) { remove(gExp.full); toast("Export cancelled"); return; }
    sync();
    toast("Exported %s (%.1f s)", gExp.rel, gExp.frames / (double)gRate);
}
static void exportStep(int budget) {   // budget: Frames je Aufruf
    static float buf[4096 * 2];
    static uint8_t pcm[4096 * 6];
    while (budget > 0) {
        int n = scExportRender(buf, 4096);
        if (n <= 0) { exportFinish(false); return; }
        for (int i = 0; i < n * 2; i++) {
            float v = buf[i]; v = v > 1 ? 1 : v < -1 ? -1 : v;
            int32_t x = (int32_t)lrintf(v * 8388607.0f);
            pcm[i * 3] = (uint8_t)x; pcm[i * 3 + 1] = (uint8_t)(x >> 8); pcm[i * 3 + 2] = (uint8_t)(x >> 16);
        }
        fwrite(pcm, 6, (size_t)n, gExp.f);
        gExp.frames += n;
        budget -= n;
    }
}

// --- Projekt-Ansicht --------------------------------------------------------------------------
// Projekte haben freie Namen: X/Y blaettert durch die Dateien in projects/, A oeffnet das gezeigte.
// SAVE AS und NEW fragen den Namen ueber die Bildschirmtastatur.
enum { PR_PATTERN, PR_PBPM, PR_PSWING, PR_PMULT, PR_PCHG, PR_ROOT, PR_SCALE, PR_TEMPO, PR_SWING, PR_MASTER,
       PR_MORPH, PR_OPEN, PR_SAVE, PR_SAVEAS, PR_NEW, PR_DEMO, PR_EXPORT, PR_N };
static const char* PRNAME[PR_N] = {"PATTERN", "PAT TEMPO", "PAT SWING", "PAT SPEED", "PAT CHANGE", "KEY ROOT", "KEY SCALE",
    "PROJECT TEMPO", "PROJECT SWING", "MASTER", "SCENE MORPH", "OPEN PROJECT", "SAVE", "SAVE AS...", "NEW PROJECT...",
    "LOAD DEMO", "EXPORT SONG WAV"};
static bool prDivider(int r) { return r == PR_TEMPO || r == PR_OPEN || r == PR_EXPORT; }

enum { MAX_PRJ = 256 };
static char gPrjList[MAX_PRJ][48];
static int  gNPrj = 0, gPrjBrowse = -1;   // -1 = das aktuelle Projekt
static int cmpStr(const void* a, const void* b) { return strcasecmp((const char*)a, (const char*)b); }
static void scanProjects() {
    gNPrj = 0;
    char d[700]; pathOf(d, sizeof d, "projects");
    DIR* dir = opendir(d);
    if (!dir) return;
    struct dirent* e;
    while ((e = readdir(dir)) && gNPrj < MAX_PRJ) {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] == '.' || l < 4 || l - 3 >= sizeof gPrjList[0] || strcasecmp(e->d_name + l - 3, ".sc")) continue;
        snprintf(gPrjList[gNPrj], sizeof gPrjList[0], "%.*s", (int)(l - 3), e->d_name);
        gNPrj++;
    }
    closedir(dir);
    qsort(gPrjList, gNPrj, sizeof gPrjList[0], cmpStr);
}
static const char* browseName() { return gPrjBrowse >= 0 && gPrjBrowse < gNPrj ? gPrjList[gPrjBrowse] : gPrjName; }
static void browseStep(int dir) {
    scanProjects();
    if (!gNPrj) return;
    int cur = gPrjBrowse;
    if (cur < 0) {   // vom aktuellen Projekt aus weiterblaettern
        int k = -1;
        for (int i = 0; i < gNPrj; i++) if (!strcasecmp(gPrjList[i], gPrjName)) k = i;
        if (k >= 0) cur = k + dir;
        else { int ins = 0; while (ins < gNPrj && strcasecmp(gPrjList[ins], gPrjName) < 0) ins++; cur = dir > 0 ? ins : ins - 1; }
    } else cur += dir;
    gPrjBrowse = clampi(cur, 0, gNPrj - 1);
}
// Erlaubte Zeichen fuer Namen: Buchstaben, Ziffern, Leerzeichen, - und _ (passt auf jedes Dateisystem)
static bool nameOk(const char* n) {
    if (!n[0] || n[0] == ' ' || n[0] == '.') return false;
    for (const char* c = n; *c; c++) if (!isalnum((unsigned char)*c) && !strchr(" -_", *c)) return false;
    return true;
}
enum { KM_SAVEAS = 1, KM_NEW };
static void openKeys(int mode, const char* start) {
    U.modal = M_KEYS; U.keyMode = mode; U.keyRow = 0; U.keyCol = 0;
    snprintf(U.keyBuf, sizeof U.keyBuf, "%s", start);
}
static void keysDone() {
    char n[32]; snprintf(n, sizeof n, "%s", U.keyBuf);
    int l = (int)strlen(n); while (l > 0 && n[l - 1] == ' ') n[--l] = 0;
    if (!nameOk(n)) { toast("Name: letters, digits, space, - and _"); return; }
    U.modal = M_NONE;
    if (U.keyMode == KM_SAVEAS) {
        snprintf(gPrjName, sizeof gPrjName, "%s", n);
        toast(prjSave() ? "Saved as %s" : "Could not save %s", gPrjName);
        U.dirty = false;
    } else {
        if (U.dirty) prjSave();
        if (prjExists(n)) { toast("%s exists - open it with OPEN PROJECT", n); return; }
        lockA(); scStop(); scSmpClear(); scLoad(""); unlockA();
        snprintf(gPrjName, sizeof gPrjName, "%s", n);
        prjSave();
        U.dirty = false;
        toast("New project %s", gPrjName);
    }
    gPrjBrowse = -1;
    settingsSave();
}
static void prEdit(int dir, bool coarse) {
    int p = scPattern();
    int m = coarse ? 10 : 1;
    switch (U.prjRow) {
    case PR_PATTERN: { int n = clampi((scPatNext() >= 0 ? scPatNext() : p) + dir, 0, NPATTERNS - 1);
                       if (scIsRunning()) scPatQueue(n); else scSetPattern(n); return; }
    case PR_PBPM: { int b = scPatBpm(p); b = b ? b + dir * m : (dir > 0 ? scTempoNow() : 0); scSetPatBpm(p, b < 30 ? 0 : b); break; }
    case PR_PSWING: { int s = scPatSwing(p); s = s ? s + dir : (dir > 0 ? 50 : 0); scSetPatSwing(p, s < 50 ? 0 : s); break; }
    case PR_PMULT: scSetPatMult(p, scPatMult(p) + dir); break;
    case PR_PCHG: scSetPatChg(p, scPatChg(p) + dir * (coarse ? 16 : 1)); break;
    case PR_ROOT: scSetPatKey(p, scPatRoot(p) + dir, scPatScale(p)); break;
    case PR_SCALE: scSetPatKey(p, scPatRoot(p), clampi(scPatScale(p) + dir, 0, scNScales() - 1)); break;
    case PR_TEMPO: { int s = slotOf(pIndex("bpm")); scSet(-1, s, clampi(scGet(-1, s) + dir * m, 30, 300)); break; }
    case PR_SWING: { int s = slotOf(pIndex("swing")); scSet(-1, s, clampi(scGet(-1, s) + dir, 50, 80)); break; }
    case PR_MASTER: { int s = slotOf(pIndex("master.vol")); scSet(-1, s, clampi(scGet(-1, s) + dir * (coarse ? 8 : 1), 0, 127)); break; }
    case PR_MORPH: { float v = scGetMorph() + dir * (coarse ? 0.1f : 0.02f); scSetMorph(v < 0 ? 0 : v > 1 ? 1 : v); return; }
    case PR_OPEN: browseStep(dir * (coarse ? 10 : 1)); return;
    default: return;
    }
    touched();
}
static void prAction() {
    switch (U.prjRow) {
    case PR_OPEN: {
        char n[48]; snprintf(n, sizeof n, "%s", browseName());
        if (!strcmp(n, gPrjName) && !prjExists(n)) { toast("%s is not saved yet", n); break; }
        if (U.dirty) prjSave();
        if (prjLoad(n)) { snprintf(gPrjName, sizeof gPrjName, "%s", n); toast("Opened %s", gPrjName); U.dirty = false; settingsSave(); }
        else toast("Could not open %s", n);
        gPrjBrowse = -1;
        break;
    }
    case PR_SAVE: toast(prjSave() ? "Saved %s" : "Could not save %s", gPrjName); U.dirty = false; settingsSave(); break;
    case PR_SAVEAS: openKeys(KM_SAVEAS, gPrjName); break;
    case PR_NEW: openKeys(KM_NEW, ""); break;
    case PR_DEMO:
        lockA(); scStop(); scDemo(); unlockA();
        toast("Demo loaded into %s", gPrjName); touched(); break;
    case PR_EXPORT: exportBegin(); break;
    default: break;
    }
}

// --- USB-Modus (Groovebox-OS): pc-modus.sh baut das USB-Geraet um ------------------------------
static bool isGrooveboxOS() { struct stat st; return stat("/etc/groovebox-os", &st) == 0; }
static bool haveUsbScript() { char p[700]; pathOf(p, sizeof p, "pc-modus.sh"); struct stat st; return isGrooveboxOS() && stat(p, &st) == 0; }
static int  gUsbNow = 0;                    // was gerade aktiv ist
static volatile int gUsbJob = 0;            // 0 frei, 1 laeuft, 2 fertig
static char gUsbCmd[900];
static int usbJobThread(void*) { if (system(gUsbCmd) != 0) printf("pc-modus.sh meldet einen Fehler\n"); gUsbJob = 2; return 0; }
static int detectUsbMode() {
    FILE* f = fopen("/sys/kernel/config/usb_gadget/groovebox/UDC", "r");
    if (!f) return 0;
    char b[64] = "";
    bool on = fgets(b, sizeof b, f) && b[0] > ' ';
    fclose(f);
    if (!on) return 0;
    struct stat st;
    bool aud = stat("/sys/kernel/config/usb_gadget/groovebox/configs/c.1/uac2.usb0", &st) == 0 ||
               stat("/sys/kernel/config/usb_gadget/groovebox/configs/c.1/uac1.usb0", &st) == 0;
    return aud ? 2 : 1;
}
static void applyUsbMode() {
    if (!haveUsbScript() || gUsbJob) return;
    usbAudioStop();
    pcInStop();
    static const char* args[3] = {"aus", "an", "an audio"};
    snprintf(gUsbCmd, sizeof gUsbCmd, "GB_USB_NET=ncm GB_UAC=2 GB_UAC_IN=1 GB_UAC_CH=2 bash '%spc-modus.sh' %s nosleep", gBase, args[gCfg.usbMode % 3]);
    gUsbJob = 1;
    toast(gCfg.usbMode ? "Switching to PC mode ..." : "Switching to host mode ...");
    SDL_Thread* t = SDL_CreateThread(usbJobThread, "usbmode", nullptr);
    if (t) SDL_DetachThread(t); else usbJobThread(nullptr);
}
// Adresse fuer den Browser: im PC-Modus 192.168.77.1, sonst die erste IPv4 (WLAN/LAN)
static void deviceAddr(char* out, int n) {
    out[0] = 0;
    struct ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) != 0) return;
    for (struct ifaddrs* i = ifa; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !strcmp(i->ifa_name, "lo")) continue;
        char ip[32]; inet_ntop(AF_INET, &((struct sockaddr_in*)i->ifa_addr)->sin_addr, ip, sizeof ip);
        bool usb = !strncmp(i->ifa_name, "usb", 3);
        if (!out[0] || usb) snprintf(out, n, "%s", ip);
        if (usb) break;
    }
    freeifaddrs(ifa);
}

// --- SYSTEM-Ansicht ------------------------------------------------------------------------------
enum { SY_USB, SY_ADDR, SY_USBAUD, SY_USBVOL, SY_PCIN, SY_MIDIIN, SY_CLKIN, SY_CLKOUT, SY_NOTESOUT, SY_MIDIDEV,
       SY_DISP, SY_LEARN, SY_QUIT, SY_N };
static const char* SYNAME[SY_N] = {"USB MODE", "WEB ADDRESS", "USB AUDIO", "USB VOLUME", "PC AUDIO IN", "MIDI IN",
    "CLOCK IN", "CLOCK OUT", "NOTES OUT", "MIDI DEVICES", "DISPLAY SHIFT", "LEARN BUTTONS", "QUIT"};
static bool syDivider(int r) { return r == SY_MIDIIN || r == SY_DISP; }
static int gUsbWant = -1;                   // in der Liste gewaehlt, noch nicht angewendet
static void syEdit(int dir, bool coarse) {
    switch (U.sysRow) {
    case SY_USB: if (gUsbWant < 0) gUsbWant = gCfg.usbMode; gUsbWant = clampi(gUsbWant + dir, 0, 2); return;
    case SY_USBVOL: gCfg.usbVol = clampi(gCfg.usbVol + dir * (coarse ? 10 : 5), 0, 100); usbAudioSetGain(gCfg.usbVol / 100.f); break;
    case SY_PCIN: gCfg.pcIn = dir > 0; break;
    case SY_MIDIIN: gCfg.midiIn = dir > 0; break;
    case SY_CLKIN: gCfg.clockIn = dir > 0; break;
    case SY_CLKOUT: gCfg.clockOut = dir > 0; break;
    case SY_NOTESOUT: gCfg.notesOut = dir > 0; break;
    case SY_DISP: gDispOff += dir * (coarse ? 30 : 3); drmSetByteOffset(gDispOff); break;
    default: return;
    }
    settingsSave();
}
static void syAction() {
    switch (U.sysRow) {
    case SY_USB:
        if (!haveUsbScript()) { toast("USB modes need Groovebox-OS"); break; }
        if (gUsbWant >= 0) gCfg.usbMode = gUsbWant;
        gUsbWant = -1;
        settingsSave();
        applyUsbMode();
        break;
    case SY_LEARN: gLearnReq = true; break;
    case SY_QUIT: U.quit = true; break;
    case SY_DISP: if (!drmActive()) toast("Only on the built-in display (DRM)"); break;
    default: break;
    }
}

// ============================================================================================
// Eingabe
// ============================================================================================
// X = Wert hoch, Y = Wert runter; B gehalten = grob (SHIFT). Gehalten wiederholt es, nach
// einer Sekunde in groesseren Schritten.
static int valueDir(bool* coarse) {
    *coarse = IN.down[BT_B];
    int d = 0;
    if (IN.repeat(BT_X, U.now)) d = +1;
    if (IN.repeat(BT_Y, U.now)) d = -1;
    if (d && (IN.heldMs(BT_X, U.now) > 1200 || IN.heldMs(BT_Y, U.now) > 1200)) *coarse = true;
    return d;
}
static void handleTrack() {
    bool B = IN.down[BT_B];
    // Step-Cursor / Reiter
    if (IN.repeat(BT_LEFT, U.now)) {
        if (B && U.sel != FXT) { U.tab = (U.tab + TAB_N - 1) % TAB_N; }
        else U.cur = (U.cur + trackLen() - 1) % trackLen();
    }
    if (IN.repeat(BT_RIGHT, U.now)) {
        if (B && U.sel != FXT) { U.tab = (U.tab + 1) % TAB_N; }
        else U.cur = (U.cur + 1) % trackLen();
    }
    if (IN.repeat(BT_UP, U.now))   { if (B) { int n = clampi((scPatNext() >= 0 ? scPatNext() : scPattern()) - 1, 0, NPATTERNS - 1); if (scIsRunning()) scPatQueue(n); else scSetPattern(n); } else moveRow(-1); }
    if (IN.repeat(BT_DOWN, U.now)) { if (B) { int n = clampi((scPatNext() >= 0 ? scPatNext() : scPattern()) + 1, 0, NPATTERNS - 1); if (scIsRunning()) scPatQueue(n); else scSetPattern(n); } else moveRow(+1); }
    // A: antippen = Trig an/aus, halten + Wert = Lock auf diesem Step
    if (IN.pressed(BT_A)) {
        if (B) { scMute(U.sel, !scIsMute(U.sel)); toast(scIsMute(U.sel) ? "Track muted" : "Track unmuted"); IN.mask |= 1u << BT_A; }
        else { U.aHeld = true; U.aEdited = false; }
    }
    if (U.aHeld && IN.pressed(BT_B)) { clearStepLocks(); U.aEdited = true; }
    if (U.aHeld && IN.released(BT_A)) {
        if (!U.aEdited) toggleTrig();
        U.aHeld = false;
    }
    bool coarse;
    int d = valueDir(&coarse);
    if (U.aHeld) coarse = false;   // B ist mit gehaltenem A "Locks loeschen"
    if (d) editParam(curParam(), d, coarse);
    // B + L1/R1: 16 Steps weiter
    if (B && IN.pressed(BT_L1)) { U.cur = (U.cur + trackLen() - 16 % trackLen()) % trackLen(); IN.mask |= 1u << BT_L1; }
    if (B && IN.pressed(BT_R1)) { U.cur = (U.cur + 16) % trackLen(); IN.mask |= 1u << BT_R1; }
}
static void handleSong() {
    int n = scSongN();
    if (IN.repeat(BT_UP, U.now)) { if (IN.down[BT_B] && U.songRow > 0) { scSongMove(U.songRow, -1); U.songRow--; touched(); } else if (U.songRow > 0) U.songRow--; }
    if (IN.repeat(BT_DOWN, U.now)) { if (IN.down[BT_B] && U.songRow < n - 1) { scSongMove(U.songRow, +1); U.songRow++; touched(); } else if (U.songRow < n - 1) U.songRow++; }
    if (IN.repeat(BT_LEFT, U.now) && U.songField > 0) U.songField--;
    if (IN.repeat(BT_RIGHT, U.now) && U.songField < SF_N - 1) U.songField++;
    if (IN.pressed(BT_A)) {
        if (IN.down[BT_B]) { if (n) { scSongDel(U.songRow); if (U.songRow >= scSongN() && U.songRow > 0) U.songRow--; toast("Row deleted"); } }
        else if (n) { scSongDup(U.songRow); U.songRow++; toast("Row duplicated"); }
        else { scSongAdd(scPattern(), 1); U.songRow = 0; toast("Row added"); }
        touched();
    }
    bool coarse;
    int d = valueDir(&coarse);
    if (d) songEdit(d, coarse);
}
static void handleProject() {
    if (IN.repeat(BT_UP, U.now) && U.prjRow > 0) U.prjRow--;
    if (IN.repeat(BT_DOWN, U.now) && U.prjRow < PR_N - 1) U.prjRow++;
    if (IN.pressed(BT_A)) prAction();
    bool coarse;
    int d = valueDir(&coarse);
    if (d) prEdit(d, coarse);
}
static void handleSystem() {
    if (IN.repeat(BT_UP, U.now) && U.sysRow > 0) U.sysRow--;
    if (IN.repeat(BT_DOWN, U.now) && U.sysRow < SY_N - 1) U.sysRow++;
    if (IN.pressed(BT_A)) syAction();
    bool coarse;
    int d = valueDir(&coarse);
    if (d) syEdit(d, coarse);
}
// Bildschirmtastatur: Steuerkreuz waehlt, A tippt, B loescht, Y gross/klein, START fertig, SELECT bricht ab
static const char* KEYROWS[2][4] = {
    {"ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789-_ ", "<>"},
    {"abcdefghijklm", "nopqrstuvwxyz", "0123456789-_ ", "<>"}};
static int gKeyCase = 1;
static void keyType(char c) {
    size_t l = strlen(U.keyBuf);
    if (l < 24) { U.keyBuf[l] = c; U.keyBuf[l + 1] = 0; }
}
static void handleKeys() {
    int rows = 4;
    int cols = (int)strlen(KEYROWS[gKeyCase][U.keyRow]);
    if (IN.repeat(BT_UP, U.now)) U.keyRow = (U.keyRow + rows - 1) % rows;
    if (IN.repeat(BT_DOWN, U.now)) U.keyRow = (U.keyRow + 1) % rows;
    cols = (int)strlen(KEYROWS[gKeyCase][U.keyRow]);
    if (U.keyCol >= cols) U.keyCol = cols - 1;
    if (IN.repeat(BT_LEFT, U.now)) U.keyCol = (U.keyCol + cols - 1) % cols;
    if (IN.repeat(BT_RIGHT, U.now)) U.keyCol = (U.keyCol + 1) % cols;
    if (IN.pressed(BT_Y)) gKeyCase ^= 1;
    if (IN.repeat(BT_B, U.now)) { size_t l = strlen(U.keyBuf); if (l) U.keyBuf[l - 1] = 0; }
    if (IN.repeat(BT_A, U.now)) {
        if (U.keyRow == 3) { if (U.keyCol == 0) { size_t l = strlen(U.keyBuf); if (l) U.keyBuf[l - 1] = 0; } else keysDone(); }
        else keyType(KEYROWS[gKeyCase][U.keyRow][U.keyCol]);
    }
    if (IN.pressed(BT_X)) keyType(' ');
    if (IN.pressed(BT_START)) keysDone();
    if (IN.pressed(BT_SELECT)) U.modal = M_NONE;
}
static void handleModal() {
    if (U.modal == M_KEYS) { handleKeys(); return; }
    if (U.modal == M_HELP) {
        if (IN.pressed(BT_B) || IN.pressed(BT_A) || IN.pressed(BT_SELECT)) U.modal = M_NONE;
        return;
    }
    if (U.modal == M_MENU) {
        if (IN.repeat(BT_UP, U.now)) U.menuSel = (U.menuSel + MI_N - 1) % MI_N;
        if (IN.repeat(BT_DOWN, U.now)) U.menuSel = (U.menuSel + 1) % MI_N;
        if (IN.pressed(BT_A)) menuDo(U.menuSel);
        else if (IN.pressed(BT_B) || IN.pressed(BT_SELECT)) U.modal = M_NONE;
        return;
    }
    if (U.modal == M_SAMPLES) {
        if (IN.repeat(BT_UP, U.now) && U.smpSel > 0) U.smpSel--;
        if (IN.repeat(BT_DOWN, U.now) && U.smpSel < gNSmpView - 1) U.smpSel++;
        if (IN.repeat(BT_L1, U.now)) U.smpSel = U.smpSel > 10 ? U.smpSel - 10 : 0;
        if (IN.repeat(BT_R1, U.now)) U.smpSel = U.smpSel + 10 < gNSmpView ? U.smpSel + 10 : (gNSmpView ? gNSmpView - 1 : 0);
        const SmpEnt* e = U.smpSel < gNSmpView ? &gSmpView[U.smpSel] : nullptr;
        if (IN.pressed(BT_LEFT)) { smpLeave(); return; }
        if (IN.pressed(BT_RIGHT) && e && e->kind == 1) { smpEnter(e->name); return; }
        if (IN.pressed(BT_A) && e && e->kind == 0) { smpLeave(); return; }
        if (IN.pressed(BT_A) && e && e->kind == 1) { char n[96]; snprintf(n, sizeof n, "%s", e->name); smpEnter(n); return; }
        if (IN.pressed(BT_A) && e && e->kind == 2) {
            int slot = smpTargetSlot();
            if (poolLoadFile(slot, gFiles[e->file].rel)) {
                toast("Slot %d: %s", slot + 1, scSmpName(slot));
                lockA(); scSmpPreview(slot); unlockA();
                touched();
            } else toast("Could not load (sample memory full or bad file)");
        }
        if (IN.pressed(BT_X) && scSmpFrames(smpTargetSlot())) { lockA(); scSmpPreview(smpTargetSlot()); unlockA(); }
        if (IN.pressed(BT_B) || IN.pressed(BT_SELECT)) { lockA(); scSmpPreviewStop(); unlockA(); U.modal = M_NONE; }
        return;
    }
}
static void handleInput() {
    if (gExp.on) { if (IN.pressed(BT_B)) exportFinish(true); return; }
    buildRows();
    // SELECT antippen = Menue, halten = Hilfe solange gehalten
    static bool selUsed = false;
    if (IN.pressed(BT_SELECT) && U.modal == M_NONE) selUsed = false;
    if (U.modal != M_NONE) { handleModal(); return; }
    if (IN.down[BT_SELECT] && IN.heldMs(BT_SELECT, U.now) > 500) { U.modal = M_HELP; selUsed = true; return; }
    if (IN.released(BT_SELECT) && !selUsed) { U.modal = M_MENU; return; }
    // START: Play/Stop (im Song: Song), B+START gehalten = FILL
    if (IN.pressed(BT_START)) {
        if (IN.down[BT_B]) scFill(1);
        else if (U.view == V_SONG && scSongN()) {
            if (scIsRunning()) { scStop(); scSongStop(); }
            else { if (U.songRow < scSongN()) scSongSetF(U.songRow, 0, scSongGet(U.songRow, 0)); scSongStart(); scStart(); }
        } else if (scIsRunning()) { scStop(); if (scSongOn()) scSongStop(); }
        else scStart();
    }
    if (scIsFill() && !IN.down[BT_START]) scFill(0);
    // L2/R2: Ansicht, L1/R1: Spur
    if (IN.pressed(BT_L2)) U.view = (U.view + V_N - 1) % V_N;
    if (IN.pressed(BT_R2)) U.view = (U.view + 1) % V_N;
    if (!IN.down[BT_B]) {
        if (IN.repeat(BT_L1, U.now)) { U.sel = (U.sel + NTRACKS) % (NTRACKS + 1); U.aHeld = false; }
        if (IN.repeat(BT_R1, U.now)) { U.sel = (U.sel + 1) % (NTRACKS + 1); U.aHeld = false; }
    }
    if (U.cur >= trackLen()) U.cur = trackLen() - 1;
    buildRows();
    if (U.view == V_TRACK) handleTrack();
    else if (U.view == V_SONG) handleSong();
    else if (U.view == V_PROJECT) handleProject();
    else handleSystem();
    // rechter Stick waagerecht: Scene-Crossfader (A <-> B), wie am Octatrack
    if (fabsf(IN.rx) > 0.2f) {
        float m = scGetMorph() + IN.rx * 0.03f;
        scSetMorph(m < 0 ? 0 : m > 1 ? 1 : m);
    }
}

// ============================================================================================
// Zeichnen
// ============================================================================================
enum { HEAD_H = 26, RAIL_W = 118, LIST_X = 124, STEPS_Y = 340, HELP_Y = 458 };
static void drawHeader() {
    fill(0, 0, SW, HEAD_H, C_PANEL);
    for (int x = 0; x < SW; x++) FB[(HEAD_H - 1) * SW + x] = mix(0xff4d9d, 0x5ec8ff, x * 256 / SW);
    char b[64];
    snprintf(b, sizeof b, "%s%s", gPrjName, U.dirty ? "*" : "");
    text(8, 5, b, C_TXT, 10);
    int p = scPattern(), q = scPatNext();
    if (q >= 0 && q != p) snprintf(b, sizeof b, "P%02d>%02d", p + 1, q + 1); else snprintf(b, sizeof b, "P%02d", p + 1);
    text(96, 5, b, q >= 0 && q != p ? C_LOCK : C_TXT);
    snprintf(b, sizeof b, "%d", scTempoNow());
    text(152, 5, b, gEng.extBpm > 0 ? C_LOCK : C_TXT);   // externe Clock: Tempo in Orange
    text(152 + ((int)strlen(b) + 1) * FONT_W, 5, "BPM", C_FAINT);
    if (scIsRunning()) triRight(222, 6, 13, C_PLAY); else fill(222, 7, 11, 11, C_DIM);
    if (scSongOn()) { snprintf(b, sizeof b, "S%d", scSongPos() + 1); text(238, 5, b, C_PLAY); }
    else if (scIsFill()) text(238, 5, "FILL", C_LOCK);
    // Scene-Crossfader
    float m = scGetMorph();
    fill(278, 11, 44, 3, C_LINE);
    fill(278 + (int)(m * 40), 7, 4, 11, C_SCENE);
    if (gAudio) {
        snprintf(b, sizeof b, "%d%%", (int)(gCpu * 100 + 0.5f));
        text(330, 5, "CPU", C_FAINT); text(356, 5, b, gCpu > 0.8f ? C_REC : C_DIM);
    }
    static int lastMidi = 0; static uint32_t midiAt = 0;
    if (gMidiInSeen != lastMidi) { lastMidi = gMidiInSeen; midiAt = U.now; }
    if (U.now - midiAt < 150) fill(396, 9, 6, 6, C_PLAY);   // MIDI kommt an
    // Ansichten
    int x = SW - 8;
    for (int v = V_N - 1; v >= 0; v--) {
        int w = (int)strlen(VIEWNAME[v]) * FONT_W + 12;
        x -= w;
        if (v == U.view) fill(x, 3, w, HEAD_H - 7, C_SEL);
        text(x + 6, 5, VIEWNAME[v], v == U.view ? C_TXT : C_DIM);
        x -= 2;
    }
}
static void drawRail() {
    fill(0, HEAD_H, RAIL_W, STEPS_Y - HEAD_H - 4, C_BG);
    int rh = 34;
    for (int t = 0; t <= NTRACKS; t++) {
        int y = HEAD_H + 4 + t * rh;
        bool s = t == U.sel;
        fill(2, y, RAIL_W - 4, rh - 3, s ? C_SEL : C_PANEL);
        fill(2, y, 4, rh - 3, HUE[t]);
        char b[24];
        if (t == FXT) snprintf(b, sizeof b, "FX");
        else snprintf(b, sizeof b, "%d %s", t + 1, scMachName(machOf(t)));
        bool mute = t < FXT && (scIsMute(t) || (scSongMuteNow() >> t & 1));
        text(12, y + 1, b, mute ? C_FAINT : (s ? C_TXT : C_DIM), 11);
        // laufender Step als kleiner Balken
        int len = scTrackLen(t), ps = scPlayStep(t);
        if (ps >= 0 && len > 0) {
            fill(12, y + 21, RAIL_W - 24, 3, C_LINE);
            fill(12 + ps * (RAIL_W - 24) / len, y + 20, 4, 5, HUE[t]);
            if (t < FXT && scStepGet(t, ps) == 1) fill(RAIL_W - 10, y + 19, 6, 7, HUE[t]);
        }
        if (mute) text(RAIL_W - 18, y + 1, "M", C_REC);
    }
}
static void drawList() {
    int x0 = LIST_X, w = SW - x0 - 4, y = HEAD_H + 4;
    fill(x0 - 2, HEAD_H, SW - x0 + 2, STEPS_Y - HEAD_H - 4, C_BG);
    uint32_t hue = HUE[U.sel];
    // Reiter (nicht auf der FX-Spur: dort stehen nur die Effekte)
    if (U.sel != FXT) {
        int tx = x0;
        for (int t = 0; t < TAB_N; t++) {
            int tw = 58;
            bool c = t == U.tab;
            fill(tx, y, tw - 3, 20, c ? C_SEL : C_PANEL);
            if (c) fill(tx, y + 18, tw - 3, 2, hue);
            textC(tx + (tw - 3) / 2, y + 2, TABNAME[t], c ? C_TXT : C_DIM);
            tx += tw;
        }
    } else {
        char b[64];
        snprintf(b, sizeof b, U.aHeld ? "STEP %d - LOCKS FOR THIS STEP" : "FX TRACK - HOLD A TO LOCK STEP %d", U.cur + 1);
        text(x0 + 4, y + 2, b, U.aHeld ? C_LOCK : C_DIM);
    }
    y += 26;
    int rowH = 22, vis = (STEPS_Y - 8 - y) / rowH;
    int cp = curParam();
    int sr = rowSelRef();
    if (sr < U.listScroll) U.listScroll = sr;
    if (sr >= U.listScroll + vis) U.listScroll = sr - vis + 1;
    if (U.listScroll > gNRows - vis) U.listScroll = gNRows - vis;
    if (U.listScroll < 0) U.listScroll = 0;
    for (int r = U.listScroll; r < gNRows && r < U.listScroll + vis; r++, y += rowH) {
        int p = gRows[r];
        if (p < 0) { text(x0 + 4, y + 4, hdrName(p), hue); fill(x0 + 4, y + 20, w - 8, 1, C_LINE); continue; }
        bool s = p == cp;
        fill(x0, y, w, rowH - 2, s ? C_SEL : C_PANEL);
        if (s) fill(x0, y, 3, rowH - 2, hue);
        text(x0 + 8, y + 2, rowName(p), s ? C_TXT : C_DIM, 12);
        int base = baseValue(p);
        int lv = lockValue(p, U.cur);
        bool showLock = lv != -32768;
        int sv = sceneValue(p);
        bool isScene = !(showLock && U.aHeld) && sv != -32768;
        if (isScene) { lv = sv; showLock = true; }
        const char* vt = valueText(p, base);
        text(x0 + 112, y + 2, vt, showLock && U.aHeld ? C_DIM : C_TXT, 26);
        // Balken: wo der Wert im Bereich steht
        const ParamDef& d = PARAMS[p];
        int bw = 70, bx = x0 + w - bw - 8;
        if (d.hi > d.lo && !scEnumCount(p)) {
            fill(bx, y + 9, bw, 3, C_LINE);
            fill(bx, y + 9, (base - d.lo) * bw / (d.hi - d.lo), 3, s ? hue : C_DIM);
        }
        if (showLock) {
            char lb[40]; snprintf(lb, sizeof lb, "%s", valueText(p, lv));
            int lx = x0 + 112 + 27 * FONT_W;
            uint32_t lc = isScene ? C_SCENE : C_LOCK;
            fill(lx - 4, y + 1, bx - lx, rowH - 4, mix(C_PANEL, lc, 40));
            text(lx, y + 2, lb, lc, (bx - lx - 6) / FONT_W);
        }
    }
    if (gNRows > vis) {   // Bildlaufleiste
        int top = HEAD_H + 30, h = STEPS_Y - 8 - top;
        fill(SW - 4, top, 2, h, C_LINE);
        fill(SW - 4, top + U.listScroll * h / gNRows, 2, vis * h / gNRows, hue);
    }
}
static void drawSteps() {
    int y0 = STEPS_Y;
    fill(0, y0 - 4, SW, HELP_Y - y0 + 4, C_BG);
    fill(0, y0 - 4, SW, 1, C_LINE);
    int len = trackLen(), page = U.cur / 16, pages = (len + 15) / 16;
    int ps = scPlayStep(U.sel);
    uint32_t hue = HUE[U.sel];
    int cp = U.view == V_TRACK ? curParam() : -1;
    char b[64];
    snprintf(b, sizeof b, "%s  STEP %d/%d  PAGE %d/%d", U.sel == FXT ? "FX" : "TRACK", U.cur + 1, len, page + 1, pages);
    text(8, y0, b, C_DIM);
    if (scNSpeeds()) { snprintf(b, sizeof b, "SPEED %s", scSpeedName(scSpeed(U.sel))); textR(SW - 8, y0, b, C_DIM); }
    if (cp >= 0 && lockable(cp)) { snprintf(b, sizeof b, "LOCKS: %s", rowName(cp)); textC(SW / 2 + 60, y0, b, C_LOCK); }
    int cw = 38, gx = 16, y = y0 + 22;
    for (int i = 0; i < 16; i++) {
        int s = page * 16 + i;
        int x = gx + i * cw + (i / 4) * 4;
        bool in = s < len;
        int on = in ? scStepGet(U.sel, s) : 0;
        uint32_t bg = !in ? C_BG : (i / 4) % 2 ? 0x1b1e24 : C_PANEL;
        fill(x, y, cw - 4, 54, bg);
        if (!in) continue;
        if (on == 1) fill(x + 3, y + 3, cw - 10, 20, hue);
        else if (on == 2 || scNLocks(U.sel, s) > 0) frame(x + 3, y + 3, cw - 10, 20, mix(hue, C_BG, 100));
        if (scNLocks(U.sel, s) > 0) fill(x + cw - 12, y + 5, 4, 4, C_LOCK);
        // Lock-Spur des gewaehlten Reglers
        if (cp >= 0 && lockable(cp)) {
            int lv = lockValue(cp, s);
            const ParamDef& d = PARAMS[cp];
            if (lv != -32768 && d.hi > d.lo) {
                int bw = (lv - d.lo) * (cw - 10) / (d.hi - d.lo);
                fill(x + 3, y + 26, cw - 10, 5, C_LINE);
                fill(x + 3, y + 26, bw < 1 ? 1 : bw, 5, C_LOCK);
            }
        }
        if (s == ps) frame(x, y, cw - 4, 54, C_TXT);
        if (s == U.cur) { frame(x - 2, y - 2, cw, 58, U.aHeld ? C_LOCK : hue); frame(x - 1, y - 1, cw - 2, 56, U.aHeld ? C_LOCK : hue); }
        snprintf(b, sizeof b, "%d", s + 1);
        textC(x + (cw - 4) / 2, y + 34, b, on == 1 ? C_TXT : C_FAINT);
    }
    // Pegel
    float pk = gPeak; gPeak *= 0.85f;
    int mw = (int)(fminf(pk, 1.0f) * 120);
    fill(SW - 136, y0 + 86, 120, 5, C_LINE);
    fill(SW - 136, y0 + 86, mw, 5, pk > 0.98f ? C_REC : C_PLAY);
    if (U.scene >= 0) { snprintf(b, sizeof b, "EDITING SCENE %c - values go to the scene", 'A' + U.scene); text(8, y0 + 80, b, C_SCENE); }
}
static void drawSong() {
    int x0 = LIST_X, y = HEAD_H + 4;
    fill(x0 - 2, HEAD_H, SW - x0 + 2, STEPS_Y - HEAD_H - 4, C_BG);
    static const char* FN[SF_MUTE0] = {"TYPE", "PAT", "REP", "LEN", "START", "BPM", "SCN"};
    static const int FX_[SF_MUTE0] = {32, 80, 120, 160, 204, 252, 296};
    text(x0 + 4, y + 2, "#", C_DIM);
    for (int f = 0; f < SF_MUTE0; f++) text(x0 + FX_[f], y + 2, FN[f], U.songField == f ? C_TXT : C_DIM);
    text(x0 + 340, y + 2, "MUTES", U.songField >= SF_MUTE0 ? C_TXT : C_DIM);
    y += 22;
    int n = scSongN(), rowH = 20, vis = (STEPS_Y - 10 - y) / rowH;
    static int scroll = 0;
    if (U.songRow < scroll) scroll = U.songRow;
    if (U.songRow >= scroll + vis) scroll = U.songRow - vis + 1;
    if (!n) { text(x0 + 8, y + 8, "Song is empty. Press A to add the current pattern.", C_DIM); return; }
    for (int i = scroll; i < n && i < scroll + vis; i++, y += rowH) {
        bool s = i == U.songRow, play = scSongOn() && scSongPos() == i;
        fill(x0, y, SW - x0 - 4, rowH - 2, s ? C_SEL : C_PANEL);
        if (play) fill(x0, y, 3, rowH - 2, C_PLAY);
        char b[24];
        snprintf(b, sizeof b, "%d", i + 1); text(x0 + 6, y + 1, b, play ? C_PLAY : C_DIM);
        int type = scSongGet(i, 2);
        const char* TN[3] = {"PAT", "LOOP", "HALT"};
        auto cell = [&](int f, const char* str) {
            bool c = s && U.songField == f;
            if (c) fill(x0 + FX_[f] - 3, y, (int)strlen(str) * FONT_W + 6, rowH - 2, C_LINE);
            text(x0 + FX_[f], y + 1, str, c ? C_TXT : (s ? C_TXT : C_DIM));
        };
        cell(SF_TYPE, TN[type < 3 ? type : 0]);
        if (type == 2) continue;
        if (type == 1) snprintf(b, sizeof b, ">%d", scSongGet(i, 0) + 1); else snprintf(b, sizeof b, "%02d", scSongGet(i, 0) + 1);
        cell(SF_PAT, b);
        int rep = scSongGet(i, 1);
        if (type == 1 && rep == 0) snprintf(b, sizeof b, "INF"); else snprintf(b, sizeof b, "x%d", rep);
        cell(SF_REP, b);
        if (type == 1) continue;
        int len = scSongGet(i, 3); if (len) snprintf(b, sizeof b, "%d", len); else snprintf(b, sizeof b, "AUTO");
        cell(SF_LEN, b);
        snprintf(b, sizeof b, "%d", scSongGet(i, 4) + 1); cell(SF_START, b);
        int bpm = scSongGet(i, 5); if (bpm) snprintf(b, sizeof b, "%d", bpm); else snprintf(b, sizeof b, "-");
        cell(SF_BPM, b);
        int sc = scSongGet(i, 7); if (sc) snprintf(b, sizeof b, "%d%%", sc - 1); else snprintf(b, sizeof b, "-");
        cell(SF_SCENE, b);
        int mu = scSongGet(i, 6);
        for (int t = 0; t < NTRACKS; t++) {
            int mx = x0 + 340 + t * 18;
            bool c = s && U.songField == SF_MUTE0 + t;
            fill(mx, y + 3, 14, 12, mu >> t & 1 ? C_REC : mix(C_PANEL, HUE[t], 90));
            if (c) frame(mx - 2, y + 1, 18, 16, C_TXT);
        }
    }
}
static void drawProject() {
    int x0 = LIST_X, y = HEAD_H + 4, w = SW - x0 - 4;
    fill(x0 - 2, HEAD_H, SW - x0 + 2, STEPS_Y - HEAD_H - 4, C_BG);
    int rowH = 19, vis = (STEPS_Y - 8 - y) / rowH - 1;
    static int scroll = 0;
    if (U.prjRow < scroll) scroll = U.prjRow;
    if (U.prjRow >= scroll + vis) scroll = U.prjRow - vis + 1;
    int p = scPattern();
    for (int r = scroll; r < PR_N && r < scroll + vis + 1 && y < STEPS_Y - rowH; r++, y += rowH) {
        if (prDivider(r) && r != scroll) { fill(x0 + 4, y + 1, w - 8, 1, C_LINE); y += 4; }
        bool s = r == U.prjRow;
        fill(x0, y, w, rowH - 2, s ? C_SEL : C_PANEL);
        if (s) fill(x0, y, 3, rowH - 2, C_PLAY);
        text(x0 + 8, y + 1, PRNAME[r], s ? C_TXT : C_DIM);
        char b[80] = "";
        switch (r) {
        case PR_PATTERN: if (scPatNext() >= 0 && scPatNext() != p) snprintf(b, sizeof b, "%02d > %02d (queued)", p + 1, scPatNext() + 1); else snprintf(b, sizeof b, "%02d%s", p + 1, scPatUsed(p) ? "" : " (empty)"); break;
        case PR_PBPM: if (scPatBpm(p)) snprintf(b, sizeof b, "%d", scPatBpm(p)); else snprintf(b, sizeof b, "project"); break;
        case PR_PSWING: if (scPatSwing(p)) snprintf(b, sizeof b, "%d%%", scPatSwing(p)); else snprintf(b, sizeof b, "project"); break;
        case PR_PMULT: snprintf(b, sizeof b, "%s", scSpeedName(scPatMult(p))); break;
        case PR_PCHG: snprintf(b, sizeof b, "%d steps", scPatChg(p)); break;
        case PR_ROOT: snprintf(b, sizeof b, "%s", scNoteName(scPatRoot(p))); break;
        case PR_SCALE: snprintf(b, sizeof b, "%s", scScaleName(scPatScale(p))); break;
        case PR_TEMPO: snprintf(b, sizeof b, "%d BPM", scGet(-1, slotOf(pIndex("bpm")))); break;
        case PR_SWING: snprintf(b, sizeof b, "%d%%", scGet(-1, slotOf(pIndex("swing")))); break;
        case PR_MASTER: snprintf(b, sizeof b, "%s", scParamText(pIndex("master.vol"), (float)scGet(-1, slotOf(pIndex("master.vol"))), -1)); break;
        case PR_MORPH: snprintf(b, sizeof b, "%d%%  (A %s / B %s)", (int)lrintf(scGetMorph() * 100), scSceneCount(0, U.sel < FXT ? U.sel : 0) ? "set" : "-", scSceneCount(1, U.sel < FXT ? U.sel : 0) ? "set" : "-"); break;
        case PR_OPEN:
            if (gPrjBrowse < 0) snprintf(b, sizeof b, "%s  (current%s)", gPrjName, prjExists(gPrjName) ? "" : ", not saved");
            else snprintf(b, sizeof b, "< %s >  A = open", browseName());
            break;
        case PR_SAVE: snprintf(b, sizeof b, "A = save %s%s", gPrjName, U.dirty ? "  (changed)" : ""); break;
        case PR_SAVEAS: snprintf(b, sizeof b, "A = new name"); break;
        case PR_NEW: snprintf(b, sizeof b, "A = name it, starts empty"); break;
        case PR_DEMO: snprintf(b, sizeof b, "A"); break;
        case PR_EXPORT: snprintf(b, sizeof b, "A = %d rows > samples/export/", scSongN()); break;
        }
        text(x0 + 150, y + 1, b, s ? C_TXT : C_DIM, 44);
    }
}
static void box(int x, int y, int w, int h, const char* title);
static void drawSystem() {
    int x0 = LIST_X, y = HEAD_H + 4, w = SW - x0 - 4;
    fill(x0 - 2, HEAD_H, SW - x0 + 2, STEPS_Y - HEAD_H - 4, C_BG);
    int rowH = 19;
    bool os = haveUsbScript();
    for (int r = 0; r < SY_N && y < STEPS_Y - rowH; r++, y += rowH) {
        if (syDivider(r)) { fill(x0 + 4, y + 1, w - 8, 1, C_LINE); y += 4; }
        bool s = r == U.sysRow;
        fill(x0, y, w, rowH - 2, s ? C_SEL : C_PANEL);
        if (s) fill(x0, y, 3, rowH - 2, C_PLAY);
        text(x0 + 8, y + 1, SYNAME[r], s ? C_TXT : C_DIM);
        char b[96] = "";
        uint32_t c = s ? C_TXT : C_DIM;
        static const char* UM[3] = {"HOST (devices on R36S)", "PC (MIDI + network)", "PC + AUDIO INTERFACE"};
        switch (r) {
        case SY_USB:
            if (!os) snprintf(b, sizeof b, "Groovebox-OS only");
            else if (gUsbJob) snprintf(b, sizeof b, "switching ...");
            else if (gUsbWant >= 0 && gUsbWant != gUsbNow) { snprintf(b, sizeof b, "%s  A = apply", UM[gUsbWant]); c = C_LOCK; }
            else snprintf(b, sizeof b, "%s", UM[gUsbNow]);
            break;
        case SY_ADDR: {
            char ip[40]; deviceAddr(ip, sizeof ip);
            if (!webPort()) snprintf(b, sizeof b, "web server off");
            else if (!ip[0]) snprintf(b, sizeof b, "no network (PC mode or WLAN)");
            else if (webPort() == 80) snprintf(b, sizeof b, "http://%s", ip);
            else snprintf(b, sizeof b, "http://%s:%d", ip, webPort());
            break;
        }
        case SY_USBAUD: {
            int st = usbAudioStatus();
            snprintf(b, sizeof b, "%s", !usbAudioActive() ? "off" : st == 2 ? "PC is listening" : "waiting for PC");
            if (usbAudioActive() && usbAudioRate() > 0) { size_t l = strlen(b); snprintf(b + l, sizeof b - l, " (%d Hz)", usbAudioRate()); }
            break;
        }
        case SY_USBVOL: snprintf(b, sizeof b, "%d %%", gCfg.usbVol); break;
        case SY_PCIN: snprintf(b, sizeof b, "%s", gCfg.pcIn ? "ON - PC playback is mixed in" : "OFF"); break;
        case SY_MIDIIN: snprintf(b, sizeof b, "%s", gCfg.midiIn ? "ON - ch 1-8 play tracks, PC = pattern" : "OFF"); break;
        case SY_CLKIN: snprintf(b, sizeof b, gCfg.clockIn ? (gEng.extBpm > 0 ? "FOLLOW - %.1f BPM" : "FOLLOW - no clock") : "OFF", gEng.extBpm); break;
        case SY_CLKOUT: snprintf(b, sizeof b, "%s", gCfg.clockOut ? "ON - clock + start/stop" : "OFF"); break;
        case SY_NOTESOUT: snprintf(b, sizeof b, "%s", gCfg.notesOut ? "ON - track n = channel n" : "OFF"); break;
        case SY_MIDIDEV: {
            char d[256] = ""; int n = midiPortInfo(d, sizeof d);
            for (char* q = d; *q; q++) if (*q == '\n') *q = ',';
            unsigned in = 0, out = 0; midiCounts(&in, &out);
            if (n <= 0) snprintf(b, sizeof b, "none connected%s", strcmp(midiBackend(), "AUS") ? "" : " (no ALSA MIDI)");
            else snprintf(b, sizeof b, "%s", d);
            (void)in; (void)out;
            break;
        }
        case SY_DISP: snprintf(b, sizeof b, drmActive() ? "%d px (%d)" : "only on the built-in display", gDispOff / 3, gDispOff); break;
        case SY_LEARN: snprintf(b, sizeof b, "A = press every button once"); break;
        case SY_QUIT: snprintf(b, sizeof b, "A (saves first)"); break;
        }
        text(x0 + 130, y + 1, b, c, 46);
    }
}
static void drawKeys() {
    int w = 500, h = 250, x = (SW - w) / 2, y = 90;
    box(x, y, w, h, U.keyMode == KM_NEW ? "NEW PROJECT - NAME" : "SAVE AS - NAME");
    fill(x + 16, y + 34, w - 32, 26, C_BG);
    frame(x + 16, y + 34, w - 32, 26, C_LINE);
    text(x + 24, y + 39, U.keyBuf, C_TXT);
    if ((U.now / 400) % 2 == 0) fill(x + 24 + (int)strlen(U.keyBuf) * FONT_W, y + 40, 2, 15, C_PLAY);
    for (int r = 0; r < 4; r++) {
        const char* row = KEYROWS[gKeyCase][r];
        int n = (int)strlen(row);
        for (int c = 0; c < n; c++) {
            int kw = r == 3 ? 120 : 30, kx = x + 22 + c * (r == 3 ? 140 : 35), ky = y + 74 + r * 36;
            bool sel = r == U.keyRow && c == U.keyCol;
            fill(kx, ky, kw, 30, sel ? C_PLAY : C_SEL);
            char lab[16];
            if (r == 3) snprintf(lab, sizeof lab, "%s", c == 0 ? "DELETE" : "DONE");
            else if (row[c] == ' ') snprintf(lab, sizeof lab, "_");
            else snprintf(lab, sizeof lab, "%c", row[c]);
            if (r < 3 && row[c] == ' ') { fill(kx + 8, ky + 20, kw - 16, 2, sel ? C_BG : C_DIM); continue; }
            textC(kx + kw / 2, ky + 7, lab, sel ? C_BG : C_TXT);
        }
    }
    text(x + 16, y + h - 22, "A type  B delete  X space  Y abc/ABC  START done", C_FAINT);
}
static const char* helpLine() {
    if (U.modal == M_KEYS) return "D-pad choose  A type  B delete  START done  SELECT cancel";
    if (U.modal == M_MENU) return "UP/DOWN choose   A do it   B close";
    if (U.modal == M_SAMPLES) return "A open/load  LEFT up  RIGHT into  L1/R1 page  X hear  B close";
    if (U.modal == M_HELP) return "any of A / B / SELECT closes";
    if (U.view == V_SONG) return "X/Y value  A dup row  B+A delete  B+UP/DN move  START play song";
    if (U.view == V_PROJECT) return "UP/DOWN item   X/Y value / browse projects   A action";
    if (U.view == V_SYSTEM) return "UP/DOWN item   X/Y change   A apply / action";
    if (U.aHeld) return "A held: X/Y lock value on this step   B clear locks";
    return "A trig  hold A+X/Y lock  X/Y value  B+L/R tab  B+UP/DN pattern";
}
static void drawHelpLine() {
    fill(0, HELP_Y, SW, SH - HELP_Y, C_PANEL);
    if (U.toast[0] && U.now < U.toastUntil) { text(8, HELP_Y + 3, U.toast, C_LOCK, 78); return; }
    text(8, HELP_Y + 3, helpLine(), C_DIM, 78);
}
static void box(int x, int y, int w, int h, const char* title) {
    shade(0, 0, SW, SH, 0x000000, 150);
    fill(x, y, w, h, C_PANEL);
    frame(x, y, w, h, C_LINE);
    fill(x, y, w, 24, C_SEL);
    text(x + 10, y + 4, title, C_TXT);
}
static void drawMenu() {
    int w = 340, h = 30 + MI_N * 20 + 8, x = (SW - w) / 2, y = (SH - h) / 2;
    char t[64]; snprintf(t, sizeof t, "MENU - %s", U.sel == FXT ? "FX TRACK" : "TRACK");
    box(x, y, w, h, t);
    for (int i = 0; i < MI_N; i++) {
        int ry = y + 30 + i * 20;
        if (i == U.menuSel) fill(x + 4, ry - 1, w - 8, 19, C_LINE);
        text(x + 14, ry, MENU[i], i == U.menuSel ? C_TXT : C_DIM);
    }
}
static void folderIcon(int x, int y, uint32_t c) { fill(x, y + 3, 6, 2, c); fill(x, y + 5, 13, 9, c); }
static void waveIcon(int x, int y, uint32_t c) {
    static const int hh[6] = {3, 8, 5, 10, 4, 7};
    for (int i = 0; i < 6; i++) fill(x + i * 2, y + 9 - hh[i] / 2, 1, hh[i], c);
}
static void drawSamples() {
    int w = 580, h = 392, x = (SW - w) / 2, y = 34;
    char t[96]; snprintf(t, sizeof t, "LOAD SAMPLE INTO SLOT %d", smpTargetSlot() + 1);
    box(x, y, w, h, t);
    // Pfad als Brotkrumen
    char path[240]; snprintf(path, sizeof path, "samples/%s", gSmpDir);
    fill(x + 1, y + 24, w - 2, 20, C_BG);
    int pl = (int)strlen(path), maxc = (w - 24) / FONT_W;
    text(x + 10, y + 26, pl > maxc ? path + (pl - maxc) : path, C_SCENE, maxc);
    int rowH = 20, top = y + 50, vis = (h - 82) / rowH;
    if (U.smpSel < U.smpScroll) U.smpScroll = U.smpSel;
    if (U.smpSel >= U.smpScroll + vis) U.smpScroll = U.smpSel - vis + 1;
    const char* cur = scSmpName(smpTargetSlot());
    for (int i = U.smpScroll; i < gNSmpView && i < U.smpScroll + vis; i++) {
        const SmpEnt& e = gSmpView[i];
        int ry = top + (i - U.smpScroll) * rowH;
        bool s = i == U.smpSel;
        if (s) fill(x + 4, ry - 2, w - 8, rowH - 1, C_LINE);
        if (e.kind == 0) { text(x + 14, ry, "..", s ? C_TXT : C_DIM); text(x + 40, ry, "(up)", C_FAINT); continue; }
        if (e.kind == 1) {
            folderIcon(x + 12, ry, C_LOCK);
            text(x + 32, ry, e.name, s ? C_TXT : C_DIM, 52);
            char n[16]; snprintf(n, sizeof n, "%d", e.count); textR(x + w - 14, ry, n, C_FAINT);
            continue;
        }
        char nm[SMP_NAME]; smpNameOf(gFiles[e.file].rel, nm, sizeof nm);
        bool inSlot = cur[0] && !strcmp(nm, cur);
        waveIcon(x + 12, ry, inSlot ? C_PLAY : C_PLAY & 0x7f7f7f);
        text(x + 32, ry, e.name, s ? C_TXT : (inSlot ? C_PLAY : C_DIM), 60);
    }
    if (gNSmpView > vis) {
        int bh = vis * rowH;
        fill(x + w - 5, top, 2, bh, C_LINE);
        fill(x + w - 5, top + U.smpScroll * bh / gNSmpView, 2, vis * bh / gNSmpView > 4 ? vis * bh / gNSmpView : 4, C_SCENE);
    }
    char b[96]; snprintf(b, sizeof b, "%d files  -  free sample memory %.1f s", gNFiles, scSmpFree() / 48000.0);
    text(x + 12, y + h - 22, b, C_FAINT);
}
static void drawHelp() {
    box(30, 34, SW - 60, SH - 68, "CONTROLS");
    static const char* L[] = {
        "L1 / R1        track (1-8, FX)",          "L2 / R2        view: TRACK / SONG / PROJ / SYSTEM",
        "UP / DOWN      parameter",                 "LEFT / RIGHT   step cursor",
        "X / Y          value up / down",           "B held         coarse  (+L/R tab, +UP/DN pattern)",
        "A tap          trig on / off",             "A hold + X/Y   lock value on the step (trigless)",
        "A hold + B     clear the step's locks",    "B + A          mute track",
        "B + L1 / R1    jump 16 steps",             "START          play / stop  (SONG view: song)",
        "B + START      fill while held",           "SELECT tap     menu (copy, paste, samples...)",
        "SELECT hold    this help",                 "right stick    scene crossfader A <-> B",
        "menu > Edit scene A/B   X/Y then set scene values",
    };
    int y = 66;
    for (const char* s : L) { text(48, y, s, C_TXT); y += 20; }
    char b[160]; snprintf(b, sizeof b, "files: %s  (projects/, samples/)", gBase);
    text(48, y + 8, b, C_DIM, 68);
}
static void drawAll() {
    buildRows();
    fill(0, 0, SW, SH, C_BG);
    drawHeader();
    drawRail();
    if (U.view == V_TRACK) drawList();
    else if (U.view == V_SONG) drawSong();
    else if (U.view == V_PROJECT) drawProject();
    else drawSystem();
    drawSteps();
    drawHelpLine();
    if (U.modal == M_MENU) drawMenu();
    else if (U.modal == M_SAMPLES) drawSamples();
    else if (U.modal == M_HELP) drawHelp();
    else if (U.modal == M_KEYS) drawKeys();
    if (gExp.on) {
        box(120, 180, 400, 110, "EXPORTING SONG");
        char b[96]; snprintf(b, sizeof b, "%s", gExp.rel);
        text(140, 214, b, C_TXT, 46);
        snprintf(b, sizeof b, "%.1f s rendered, song row %d of %d", gExp.frames / (double)gRate, scSongPos() + 1, scSongN());
        text(140, 236, b, C_DIM);
        int w = scSongN() ? 360 * (scSongPos() + 1) / scSongN() : 0; if (w > 360) w = 360;
        fill(140, 260, 360, 6, C_LINE); fill(140, 260, w, 6, C_PLAY);
        text(140, 270, "B = cancel", C_FAINT);
    }
}

// ============================================================================================
// Power-Taste (Groovebox-OS): kurz = Bild aus/an, lang = sichern und ausschalten
// ============================================================================================
static int gPwrFd = -2;
static uint32_t gPwrDown = 0;
static bool gScreenOff = false;
static void setBacklight(bool on) {
    DIR* d = opendir("/sys/class/backlight");
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[300]; snprintf(p, sizeof p, "/sys/class/backlight/%s/bl_power", e->d_name);
        FILE* f = fopen(p, "w");
        if (f) { fputs(on ? "0" : "4", f); fclose(f); }
    }
    closedir(d);
}
static void pollPower() {
    if (gPwrFd == -2) {
        gPwrFd = -1;
        struct stat st;
        if (stat("/etc/groovebox-os", &st) != 0) return;   // ArkOS regelt Power selbst
        for (int i = 0; i < 16 && gPwrFd < 0; i++) {
            char p[32]; snprintf(p, sizeof p, "/dev/input/event%d", i);
            int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            char nm[64] = "";
            ioctl(fd, EVIOCGNAME(sizeof nm), nm);
            if (strstr(nm, "pwrkey")) { gPwrFd = fd; printf("Power-Taste: %s\n", nm); } else close(fd);
        }
    }
    if (gPwrFd < 0) return;
    struct input_event ev;
    while (read(gPwrFd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
        if (ev.type != EV_KEY || ev.code != KEY_POWER) continue;
        if (ev.value == 1) gPwrDown = U.now ? U.now : 1;
        else if (ev.value == 0 && gPwrDown) {
            if (U.now - gPwrDown < 1500) { gScreenOff = !gScreenOff; setBacklight(!gScreenOff); }
            gPwrDown = 0;
        }
    }
    if (gPwrDown && U.now - gPwrDown >= 1500) {
        gPwrDown = 0;
        prjSave();
        settingsSave();
        sync();
        if (system("systemctl poweroff") != 0) printf("Ausschalten fehlgeschlagen\n");
        U.quit = true;
    }
}


// ============================================================================================
// Bild zeigen + Tasten lernen (wie im alten SYNTH CITY: jede Taste einmal druecken)
// ============================================================================================
static bool gUseDrm = false;
static SDL_Renderer* gRen = nullptr;
static SDL_Texture* gTex = nullptr;
static void present() {
    if (gUseDrm) drmPresent(FB, SW, SH);
    else if (gTex) {
        SDL_UpdateTexture(gTex, nullptr, FB, SW * 4);
        SDL_RenderClear(gRen);
        SDL_RenderCopy(gRen, gTex, nullptr, nullptr);
        SDL_RenderPresent(gRen);
    }
}
static bool runLearn(const char* mapPath) {
    SDL_Joystick* j = IN.joy;
    if (!j) { printf("Kein Gamepad gefunden - Lernmodus uebersprungen\n"); return false; }
    static const char* prompts[BT_COUNT + 4] = {
        "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT", "A", "B", "X", "Y", "L1", "R1", "L2", "R2",
        "SELECT", "START", "LEFT STICK UP", "RIGHT STICK UP", "LEFT STICK RIGHT", "RIGHT STICK RIGHT"};
    int nb = SDL_JoystickNumButtons(j), nh = SDL_JoystickNumHats(j), na = SDL_JoystickNumAxes(j);
    Binding res[BT_COUNT];
    int axL = -1, axR = -1, sL = -1, sR = -1, axLx = -1, axRx = -1, sLx = 1, sRx = 1;
    for (int item = 0; item < BT_COUNT + 4; item++) {
        bool stick = item >= BT_COUNT;
        Uint8 bb[64] = {0}; Uint8 bh[8] = {0}; float ba[16] = {0};
        SDL_PumpEvents();
        for (int i = 0; i < nb && i < 64; i++) bb[i] = SDL_JoystickGetButton(j, i);
        for (int i = 0; i < nh && i < 8; i++) bh[i] = SDL_JoystickGetHat(j, i);
        for (int i = 0; i < na && i < 16; i++) ba[i] = SDL_JoystickGetAxis(j, i) / 32767.f;
        Uint32 t0 = SDL_GetTicks();
        bool done = false;
        while (!done) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) return false;
            if (!stick) {
                for (int i = 0; i < nb && i < 64 && !done; i++)
                    if (SDL_JoystickGetButton(j, i) && !bb[i]) { res[item].type = 1; res[item].idx = i; done = true; }
                for (int i = 0; i < nh && i < 8 && !done; i++) {
                    int h = SDL_JoystickGetHat(j, i) & ~bh[i];
                    if (h) { res[item].type = 2; res[item].idx = i; res[item].val = h; done = true; }
                }
            }
            for (int i = 0; i < na && i < 16 && !done; i++) {
                float a = SDL_JoystickGetAxis(j, i) / 32767.f;
                if (fabsf(a - ba[i]) > 0.6f) {
                    int sg = a > ba[i] ? 1 : -1;
                    if (stick) {
                        int si = item - BT_COUNT;
                        if (si == 0) { axL = i; sL = sg; } else if (si == 1) { axR = i; sR = sg; }
                        else if (si == 2) { axLx = i; sLx = sg; } else { axRx = i; sRx = sg; }
                    } else { res[item].type = 3; res[item].idx = i; res[item].val = sg; }
                    done = true;
                }
            }
            int left = stick ? 8 - (int)((SDL_GetTicks() - t0) / 1000) : 99;
            if (stick && left <= 0) done = true;
            fill(0, 0, SW, SH, C_BG);
            textC(SW / 2, 60, "LEARN BUTTONS", C_LOCK);
            textC(SW / 2, 160, "PRESS:", C_DIM);
            textC(SW / 2, 196, prompts[item], C_TXT);
            char b[64]; snprintf(b, sizeof b, "%d / %d", item + 1, BT_COUNT + 4);
            textC(SW / 2, 260, b, C_DIM);
            if (stick) { snprintf(b, sizeof b, "No stick? Skipping in %ds", left); textC(SW / 2, 300, b, C_DIM); }
            present();
            SDL_Delay(10);
        }
        Uint32 t1 = SDL_GetTicks();
        for (;;) {   // warten, bis alles losgelassen ist
            SDL_PumpEvents();
            bool any = false;
            for (int i = 0; i < nb && i < 64; i++) if (SDL_JoystickGetButton(j, i) && !bb[i]) any = true;
            for (int i = 0; i < nh && i < 8; i++) if (SDL_JoystickGetHat(j, i) & ~bh[i]) any = true;
            for (int i = 0; i < na && i < 16; i++) if (fabsf(SDL_JoystickGetAxis(j, i) / 32767.f - ba[i]) > 0.3f) any = true;
            if (!any || SDL_GetTicks() - t1 > 5000) break;
            SDL_Delay(10);
        }
        SDL_Delay(120);
    }
    for (int b = 0; b < BT_COUNT; b++) IN.map[b] = res[b];
    IN.axLY = axL; IN.sgnLY = sL; IN.axRY = axR; IN.sgnRY = sR;
    IN.axLX = axLx; IN.sgnLX = sLx; IN.axRX = axRx; IN.sgnRX = sRx; IN.haveLX = IN.haveRX = true;
    IN.haveMap = true;
    IN.gc = nullptr;   // ab jetzt die Rohdaten des Joysticks
    bool ok = IN.saveMap(mapPath);
    printf("Tastenbelegung %s: %s\n", ok ? "gespeichert" : "NICHT gespeichert", mapPath);
    return true;
}
static char gMapPath[700];


// ============================================================================================
// Selbsttest: drueckt Tasten wie ein Mensch und prueft, was im Kern ankommt
// ============================================================================================
static int gFails = 0;
static void simFrame(unsigned mask) {
    for (int b = 0; b < BT_COUNT; b++) {
        IN.prev[b] = IN.down[b];
        IN.down[b] = (mask >> b) & 1;
        if (IN.down[b] && !IN.prev[b]) { IN.downSince[b] = U.now; IN.lastRep[b] = U.now; }
    }
    IN.mask = 0;
    U.now += 16;
    handleInput();
    drawAll();
}
static void tap(int b, unsigned hold = 0) { simFrame(hold | 1u << b); simFrame(hold); }
static void idle(int n = 1) { for (int i = 0; i < n; i++) simFrame(0); }
#define CHECK(c, msg) do { if (!(c)) { printf("FEHLER: %s (Zeile %d)\n", msg, __LINE__); gFails++; } else printf("ok: %s\n", msg); } while (0)
static int selfTest(const char* shotDir) {
    char p[800];
    auto shot = [&](const char* n) { if (!shotDir) return; snprintf(p, sizeof p, "%s/%s.bmp", shotDir, n); saveBmp(p); };
    scLoad("");
    idle(2);
    shot("01-track");
    // Trig setzen und wieder loeschen
    tap(BT_A); CHECK(scStepGet(0, 0) == 1, "A setzt Trig auf Step 1");
    tap(BT_RIGHT); tap(BT_RIGHT); tap(BT_A); CHECK(scStepGet(0, 2) == 1, "Cursor rechts + A: Trig auf Step 3");
    tap(BT_A); CHECK(scStepGet(0, 2) == 0, "nochmal A: Trig weg");
    // Wert aendern (NOTE ist die erste Zeile im TRIG-Reiter)
    int note0 = scGet(0, slotOf(P_NOTE));
    tap(BT_X); tap(BT_X); CHECK(scGet(0, slotOf(P_NOTE)) == note0 + 2, "X zweimal: NOTE +2");
    tap(BT_Y); CHECK(scGet(0, slotOf(P_NOTE)) == note0 + 1, "Y: NOTE -1");
    // Lock: A halten + X auf leerem Step -> trigless Lock, Grundwert bleibt
    tap(BT_RIGHT);   // Step 4
    simFrame(1u << BT_A); simFrame(1u << BT_A | 1u << BT_X); simFrame(1u << BT_A); simFrame(0);
    CHECK(scStepGet(0, 3) == 2, "A halten + X auf leerem Step: trigless Lock");
    CHECK(scLockGet(0, 3, slotOf(P_NOTE)) == note0 + 2, "Lock-Wert = Grundwert + 1");
    CHECK(scGet(0, slotOf(P_NOTE)) == note0 + 1, "Grundwert unveraendert");
    tap(BT_A); CHECK(scStepGet(0, 3) == 1 && scLockGet(0, 3, slotOf(P_NOTE)) == note0 + 2, "Trig auf den Lock-Step: Lock bleibt");
    shot("02-locks");
    simFrame(1u << BT_A); simFrame(1u << BT_A | 1u << BT_B); simFrame(1u << BT_A); simFrame(0);
    CHECK(scNLocks(0, 3) == 0, "A halten + B: Locks des Steps geloescht");
    // Reiter und Machine
    tap(BT_RIGHT, 1u << BT_B); tap(BT_RIGHT, 1u << BT_B);
    CHECK(U.tab == TAB_SRC, "B + RECHTS zweimal: Reiter SRC");
    buildRows();
    CHECK(curParam() == P_MACHINE, "erste Zeile in SRC ist ENGINE");
    int m0 = machOf(0);
    tap(BT_X); CHECK(machOf(0) == m0 + 1, "X: naechste Engine");
    shot("03-src");
    tap(BT_Y);
    // Spurwechsel, FX-Spur
    tap(BT_L1); CHECK(U.sel == FXT, "L1 von Spur 1: FX-Spur");
    buildRows();
    int fxp = curParam();
    CHECK(fxp >= 0 && fxLockable(fxp), "FX-Spur zeigt Effektregler");
    simFrame(1u << BT_A); simFrame(1u << BT_A | 1u << BT_X); simFrame(1u << BT_A); simFrame(0);
    CHECK(scLockGet(FXT, U.cur, slotOf(fxp)) != -32768, "FX-Lock auf dem Step");
    shot("04-fx");
    tap(BT_R1); CHECK(U.sel == 0, "R1: zurueck zu Spur 1");
    // Scene B bearbeiten: der Wert landet in der Scene, nicht im Grundwert
    tap(BT_LEFT, 1u << BT_B); tap(BT_LEFT, 1u << BT_B);   // Reiter TRIG
    U.scene = 1;
    int nb = scGet(0, slotOf(P_NOTE));
    tap(BT_X);
    CHECK(scSceneGet(1, 0, slotOf(P_NOTE)) == nb + 1 && scGet(0, slotOf(P_NOTE)) == nb, "Scene B: Wert in der Scene");
    shot("04b-scene");
    menuDo(MI_SCENE_CLR);
    CHECK(scSceneGet(1, 0, slotOf(P_NOTE)) == -32768, "Scene B geleert");
    U.scene = -1;
    // Mute
    tap(BT_A, 1u << BT_B); CHECK(scIsMute(0) == 1, "B + A: Spur stumm");
    tap(BT_A, 1u << BT_B); CHECK(scIsMute(0) == 0, "B + A: Spur wieder an");
    // Play / Stop
    tap(BT_START); CHECK(scIsRunning() == 1, "START: laeuft");
    tap(BT_START); CHECK(scIsRunning() == 0, "START: steht");
    // Song
    tap(BT_R2); CHECK(U.view == V_SONG, "R2: SONG-Ansicht");
    tap(BT_A); CHECK(scSongN() == 1, "A: Songzeile angelegt");
    tap(BT_A); CHECK(scSongN() == 2, "A: Zeile verdoppelt");
    tap(BT_RIGHT); tap(BT_RIGHT); tap(BT_X); CHECK(scSongGet(1, 1) == 2, "REP der zweiten Zeile +1");
    for (int i = 0; i < SF_MUTE0 - 2; i++) tap(BT_RIGHT);
    tap(BT_X); CHECK(scSongGet(1, 6) == 1, "Mute-Bit Spur 1 in der Songzeile");
    shot("05-song");
    tap(BT_A, 1u << BT_B); CHECK(scSongN() == 1, "B + A: Zeile geloescht");
    // Projekt
    tap(BT_R2); CHECK(U.view == V_PROJECT, "R2: PROJECT-Ansicht");
    int bpmSlot = slotOf(pIndex("bpm")), bpm0 = scGet(-1, bpmSlot);
    for (int i = 0; i < PR_TEMPO; i++) tap(BT_DOWN);
    tap(BT_X); CHECK(scGet(-1, bpmSlot) == bpm0 + 1, "PROJECT TEMPO +1");
    shot("06-project");
    // Menue: Pattern kopieren/einfuegen
    tap(BT_R2);
    simFrame(1u << BT_SELECT); simFrame(0);
    CHECK(U.modal == M_MENU, "SELECT antippen: Menue");
    shot("07-menu");
    tap(BT_B); CHECK(U.modal == M_NONE, "B: Menue zu");
    for (int i = 0; i < 40; i++) simFrame(1u << BT_SELECT);
    CHECK(U.modal == M_HELP, "SELECT halten: Hilfe");
    shot("08-help");
    simFrame(0); tap(BT_B);
    // Speichern + Laden: das Dokument kommt unveraendert zurueck
    snprintf(gPrjName, sizeof gPrjName, "selftest");
    static char before[DOC_MAX * 48];
    snprintf(before, sizeof before, "%s", scSave());
    CHECK(prjSave(), "Projekt gespeichert");
    lockA(); scLoad(""); unlockA();
    CHECK(prjLoad("selftest"), "Projekt geladen");
    CHECK(!strcmp(before, scSave()), "Dokument nach Laden gleich");
    prjPath("selftest", p, sizeof p); remove(p);
    // Demo + Rendern: es kommt Ton heraus
    scDemo(); scStart();
    static float buf[48000 * 2];
    float pk = 0;
    for (int i = 0; i < 48000; i += 256) scRender(buf + i * 2, 256);
    for (int i = 0; i < 96000; i++) pk = fmaxf(pk, fabsf(buf[i]));
    CHECK(pk > 0.01f, "Demo klingt");
    U.view = V_TRACK; U.tab = TAB_TRIG; idle(3);
    shot("09-demo");
    scStop();
    // Export: zwei Songzeilen als WAV, die Datei hat Ton und einen gueltigen Kopf
    while (scSongN()) scSongDel(0);
    scSongAdd(0, 1); scSongAdd(1, 1);
    snprintf(gPrjName, sizeof gPrjName, "selftest");
    CHECK(exportBegin(), "Export gestartet");
    simFrame(0);
    shot("10-export");
    for (int i = 0; i < 2000 && gExp.on; i++) exportStep(9600);
    CHECK(!gExp.on, "Export fertig");
    int fr = 0, ch = 0; float rate = 0;
    float* w = wavRead(gExp.full, &fr, &ch, &rate);
    float epk = 0; for (int i = 0; w && i < fr * ch; i++) epk = fmaxf(epk, fabsf(w[i]));
    printf("   Export: %d Frames, %d Kanaele, %.0f Hz, Spitze %.3f\n", fr, ch, rate, epk);
    CHECK(w && ch == 2 && fr > 48000 * 4 && epk > 0.01f, "Export-WAV lesbar, stereo, mit Ton");
    free(w);
    remove(gExp.full);
    // Sample laden: WAV schreiben, ueber die Auswahl in den Slot der Spur holen
    {
        static float tone[4800];
        for (int i = 0; i < 4800; i++) tone[i] = 0.5f * sinf(i * 0.05f);
        char dd[800]; snprintf(dd, sizeof dd, "%ssamples/kit", gBase); mkdir(dd, 0755);
        snprintf(dd, sizeof dd, "%ssamples/kit/sub", gBase); mkdir(dd, 0755);
        char sp[800]; snprintf(sp, sizeof sp, "%ssamples/kit/zz-selftest.wav", gBase);
        wavWrite(sp, tone, 4800, 48000, 1);
        char sp2[800]; snprintf(sp2, sizeof sp2, "%ssamples/kit/sub/deep.wav", gBase);
        wavWrite(sp2, tone, 100, 48000, 1);
        U.sel = 0; U.view = V_TRACK;
        gSmpDir[0] = 0;
        openSamples();
        int k = -1; for (int i = 0; i < gNSmpView; i++) if (gSmpView[i].kind == 1 && !strcmp(gSmpView[i].name, "kit")) k = i;
        CHECK(k >= 0 && gSmpView[k].count == 2, "Explorer zeigt Ordner kit mit 2 Samples");
        U.smpSel = k < 0 ? 0 : k;
        tap(BT_A);
        CHECK(!strcmp(gSmpDir, "kit") && gSmpView[0].kind == 0 && gSmpView[1].kind == 1 && gSmpView[2].kind == 2, "A: in den Ordner, .. / Ordner / Dateien");
        U.smpSel = 2;
        tap(BT_A);
        int slot = smpTargetSlot();
        CHECK(scSmpFrames(slot) == 4800 && !strcmp(scSmpName(slot), "zz-selftest"), "Sample im Slot der Spur");
        U.smpSel = 1; tap(BT_RIGHT);
        CHECK(!strcmp(gSmpDir, "kit/sub"), "RECHTS: in den Unterordner");
        tap(BT_LEFT); CHECK(!strcmp(gSmpDir, "kit") && U.smpSel == 1, "LINKS: zurueck, Cursor auf dem Ordner");
        U.smpSel = 2; idle(1);
        shot("11-samples");
        tap(BT_B);
        // Projekt mit Sample: nach dem Laden sucht der Slot seine Datei selbst
        CHECK(prjSave(), "Projekt mit Sample gespeichert");
        lockA(); scSmpClear(); scLoad(""); unlockA();
        CHECK(prjLoad("selftest") && scSmpFrames(slot) == 4800, "Sample nach Laden wieder da");
        prjPath("selftest", p, sizeof p); remove(p);
        remove(sp); remove(sp2);
        snprintf(dd, sizeof dd, "%ssamples/kit/sub", gBase); rmdir(dd);
        snprintf(dd, sizeof dd, "%ssamples/kit", gBase); rmdir(dd);
    }
    // Projektnamen: SAVE AS ueber die Bildschirmtastatur
    U.modal = M_NONE; U.view = V_PROJECT; U.prjRow = PR_SAVEAS;
    snprintf(gPrjName, sizeof gPrjName, "old");
    tap(BT_A); CHECK(U.modal == M_KEYS, "SAVE AS: Tastatur offen");
    for (int i = 0; i < 5; i++) tap(BT_B);   // "old" loeschen
    gKeyCase = 0;
    tap(BT_RIGHT); tap(BT_A);                // B
    tap(BT_A, 0); tap(BT_DOWN); tap(BT_DOWN); tap(BT_A);   // B, dann 2. Zeile: "1"? (Zeile 3 = Ziffern)
    tap(BT_X);                               // Leerzeichen
    tap(BT_Y); tap(BT_UP); tap(BT_UP); tap(BT_LEFT); tap(BT_A);   // klein: "a"
    shot("12-keys");
    char typed[32]; snprintf(typed, sizeof typed, "%s", U.keyBuf);
    printf("   getippt: '%s'\n", typed);
    CHECK(!strcmp(typed, "BB1 a"), "Tastatur tippt 'BB1 a'");
    tap(BT_START);
    CHECK(U.modal == M_NONE && !strcmp(gPrjName, "BB1 a") && prjExists("BB1 a"), "Projekt unter freiem Namen gespeichert");
    // Durchblaettern und Oeffnen
    snprintf(gPrjName, sizeof gPrjName, "zz other"); prjSave();
    U.prjRow = PR_OPEN; gPrjBrowse = -1;
    tap(BT_Y);
    CHECK(!strcmp(browseName(), "BB1 a"), "Y blaettert zum vorigen Projekt");
    shot("13-open");
    tap(BT_A); CHECK(!strcmp(gPrjName, "BB1 a"), "A oeffnet das gezeigte Projekt");
    prjPath("BB1 a", p, sizeof p); remove(p); prjPath("zz other", p, sizeof p); remove(p);
    // MIDI: Clock fuehrt das Tempo, Program Change waehlt das Pattern, Noten spielen die Spur
    gCfg.clockIn = 1; gCfg.midiIn = 1; gRate = 48000;
    for (int i = 0; i <= 48; i++) { MidiMsg m = {0xF8, 0, 0, 0, 100.0 + i * (60.0 / (100.0 * 24.0))}; midiInHandle(m); }
    printf("   Clock: %.2f BPM\n", gEng.extBpm);
    CHECK(scTempoNow() == 100, "MIDI-Clock 100 BPM: Tempo folgt");
    { MidiMsg m = {0xFA, 0, 0, 0, 0}; midiInHandle(m); } CHECK(scIsRunning(), "MIDI Start");
    { MidiMsg m = {0xFC, 0, 0, 0, 0}; midiInHandle(m); } CHECK(!scIsRunning(), "MIDI Stop");
    gCfg.clockIn = 0; midiBlock(256); CHECK(gEng.extBpm == 0, "Clock aus: eigenes Tempo");
    { MidiMsg m = {0xC0, 2, 0, 0, 0}; midiInHandle(m); } CHECK(scPattern() == 2, "Program Change 3: Pattern 03");
    scSetPattern(0);
    {
        int before = 0; for (int i = 0; i < NVOICES; i++) before += gEng.v[i].used ? 1 : 0;
        MidiMsg m = {0x92, 60, 100, 0, 0}; midiInHandle(m);
        int after = 0; for (int i = 0; i < NVOICES; i++) after += gEng.v[i].used && gEng.v[i].track == 2 ? 1 : 0;
        CHECK(after > 0, "Note auf Kanal 3 spielt Spur 3");
        (void)before;
    }
    // Noten hinaus: ein Trig erzeugt Note-On und spaeter Note-Off
    gCfg.notesOut = 1; gNOffs = 0;
    noteOutCb(nullptr, 1, 64, 100, gEng.seq.pos, 4800);
    CHECK(gNOffs == 1 && gOffs[0].st == 0x81 && gOffs[0].note == 64, "Trig als MIDI-Note, Note-Off vorgemerkt");
    flushOffs(-1); CHECK(gNOffs == 0, "Note-Offs gesendet");
    gCfg.notesOut = 0;
    // Webserver: hochladen, Liste, herunterladen, loeschen - ueber echtes HTTP
    if (webStart(gBase, 18080)) {
        auto http = [&](const char* req, const void* body, int blen, char* out, int on) -> int {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((uint16_t)webPort()); a.sin_addr.s_addr = htonl(0x7f000001);
            if (connect(fd, (sockaddr*)&a, sizeof a)) { close(fd); return -1; }
            char h[600]; int hl = snprintf(h, sizeof h, "%s HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\n\r\n", req, blen);
            if (send(fd, h, hl, 0) < 0 || (blen && send(fd, body, blen, 0) < 0)) { close(fd); return -1; }
            int got = 0, r;
            while (got < on - 1 && (r = (int)recv(fd, out + got, on - 1 - got, 0)) > 0) got += r;
            out[got] = 0; close(fd);
            int code = 0; sscanf(out, "HTTP/1.1 %d", &code);
            return code;
        };
        static char resp[200000];
        static float tone[2000]; for (int i = 0; i < 2000; i++) tone[i] = 0.3f * sinf(i * 0.1f);
        char tmpw[800]; snprintf(tmpw, sizeof tmpw, "%sweb-tmp.wav", gBase); wavWrite(tmpw, tone, 2000, 48000, 1);
        FILE* f = fopen(tmpw, "rb"); static char wb[20000]; int wn = (int)fread(wb, 1, sizeof wb, f); fclose(f); remove(tmpw);
        CHECK(http("PUT /api/upload?root=samples&path=web%2Fkick.wav", wb, wn, resp, sizeof resp) == 200, "Web: Sample hochgeladen");
        char fp[800]; snprintf(fp, sizeof fp, "%ssamples/web/kick.wav", gBase);
        struct stat st; CHECK(stat(fp, &st) == 0 && st.st_size == wn, "Web: Datei liegt in samples/web/");
        CHECK(http("GET /api/list", nullptr, 0, resp, sizeof resp) == 200 && strstr(resp, "\"web/kick.wav\""), "Web: Liste nennt das Sample");
        const char* prj = "trk.0.vol = 90\n";
        CHECK(http("PUT /api/upload?root=projects&path=from%20phone.sc", prj, (int)strlen(prj), resp, sizeof resp) == 200 && prjExists("from phone"), "Web: Projekt hochgeladen");
        CHECK(http("GET /api/file?root=projects&path=from%20phone.sc", nullptr, 0, resp, sizeof resp) == 200 && strstr(resp, "trk.0.vol = 90"), "Web: Projekt heruntergeladen");
        CHECK(http("PUT /api/upload?root=samples&path=..%2Fevil.wav", "x", 1, resp, sizeof resp) == 400, "Web: Pfad mit .. abgelehnt");
        CHECK(http("PUT /api/upload?root=projects&path=x.exe", "x", 1, resp, sizeof resp) == 400, "Web: falsche Endung abgelehnt");
        CHECK(http("GET /", nullptr, 0, resp, sizeof resp) == 200 && strstr(resp, "SYNTH CITY CORE"), "Web: Seite");
        CHECK(http("POST /api/delete?root=samples&path=web%2Fkick.wav", nullptr, 0, resp, sizeof resp) == 200 && stat(fp, &st) != 0, "Web: geloescht");
        CHECK(http("PUT /api/upload?root=samples&path=web%2Fa%2Fb%2Fc%2Fd%2Fdeep.wav", wb, wn, resp, sizeof resp) == 200, "Web: Ordner-Import mit tiefem Pfad");
        CHECK(http("POST /api/mkdir?root=samples&path=web%2Fleer", nullptr, 0, resp, sizeof resp) == 200 &&
              http("GET /api/list", nullptr, 0, resp, sizeof resp) == 200 && strstr(resp, "\"web/leer\",\"d\":1"), "Web: leerer Ordner angelegt und gelistet");
        CHECK(http("POST /api/rmdir?root=samples&path=web", nullptr, 0, resp, sizeof resp) == 200 && stat(fp, &st) != 0, "Web: Ordner samt Inhalt geloescht");
        CHECK(http("POST /api/rmdir?root=samples&path=..", nullptr, 0, resp, sizeof resp) == 400, "Web: rmdir ausserhalb abgelehnt");
        prjPath("from phone", p, sizeof p); remove(p);
        snprintf(fp, sizeof fp, "%ssamples/web", gBase); rmdir(fp);
    } else CHECK(false, "Webserver startet");
    U.view = V_SYSTEM; U.modal = M_NONE; idle(2);
    shot("14-system");
    printf("%s: %d Fehler\n", gFails ? "SELBSTTEST FEHLGESCHLAGEN" : "Selbsttest bestanden", gFails);
    return gFails ? 1 : 0;
}

// ============================================================================================
// Start
// ============================================================================================
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc >= 3 && !strcmp(argv[1], "--dhcpd")) return runDhcpd(argv[2]);   // Helfer fuer pc-modus.sh
    bool test = false, windowed = false, bench = false, learnArg = false;
    const char* shotDir = nullptr;
    int bufFrames = 256;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) test = true;
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) { test = true; shotDir = argv[++i]; }
        else if (!strcmp(argv[i], "--window")) windowed = true;
        else if (!strcmp(argv[i], "--bench")) bench = true;
        else if (!strcmp(argv[i], "--learn")) learnArg = true;
        else if (!strcmp(argv[i], "--buffer") && i + 1 < argc) bufFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--home") && i + 1 < argc) snprintf(gBase, sizeof gBase, "%s/", argv[++i]);
    }
    if (const char* h = getenv("SC_HOME")) snprintf(gBase, sizeof gBase, "%s/", h);
    else if (!strcmp(gBase, "./")) {   // neben dem Programm (R36S: /opt/groovebox bzw. Ports-Ordner)
        char exe[512]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0) { exe[n] = 0; char* s = strrchr(exe, '/'); if (s) { s[1] = 0; snprintf(gBase, sizeof gBase, "%s", exe); } }
    }
    if (const char* b = getenv("SC_AUDIO_BUF")) bufFrames = atoi(b);
    P_MACHINE = pIndex("trk.machine"); P_SMP = pIndex("trk.smp"); P_NOTE = pIndex("trk.note"); P_QUANT = pIndex("trk.quant");
    mkdirs();
    settingsLoad();
    printf("SYNTH CITY CORE - Dateien: %s\n", gBase);

    if (test) {
        scInit(48000);
        return selfTest(shotDir);
    }
    if (bench) {   // Wie schnell rechnet der Kern auf diesem Geraet? (Demo, 20 s, 256er-Bloecke)
        scInit(48000);
        scDemo(); scStart();
        static float buf[256 * 2];
        double worst = 0, sum = 0; int n = 0;
        uint32_t t0 = SDL_GetTicks();
        for (int i = 0; i < 48000 * 20 / 256; i++, n++) {
            uint64_t a = SDL_GetPerformanceCounter();
            scRender(buf, 256);
            double s = (double)(SDL_GetPerformanceCounter() - a) / SDL_GetPerformanceFrequency() * 48000.0 / 256.0;
            sum += s; if (s > worst) worst = s;
        }
        printf("Bench: 20 s Demo in %u ms -> %.1fx Echtzeit, Last im Mittel %.0f %%, schlimmster Block %.0f %%\n",
               SDL_GetTicks() - t0, 20000.0 / (SDL_GetTicks() - t0 + 1), sum / n * 100, worst * 100);
        return 0;
    }

    const char* disp = getenv("GB_DISPLAY");
    bool useDrm = gUseDrm = disp && !strcmp(disp, "drm") && !windowed && drmOpen();
    if (useDrm) setenv("SDL_VIDEODRIVER", "offscreen", 1);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_Init: %s - ohne Video weiter\n", SDL_GetError());
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
            printf("SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
    }
    drmSetByteOffset(gDispOff);
    char db[700]; pathOf(db, sizeof db, "gamecontrollerdb.txt");
    SDL_GameControllerAddMappingsFromFile(db);
    // Tastenbelegung: die eigene, sonst die des alten SYNTH CITY (gleiches Format)
    pathOf(gMapPath, sizeof gMapPath, "input.cfg");
    const char* maps[] = {gMapPath, "/roms/ports/groovebox/input.cfg", "/roms2/ports/groovebox/input.cfg"};
    for (const char* m : maps) if (IN.loadMap(m)) { printf("Tastenbelegung: %s\n", m); break; }
    IN.open();

    SDL_Window* win = nullptr; SDL_Renderer* ren = nullptr; SDL_Texture* tex = nullptr;
    if (!useDrm) {
        win = SDL_CreateWindow("SYNTH CITY", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SW, SH,
                               windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (win) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (win && !ren) ren = SDL_CreateRenderer(win, -1, 0);
        if (ren) { SDL_RenderSetLogicalSize(ren, SW, SH); tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SW, SH); }
        if (!tex) printf("Kein Fenster: %s\n", SDL_GetError());
    }
    SDL_ShowCursor(SDL_DISABLE);
    gRen = ren; gTex = tex;
    if (learnArg || (!IN.haveMap && IN.joy && !IN.gc)) runLearn(gMapPath);

    scInit(48000);
    if (!prjLoad(gPrjName)) { scDemo(); printf("neues Projekt %s (Demo)\n", gPrjName); }
    if (openAudio(bufFrames)) {
        if (gRate != 48000) scInit((float)gRate);   // vor dem Start: nichts geht verloren
        SDL_PauseAudioDevice(gAudio, 0);
    }

    gEng.noteOut = noteOutCb;
    midiStart();
    usbAudioSetEngineRate(gRate);
    usbAudioSetGain(gCfg.usbVol / 100.f);
    webStart(gBase, 80);
    gUsbNow = detectUsbMode();
    if (haveUsbScript() && gCfg.usbMode != gUsbNow) applyUsbMode();   // PC-Modus ueber Neustarts merken

    uint32_t lastSave = 0, usbAt = 0, smpChangedAt = 0, updAt = 0;
    int smpSeq = webSampleSeq(), prjSeqSeen = webProjectSeq(), evSeen = 0;
    { char b[8]; evSeen = webLastEvent(b, sizeof b); }
    while (!U.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) U.quit = true;
            if (e.type == SDL_JOYDEVICEADDED && !IN.joy) IN.open();
        }
        U.now = SDL_GetTicks();
        IN.poll();
        IN.mask = 0;
        // Notausgang wie im alten SYNTH CITY: SELECT + START 2 s
        if (IN.down[BT_SELECT] && IN.down[BT_START] && IN.heldMs(BT_START, U.now) > 2000) U.quit = true;
        lockA();
        handleInput();
        unlockA();
        if (gExp.on) exportStep(gRate / 5);
        pollPower();
        // automatisch sichern: 4 s nach der letzten Aenderung, hoechstens alle 20 s
        if (U.dirty && U.now - U.editAt > 4000 && U.now - lastSave > 20000) {
            if (prjSave()) { U.dirty = false; lastSave = U.now; settingsSave(); }
        }
        // USB: Geraet umgebaut? USB-Audio und PC-Eingang folgen dem Modus
        if (gUsbJob == 2) {
            gUsbJob = 0;
            gUsbNow = detectUsbMode();
            if (gUsbNow != gCfg.usbMode) toast("USB mode failed - see pc-modus.log");
            else toast(gUsbNow ? "PC mode on - browser: http://192.168.77.1" : "Host mode on");
        }
        if (U.now - usbAt > 1500 && !gUsbJob) {
            usbAt = U.now;
            gUsbNow = detectUsbMode();
            bool want = gUsbNow == 2;
            if (want && !usbAudioActive()) { usbAudioSetChannels(2); usbAudioStart(); }
            if (!want && usbAudioActive()) usbAudioStop();
            bool wantIn = want && gCfg.pcIn;
            if (wantIn && !pcInActive()) pcInStart();
            if (!wantIn && pcInActive()) pcInStop();
        }
        // Web: was hochgeladen wurde, kommt ohne Neustart an
        {
            char ev[160];
            int seq = webLastEvent(ev, sizeof ev);
            if (seq != evSeen) { evSeen = seq; toast("Web: %s", ev); }
            if (webSampleSeq() != smpSeq) { smpSeq = webSampleSeq(); smpChangedAt = U.now ? U.now : 1; }
            if (smpChangedAt && U.now - smpChangedAt > 1500) {   // Samples, die das Projekt vermisst, jetzt laden
                smpChangedAt = 0;
                int got = poolResolveWanted();
                if (got) toast("%d missing sample(s) loaded", got);
            }
            if (webProjectSeq() != prjSeqSeen) { prjSeqSeen = webProjectSeq(); gPrjBrowse = -1; }
            if (webUpdateReady() && !updAt) { updAt = U.now ? U.now : 1; prjSave(); settingsSave(); toast("Update received - restarting ..."); }
            if (updAt && U.now - updAt > 1500) U.quit = true;
        }
        if (gLearnReq) { gLearnReq = false; lockA(); scStop(); unlockA(); runLearn(gMapPath); IN.poll(); IN.poll(); }
        if (!gScreenOff) { drawAll(); present(); }
        if (useDrm || !ren || gScreenOff) SDL_Delay(16);   // ohne VSync: rund 60 Bilder/s
    }
    if (U.dirty) prjSave();
    settingsSave();
    if (gAudio) SDL_CloseAudioDevice(gAudio);
    flushOffs(-1);
    midiStop();
    usbAudioStop();
    drmClose();
    SDL_Quit();
    return 0;
}
