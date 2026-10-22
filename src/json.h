/* Minimal strict JSON parser for untrusted signaling and API payloads. */
#ifndef LS_JSON_H
#define LS_JSON_H

#include <stddef.h>

typedef enum { J_NULL, J_FALSE, J_TRUE, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal {
    JType type;
    double num;
    char *str;        /* J_STR: UTF-8, NUL-terminated (embedded NULs rejected) */
    size_t u16len;    /* J_STR: length in UTF-16 code units, like JavaScript */
    int n;            /* number of children */
    char **keys;      /* J_OBJ keys */
    struct JVal **kids;
} JVal;

/* Parse n bytes. Returns NULL on any syntax error or if nesting exceeds 32 levels. */
JVal *json_parse(const char *text, size_t n);
void json_free(JVal *v);

/* Object lookup (last duplicate wins, as in JSON.parse). NULL when missing. */
const JVal *json_get(const JVal *obj, const char *key);
/* Helpers: return 1 when the key exists with the given type. */
const char *json_str(const JVal *obj, const char *key);       /* NULL unless string */
int json_is_safe_int(const JVal *v);
int json_bool(const JVal *obj, const char *key, int *out);    /* 1 if boolean */

#endif
