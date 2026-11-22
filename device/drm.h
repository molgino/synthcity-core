// Direkte Bildausgabe ueber DRM/KMS ohne GPU und ohne libdrm (siehe drm.cpp)
#pragma once
#include <stdint.h>
#include <errno.h>
bool drmOpen();                                       // erste angeschlossene Anzeige uebernehmen
bool drmActive();
int  drmWidth();
int  drmHeight();
void drmPresent(const uint32_t* px, int w, int h);    // ARGB8888, Zeilen ohne Luecke
void drmSetByteOffset(int bytes);                     // Ausgleich fuer versetzt lesende Panels
void drmClose();
