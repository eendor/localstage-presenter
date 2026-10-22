#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <ftw.h>
#include <strings.h>
#include <sys/random.h>
#include <unistd.h>
#endif

static void die_oom(void) {
    fputs("Out of memory\n", stderr);
    abort();
}

void buf_init(Buf *b) { b->d = NULL; b->len = b->cap = 0; }

void buf_free(Buf *b) { free(b->d); buf_init(b); }

void buf_reserve(Buf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra + 1) cap *= 2;
    char *d = realloc(b->d, cap);
    if (!d) die_oom();
    b->d = d;
    b->cap = cap;
}

void buf_add(Buf *b, const void *data, size_t n) {
    buf_reserve(b, n);
    memcpy(b->d + b->len, data, n);
    b->len += n;
    b->d[b->len] = 0;
}

void buf_puts(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }

void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list cp;
    va_copy(cp, ap);
    int n = vsnprintf(NULL, 0, fmt, cp);
    va_end(cp);
    if (n > 0) {
        buf_reserve(b, (size_t)n);
        vsnprintf(b->d + b->len, (size_t)n + 1, fmt, ap);
        b->len += (size_t)n;
    }
    va_end(ap);
}

void buf_consume(Buf *b, size_t n) {
    if (n >= b->len) { b->len = 0; if (b->d) b->d[0] = 0; return; }
    memmove(b->d, b->d + n, b->len - n);
    b->len -= n;
    b->d[b->len] = 0;
}

void buf_json_str(Buf *b, const char *s) {
    buf_add(b, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"': buf_add(b, "\\\"", 2); break;
        case '\\': buf_add(b, "\\\\", 2); break;
        case '\n': buf_add(b, "\\n", 2); break;
        case '\r': buf_add(b, "\\r", 2); break;
        case '\t': buf_add(b, "\\t", 2); break;
        default:
            if (*p < 0x20) buf_printf(b, "\\u%04x", *p);
            else buf_add(b, p, 1);
        }
    }
    buf_add(b, "\"", 1);
}

#ifdef _WIN32
void random_bytes(void *out, size_t n) {
    if (BCryptGenRandom(NULL, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        fputs("System random number generator failed\n", stderr);
        abort();
    }
}
#elif defined(__APPLE__)
void random_bytes(void *out, size_t n) {
    unsigned char *p = out;
    while (n) {
        size_t k = n > 256 ? 256 : n; /* getentropy limit */
        if (getentropy(p, k) != 0) { perror("getentropy"); abort(); }
        p += k;
        n -= k;
    }
}
#else
void random_bytes(void *out, size_t n) {
    unsigned char *p = out;
    while (n) {
        ssize_t r = getrandom(p, n, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("getrandom");
            abort();
        }
        p += r;
        n -= (size_t)r;
    }
}
#endif

void random_hex(char *out, size_t bytes) {
    static const char hex[] = "0123456789abcdef";
    unsigned char tmp[64];
    if (bytes > sizeof tmp) bytes = sizeof tmp;
    random_bytes(tmp, bytes);
    for (size_t i = 0; i < bytes; i++) {
        out[2 * i] = hex[tmp[i] >> 4];
        out[2 * i + 1] = hex[tmp[i] & 15];
    }
    out[2 * bytes] = 0;
}

void random_uuid(char out[37]) {
    unsigned char u[16];
    random_bytes(u, 16);
    u[6] = (u[6] & 0x0f) | 0x40;
    u[8] = (u[8] & 0x3f) | 0x80;
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
}

/* --- SHA-1 (only used for the WebSocket handshake, as RFC 6455 requires) --- */
#define ROL(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
static void sha1_block(uint32_t h[5], const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1(const void *data, size_t n, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    const uint8_t *p = data;
    size_t i = 0;
    for (; i + 64 <= n; i += 64) sha1_block(h, p + i);
    uint8_t tail[128] = {0};
    size_t rem = n - i;
    memcpy(tail, p + i, rem);
    tail[rem] = 0x80;
    size_t total = rem + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8;
    for (int j = 0; j < 8; j++) tail[total - 1 - j] = (uint8_t)(bits >> (8 * j));
    sha1_block(h, tail);
    if (total == 128) sha1_block(h, tail + 64);
    for (int j = 0; j < 5; j++) {
        out[4 * j] = (uint8_t)(h[j] >> 24);
        out[4 * j + 1] = (uint8_t)(h[j] >> 16);
        out[4 * j + 2] = (uint8_t)(h[j] >> 8);
        out[4 * j + 3] = (uint8_t)h[j];
    }
}

void base64_encode(const uint8_t *in, size_t n, char *out) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = t[v >> 18 & 63];
        out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}

int secure_streq(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = la != lb;
    size_t n = la > lb ? la : lb;
    for (size_t i = 0; i < n; i++) diff |= (unsigned char)((i < la ? a[i] : 0) ^ (i < lb ? b[i] : 0));
    return diff == 0;
}

#ifdef _WIN32
int64_t now_wall_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime; /* 100 ns since 1601 */
    return (int64_t)(t / 10000 - 11644473600000ULL);
}

int64_t now_mono_ms(void) { return (int64_t)GetTickCount64(); }

int str_starts(const char *s, const char *prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

int str_ieq(const char *a, const char *b) { return _stricmp(a, b) == 0; }

int remove_tree(const char *path) {
    if (!path || !*path) return 0;
    char pattern[MAX_PATH + 4];
    snprintf(pattern, sizeof pattern, "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    int rc = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            char child[MAX_PATH + 4];
            snprintf(child, sizeof child, "%s\\%s", path, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) rc |= remove_tree(child);
            else {
                SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileA(child) && !RemoveDirectoryA(child)) rc = -1;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (!RemoveDirectoryA(path)) rc = -1;
    return rc;
}
#else
int64_t now_wall_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t now_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int str_starts(const char *s, const char *prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

int str_ieq(const char *a, const char *b) { return strcasecmp(a, b) == 0; }

static int remove_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
    (void)sb; (void)flag; (void)ftw;
    return remove(path) ? -1 : 0;
}

int remove_tree(const char *path) {
    if (!path || !*path) return 0;
    return nftw(path, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}
#endif
