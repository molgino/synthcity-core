// Ein kleiner Ersatz fuer libc und libm, nur fuer den WebAssembly-Bau.
// Der Browser-Bau soll ohne Werkzeugkette von aussen auskommen: clang kann wasm32 von Haus aus,
// es fehlen nur diese Funktionen. Alles hier ist bewusst knapp - fuer Audio reicht float-Genauigkeit.
// Der Kern selbst bleibt unveraendert; er sieht dieselben Namen wie unter Linux.
#ifndef SC_FREESTANDING_H
#define SC_FREESTANDING_H
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Speicher ---------------------------------------------------------------------------------
extern unsigned char __heap_base;   // vom Binder gesetzt: hier beginnt der freie Speicher

// Der Compiler ruft memcpy und memset von sich aus auf - sie brauchen echte Symbole.
void*  malloc(size_t n);
void   free(void* p);
void*  memcpy(void* d, const void* s, size_t n);
void*  memset(void* d, int v, size_t n);
void*  memmove(void* d, const void* s, size_t n);
size_t strlen(const char* s);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);
char*  strchr(const char* s, int c);
long   strtol(const char* s, char** end, int base);

// --- Mathematik ----------------------------------------------------------------------------------
#define SC_PI 3.14159265358979f
static inline float sqrtf(float x) { return __builtin_sqrtf(x); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline float fabsf(float x) { return x < 0 ? -x : x; }
static inline float floorf(float x) { float t = (float)(int)x; return (x < 0 && t != x) ? t - 1 : t; }
static inline long long llround(double x) { return (long long)(x < 0 ? x - 0.5 : x + 0.5); }

// Reihenentwicklung nach Argumentreduktion - fuer Audio genau genug (Fehler < 1e-6).
static inline float sinf(float x) {
    float k = floorf(x * (1.0f / (2.0f * SC_PI)) + 0.5f);
    x -= k * 2.0f * SC_PI;                       // -pi .. pi
    float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.00833333f +
               x2 * (-0.00019841f + x2 * 2.7557e-6f))));
}
static inline float cosf(float x) { return sinf(x + SC_PI * 0.5f); }
static inline float tanf(float x) {
    float c = cosf(x);
    if (c > -1e-7f && c < 1e-7f) c = c < 0 ? -1e-7f : 1e-7f;
    return sinf(x) / c;
}
// 2^x ueber den Exponenten im Bitmuster plus Polynom fuer den Rest.
static inline float exp2f(float x) {
    if (x < -126.0f) return 0.0f;
    if (x > 127.0f) x = 127.0f;
    float fi = floorf(x);
    float f = x - fi;
    float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f +
              f * (0.00961813f + f * 0.00133336f))));
    union { float f; uint32_t u; } s;
    s.u = (uint32_t)((int)fi + 127) << 23;
    return p * s.f;
}
static inline float log2f(float x) {
    if (x <= 0.0f) return -1e30f;
    union { float f; uint32_t u; } v; v.f = x;
    int e = (int)((v.u >> 23) & 0xff) - 127;
    v.u = (v.u & 0x007fffffu) | 0x3f800000u;     // Mantisse auf 1..2
    float m = v.f - 1.0f;
    float p = m * (1.4426950f + m * (-0.7213475f + m * (0.4808983f +
              m * (-0.3606738f + m * (0.2885390f + m * (-0.2110107f + m * 0.0864279f))))));
    return (float)e + p;
}
static inline float logf(float x)   { return log2f(x) * 0.69314718f; }
static inline float log10f(float x) { return log2f(x) * 0.30102999f; }
static inline float expf(float x)   { return exp2f(x * 1.44269504f); }
static inline float powf(float a, float b) {
    if (a <= 0.0f) return 0.0f;
    return exp2f(b * log2f(a));
}
static inline float tanhf(float x) {
    if (x > 9.0f) return 1.0f;
    if (x < -9.0f) return -1.0f;
    float e = expf(2.0f * x);
    return (e - 1.0f) / (e + 1.0f);
}

static inline float atanf(float x) {
    const float a = fabsf(x);
    float t = a > 1.0f ? 1.0f / a : a;
    const bool rotate = t > 0.414213562f;
    if (rotate) t = (t - 1.0f) / (t + 1.0f);
    const float z = t * t;
    float angle = t * (1.0f + z * (-1.0f / 3.0f + z * (1.0f / 5.0f
                    + z * (-1.0f / 7.0f + z * (1.0f / 9.0f - z / 11.0f)))));
    if (rotate) angle += SC_PI * 0.25f;
    if (a > 1.0f) angle = SC_PI * 0.5f - angle;
    return x < 0.0f ? -angle : angle;
}

// snprintf kennt genau die Formate, die der Kern benutzt: %d %s %.Nf %+d
int snprintf(char* out, size_t size, const char* fmt, ...);

#ifdef __cplusplus
}
#endif
#endif
