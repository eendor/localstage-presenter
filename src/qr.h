/* QR Code (model 2) encoder: byte mode, error correction level M, versions 1-10. */
#ifndef LS_QR_H
#define LS_QR_H

#include "util.h"

#define QR_MAX_SIZE 57 /* version 10 */

typedef struct {
    int size;
    unsigned char m[QR_MAX_SIZE][QR_MAX_SIZE]; /* [y][x], 1 = dark */
} QrCode;

/* Encode text; mask = -1 picks the lowest-penalty mask. Returns 0 if text is too long (> 216 bytes). */
int qr_encode(const char *text, int mask, QrCode *out);
/* Append an SVG rendering of text to b. Returns 0 if it does not fit. */
int qr_svg(const char *text, Buf *b);

#endif
