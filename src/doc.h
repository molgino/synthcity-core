// Das Projekt ist ein DOKUMENT: eine Liste aus (stabile ID, Wert), nicht das Abbild einer struct.
// Darum braucht eine neue Version keine Migration: was sie nicht kennt, schreibt sie unveraendert
// zurueck, und was sie vermisst, bekommt seinen Standardwert.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum { DOC_MAX = 24000, DOC_KEY = 96, DOC_SLOTS = 65536, DOC_STR = 128, DOC_SVAL = 32 };   // Schluessel wie "pat.0.trk.3.st.12.flt.freq.slide"   // Slots: Zweierpotenz > 2 * DOC_MAX

struct Doc {
    struct E { char k[DOC_KEY]; int32_t v; };
    // Fast alles im Dokument ist eine Zahl. Namen sind es nicht - sie bekommen eine
    // eigene, kleine Liste, damit die Zahlenliste schlank bleibt.
    struct SE { char k[DOC_KEY]; char v[DOC_SVAL]; };
    E e[DOC_MAX];
    SE se[DOC_STR];
    int ns = 0;
    int n = 0;
    int32_t idx[DOC_SLOTS];      // offene Adressierung, -1 = frei
    bool overflow = false;       // Datei groesser als der Puffer: nichts still verlieren

    Doc() { clear(); }
    void clear() { n = 0; ns = 0; overflow = false; for (int i = 0; i < DOC_SLOTS; i++) idx[i] = -1; }

    // --- Namen ---------------------------------------------------------------------------
    int findStr(const char* k) const {
        for (int i = 0; i < ns; i++) if (!strcmp(se[i].k, k)) return i;
        return -1;
    }
    bool setStr(const char* k, const char* v) {
        int at = findStr(k);
        if (at < 0) {
            if (ns >= DOC_STR || strlen(k) >= DOC_KEY) { overflow = true; return false; }
            at = ns++;
            snprintf(se[at].k, DOC_KEY, "%s", k);
        }
        snprintf(se[at].v, DOC_SVAL, "%s", v ? v : "");
        return true;
    }
    const char* getStr(const char* k, const char* def = "") const {
        int at = findStr(k);
        return at < 0 ? def : se[at].v;
    }
    static bool isNumber(const char* v) {
        if (!*v) return false;
        if (*v == '-' || *v == '+') v++;
        if (!*v) return false;
        for (; *v; v++) if (*v < '0' || *v > '9') return false;
        return true;
    }

    static uint32_t hash(const char* s) {
        uint32_t h = 2166136261u;
        for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
        return h;
    }
    int find(const char* k) const {
        uint32_t i = hash(k) & (DOC_SLOTS - 1);
        for (int step = 0; step < DOC_SLOTS; step++, i = (i + 1) & (DOC_SLOTS - 1)) {
            if (idx[i] < 0) return -1;
            if (!strcmp(e[idx[i]].k, k)) return idx[i];
        }
        return -1;
    }
    bool set(const char* k, int32_t v) {
        int at = find(k);
        if (at >= 0) { e[at].v = v; return true; }
        if (n >= DOC_MAX || strlen(k) >= DOC_KEY) { overflow = true; return false; }
        snprintf(e[n].k, DOC_KEY, "%s", k);
        e[n].v = v;
        uint32_t i = hash(k) & (DOC_SLOTS - 1);
        while (idx[i] >= 0) i = (i + 1) & (DOC_SLOTS - 1);
        idx[i] = n++;
        return true;
    }
    int32_t get(const char* k, int32_t def) const { int at = find(k); return at < 0 ? def : e[at].v; }
    bool has(const char* k) const { return find(k) >= 0; }

    // Dateien gibt es nur dort, wo es ein Dateisystem gibt - der Browser-Bau laesst sie weg.
#ifndef SC_NO_FILES
    bool save(const char* path) const {
        FILE* f = fopen(path, "w");
        if (!f) return false;
        fprintf(f, "# synthcity 1\n");
        for (int i = 0; i < n; i++) fprintf(f, "%s = %d\n", e[i].k, (int)e[i].v);
        for (int i = 0; i < ns; i++) fprintf(f, "%s = %s\n", se[i].k, se[i].v);
        fclose(f);
        return true;
    }
    bool load(const char* path) {
        FILE* f = fopen(path, "r");
        if (!f) return false;
        clear();
        char line[DOC_KEY + 32];
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            char* eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            char* k = line;
            while (*k == ' ') k++;
            char* end = eq - 1;
            while (end > k && (*end == ' ' || *end == '\t')) *end-- = 0;
            char* v = eq + 1;
            while (*v == ' ' || *v == '\t') v++;
            char* ve = v;
            while (*ve && *ve != '\n' && *ve != '\r') ve++;
            while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
            *ve = 0;
            if (isNumber(v)) set(k, (int32_t)strtol(v, nullptr, 10));
            else             setStr(k, v);
        }
        fclose(f);
        return true;
    }
#endif
};

// Schluessel bauen - eine Stelle, damit Schreiben und Lesen nicht auseinanderlaufen.
inline void docKeyTrack(char* out, int nOut, int pat, int trk, const char* id) {
    snprintf(out, nOut, "pat.%d.trk.%d.%s", pat, trk, id + (strncmp(id, "trk.", 4) ? 0 : 4));
}
inline void docKeyScene(char* out, int nOut, int pat, int sc, int trk, const char* id) {
    snprintf(out, nOut, "pat.%d.sc.%d.trk.%d.%s", pat, sc, trk, id + (strncmp(id, "trk.", 4) ? 0 : 4));
}
inline void docKeyStep(char* out, int nOut, int pat, int trk, int step, const char* suffix) {
    snprintf(out, nOut, "pat.%d.trk.%d.st.%d%s%s", pat, trk, step, suffix[0] ? "." : "", suffix);
}
