// CLAP-Plugin. Der ehrliche Pruefstein fuer das Datenmodell: Wenn hier viel Code entsteht,
// stimmt etwas am Kern nicht. Die Parameterliste wird AUS der Tabelle erzeugt, der Zustand
// IST das Dokument - beides steht nur einmal im Projekt.
#include "../src/engine.h"
#include "../src/text.h"
#include <clap/clap.h>
#include <stdio.h>
#include <string.h>

// --- Welche Parameter sieht der Host? ----------------------------------------------------
// Nicht alle 8 Spuren auf einmal: Ableton Live blendet oberhalb von 32 Parametern nicht mehr
// automatisch ein und zeigt hoechstens 128. Darum wie an der Hardware: die globalen Werte,
// die Performance-Ebene und die Regler der GEWAEHLTEN Spur.
enum PKindX { PX_GLOBAL, PX_TRACK, PX_MORPH, PX_FILL, PX_MUTE, PX_FOCUS };

struct PParam {
    clap_id     id;
    const char* name;
    const char* module;
    double      lo, hi, def;
    uint8_t     kind;     // PKindX
    int16_t     slot;     // Slot im Modell bzw. Spurnummer bei PX_MUTE
    int16_t     pidx;     // Index in PARAMS (fuer Einheit und Text)
};

static PParam gPar[128];
static int    gNPar = 0;

static void buildParams() {
    if (gNPar) return;
    for (int i = 0; i < NPARAMS; i++) {
        if (PARAMS[i].scope != PS_GLOBAL) continue;
        gPar[gNPar++] = {pidHash(PARAMS[i].id), PARAMS[i].name, "Global",
                         (double)PARAMS[i].lo, (double)PARAMS[i].hi, (double)PARAMS[i].def,
                         PX_GLOBAL, (int16_t)paramSlot(i), (int16_t)i};
    }
    gPar[gNPar++] = {pidHash("perf.morph"), "MORPH", "Performance", 0, 1, 0, PX_MORPH, 0, -1};
    gPar[gNPar++] = {pidHash("perf.fill"),  "FILL",  "Performance", 0, 1, 0, PX_FILL,  0, -1};
    static char muteName[NTRACKS][16], muteId[NTRACKS][24];
    for (int t = 0; t < NTRACKS; t++) {
        snprintf(muteName[t], sizeof muteName[t], "MUTE %d", t + 1);
        snprintf(muteId[t], sizeof muteId[t], "perf.mute.%d", t);
        gPar[gNPar++] = {pidHash(muteId[t]), muteName[t], "Performance", 0, 1, 0,
                         PX_MUTE, (int16_t)t, -1};
    }
    gPar[gNPar++] = {pidHash("ui.track"), "SPUR", "Spur", 1, NTRACKS, 1, PX_FOCUS, 0, -1};
    for (int i = 0; i < NPARAMS && gNPar < 128; i++) {
        if (PARAMS[i].scope != PS_TRACK) continue;
        gPar[gNPar++] = {pidHash(PARAMS[i].id), PARAMS[i].name, "Spur",
                         (double)PARAMS[i].lo, (double)PARAMS[i].hi, (double)PARAMS[i].def,
                         PX_TRACK, (int16_t)paramSlot(i), (int16_t)i};
    }
}
static const PParam* findPar(clap_id id) {
    for (int i = 0; i < gNPar; i++) if (gPar[i].id == id) return &gPar[i];
    return nullptr;
}

// --- Der Plugin-Zustand ------------------------------------------------------------------
struct Plug {
    clap_plugin_t      plugin;
    const clap_host_t* host = nullptr;
    Engine             eng;
    int                focus = 0;        // bearbeitete Spur
    bool               wasPlaying = false;
    bool               needRescan = false;

    double get(const PParam& p) const {
        switch (p.kind) {
        case PX_GLOBAL: return eng.P.g[p.slot];
        case PX_TRACK:  return eng.P.pat[eng.pat].t[focus].p[p.slot];
        case PX_MORPH:  return eng.morph;
        case PX_FILL:   return eng.seq.fill ? 1 : 0;
        case PX_MUTE:   return eng.mute[p.slot] ? 1 : 0;
        default:        return focus + 1;
        }
    }
    void set(const PParam& p, double v) {
        switch (p.kind) {
        case PX_GLOBAL:
            eng.P.g[p.slot] = (int16_t)(v + 0.5);
            if (!strcmp(PARAMS[p.pidx].id, "bpm")) eng.seq.bpm = v;
            break;
        case PX_TRACK:
            if (!strcmp(PARAMS[p.pidx].id, "trk.machine")) {
                machineSelect(eng.P.pat[eng.pat].t[focus].p, (int)(v + 0.5));
                needRescan = true;              // die beiden Quellregler stehen jetzt anders
            } else eng.P.pat[eng.pat].t[focus].p[p.slot] = (int16_t)(v + 0.5);
            break;
        case PX_MORPH: eng.morph = (float)v; break;
        case PX_FILL:  eng.seq.fill = v > 0.5; break;
        case PX_MUTE:  eng.mute[p.slot] = v > 0.5; break;
        default: {
            int nf = (int)(v + 0.5) - 1;
            if (nf < 0) nf = 0;
            if (nf >= NTRACKS) nf = NTRACKS - 1;
            if (nf != focus) { focus = nf; needRescan = true; }   // alle Spurwerte sind andere
            break; }
        }
    }
};
static Plug* self(const clap_plugin_t* p) { return (Plug*)p->plugin_data; }

// --- Erweiterung: Parameter ---------------------------------------------------------------
static uint32_t paCount(const clap_plugin_t*) { buildParams(); return (uint32_t)gNPar; }

static bool paInfo(const clap_plugin_t*, uint32_t i, clap_param_info_t* info) {
    buildParams();
    if (i >= (uint32_t)gNPar) return false;
    const PParam& p = gPar[i];
    memset(info, 0, sizeof *info);
    info->id = p.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (p.kind != PX_MORPH) info->flags |= CLAP_PARAM_IS_STEPPED;   // das Modell rechnet ganzzahlig
    if (p.kind == PX_FOCUS) info->flags |= CLAP_PARAM_REQUIRES_PROCESS;
    info->min_value = p.lo; info->max_value = p.hi; info->default_value = p.def;
    snprintf(info->name, sizeof info->name, "%s", p.name);
    snprintf(info->module, sizeof info->module, "%s", p.module);
    return true;
}
static bool paGet(const clap_plugin_t* pl, clap_id id, double* out) {
    buildParams();
    const PParam* p = findPar(id);
    if (!p) return false;
    *out = self(pl)->get(*p);
    return true;
}
// Die Anzeige kommt aus derselben Tabelle wie die Oberflaeche - Einheit inklusive.
static bool paText(const clap_plugin_t* pl, clap_id id, double v, char* out, uint32_t n) {
    buildParams();
    const PParam* p = findPar(id);
    if (!p) return false;
    if (p->kind == PX_MORPH) { snprintf(out, n, "%.0f %%", v * 100.0); return true; }
    if (p->kind == PX_FILL || p->kind == PX_MUTE) { snprintf(out, n, v > 0.5 ? "AN" : "AUS"); return true; }
    if (p->kind == PX_FOCUS) { snprintf(out, n, "SPUR %d", (int)(v + 0.5)); return true; }
    // Die beiden Quellregler heissen so, wie die Machine der gewaehlten Spur sie nennt.
    Plug* P = self(pl);
    int mach = P->eng.P.pat[P->eng.pat].t[P->focus].p[paramSlot(paramFind("trk.machine"))];
    paramText(PARAMS[p->pidx], (float)v, out, (int)n, mach);
    return true;
}
static bool paFromText(const clap_plugin_t*, clap_id id, const char* txt, double* out) {
    buildParams();
    const PParam* p = findPar(id);
    if (!p) return false;
    *out = atof(txt);
    if (*out < p->lo) *out = p->lo;
    if (*out > p->hi) *out = p->hi;
    return true;
}
static void handleEvent(Plug* P, const clap_event_header_t* h);
static void paFlush(const clap_plugin_t* pl, const clap_input_events_t* in, const clap_output_events_t*) {
    if (!in) return;
    uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; i++) handleEvent(self(pl), in->get(in, i));
}
static const clap_plugin_params_t kParams = {paCount, paInfo, paGet, paText, paFromText, paFlush};

// --- Erweiterung: Zustand ist das Dokument -------------------------------------------------
static bool stSave(const clap_plugin_t* pl, const clap_ostream_t* os) {
    Plug* P = self(pl);
    static Doc d;                       // gross - nie auf dem Stack
    d.clear();
    projectToDoc(P->eng.P, d);
    d.set("ui.track", P->focus);
    char buf[DOC_KEY + 32];
    for (int i = 0; i < d.n; i++) {
        int len = snprintf(buf, sizeof buf, "%s = %d\n", d.e[i].k, d.e[i].v);
        const char* at = buf;
        while (len > 0) {
            int64_t w = os->write(os, at, (uint64_t)len);
            if (w <= 0) return false;
            at += w; len -= (int)w;
        }
    }
    return true;
}
static bool stLoad(const clap_plugin_t* pl, const clap_istream_t* is) {
    Plug* P = self(pl);
    static char text[DOC_MAX * 48];
    size_t got = 0;
    while (got + 1 < sizeof text) {
        int64_t r = is->read(is, text + got, sizeof text - got - 1);
        if (r <= 0) break;
        got += (size_t)r;
    }
    text[got] = 0;
    static Doc d; d.clear();
    char* line = text;
    while (line && *line) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char* eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            char* end = eq - 1;
            while (end > line && (*end == ' ' || *end == '\t')) *end-- = 0;
            d.set(line, (int32_t)strtol(eq + 1, nullptr, 10));
        }
        line = nl ? nl + 1 : nullptr;
    }
    projectFromDoc(d, P->eng.P);
    P->focus = d.get("ui.track", 0);
    P->eng.seq.bpm = P->eng.P.g[paramSlot(paramFind("bpm"))];
    P->eng.resetTrackValues();
    if (P->host) {
        const clap_host_params_t* hp = (const clap_host_params_t*)P->host->get_extension(P->host, CLAP_EXT_PARAMS);
        if (hp) hp->rescan(P->host, CLAP_PARAM_RESCAN_VALUES);
    }
    return true;
}
static const clap_plugin_state_t kState = {stSave, stLoad};

// --- Erweiterungen: Anschluesse -------------------------------------------------------------
static uint32_t apCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }
static bool apGet(const clap_plugin_t*, uint32_t i, bool isInput, clap_audio_port_info_t* info) {
    if (isInput || i > 0) return false;
    memset(info, 0, sizeof *info);
    info->id = 0;
    snprintf(info->name, sizeof info->name, "Summe");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
static const clap_plugin_audio_ports_t kAudioPorts = {apCount, apGet};

static uint32_t npCount(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }
static bool npGet(const clap_plugin_t*, uint32_t i, bool isInput, clap_note_port_info_t* info) {
    if (!isInput || i > 0) return false;
    memset(info, 0, sizeof *info);
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    snprintf(info->name, sizeof info->name, "Noten");
    return true;
}
static const clap_plugin_note_ports_t kNotePorts = {npCount, npGet};

// --- Ereignisse ------------------------------------------------------------------------------
static void playNote(Plug* P, int note, double vel) {
    Ev e{P->eng.seq.pos, (uint8_t)P->focus, EV_TRIG, 0, (int16_t)note,
         (int16_t)(vel * 127.0), (int32_t)(P->eng.sr * 0.25f)};
    P->eng.fire(e);               // von Hand gespielt: dieselbe Kette wie ein Trig
}
static void handleEvent(Plug* P, const clap_event_header_t* h) {
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    switch (h->type) {
    case CLAP_EVENT_PARAM_VALUE: {
        const clap_event_param_value_t* e = (const clap_event_param_value_t*)h;
        const PParam* p = findPar(e->param_id);
        if (p) P->set(*p, e->value);
        break; }
    case CLAP_EVENT_NOTE_ON: {
        const clap_event_note_t* e = (const clap_event_note_t*)h;
        playNote(P, e->key, e->velocity);
        break; }
    case CLAP_EVENT_MIDI: {
        const clap_event_midi_t* e = (const clap_event_midi_t*)h;
        if ((e->data[0] & 0xf0) == 0x90 && e->data[2]) playNote(P, e->data[1], e->data[2] / 127.0);
        break; }
    default: break;
    }
}

// --- Das Plugin selbst --------------------------------------------------------------------
static bool plInit(const clap_plugin_t*) { buildParams(); return true; }
static void plDestroy(const clap_plugin_t* pl) { delete self(pl); }
static bool plActivate(const clap_plugin_t* pl, double sr, uint32_t, uint32_t) {
    self(pl)->eng.prepare((float)sr);
    return true;
}
static void plDeactivate(const clap_plugin_t*) {}
static bool plStartProcessing(const clap_plugin_t*) { return true; }
static void plStopProcessing(const clap_plugin_t*) {}
static void plReset(const clap_plugin_t* pl) { self(pl)->eng.prepare(self(pl)->eng.sr); }
static void plMainThread(const clap_plugin_t* pl) {
    Plug* P = self(pl);
    if (!P->needRescan || !P->host) return;
    P->needRescan = false;
    const clap_host_params_t* hp = (const clap_host_params_t*)P->host->get_extension(P->host, CLAP_EXT_PARAMS);
    if (hp) hp->rescan(P->host, CLAP_PARAM_RESCAN_VALUES);
}

static clap_process_status plProcess(const clap_plugin_t* pl, const clap_process_t* pr) {
    Plug* P = self(pl);
    if (!pr->audio_outputs_count || pr->audio_outputs[0].channel_count < 2) return CLAP_PROCESS_ERROR;

    // Transport: der Host gibt Tempo und Lauf vor, solange er einen hat.
    if (pr->transport) {
        if (pr->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) P->eng.seq.bpm = pr->transport->tempo;
        bool playing = (pr->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        if (playing && !P->wasPlaying) P->eng.seq.start();
        if (!playing && P->wasPlaying) P->eng.stop();
        P->wasPlaying = playing;
    }

    const clap_input_events_t* in = pr->in_events;
    uint32_t nev = in ? in->size(in) : 0, ei = 0;
    uint32_t frames = pr->frames_count, done = 0;
    float buf[4096];
    float* L = pr->audio_outputs[0].data32[0];
    float* R = pr->audio_outputs[0].data32[1];

    while (done < frames) {                      // zwischen den Host-Ereignissen rendern
        while (ei < nev) {
            const clap_event_header_t* h = in->get(in, ei);
            if (h->time > done) break;
            handleEvent(P, h);
            ei++;
        }
        uint32_t next = frames;
        if (ei < nev) { const clap_event_header_t* h = in->get(in, ei); if (h->time < next) next = h->time; }
        uint32_t c = next - done;
        if (c > 2048) c = 2048;
        if (c == 0) c = 1;
        P->eng.render(buf, (int)c);
        for (uint32_t i = 0; i < c; i++) { L[done + i] = buf[i * 2]; R[done + i] = buf[i * 2 + 1]; }
        done += c;
    }
    if (P->needRescan && P->host) P->host->request_callback(P->host);
    return CLAP_PROCESS_CONTINUE;
}

static const void* plExtension(const clap_plugin_t*, const char* id) {
    if (!strcmp(id, CLAP_EXT_PARAMS))      return &kParams;
    if (!strcmp(id, CLAP_EXT_STATE))       return &kState;
    if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &kAudioPorts;
    if (!strcmp(id, CLAP_EXT_NOTE_PORTS))  return &kNotePorts;
    return nullptr;
}

static const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                        CLAP_PLUGIN_FEATURE_DRUM_MACHINE,
                                        CLAP_PLUGIN_FEATURE_STEREO, nullptr};
static const clap_plugin_descriptor_t kDesc = {
    CLAP_VERSION_INIT,
    "de.synthcity.core",
    "SYNTH CITY Core",
    "SYNTH CITY",
    "https://github.com/molgino/synthcity-core",
    "", "", "0.3.0",
    "Groovebox-Kern: Sequencer mit P-Locks, Conditions, Slides und Scenes.",
    kFeatures};

static const clap_plugin_t* createPlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (strcmp(id, kDesc.id)) return nullptr;
    buildParams();
    Plug* P = new Plug();
    P->host = host;
    P->plugin.desc = &kDesc;
    P->plugin.plugin_data = P;
    P->plugin.init = plInit;
    P->plugin.destroy = plDestroy;
    P->plugin.activate = plActivate;
    P->plugin.deactivate = plDeactivate;
    P->plugin.start_processing = plStartProcessing;
    P->plugin.stop_processing = plStopProcessing;
    P->plugin.reset = plReset;
    P->plugin.process = plProcess;
    P->plugin.get_extension = plExtension;
    P->plugin.on_main_thread = plMainThread;
    return &P->plugin;
}
static uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }
static const clap_plugin_descriptor_t* factoryDesc(const clap_plugin_factory_t*, uint32_t i) {
    return i ? nullptr : &kDesc;
}
static const clap_plugin_factory_t kFactory = {factoryCount, factoryDesc, createPlugin};

static bool entryInit(const char*) { buildParams(); return true; }
static void entryDeinit(void) {}
static const void* entryFactory(const char* id) {
    return strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &kFactory;
}
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryFactory};
