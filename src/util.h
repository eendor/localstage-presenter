/* LocalStage — small shared helpers: buffers, randomness, hashing, encoding. */
#ifndef LS_UTIL_H
#define LS_UTIL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *d;
    size_t len;
    size_t cap;
} Buf;

void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_reserve(Buf *b, size_t extra);
void buf_add(Buf *b, const void *data, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_printf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void buf_consume(Buf *b, size_t n);
/* Append s as a JSON string literal (with quotes). */
void buf_json_str(Buf *b, const char *s);

void random_bytes(void *out, size_t n);
void random_hex(char *out, size_t bytes); /* out must hold 2*bytes+1 */
void random_uuid(char out[37]);

void sha1(const void *data, size_t n, uint8_t out[20]);
void base64_encode(const uint8_t *in, size_t n, char *out); /* out: 4*ceil(n/3)+1 */

/* Constant-time comparison of two NUL-terminated strings. */
int secure_streq(const char *a, const char *b);

int64_t now_wall_ms(void);
int64_t now_mono_ms(void);

int str_starts(const char *s, const char *prefix);
int str_ieq(const char *a, const char *b);

/* Recursively delete a directory tree. Returns 0 on success. */
int remove_tree(const char *path);

#endif
