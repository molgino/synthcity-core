// Bildausgabe direkt ueber DRM/KMS (Groovebox-OS): ein "Dumb-Buffer", in den das fertige Bild
// kopiert wird. Kein OpenGL, keine libdrm - nur die Kernel-Schnittstelle (ioctl), damit der
// Cross-Build mit dem schlanken Sysroot auskommt. Das Prinzip stammt aus r36-groovebox/src/drmout.cpp.
#include "drm.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

static int      g_fd = -1;
static uint32_t g_crtc = 0, g_conn = 0, g_fb = 0, g_handle = 0, g_pitch = 0;
static uint64_t g_size = 0;
static uint8_t* g_map = nullptr;
static drm_mode_modeinfo g_mode;
static drm_mode_crtc g_saved;
static bool g_haveSaved = false;
static int  g_off = 0;           // Byte-Versatz im Zeilenstrom (manche R36S-Panels lesen versetzt)

static int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do r = ioctl(fd, req, arg); while (r == -1 && (errno == EINTR || errno == EAGAIN));
    return r;
}

void drmSetByteOffset(int bytes) { g_off = bytes; }
int  drmWidth()  { return g_fd >= 0 ? g_mode.hdisplay : 0; }
int  drmHeight() { return g_fd >= 0 ? g_mode.vdisplay : 0; }
bool drmActive() { return g_fd >= 0; }

static bool openCard(const char* path) {
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    xioctl(fd, DRM_IOCTL_SET_MASTER, nullptr);

    drm_mode_card_res res; memset(&res, 0, sizeof res);
    if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0 || !res.count_connectors) { close(fd); return false; }
    uint32_t conns[16] = {0}, crtcs[16] = {0}, encs[16] = {0}, fbs[16] = {0};
    if (res.count_connectors > 16) res.count_connectors = 16;
    if (res.count_crtcs > 16) res.count_crtcs = 16;
    if (res.count_encoders > 16) res.count_encoders = 16;
    if (res.count_fbs > 16) res.count_fbs = 16;
    res.connector_id_ptr = (uint64_t)(uintptr_t)conns;
    res.crtc_id_ptr      = (uint64_t)(uintptr_t)crtcs;
    res.encoder_id_ptr   = (uint64_t)(uintptr_t)encs;
    res.fb_id_ptr        = (uint64_t)(uintptr_t)fbs;
    if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) { close(fd); return false; }

    for (uint32_t i = 0; i < res.count_connectors && !g_conn; i++) {
        drm_mode_get_connector c; memset(&c, 0, sizeof c);
        c.connector_id = conns[i];
        if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) continue;   // erster Aufruf: Anzahl (und Abtasten)
        if (c.connection != 1 || !c.count_modes) continue;
        static drm_mode_modeinfo modes[64];
        uint32_t cenc[16] = {0};
        uint32_t nm = c.count_modes > 64 ? 64 : c.count_modes;
        uint32_t ne = c.count_encoders > 16 ? 16 : c.count_encoders;
        drm_mode_get_connector c2; memset(&c2, 0, sizeof c2);
        c2.connector_id = conns[i];
        c2.count_modes = nm; c2.modes_ptr = (uint64_t)(uintptr_t)modes;
        c2.count_encoders = ne; c2.encoders_ptr = (uint64_t)(uintptr_t)cenc;
        if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c2) < 0 || !c2.count_modes) continue;
        g_mode = modes[0];
        for (uint32_t k = 0; k < nm; k++)
            if (modes[k].type & DRM_MODE_TYPE_PREFERRED) { g_mode = modes[k]; break; }
        uint32_t crtc = 0;
        if (c2.encoder_id) {
            drm_mode_get_encoder e; memset(&e, 0, sizeof e);
            e.encoder_id = c2.encoder_id;
            if (xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &e) == 0) crtc = e.crtc_id;
        }
        for (uint32_t k = 0; !crtc && k < ne; k++) {
            drm_mode_get_encoder e; memset(&e, 0, sizeof e);
            e.encoder_id = cenc[k];
            if (xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &e) < 0) continue;
            for (uint32_t j = 0; j < res.count_crtcs; j++)
                if (e.possible_crtcs & (1u << j)) { crtc = crtcs[j]; break; }
        }
        if (crtc) { g_conn = conns[i]; g_crtc = crtc; }
    }
    if (!g_conn) { close(fd); return false; }

    drm_mode_create_dumb cd; memset(&cd, 0, sizeof cd);
    cd.width = g_mode.hdisplay; cd.height = g_mode.vdisplay; cd.bpp = 32;
    if (xioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) { close(fd); g_conn = 0; return false; }
    g_handle = cd.handle; g_pitch = cd.pitch; g_size = cd.size;
    drm_mode_fb_cmd fb; memset(&fb, 0, sizeof fb);
    fb.width = g_mode.hdisplay; fb.height = g_mode.vdisplay; fb.pitch = g_pitch;
    fb.bpp = 32; fb.depth = 24; fb.handle = g_handle;
    if (xioctl(fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0) { close(fd); g_conn = 0; return false; }
    g_fb = fb.fb_id;
    drm_mode_map_dumb md; memset(&md, 0, sizeof md);
    md.handle = g_handle;
    if (xioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md) < 0) { close(fd); g_conn = 0; return false; }
    void* m = mmap(nullptr, g_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)md.offset);
    if (m == MAP_FAILED) { close(fd); g_conn = 0; return false; }
    g_map = (uint8_t*)m;
    memset(g_map, 0, g_size);

    memset(&g_saved, 0, sizeof g_saved);
    g_saved.crtc_id = g_crtc;
    g_haveSaved = xioctl(fd, DRM_IOCTL_MODE_GETCRTC, &g_saved) == 0;

    drm_mode_crtc sc; memset(&sc, 0, sizeof sc);
    sc.crtc_id = g_crtc; sc.fb_id = g_fb;
    sc.set_connectors_ptr = (uint64_t)(uintptr_t)&g_conn; sc.count_connectors = 1;
    sc.mode = g_mode; sc.mode_valid = 1;
    if (xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &sc) < 0) {
        printf("DRM: Modus setzen fehlgeschlagen (%s)\n", path);
        munmap(g_map, g_size); g_map = nullptr; close(fd); g_conn = 0;
        return false;
    }
    g_fd = fd;
    printf("DRM-Ausgabe: %s, %dx%d@%d\n", path, g_mode.hdisplay, g_mode.vdisplay, g_mode.vrefresh);
    return true;
}

bool drmOpen() {
    for (int card = 0; card < 4; card++) {
        char p[32]; snprintf(p, sizeof p, "/dev/dri/card%d", card);
        if (openCard(p)) return true;
    }
    printf("DRM: keine Anzeige gefunden\n");
    return false;
}

void drmPresent(const uint32_t* px, int w, int h) {
    if (g_fd < 0 || !g_map) return;
    int W = g_mode.hdisplay, H = g_mode.vdisplay;
    static uint32_t tmp[4096];
    if (w == W && h == H && g_off != 0 && w <= 4096) {
        // Das Panel zeigt an Byte k den Byte k+off des Zeilenstroms -> vorab um -off rotieren
        // (siehe r36-groovebox: off = 3*N + r, der Versatz laeuft ueber das ganze Bild weiter).
        int L = w * 3, o = ((g_off % L) + L) % L, N = o / 3, r = o % 3;
        for (int y = 0; y < h; y++) {
            const uint32_t* cur = px + (size_t)y * w;
            const uint32_t* prv = px + (size_t)(y > 0 ? y - 1 : h - 1) * w;
            uint32_t prev = N > 0 ? prv[w - N - 1] : prv[w - 1];
            const uint32_t* src0 = prv + w - N;
            if (r == 0) {
                for (int x = 0; x < N; x++) tmp[x] = 0xff000000u | src0[x];
                for (int x = N; x < w; x++) tmp[x] = 0xff000000u | cur[x - N];
            } else {
                int sh1 = r == 1 ? 16 : 8, sh2 = r == 1 ? 8 : 16;
                uint32_t m1 = r == 1 ? 0xffu : 0xffffu, m2 = r == 1 ? 0xffffu : 0xffu;
                for (int x = 0; x < w; x++) {
                    uint32_t c = x < N ? src0[x] : cur[x - N];
                    tmp[x] = 0xff000000u | ((prev & m1) << sh1) | ((c >> sh2) & m2);
                    prev = c;
                }
            }
            memcpy(g_map + (size_t)y * g_pitch, tmp, (size_t)w * 4);
        }
    } else if (w == W && h == H) {
        for (int y = 0; y < h; y++) memcpy(g_map + (size_t)y * g_pitch, px + (size_t)y * w, (size_t)w * 4);
    } else {                     // andere Aufloesung: naechster Nachbar
        for (int y = 0; y < H; y++) {
            uint32_t* d = (uint32_t*)(g_map + (size_t)y * g_pitch);
            const uint32_t* s = px + (size_t)(y * h / H) * w;
            for (int x = 0; x < W; x++) d[x] = s[x * w / W];
        }
    }
}

void drmClose() {
    if (g_fd < 0) return;
    if (g_haveSaved && g_saved.fb_id) {
        g_saved.set_connectors_ptr = (uint64_t)(uintptr_t)&g_conn; g_saved.count_connectors = 1;
        xioctl(g_fd, DRM_IOCTL_MODE_SETCRTC, &g_saved);
    }
    if (g_map) munmap(g_map, g_size);
    g_map = nullptr;
    close(g_fd);
    g_fd = -1;
}
