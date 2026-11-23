// WAV lesen und schreiben - nur fuer Tests und Export, NIE im Audio-Thread.
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

inline bool wavWrite(const char* path, const float* inter, int frames, int sr, int ch = 2) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    int bytes = frames * ch * 2;
    uint32_t v; uint16_t w;
    fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); v = 16;          fwrite(&v, 4, 1, f);
    w = 1;            fwrite(&w, 2, 1, f);
    w = (uint16_t)ch; fwrite(&w, 2, 1, f);
    v = (uint32_t)sr; fwrite(&v, 4, 1, f);
    v = (uint32_t)(sr * ch * 2); fwrite(&v, 4, 1, f);
    w = (uint16_t)(ch * 2); fwrite(&w, 2, 1, f);
    w = 16;           fwrite(&w, 2, 1, f);
    fwrite("data", 1, 4, f); v = (uint32_t)bytes; fwrite(&v, 4, 1, f);
    for (int i = 0; i < frames * ch; i++) {
        float x = inter[i];
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;
        int16_t s = (int16_t)(x * 32767.0f);
        fwrite(&s, 2, 1, f);
    }
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

// Liest eine mit wavWrite erzeugte Datei (16 Bit, feste Kopfgroesse).
inline int16_t* wavRead(const char* path, int* frames, int* sr, int* ch) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    char hdr[44];
    if (fread(hdr, 1, 44, f) != 44) { fclose(f); return nullptr; }
    uint16_t c; uint32_t rate, bytes;
    memcpy(&c, hdr + 22, 2); memcpy(&rate, hdr + 24, 4); memcpy(&bytes, hdr + 40, 4);
    int n = (int)(bytes / 2);
    int16_t* d = (int16_t*)malloc(bytes ? bytes : 2);
    if (fread(d, 2, n, f) != (size_t)n) { fclose(f); free(d); return nullptr; }
    fclose(f);
    *frames = n / (c ? c : 1); *sr = (int)rate; *ch = c;
    return d;
}
