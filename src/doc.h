// Das Projekt ist ein DOKUMENT: eine Liste aus (stabile ID, Wert), nicht das Abbild einer struct.
// Darum braucht eine neue Version keine Migration: was sie nicht kennt, schreibt sie unveraendert
// zurueck, und was sie vermisst, bekommt seinen Standardwert.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// DOC_MAX ist nur noch eine Notbremse gegen Unsinn, keine Groesse: das Dokument waechst mit dem
// Projekt. Der schlimmste echte Fall (16 Patterns x 9 Spuren x 128 Steps x 12 Locks mit Slides)
// sind rund 490 000 Eintraege - frueher war bei 48 000 Schluss, und ein dichtes Projekt liess
// sich nicht mehr speichern.
enum { DOC_MAX = 4000000, DOC_KEY = 96, DOC_STR = 128, DOC_SVAL = 32 };   // Schluessel wie "pat.0.trk.3.st.12.flt.freq.slide"

struct Doc {
    // Ein Eintrag zeigt auf seinen Schluessel im Schluessel-Speicher (statt 96 feste Bytes je
    // Eintrag) - so belegt ein Projekt nur, was es braucht.
    struct E { char* k; int32_t v; };
    // Fast alles im Dokument ist eine Zahl. Namen sind es nicht - sie bekommen eine
    // eigene, kleine Liste, damit die Zahlenliste schlank bleibt.
    struct SE { char k[DOC_KEY]; char v[DOC_SVAL]; };
    E* e = nullptr;
    SE se[DOC_STR];
    int ns = 0;
    int n = 0;
    int limit = DOC_MAX;         // Tests duerfen sie senken
    bool overflow = false;       // Notbremse gezogen oder kein Speicher: nichts still verlieren

    Doc() { clear(); }
    ~Doc() { free(e); free(keys); free(idx); free(txt); }
    Doc(const Doc&) = delete;
    Doc& operator=(const Doc&) = delete;
    void clear() {
        n = 0; ns = 0; overflow = false; klen = 0;
        for (int i = 0; i < slots; i++) idx[i] = -1;
    }

    // Wachsen - nie im Audio-Faden: ein Dokument entsteht nur beim Speichern und Laden.
    // Ohne realloc (den hat der Browser-Bau nicht): neu anfordern, kopieren, alten freigeben.
    static void* grow(void* old, size_t oldBytes, size_t newBytes) {
        void* p = malloc(newBytes);
        if (!p) return nullptr;
        if (old && oldBytes) memcpy(p, old, oldBytes);
        free(old);
        return p;
    }
    bool reserve(int want) {
        if (want <= cap) return true;
        int nc = cap ? cap : 4096;
        while (nc < want) nc *= 2;
        E* ne = (E*)grow(e, sizeof(E) * (size_t)n, sizeof(E) * (size_t)nc);
        if (!ne) return false;
        e = ne; cap = nc;
        if (slots < 2 * cap) {                       // Hashtabelle neu aufbauen
            int ns2 = 1; while (ns2 < 2 * cap) ns2 *= 2;
            int32_t* ni = (int32_t*)malloc(sizeof(int32_t) * (size_t)ns2);
            if (!ni) return false;
            free(idx); idx = ni; slots = ns2;
            for (int i = 0; i < slots; i++) idx[i] = -1;
            for (int i = 0; i < n; i++) {
                uint32_t h = hash(e[i].k) & (uint32_t)(slots - 1);
                while (idx[h] >= 0) h = (h + 1) & (uint32_t)(slots - 1);
                idx[h] = i;
            }
        }
        return true;
    }
    char* keep(const char* k, size_t len) {          // Schluessel in den Speicher legen
        if (klen + len + 1 > kcap) {
            size_t nk = kcap ? kcap : 65536;
            while (nk < klen + len + 1) nk *= 2;
            char* old = keys;
            char* nkeys = (char*)grow(keys, klen, nk);
            if (!nkeys) return nullptr;
            keys = nkeys; kcap = nk;
            for (int i = 0; i < n; i++) e[i].k = keys + (e[i].k - old);   // Zeiger nachziehen
        }
        char* at = keys + klen;
        memcpy(at, k, len + 1);
        klen += len + 1;
        return at;
    }

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
        if (!slots) return -1;
        uint32_t i = hash(k) & (uint32_t)(slots - 1);
        for (int step = 0; step < slots; step++, i = (i + 1) & (uint32_t)(slots - 1)) {
            if (idx[i] < 0) return -1;
            if (!strcmp(e[idx[i]].k, k)) return idx[i];
        }
        return -1;
    }
    bool set(const char* k, int32_t v) {
        int at = find(k);
        if (at >= 0) { e[at].v = v; return true; }
        size_t len = strlen(k);
        if (n >= limit || len >= DOC_KEY || !reserve(n + 1)) { overflow = true; return false; }
        char* kk = keep(k, len);
        if (!kk) { overflow = true; return false; }
        e[n].k = kk;
        e[n].v = v;
        uint32_t i = hash(k) & (uint32_t)(slots - 1);
        while (idx[i] >= 0) i = (i + 1) & (uint32_t)(slots - 1);
        idx[i] = n++;
        return true;
    }
    int32_t get(const char* k, int32_t def) const { int at = find(k); return at < 0 ? def : e[at].v; }
    bool has(const char* k) const { return find(k) >= 0; }

    // Das ganze Dokument als Text, in einem Puffer, der mitwaechst (gehoert dem Dokument,
    // gilt bis zum naechsten Aufruf). nullptr = unvollstaendig oder kein Speicher.
    const char* text() {
        if (overflow) return nullptr;
        size_t need = 1;
        for (int i = 0; i < n; i++) need += strlen(e[i].k) + 16;          // " = " + Zahl + "\n"
        for (int i = 0; i < ns; i++) need += strlen(se[i].k) + strlen(se[i].v) + 5;
        if (need > tcap) {
            char* nt = (char*)grow(txt, 0, need);
            if (!nt) return nullptr;
            txt = nt; tcap = need;
        }
        return writeText(txt, tcap) ? txt : nullptr;
    }
    bool writeText(char* out, size_t size) const {
        if (overflow || !size) return false;
        size_t at = 0;
        for (int i = 0; i < n; i++) {
            int len = snprintf(out + at, size - at, "%s = %d\n", e[i].k, (int)e[i].v);
            if (len < 0 || (size_t)len >= size - at) return false;
            at += len;
        }
        for (int i = 0; i < ns; i++) {
            int len = snprintf(out + at, size - at, "%s = %s\n", se[i].k, se[i].v);
            if (len < 0 || (size_t)len >= size - at) return false;
            at += len;
        }
        out[at] = 0;
        return true;
    }

    // Dateien gibt es nur dort, wo es ein Dateisystem gibt - der Browser-Bau laesst sie weg.
#ifndef SC_NO_FILES
    bool save(const char* path) const {
        if (overflow) return false;
        FILE* f = fopen(path, "w");
        if (!f) return false;
        bool ok = fprintf(f, "# synthcity 1\n") >= 0;
        for (int i = 0; i < n && ok; i++) ok = fprintf(f, "%s = %d\n", e[i].k, (int)e[i].v) >= 0;
        for (int i = 0; i < ns && ok; i++) ok = fprintf(f, "%s = %s\n", se[i].k, se[i].v) >= 0;
        return fclose(f) == 0 && ok;
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
        bool ok = !overflow && !ferror(f);
        return fclose(f) == 0 && ok;
    }
#endif
private:
    int cap = 0, slots = 0;
    int32_t* idx = nullptr;      // offene Adressierung, -1 = frei
    char* keys = nullptr;
    size_t klen = 0, kcap = 0;
    char* txt = nullptr;
    size_t tcap = 0;
};

// Ein Textpuffer, der mitwaechst (Laden): gross genug fuer n Zeichen plus Ende, sonst nullptr.
struct TextBuf {
    char* p = nullptr; size_t cap = 0;
    char* reserve(size_t n) {
        if (n + 1 > cap) {
            size_t c = cap ? cap : 65536;
            while (c < n + 1) c *= 2;
            char* q = (char*)malloc(c);
            if (!q) return nullptr;
            free(p); p = q; cap = c;
        }
        return p;
    }
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
