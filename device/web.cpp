// Webserver auf dem Geraet: Samples und Projekte hoch- und herunterladen, App-Update (Groovebox-OS).
// Am PC: USB-Kabel, PC-Modus an, Browser auf http://192.168.77.1 (oder die IP im WLAN).
// Nach dem Vorbild von r36-groovebox/src/web.cpp, auf das Noetige gekuerzt: keine Fernbedienung,
// kein https. Jede Verbindung bekommt einen eigenen Faden; bearbeitet wird nacheinander.
#include "web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static char g_base[600];
static int  g_port = 0;
static volatile int g_smpSeq = 0, g_prjSeq = 0, g_update = 0;
static char g_last[160] = "";
static volatile int g_lastSeq = 0;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;

int  webPort() { return g_port; }
int  webSampleSeq() { return g_smpSeq; }
int  webProjectSeq() { return g_prjSeq; }
bool webUpdateReady() { return g_update != 0; }
int  webLastEvent(char* out, int n) {
    pthread_mutex_lock(&g_mx); snprintf(out, (size_t)n, "%s", g_last); int s = g_lastSeq; pthread_mutex_unlock(&g_mx);
    return s;
}
static void note(const char* what, const char* name) {
    pthread_mutex_lock(&g_mx);
    snprintf(g_last, sizeof g_last, "%s %.100s", what, name);
    g_lastSeq++;
    pthread_mutex_unlock(&g_mx);
}
static bool isOS() { struct stat st; return stat("/etc/groovebox-os", &st) == 0; }

static bool sendAll(int fd, const void* p, size_t n) {
    const char* c = (const char*)p;
    while (n) {
        ssize_t w = send(fd, c, n, MSG_NOSIGNAL);
        if (w <= 0) return false;
        c += w; n -= (size_t)w;
    }
    return true;
}
static void reply(int fd, int code, const char* type, const char* body, size_t len) {
    char h[400];
    const char* msg = code == 200 ? "OK" : code == 404 ? "Not Found" : code == 413 ? "Too Large" : "Bad Request";
    int hl = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n", code, msg, type, len);
    sendAll(fd, h, (size_t)hl);
    if (len) sendAll(fd, body, len);
}
static void replyText(int fd, int code, const char* t) { reply(fd, code, "text/plain; charset=utf-8", t, strlen(t)); }

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool queryGet(const char* q, const char* key, char* out, int n) {
    size_t kl = strlen(key);
    while (q && *q) {
        if (!strncmp(q, key, kl) && q[kl] == '=') {
            const char* v = q + kl + 1;
            int o = 0;
            while (*v && *v != '&' && o < n - 1) {
                if (*v == '%' && hexv(v[1]) >= 0 && hexv(v[2]) >= 0) { out[o++] = (char)(hexv(v[1]) * 16 + hexv(v[2])); v += 3; }
                else if (*v == '+') { out[o++] = ' '; v++; }
                else out[o++] = *v++;
            }
            out[o] = 0;
            return true;
        }
        q = strchr(q, '&');
        if (q) q++;
    }
    return false;
}
// Erlaubt: "name.wav" oder "ordner/name.wav". Kein "..", nichts Verstecktes, keine FAT-Sonderzeichen.
bool webSafeRel(const char* p, int maxDepth) {
    if (!p[0] || p[0] == '/' || p[0] == '.') return false;
    int slashes = 0;
    for (const char* c = p; *c; c++) {
        unsigned char u = (unsigned char)*c;
        if (*c == '/') { slashes++; if (c[1] == '.' || c[1] == '/' || c[1] == 0) return false; continue; }
        if (u < 32 || u == 127 || strchr("\\:*?\"<>|", u)) return false;
    }
    return slashes <= maxDepth && !strstr(p, "..") && strlen(p) < 300;
}
static bool hasExt(const char* p, const char* ext) {
    size_t l = strlen(p), e = strlen(ext);
    return l > e && !strcasecmp(p + l - e, ext);
}

// --- Liste als JSON ------------------------------------------------------------------------
struct Buf { char* p; size_t n, cap; };
static void bput(Buf& b, const char* s, size_t l) {
    if (b.n + l + 1 > b.cap) { size_t c = (b.cap ? b.cap * 2 : 8192); while (c < b.n + l + 1) c *= 2; char* q = (char*)realloc(b.p, c); if (!q) return; b.p = q; b.cap = c; }
    memcpy(b.p + b.n, s, l); b.n += l; b.p[b.n] = 0;
}
static void bstr(Buf& b, const char* s) { bput(b, s, strlen(s)); }
static void bjson(Buf& b, const char* s) {
    bput(b, "\"", 1);
    for (; *s; s++) {
        char e[8];
        if (*s == '"' || *s == '\\') { e[0] = '\\'; e[1] = *s; bput(b, e, 2); }
        else if ((unsigned char)*s < 32) { snprintf(e, sizeof e, "\\u%04x", *s); bstr(b, e); }
        else bput(b, s, 1);
    }
    bput(b, "\"", 1);
}
static void listDir(Buf& b, const char* dir, const char* sub, const char* ext, bool& first, int depth) {
    char p[900]; snprintf(p, sizeof p, "%s%s%s", dir, sub[0] ? "/" : "", sub);
    DIR* d = opendir(p);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char rel[600]; snprintf(rel, sizeof rel, "%s%s%s", sub, sub[0] ? "/" : "", e->d_name);
        char full[1200]; snprintf(full, sizeof full, "%s/%s", dir, rel);
        struct stat st;
        if (stat(full, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth >= 6) continue;
            bstr(b, first ? "{\"p\":" : ",{\"p\":"); first = false;   // Ordner auch leer zeigen
            bjson(b, rel); bstr(b, ",\"d\":1}");
            listDir(b, dir, rel, ext, first, depth + 1);
            continue;
        }
        if (!hasExt(e->d_name, ext)) continue;
        char num[64];
        bstr(b, first ? "{\"p\":" : ",{\"p\":"); first = false;
        bjson(b, rel);
        snprintf(num, sizeof num, ",\"s\":%lld,\"t\":%lld}", (long long)st.st_size, (long long)st.st_mtime);
        bstr(b, num);
    }
    closedir(d);
}
static void handleList(int fd) {
    Buf b = {nullptr, 0, 0};
    char d[700];
    bool first = true;
    bstr(b, "{\"samples\":[");
    snprintf(d, sizeof d, "%ssamples", g_base); listDir(b, d, "", ".wav", first, 0);
    first = true;
    bstr(b, "],\"projects\":[");
    snprintf(d, sizeof d, "%sprojects", g_base); listDir(b, d, "", ".sc", first, 0);
    char tail[200];
    snprintf(tail, sizeof tail, "],\"os\":%s,\"name\":\"SYNTH CITY CORE\"}", isOS() ? "true" : "false");
    bstr(b, tail);
    reply(fd, 200, "application/json", b.p ? b.p : "{}", b.p ? b.n : 2);
    free(b.p);
}
// Ordner rekursiv loeschen (nur unter samples/, Tiefe begrenzt). Rueckgabe: geloeschte Dateien, -1 = fehlt
static int rmTree(const char* path, int depth) {
    DIR* d = opendir(path);
    if (!d) return -1;
    int n = 0;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[1200]; snprintf(p, sizeof p, "%s/%s", path, e->d_name);
        struct stat st;
        if (lstat(p, &st)) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < 6) { int k = rmTree(p, depth + 1); if (k > 0) n += k; } }
        else if (!unlink(p)) n++;
    }
    closedir(d);
    rmdir(path);
    return n;
}
static void mkParents(const char* dir, const char* rel) {
    char p[1200]; snprintf(p, sizeof p, "%s/%s", dir, rel);
    for (char* c = p + strlen(dir) + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(p, 0755); *c = '/'; }
}
// Koerper in eine Datei schreiben: erst *.part, dann umbenennen - ein Abbruch hinterlaesst nichts Halbes.
static bool receiveTo(int fd, const char* full, const char* pre, int preN, long long clen) {
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.part", full);
    FILE* f = fopen(tmp, "wb");
    if (!f) return false;
    long long got = 0;
    if (preN > 0) { if (preN > clen) preN = (int)clen; fwrite(pre, 1, (size_t)preN, f); got = preN; }
    static char buf[65536];
    bool ok = true;
    while (got < clen) {
        ssize_t r = recv(fd, buf, (size_t)(clen - got < (long long)sizeof buf ? clen - got : (long long)sizeof buf), 0);
        if (r <= 0 || fwrite(buf, 1, (size_t)r, f) != (size_t)r) { ok = false; break; }
        got += r;
    }
    if (fclose(f) != 0) ok = false;
    if (!ok || rename(tmp, full)) { unlink(tmp); return false; }
    sync();
    return true;
}

static void handle(int fd) {
    struct timeval tv = {15, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    static char hb[8192];
    int hl = 0;
    char* body = nullptr;
    while (hl < (int)sizeof hb - 1) {
        ssize_t r = recv(fd, hb + hl, sizeof hb - 1 - hl, 0);
        if (r <= 0) return;
        hl += (int)r; hb[hl] = 0;
        if ((body = strstr(hb, "\r\n\r\n"))) { body += 4; break; }
    }
    if (!body) { replyText(fd, 400, "header too large"); return; }
    char method[8] = {0}, target[1024] = {0};
    if (sscanf(hb, "%7s %1023s", method, target) != 2) { replyText(fd, 400, "?"); return; }
    long long clen = 0;
    for (char* l = strstr(hb, "\r\n"); l && l < body; l = strstr(l + 2, "\r\n"))
        if (!strncasecmp(l + 2, "Content-Length:", 15)) clen = atoll(l + 17);
    int preN = hl - (int)(body - hb);
    char* q = strchr(target, '?');
    if (q) *q++ = 0;
    char root[16] = "samples", rel[400] = "";
    if (q) { queryGet(q, "root", root, sizeof root); queryGet(q, "path", rel, sizeof rel); }
    bool isS = !strcmp(root, "samples"), isP = !strcmp(root, "projects");
    const char* ext = isS ? ".wav" : ".sc";
    char dir[700], full[1200];
    snprintf(dir, sizeof dir, "%s%s", g_base, isS ? "samples" : "projects");
    snprintf(full, sizeof full, "%s/%s", dir, rel);

    if (!strcmp(method, "GET") && (!strcmp(target, "/") || !strcmp(target, "/index.html"))) {
        reply(fd, 200, "text/html; charset=utf-8", WEB_PAGE, strlen(WEB_PAGE));
        return;
    }
    if (!strcmp(method, "GET") && !strcmp(target, "/api/list")) { handleList(fd); return; }
    if (!strcmp(method, "GET") && !strcmp(target, "/api/event")) {
        char b[200]; int s = webLastEvent(b, sizeof b);
        char o[300]; snprintf(o, sizeof o, "%d %s", s, b);
        replyText(fd, 200, o);
        return;
    }
    if (!strcmp(method, "PUT") && !strcmp(target, "/api/update")) {   // Groovebox-OS: App-Update (.tgz)
        if (!isOS()) { replyText(fd, 400, "App updates via browser only on Groovebox-OS"); return; }
        if (clen <= 1000 || clen > 64LL * 1024 * 1024) { replyText(fd, 413, "invalid update file"); return; }
        char udir[700], fin[800];
        snprintf(udir, sizeof udir, "%supdate", g_base);
        mkdir(udir, 0755);
        snprintf(fin, sizeof fin, "%s/synthcity-update.tgz", udir);
        if (!receiveTo(fd, fin, body, preN, clen)) { replyText(fd, 400, "upload failed"); return; }
        unsigned char mg[2] = {0, 0};
        FILE* chk = fopen(fin, "rb");
        if (chk) { if (fread(mg, 1, 2, chk) != 2) mg[0] = 0; fclose(chk); }
        if (mg[0] != 0x1f || mg[1] != 0x8b) { unlink(fin); replyText(fd, 400, "not a .tgz update"); return; }
        note("UPDATE", "received - restarting");
        replyText(fd, 200, "ok - the device restarts and installs the update");
        g_update = 1;
        return;
    }
    if (!isS && !isP) { replyText(fd, 400, "root?"); return; }
    if (!strcmp(method, "GET") && !strcmp(target, "/api/file")) {
        if (!webSafeRel(rel, 6) || !hasExt(rel, ext)) { replyText(fd, 400, "path?"); return; }
        FILE* f = fopen(full, "rb");
        if (!f) { replyText(fd, 404, "not found"); return; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        const char* base = rel; for (const char* c = rel; *c; c++) if (*c == '/') base = c + 1;
        char h[600];
        int l = snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n"
                         "Content-Disposition: attachment; filename=\"%s\"\r\nConnection: close\r\n\r\n",
                         isS ? "audio/wav" : "text/plain; charset=utf-8", n, base);
        sendAll(fd, h, (size_t)l);
        static char buf[65536];
        size_t r;
        while ((r = fread(buf, 1, sizeof buf, f)) > 0) if (!sendAll(fd, buf, r)) break;
        fclose(f);
        return;
    }
    if (!strcmp(method, "PUT") && !strcmp(target, "/api/upload")) {
        if (!webSafeRel(rel, isS ? 6 : 0) || !hasExt(rel, ext)) { replyText(fd, 400, "invalid file name"); return; }
        if (clen <= 0 || clen > 200LL * 1024 * 1024) { replyText(fd, 413, "file too large (max 200 MB)"); return; }
        mkdir(dir, 0755);
        mkParents(dir, rel);
        if (!receiveTo(fd, full, body, preN, clen)) { replyText(fd, 400, "upload failed or card full"); return; }
        if (isS) g_smpSeq++; else g_prjSeq++;
        note(isS ? "SAMPLE" : "PROJECT", rel);
        replyText(fd, 200, "ok");
        return;
    }
    if (!strcmp(method, "POST") && !strcmp(target, "/api/mkdir") && isS) {
        if (!webSafeRel(rel, 6)) { replyText(fd, 400, "invalid folder name"); return; }
        mkParents(dir, rel);
        if (mkdir(full, 0755) && errno != EEXIST) { replyText(fd, 400, "could not create folder"); return; }
        replyText(fd, 200, "ok");
        return;
    }
    if (!strcmp(method, "POST") && !strcmp(target, "/api/rmdir") && isS) {   // Ordner samt Inhalt
        if (!webSafeRel(rel, 6)) { replyText(fd, 400, "path?"); return; }
        int n = rmTree(full, 0);
        if (n < 0) { replyText(fd, 404, "not found"); return; }
        sync();
        g_smpSeq++;
        note("DELETED FOLDER", rel);
        replyText(fd, 200, "ok");
        return;
    }
    if (!strcmp(method, "POST") && !strcmp(target, "/api/delete")) {
        if (!webSafeRel(rel, 6) || !hasExt(rel, ext)) { replyText(fd, 400, "path?"); return; }
        if (unlink(full)) { replyText(fd, 404, "not found"); return; }
        sync();
        if (isS) g_smpSeq++; else g_prjSeq++;
        note("DELETED", rel);
        replyText(fd, 200, "ok");
        return;
    }
    replyText(fd, 404, "unknown");
}

static pthread_mutex_t g_reqMx = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_conns = 0;
static void* connThread(void* arg) {
    int fd = (int)(intptr_t)arg;
    struct pollfd pf = {fd, POLLIN, 0};
    if (poll(&pf, 1, 10000) > 0 && !(pf.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        pthread_mutex_lock(&g_reqMx);
        handle(fd);
        pthread_mutex_unlock(&g_reqMx);
    }
    shutdown(fd, SHUT_WR);
    char d[256];
    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    while (recv(fd, d, sizeof d, 0) > 0) {}
    close(fd);
    __sync_fetch_and_sub(&g_conns, 1);
    return nullptr;
}
static void* acceptThread(void* arg) {
    int ls = (int)(intptr_t)arg;
    for (;;) {
        int fd = accept(ls, nullptr, nullptr);
        if (fd < 0) { if (errno != EINTR) usleep(200000); continue; }
        int ka = 1, idle = 5, intv = 2, cnt = 3;
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof ka);
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intv, sizeof intv);
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof cnt);
        if (__atomic_load_n(&g_conns, __ATOMIC_RELAXED) >= 16) { close(fd); continue; }
        __sync_fetch_and_add(&g_conns, 1);
        pthread_attr_t at; pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, 128 * 1024);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        pthread_t th;
        if (pthread_create(&th, &at, connThread, (void*)(intptr_t)fd)) connThread((void*)(intptr_t)fd);
        pthread_attr_destroy(&at);
    }
    return nullptr;
}

bool webStart(const char* basePath, int port) {
    snprintf(g_base, sizeof g_base, "%s", basePath);
    int ports[2] = {port, 8080};
    for (int i = 0; i < 2; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return false;
        int one = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)ports[i]);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(s, (struct sockaddr*)&a, sizeof a) == 0 && listen(s, 16) == 0) {
            g_port = ports[i];
            pthread_t th;
            pthread_create(&th, nullptr, acceptThread, (void*)(intptr_t)s);
            pthread_detach(th);
            printf("Web-Upload auf Port %d\n", g_port);
            return true;
        }
        close(s);
    }
    printf("Web-Upload: kein Port frei\n");
    return false;
}
