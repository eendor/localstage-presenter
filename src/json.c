#include "json.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p, *end;
    int depth;
} Parser;

static void ws(Parser *ps) {
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++;
}

static JVal *newval(JType t) {
    JVal *v = calloc(1, sizeof *v);
    if (v) v->type = t;
    return v;
}

void json_free(JVal *v) {
    if (!v) return;
    free(v->str);
    for (int i = 0; i < v->n; i++) {
        if (v->keys) free(v->keys[i]);
        json_free(v->kids[i]);
    }
    free(v->keys);
    free(v->kids);
    free(v);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int read_u4(Parser *ps, unsigned *out) {
    if (ps->end - ps->p < 4) return 0;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        int h = hexval(ps->p[i]);
        if (h < 0) return 0;
        v = v << 4 | (unsigned)h;
    }
    ps->p += 4;
    *out = v;
    return 1;
}

static void put_utf8(char *o, size_t *k, unsigned cp) {
    if (cp < 0x80) o[(*k)++] = (char)cp;
    else if (cp < 0x800) { o[(*k)++] = (char)(0xC0 | cp >> 6); o[(*k)++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o[(*k)++] = (char)(0xE0 | cp >> 12); o[(*k)++] = (char)(0x80 | (cp >> 6 & 63)); o[(*k)++] = (char)(0x80 | (cp & 63)); }
    else { o[(*k)++] = (char)(0xF0 | cp >> 18); o[(*k)++] = (char)(0x80 | (cp >> 12 & 63)); o[(*k)++] = (char)(0x80 | (cp >> 6 & 63)); o[(*k)++] = (char)(0x80 | (cp & 63)); }
}

/* Parses a string literal; returns malloc'd UTF-8 and its UTF-16 length. */
static char *parse_string(Parser *ps, size_t *u16) {
    if (ps->p >= ps->end || *ps->p != '"') return NULL;
    ps->p++;
    const char *start = ps->p;
    size_t maxlen = 0;
    while (ps->p < ps->end && *ps->p != '"') {
        if (*ps->p == '\\') ps->p++;
        ps->p++;
        maxlen++;
    }
    if (ps->p >= ps->end) return NULL;
    ps->p = start;
    char *out = malloc(maxlen * 4 + 1);
    if (!out) return NULL;
    size_t k = 0, units = 0;
    while (*ps->p != '"') {
        unsigned char c = (unsigned char)*ps->p;
        if (c < 0x20) goto fail;
        if (c == '\\') {
            ps->p++;
            char e = *ps->p++;
            unsigned cp;
            switch (e) {
            case '"': out[k++] = '"'; units++; break;
            case '\\': out[k++] = '\\'; units++; break;
            case '/': out[k++] = '/'; units++; break;
            case 'b': out[k++] = '\b'; units++; break;
            case 'f': out[k++] = '\f'; units++; break;
            case 'n': out[k++] = '\n'; units++; break;
            case 'r': out[k++] = '\r'; units++; break;
            case 't': out[k++] = '\t'; units++; break;
            case 'u':
                if (!read_u4(ps, &cp)) goto fail;
                if (cp >= 0xD800 && cp <= 0xDBFF && ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') {
                    Parser look = *ps;
                    look.p += 2;
                    unsigned lo;
                    if (read_u4(&look, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        *ps = look;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        units++;
                    }
                }
                if (cp == 0) goto fail; /* NUL would truncate C strings */
                if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD; /* lone surrogate */
                put_utf8(out, &k, cp);
                units++;
                break;
            default: goto fail;
            }
        } else {
            /* Copy one UTF-8 sequence, counting UTF-16 units. */
            int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
            if (!len || ps->end - ps->p < len) goto fail;
            for (int i = 1; i < len; i++) if (((unsigned char)ps->p[i] & 0xC0) != 0x80) goto fail;
            memcpy(out + k, ps->p, (size_t)len);
            k += (size_t)len;
            ps->p += len;
            units += len == 4 ? 2 : 1;
        }
    }
    ps->p++;
    out[k] = 0;
    *u16 = units;
    return out;
fail:
    free(out);
    return NULL;
}

static JVal *parse_value(Parser *ps);

static int push(JVal *v, char *key, JVal *kid) {
    if ((v->n & (v->n - 1)) == 0) { /* grow at powers of two */
        int cap = v->n ? v->n * 2 : 1;
        JVal **kids = realloc(v->kids, sizeof *kids * (size_t)cap);
        if (!kids) return 0;
        v->kids = kids;
        if (v->type == J_OBJ) {
            char **keys = realloc(v->keys, sizeof *keys * (size_t)cap);
            if (!keys) return 0;
            v->keys = keys;
        }
    }
    if (v->type == J_OBJ) v->keys[v->n] = key;
    v->kids[v->n++] = kid;
    return 1;
}

static JVal *parse_container(Parser *ps, int obj) {
    if (++ps->depth > 32) return NULL;
    JVal *v = newval(obj ? J_OBJ : J_ARR);
    if (!v) return NULL;
    ps->p++;
    ws(ps);
    if (ps->p < ps->end && *ps->p == (obj ? '}' : ']')) { ps->p++; ps->depth--; return v; }
    for (;;) {
        char *key = NULL;
        ws(ps);
        if (obj) {
            size_t u;
            key = parse_string(ps, &u);
            if (!key) goto fail;
            ws(ps);
            if (ps->p >= ps->end || *ps->p != ':') { free(key); goto fail; }
            ps->p++;
        }
        JVal *kid = parse_value(ps);
        if (!kid || !push(v, key, kid)) { free(key); json_free(kid); goto fail; }
        ws(ps);
        if (ps->p >= ps->end) goto fail;
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == (obj ? '}' : ']')) { ps->p++; break; }
        goto fail;
    }
    ps->depth--;
    return v;
fail:
    json_free(v);
    return NULL;
}

static JVal *parse_number(Parser *ps) {
    const char *s = ps->p;
    if (ps->p < ps->end && *ps->p == '-') ps->p++;
    if (ps->p >= ps->end) return NULL;
    if (*ps->p == '0') ps->p++;
    else if (*ps->p >= '1' && *ps->p <= '9') while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    else return NULL;
    if (ps->p < ps->end && *ps->p == '.') {
        ps->p++;
        if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') return NULL;
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    }
    if (ps->p < ps->end && (*ps->p == 'e' || *ps->p == 'E')) {
        ps->p++;
        if (ps->p < ps->end && (*ps->p == '+' || *ps->p == '-')) ps->p++;
        if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') return NULL;
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    }
    char tmp[64];
    size_t len = (size_t)(ps->p - s);
    if (len >= sizeof tmp) return NULL;
    memcpy(tmp, s, len);
    tmp[len] = 0;
    JVal *v = newval(J_NUM);
    if (v) v->num = strtod(tmp, NULL);
    return v;
}

static JVal *parse_value(Parser *ps) {
    ws(ps);
    if (ps->p >= ps->end) return NULL;
    char c = *ps->p;
    if (c == '{') return parse_container(ps, 1);
    if (c == '[') return parse_container(ps, 0);
    if (c == '"') {
        size_t u;
        char *s = parse_string(ps, &u);
        if (!s) return NULL;
        JVal *v = newval(J_STR);
        if (!v) { free(s); return NULL; }
        v->str = s;
        v->u16len = u;
        return v;
    }
    struct { const char *word; JType t; } lits[] = {{"null", J_NULL}, {"true", J_TRUE}, {"false", J_FALSE}};
    for (int i = 0; i < 3; i++) {
        size_t l = strlen(lits[i].word);
        if ((size_t)(ps->end - ps->p) >= l && memcmp(ps->p, lits[i].word, l) == 0) {
            ps->p += l;
            return newval(lits[i].t);
        }
    }
    return parse_number(ps);
}

JVal *json_parse(const char *text, size_t n) {
    Parser ps = {text, text + n, 0};
    JVal *v = parse_value(&ps);
    if (!v) return NULL;
    ws(&ps);
    if (ps.p != ps.end) { json_free(v); return NULL; }
    return v;
}

const JVal *json_get(const JVal *obj, const char *key) {
    if (!obj || obj->type != J_OBJ) return NULL;
    for (int i = obj->n - 1; i >= 0; i--)
        if (strcmp(obj->keys[i], key) == 0) return obj->kids[i];
    return NULL;
}

const char *json_str(const JVal *obj, const char *key) {
    const JVal *v = json_get(obj, key);
    return v && v->type == J_STR ? v->str : NULL;
}

int json_is_safe_int(const JVal *v) {
    return v && v->type == J_NUM && isfinite(v->num) && floor(v->num) == v->num && fabs(v->num) <= 9007199254740991.0;
}

int json_bool(const JVal *obj, const char *key, int *out) {
    const JVal *v = json_get(obj, key);
    if (!v || (v->type != J_TRUE && v->type != J_FALSE)) return 0;
    *out = v->type == J_TRUE;
    return 1;
}
