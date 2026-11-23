// Prueft das CLAP-Plugin so, wie ein Host es benutzt: Bibliothek laden, Einstieg suchen,
// erzeugen, Parameter setzen, Zustand sichern und wieder laden, rendern.
// Hier faellt auf, ob das Datenmodell trug - das Plugin darf kaum eigenen Code brauchen.
#include "../src/model.h"
#include <clap/clap.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>

static int fails = 0, checks = 0;
static void chk(bool ok, const char* what) {
    checks++;
    if (!ok) { fails++; printf("  FEHLER: %s\n", what); }
}
static void grp(const char* n) { printf("- %s\n", n); }

// --- ein winziger Host ----------------------------------------------------------------------
static bool gCallbackAngefordert = false;
static const void* hostExt(const clap_host_t*, const char*) { return nullptr; }
static void hostNix(const clap_host_t*) {}
static void hostCallback(const clap_host_t*) { gCallbackAngefordert = true; }
static clap_host_t gHost = {CLAP_VERSION_INIT, nullptr, "Testhost", "SYNTH CITY", "", "1.0",
                            hostExt, hostNix, hostNix, hostCallback};

struct EvList {
    std::vector<clap_event_param_value_t> par;
    std::vector<clap_event_note_t> note;
    std::vector<const clap_event_header_t*> all;
    void param(clap_id id, double v, uint32_t time = 0) {
        clap_event_param_value_t e{};
        e.header = {sizeof e, time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = id; e.value = v;
        e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
        par.push_back(e);
    }
    void noteOn(int key, double vel, uint32_t time = 0) {
        clap_event_note_t e{};
        e.header = {sizeof e, time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0};
        e.note_id = -1; e.port_index = 0; e.channel = 0; e.key = (int16_t)key; e.velocity = vel;
        note.push_back(e);
    }
    void build() {
        all.clear();
        for (auto& e : par)  all.push_back(&e.header);
        for (auto& e : note) all.push_back(&e.header);
    }
};
static uint32_t evSize(const clap_input_events_t* l) { return (uint32_t)((EvList*)l->ctx)->all.size(); }
static const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) {
    return ((EvList*)l->ctx)->all[i];
}
static bool outPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

// --- Zustand als Speicherstrom ---------------------------------------------------------------
struct Mem { std::string s; size_t at = 0; };
static int64_t memWrite(const clap_ostream_t* os, const void* d, uint64_t n) {
    ((Mem*)os->ctx)->s.append((const char*)d, n);
    return (int64_t)n;
}
static int64_t memRead(const clap_istream_t* is, void* d, uint64_t n) {
    Mem* m = (Mem*)is->ctx;
    uint64_t left = m->s.size() - m->at;
    if (n > left) n = left;
    memcpy(d, m->s.data() + m->at, n);
    m->at += n;
    return (int64_t)n;
}

// --- Hilfen -----------------------------------------------------------------------------------
static float gL[4096], gR[4096];
static double renderRms(const clap_plugin_t* p, int frames, EvList& ev, bool playing, double bpm = 120.0) {
    ev.build();
    clap_input_events_t in{&ev, evSize, evGet};
    clap_output_events_t out{nullptr, outPush};
    float* ch[2] = {gL, gR};
    clap_audio_buffer_t ab{};
    ab.data32 = ch; ab.channel_count = 2;
    clap_event_transport_t tr{};
    tr.header = {sizeof tr, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
    tr.flags = CLAP_TRANSPORT_HAS_TEMPO | (playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
    tr.tempo = bpm;
    clap_process_t pr{};
    pr.frames_count = (uint32_t)frames;
    pr.audio_outputs = &ab; pr.audio_outputs_count = 1;
    pr.in_events = &in; pr.out_events = &out;
    pr.transport = &tr;
    p->process(p, &pr);
    double s = 0;
    for (int i = 0; i < frames; i++) s += (double)gL[i] * gL[i];
    return sqrt(s / frames);
}
static clap_id idOf(const clap_plugin_params_t* pa, const clap_plugin_t* p, const char* name) {
    clap_param_info_t info;
    for (uint32_t i = 0; i < pa->count(p); i++)
        if (pa->get_info(p, i, &info) && !strcmp(info.name, name)) return info.id;
    return CLAP_INVALID_ID;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "build/synthcity.clap";
    printf("CLAP-Plugin-Tests (%s)\n", path);

    grp("Bibliothek und Einstieg");
    void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { printf("  FEHLER: %s\n", dlerror()); return 1; }
    const clap_plugin_entry_t* entry = (const clap_plugin_entry_t*)dlsym(lib, "clap_entry");
    chk(entry != nullptr, "Symbol clap_entry fehlt");
    if (!entry) return 1;
    chk(clap_version_is_compatible(entry->clap_version), "CLAP-Version nicht vertraeglich");
    chk(entry->init(path), "entry.init fehlgeschlagen");
    const clap_plugin_factory_t* fac =
        (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    chk(fac && fac->get_plugin_count(fac) == 1, "Fabrik liefert nicht genau ein Plugin");
    const clap_plugin_descriptor_t* desc = fac->get_plugin_descriptor(fac, 0);
    chk(desc && desc->id && desc->name && desc->features, "Beschreibung unvollstaendig");

    const clap_plugin_t* p = fac->create_plugin(fac, &gHost, desc->id);
    chk(p != nullptr, "Plugin nicht erzeugt");
    if (!p) return 1;
    chk(p->init(p), "init fehlgeschlagen");
    chk(p->activate(p, 48000.0, 32, 4096), "activate fehlgeschlagen");
    p->start_processing(p);

    const clap_plugin_params_t* pa =
        (const clap_plugin_params_t*)p->get_extension(p, CLAP_EXT_PARAMS);
    const clap_plugin_state_t* st =
        (const clap_plugin_state_t*)p->get_extension(p, CLAP_EXT_STATE);
    const clap_plugin_audio_ports_t* ap =
        (const clap_plugin_audio_ports_t*)p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
    const clap_plugin_note_ports_t* np =
        (const clap_plugin_note_ports_t*)p->get_extension(p, CLAP_EXT_NOTE_PORTS);
    chk(pa && st && ap && np, "eine Erweiterung fehlt");
    if (!pa || !st || !ap || !np) return 1;

    grp("Anschluesse");
    {
        clap_audio_port_info_t info;
        chk(ap->count(p, false) == 1 && ap->count(p, true) == 0, "falsche Zahl Audio-Anschluesse");
        chk(ap->get(p, 0, false, &info) && info.channel_count == 2, "Ausgang ist nicht stereo");
        clap_note_port_info_t ni;
        chk(np->count(p, true) == 1, "kein Noteneingang");
        chk(np->get(p, 0, true, &ni) && (ni.supported_dialects & CLAP_NOTE_DIALECT_MIDI),
            "MIDI wird nicht angenommen");
    }

    grp("Parameterliste kommt aus der Tabelle");
    {
        uint32_t n = pa->count(p);
        chk(n == NPARAMS + NTRACKS + 3u, "parameter count does not match the declared controls");
        std::vector<clap_id> seen;
        bool bereichOk = true, textOk = true;
        clap_param_info_t info;
        for (uint32_t i = 0; i < n; i++) {
            chk(pa->get_info(p, i, &info), "get_info fehlgeschlagen");
            for (clap_id s : seen) if (s == info.id) { chk(false, "doppelte Parameter-ID"); break; }
            seen.push_back(info.id);
            if (!(info.min_value <= info.default_value && info.default_value <= info.max_value))
                bereichOk = false;
            double v = 0;
            if (!pa->get_value(p, info.id, &v)) textOk = false;
            char buf[64];
            if (!pa->value_to_text(p, info.id, v, buf, sizeof buf) || !buf[0]) textOk = false;
        }
        chk(bereichOk, "ein Standardwert liegt ausserhalb seines Bereichs");
        chk(textOk, "ein Parameter liefert keinen Anzeigetext");

        // Die Anzeige kommt aus derselben Tabelle wie am Geraet - inklusive Einheit.
        char buf[64];
        clap_id vol = idOf(pa, p, "VOL");
        chk(vol != CLAP_INVALID_ID && pa->value_to_text(p, vol, 100, buf, sizeof buf)
            && strstr(buf, "dB"), "VOL wird nicht in dB angezeigt");
        clap_id freq = idOf(pa, p, "FREQ");
        chk(freq != CLAP_INVALID_ID && pa->value_to_text(p, freq, 127, buf, sizeof buf)
            && strstr(buf, "Hz"), "FREQ wird nicht in Hz angezeigt");
        clap_id note = idOf(pa, p, "NOTE");
        chk(note != CLAP_INVALID_ID && pa->value_to_text(p, note, 69, buf, sizeof buf)
            && !strcmp(buf, "A4"), "Note 69 heisst nicht A4");
    }

    grp("Zustand IST das Dokument");
    {
        // Ein Pattern gibt es nur ueber den Zustand - genau wie beim Laden eines Projekts.
        Mem m;
        m.s = "bpm = 150\n"
              "pat.0.used = 1\n"
              "pat.0.trk.0.note = 45\n"
              "pat.0.trk.0.flt.freq = 100\n"
              "pat.0.trk.0.st.0 = 1\n"
              "pat.0.trk.0.st.4 = 1\n"
              "pat.0.trk.0.st.8 = 1\n"
              "pat.0.trk.0.st.12 = 1\n"
              "ui.track = 0\n";
        clap_istream_t is{&m, memRead};
        chk(st->load(p, &is), "Zustand laden fehlgeschlagen");
        double v = 0;
        chk(pa->get_value(p, idOf(pa, p, "TEMPO"), &v) && (int)(v + 0.5) == 150,
            "Tempo aus dem Zustand nicht uebernommen");
        chk(pa->get_value(p, idOf(pa, p, "NOTE"), &v) && (int)(v + 0.5) == 45,
            "Spurwert aus dem Zustand nicht uebernommen");

        EvList ev;
        double rmsAus = renderRms(p, 2048, ev, false);
        chk(rmsAus == 0.0, "ohne laufenden Transport kommt Klang");
        double rmsAn = 0;
        for (int i = 0; i < 20; i++) rmsAn += renderRms(p, 2048, ev, true, 150.0);
        chk(rmsAn > 0.01, "mit laufendem Transport kommt kein Klang");

        clap_id threshold = CLAP_INVALID_ID;
        for (uint32_t i = 0; i < pa->count(p); i++) {
            clap_param_info_t ins;
            if (pa->get_info(p, i, &ins) && !strcmp(ins.module, "Spur/Insert 2")
                && !strcmp(ins.name, "THRESHOLD")) threshold = ins.id;
        }
        chk(threshold != CLAP_INVALID_ID, "Insert 2 threshold missing");
        EvList edit;
        edit.param(threshold, -31);
        renderRms(p, 256, edit, false);
        chk(pa->get_value(p, threshold, &v) && v == -31, "signed insert automation rounded incorrectly");

        // Sichern und in eine ZWEITE Instanz laden: dieselben Werte
        Mem out;
        clap_ostream_t os{&out, memWrite};
        chk(st->save(p, &os), "Zustand sichern fehlgeschlagen");
        chk(out.s.find("pat.0.trk.0.note = 45") != std::string::npos,
            "der gesicherte Zustand ist nicht das lesbare Dokument");
        chk(out.s.find("pat.0.trk.0.ins2.threshold = -31") != std::string::npos,
            "insert automation is missing from project state");

        const clap_plugin_t* p2 = fac->create_plugin(fac, &gHost, desc->id);
        p2->init(p2); p2->activate(p2, 48000.0, 32, 4096);
        const clap_plugin_params_t* pa2 =
            (const clap_plugin_params_t*)p2->get_extension(p2, CLAP_EXT_PARAMS);
        const clap_plugin_state_t* st2 =
            (const clap_plugin_state_t*)p2->get_extension(p2, CLAP_EXT_STATE);
        Mem back; back.s = out.s;
        clap_istream_t is2{&back, memRead};
        chk(st2->load(p2, &is2), "zweite Instanz laedt nicht");
        bool gleich = true;
        clap_param_info_t info;
        for (uint32_t i = 0; i < pa->count(p); i++) {
            pa->get_info(p, i, &info);
            double a = 0, b = 0;
            pa->get_value(p, info.id, &a);
            pa2->get_value(p2, info.id, &b);
            if (a != b) gleich = false;
        }
        chk(gleich, "nach dem Rundlauf stehen die Parameter anders");
        p2->destroy(p2);
    }

    grp("Automation und Spurwahl");
    {
        EvList ev;
        ev.param(idOf(pa, p, "VOL"), 60);
        renderRms(p, 256, ev, false);
        double v = 0;
        chk(pa->get_value(p, idOf(pa, p, "VOL"), &v) && (int)(v + 0.5) == 60,
            "Parameterereignis nicht uebernommen");

        // Spur wechseln: die Regler zeigen jetzt die andere Spur, und der Host wird gebeten,
        // seine Werte neu zu lesen.
        gCallbackAngefordert = false;
        EvList ev2;
        ev2.param(idOf(pa, p, "SPUR"), 3);
        renderRms(p, 256, ev2, false);
        chk(gCallbackAngefordert, "Host wurde nicht um ein Neulesen gebeten");
        p->on_main_thread(p);
        chk(pa->get_value(p, idOf(pa, p, "NOTE"), &v) && (int)(v + 0.5) == 60,
            "nach dem Spurwechsel steht noch der Wert der alten Spur");

        EvList ev3;
        ev3.param(idOf(pa, p, "NOTE"), 72);
        renderRms(p, 256, ev3, false);
        Mem out; clap_ostream_t os{&out, memWrite};
        st->save(p, &os);
        chk(out.s.find("pat.0.trk.2.note = 72") != std::string::npos,
            "der Wert landete nicht auf der gewaehlten Spur");

        // Auch die gewaehlte Spur gehoert zum Zustand - sonst steht das Projekt beim
        // naechsten Oeffnen auf einer anderen Spur als beim Speichern.
        const clap_plugin_t* p3 = fac->create_plugin(fac, &gHost, desc->id);
        p3->init(p3); p3->activate(p3, 48000.0, 32, 4096);
        const clap_plugin_params_t* pa3 =
            (const clap_plugin_params_t*)p3->get_extension(p3, CLAP_EXT_PARAMS);
        const clap_plugin_state_t* st3 =
            (const clap_plugin_state_t*)p3->get_extension(p3, CLAP_EXT_STATE);
        Mem back; back.s = out.s;
        clap_istream_t is3{&back, memRead};
        st3->load(p3, &is3);
        double f = 0;
        chk(pa3->get_value(p3, idOf(pa3, p3, "SPUR"), &f) && (int)(f + 0.5) == 3,
            "die gewaehlte Spur ueberlebt den Rundlauf nicht");
        double nv = 0;
        chk(pa3->get_value(p3, idOf(pa3, p3, "NOTE"), &nv) && (int)(nv + 0.5) == 72,
            "nach dem Laden zeigen die Regler nicht die gewaehlte Spur");
        p3->destroy(p3);
    }

    grp("Noten von Hand");
    {
        EvList ev;
        ev.noteOn(60, 1.0);
        double r = renderRms(p, 2048, ev, false);
        chk(r > 0.0005, "eine gespielte Note erzeugt keinen Klang");
    }

    grp("Blockgroessen, die ein Host wirklich schickt");
    {
        bool ok = true;
        const int sizes[] = {1, 7, 64, 333, 4096};
        for (int s : sizes) {
            EvList ev;
            double r = renderRms(p, s, ev, true, 128.0);
            if (!(r >= 0.0 && r < 4.0)) ok = false;
        }
        chk(ok, "eine Blockgroesse liefert Unsinn");
    }

    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
    entry->deinit();
    dlclose(lib);

    printf(fails ? "\nFEHLGESCHLAGEN: %d von %d Pruefungen\n" : "\nOK: %d Pruefungen ok, %d Fehler\n",
           fails ? fails : checks, fails ? checks : fails);
    return fails ? 1 : 0;
}
