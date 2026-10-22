#include "qr.h"

#include <stdlib.h>
#include <string.h>

/* Error correction level M, indexed by version. */
static const int ECC_PER_BLOCK[11] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
static const int NUM_BLOCKS[11] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};
#define FORMAT_ECC_M 0

typedef struct {
    int size;
    unsigned char m[QR_MAX_SIZE][QR_MAX_SIZE];
    unsigned char fn[QR_MAX_SIZE][QR_MAX_SIZE]; /* function-pattern modules */
} Grid;

static int raw_modules(int ver) {
    int r = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int na = ver / 7 + 2;
        r -= (25 * na - 10) * na - 55;
        if (ver >= 7) r -= 36;
    }
    return r;
}

static int data_codewords(int ver) { return raw_modules(ver) / 8 - ECC_PER_BLOCK[ver] * NUM_BLOCKS[ver]; }

static void setf(Grid *g, int x, int y, int dark) {
    g->m[y][x] = (unsigned char)dark;
    g->fn[y][x] = 1;
}

static unsigned char gf_mul(unsigned char x, unsigned char y) {
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (unsigned char)z;
}

static void rs_divisor(int degree, unsigned char *out) {
    memset(out, 0, (size_t)degree);
    out[degree - 1] = 1;
    unsigned char root = 1;
    for (int i = 0; i < degree; i++) {
        for (int j = 0; j < degree; j++) {
            out[j] = gf_mul(out[j], root);
            if (j + 1 < degree) out[j] ^= out[j + 1];
        }
        root = gf_mul(root, 2);
    }
}

static void rs_remainder(const unsigned char *data, int len, const unsigned char *div, int degree, unsigned char *out) {
    memset(out, 0, (size_t)degree);
    for (int i = 0; i < len; i++) {
        unsigned char factor = data[i] ^ out[0];
        memmove(out, out + 1, (size_t)degree - 1);
        out[degree - 1] = 0;
        for (int j = 0; j < degree; j++) out[j] ^= gf_mul(div[j], factor);
    }
}

static void draw_finder(Grid *g, int x, int y) {
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int ax = abs(dx), ay = abs(dy), d = ax > ay ? ax : ay;
            int xx = x + dx, yy = y + dy;
            if (xx >= 0 && xx < g->size && yy >= 0 && yy < g->size) setf(g, xx, yy, d != 2 && d != 4);
        }
}

static void draw_format(Grid *g, int mask) {
    int data = FORMAT_ECC_M << 3 | mask, rem = data;
    for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = (data << 10 | rem) ^ 0x5412;
    int s = g->size;
#define BIT(i) ((bits >> (i)) & 1)
    for (int i = 0; i <= 5; i++) setf(g, 8, i, BIT(i));
    setf(g, 8, 7, BIT(6));
    setf(g, 8, 8, BIT(7));
    setf(g, 7, 8, BIT(8));
    for (int i = 9; i < 15; i++) setf(g, 14 - i, 8, BIT(i));
    for (int i = 0; i < 8; i++) setf(g, s - 1 - i, 8, BIT(i));
    for (int i = 8; i < 15; i++) setf(g, 8, s - 15 + i, BIT(i));
    setf(g, 8, s - 8, 1);
#undef BIT
}

static void draw_function_patterns(Grid *g, int ver) {
    int s = g->size;
    for (int i = 0; i < s; i++) {
        setf(g, 6, i, i % 2 == 0);
        setf(g, i, 6, i % 2 == 0);
    }
    draw_finder(g, 3, 3);
    draw_finder(g, s - 4, 3);
    draw_finder(g, 3, s - 4);
    if (ver > 1) {
        int na = ver / 7 + 2, pos[8];
        int step = (ver * 4 + na * 2 + 1) / (na * 2 - 2) * 2;
        pos[0] = 6;
        for (int i = na - 1, p = ver * 4 + 10; i >= 1; i--, p -= step) pos[i] = p;
        for (int i = 0; i < na; i++)
            for (int j = 0; j < na; j++) {
                if ((i == 0 && j == 0) || (i == 0 && j == na - 1) || (i == na - 1 && j == 0)) continue;
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++) {
                        int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
                        setf(g, pos[i] + dx, pos[j] + dy, d != 1);
                    }
            }
    }
    draw_format(g, 0); /* reserve; redrawn with the real mask later */
    if (ver >= 7) {
        int rem = ver;
        for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        long bits = (long)ver << 12 | rem;
        for (int i = 0; i < 18; i++) {
            int bit = (bits >> i) & 1, a = s - 11 + i % 3, b = i / 3;
            setf(g, a, b, bit);
            setf(g, b, a, bit);
        }
    }
}

static int mask_bit(int mask, int x, int y) {
    switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

static void apply_mask(Grid *g, int mask) {
    for (int y = 0; y < g->size; y++)
        for (int x = 0; x < g->size; x++)
            if (!g->fn[y][x] && mask_bit(mask, x, y)) g->m[y][x] ^= 1;
}

/* Penalty rules N1 (runs), N2 (2x2 blocks) and N4 (balance). */
static long penalty(const Grid *g) {
    long p = 0;
    int s = g->size, dark = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int a = 0; a < s; a++) {
            int run = 0, prev = -1;
            for (int b = 0; b < s; b++) {
                int v = pass ? g->m[b][a] : g->m[a][b];
                if (v == prev) {
                    run++;
                    if (run == 5) p += 3;
                    else if (run > 5) p++;
                } else { run = 1; prev = v; }
            }
        }
    for (int y = 0; y + 1 < s; y++)
        for (int x = 0; x + 1 < s; x++) {
            int v = g->m[y][x];
            if (v == g->m[y][x + 1] && v == g->m[y + 1][x] && v == g->m[y + 1][x + 1]) p += 3;
        }
    for (int y = 0; y < s; y++)
        for (int x = 0; x < s; x++) dark += g->m[y][x];
    int total = s * s;
    int k = (abs(dark * 20 - total * 10) + total - 1) / total - 1;
    if (k > 0) p += (long)k * 10;
    return p;
}

int qr_encode(const char *text, int mask, QrCode *out) {
    size_t n = strlen(text);
    int ver;
    for (ver = 1; ver <= 10; ver++) {
        int count_bits = ver < 10 ? 8 : 16;
        if ((long)(4 + count_bits + 8 * n + 7) / 8 <= data_codewords(ver) && n < (1u << count_bits)) break;
    }
    if (ver > 10) return 0;
    int cap = data_codewords(ver), count_bits = ver < 10 ? 8 : 16;

    /* Bit stream: mode, length, bytes, terminator, padding. */
    unsigned char data[400] = {0};
    long bitlen = 0;
#define PUT(val, nb) do { for (int _i = (nb) - 1; _i >= 0; _i--, bitlen++) if (((val) >> _i) & 1) data[bitlen >> 3] |= (unsigned char)(0x80 >> (bitlen & 7)); } while (0)
    PUT(4, 4);
    PUT((long)n, count_bits);
    for (size_t i = 0; i < n; i++) PUT((unsigned char)text[i], 8);
    long capbits = (long)cap * 8;
    int term = capbits - bitlen < 4 ? (int)(capbits - bitlen) : 4;
    PUT(0, term);
    bitlen = (bitlen + 7) / 8 * 8;
    for (int pad = 0xEC; bitlen < capbits; pad ^= 0xEC ^ 0x11) PUT(pad, 8);
#undef PUT

    /* Split into blocks, add ECC, interleave. */
    int nb = NUM_BLOCKS[ver], ecl = ECC_PER_BLOCK[ver], raw = raw_modules(ver) / 8;
    int nshort = nb - raw % nb, shortlen = raw / nb;
    unsigned char blocks[8][200], div[32], ecc[32], final[400];
    rs_divisor(ecl, div);
    for (int i = 0, k = 0; i < nb; i++) {
        int dlen = shortlen - ecl + (i < nshort ? 0 : 1);
        memset(blocks[i], 0, sizeof blocks[i]);
        memcpy(blocks[i], data + k, (size_t)dlen);
        rs_remainder(data + k, dlen, div, ecl, ecc);
        k += dlen;
        int off = shortlen - ecl + 1; /* short blocks keep a placeholder slot */
        memcpy(blocks[i] + off, ecc, (size_t)ecl);
    }
    int fl = 0;
    for (int i = 0; i < shortlen + 1; i++)
        for (int j = 0; j < nb; j++)
            if (i != shortlen - ecl || j >= nshort) final[fl++] = blocks[j][i];

    Grid *g = calloc(1, sizeof *g);
    if (!g) return 0;
    g->size = ver * 4 + 17;
    draw_function_patterns(g, ver);

    /* Zig-zag placement. */
    long bit = 0, total = (long)fl * 8;
    for (int right = g->size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < g->size; vert++)
            for (int j = 0; j < 2; j++) {
                int x = right - j, up = ((right + 1) & 2) == 0;
                int y = up ? g->size - 1 - vert : vert;
                if (!g->fn[y][x] && bit < total) {
                    g->m[y][x] = (final[bit >> 3] >> (7 - (bit & 7))) & 1;
                    bit++;
                }
            }
    }

    if (mask < 0) {
        long best = -1;
        for (int i = 0; i < 8; i++) {
            apply_mask(g, i);
            draw_format(g, i);
            long pen = penalty(g);
            if (best < 0 || pen < best) { best = pen; mask = i; }
            apply_mask(g, i);
        }
    }
    apply_mask(g, mask);
    draw_format(g, mask);
    out->size = g->size;
    for (int y = 0; y < g->size; y++) memcpy(out->m[y], g->m[y], (size_t)g->size);
    free(g);
    return 1;
}

int qr_svg(const char *text, Buf *b) {
    QrCode q;
    if (!qr_encode(text, -1, &q)) return 0;
    int full = q.size + 8;
    buf_printf(b, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" shape-rendering=\"crispEdges\">"
                  "<rect width=\"100%%\" height=\"100%%\" fill=\"#fff\"/><path fill=\"#000\" d=\"", full, full);
    for (int y = 0; y < q.size; y++)
        for (int x = 0; x < q.size; x++)
            if (q.m[y][x]) buf_printf(b, "M%d %dh1v1h-1z", x + 4, y + 4);
    buf_puts(b, "\"/></svg>");
    return 1;
}
