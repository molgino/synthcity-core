// Umsetzung des libc-Ersatzes fuer den WebAssembly-Bau (siehe freestanding.h).
#include "freestanding.h"
#include <stdarg.h>

extern "C" {

static unsigned char* scHeap = 0;

void* malloc(size_t n) {
    if (!scHeap) scHeap = &__heap_base;
    unsigned char* p = (unsigned char*)(((size_t)scHeap + 15) & ~(size_t)15);
    scHeap = p + n;
    return p;
}
void free(void*) {}

void* memcpy(void* d, const void* s, size_t n) {
    unsigned char* a = (unsigned char*)d; const unsigned char* b = (const unsigned char*)s;
    for (size_t i = 0; i < n; i++) a[i] = b[i];
    return d;
}
void* memset(void* d, int v, size_t n) {
    unsigned char* a = (unsigned char*)d;
    for (size_t i = 0; i < n; i++) a[i] = (unsigned char)v;
    return d;
}
void* memmove(void* d, const void* s, size_t n) {
    unsigned char* a = (unsigned char*)d; const unsigned char* b = (const unsigned char*)s;
    if (a < b) for (size_t i = 0; i < n; i++) a[i] = b[i];
    else for (size_t i = n; i-- > 0;) a[i] = b[i];
    return d;
}
size_t strlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
char* strchr(const char* s, int c) {
    for (; *s; s++) if (*s == (char)c) return (char*)s;
    return c ? (char*)0 : (char*)s;
}
long strtol(const char* s, char** end, int) {
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    long v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    if (end) *end = (char*)s;
    return neg ? -v : v;
}

static int putStr(char* out, int n, int at, const char* s) {
    while (*s) { if (at < n - 1) out[at] = *s; at++; s++; }
    return at;
}
static int putLong(char* out, int n, int at, long v, int plus) {
    char tmp[24];
    int i = 0, neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    do { tmp[i++] = (char)('0' + (u % 10)); u /= 10; } while (u);
    if (neg) tmp[i++] = '-'; else if (plus) tmp[i++] = '+';
    while (i-- > 0) { if (at < n - 1) out[at] = tmp[i]; at++; }
    return at;
}
static int putFixed(char* out, int n, int at, double v, int dec, int plus) {
    if (v != v) return putStr(out, n, at, "nan");
    int neg = v < 0;
    if (neg) v = -v;
    double scale = 1.0;
    for (int i = 0; i < dec; i++) scale *= 10.0;
    long long whole = (long long)v;
    long long f = (long long)((v - (double)whole) * scale + 0.5);
    if ((double)f >= scale) { f = 0; whole++; }
    if (neg) at = putStr(out, n, at, "-");
    else if (plus) at = putStr(out, n, at, "+");
    at = putLong(out, n, at, (long)whole, 0);
    if (dec > 0) {
        at = putStr(out, n, at, ".");
        long long div = (long long)scale / 10;
        while (div > 0) { if (at < n - 1) out[at] = (char)('0' + (char)((f / div) % 10)); at++; div /= 10; }
    }
    return at;
}

int snprintf(char* out, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = (int)size, at = 0;
    for (const char* f = fmt; *f; f++) {
        if (*f != '%') { if (at < n - 1) out[at] = *f; at++; continue; }
        f++;
        int plus = 0, dec = -1;
        if (*f == '+') { plus = 1; f++; }
        if (*f == '.') { f++; dec = 0; while (*f >= '0' && *f <= '9') dec = dec * 10 + (*f++ - '0'); }
        switch (*f) {
        case 'd': case 'i': at = putLong(out, n, at, (long)va_arg(ap, int), plus); break;
        case 'u': at = putLong(out, n, at, (long)va_arg(ap, unsigned), 0); break;
        case 'f': at = putFixed(out, n, at, va_arg(ap, double), dec < 0 ? 6 : dec, plus); break;
        case 's': at = putStr(out, n, at, va_arg(ap, const char*)); break;
        case 'c': if (at < n - 1) out[at] = (char)va_arg(ap, int); at++; break;
        case '%': if (at < n - 1) out[at] = '%'; at++; break;
        default: break;
        }
    }
    va_end(ap);
    if (n > 0) out[at < n - 1 ? at : n - 1] = 0;
    return at;
}

}  // extern "C"

// C++-Laufzeit, soweit der Kern sie braucht: virtuelle Destruktoren und statische Objekte.
// Freigegeben wird nie - der Kern fordert ohnehin nur beim Start Speicher an.
void* operator new(size_t n) { return malloc(n); }
void* operator new[](size_t n) { return malloc(n); }
void  operator delete(void*) noexcept {}
void  operator delete[](void*) noexcept {}
void  operator delete(void*, size_t) noexcept {}
void  operator delete[](void*, size_t) noexcept {}
extern "C" int  __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
extern "C" void __cxa_pure_virtual() {}
