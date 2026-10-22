/* HTTP/1.1 request handling: one request per connection, no keep-alive. */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qr.h"
#include "server.h"

static const char *reason(int status) {
    switch (status) {
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 204: return "No Content";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 421: return "Misdirected Request";
    case 426: return "Upgrade Required";
    case 431: return "Request Header Fields Too Large";
    case 501: return "Not Implemented";
    default: return status >= 500 ? "Internal Server Error" : "Error";
    }
}

void http_send(Conn *c, int status, const char *ctype, const void *body, size_t n, const char *extra) {
    if (c->close_after_write || c->dead) return;
    buf_printf(&c->out,
               "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n"
               "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nX-Frame-Options: DENY\r\n%s\r\n",
               status, reason(status), ctype, n, extra ? extra : "");
    if (n && strcmp(c->method, "HEAD")) buf_add(&c->out, body, n);
    c->close_after_write = 1;
    c->st = CS_DRAIN; /* any unread request body is discarded by the lingering close */
}

void http_json(Conn *c, int status, const char *json) {
    http_send(c, status, "application/json; charset=utf-8", json, strlen(json), "Cache-Control: no-store\r\n");
}

void http_error(Conn *c, int status, const char *message) {
    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"error\":");
    buf_json_str(&b, message);
    buf_puts(&b, "}");
    http_json(c, status, b.d);
    buf_free(&b);
}

static void status_only(Conn *c, int status) {
    const char *r = reason(status);
    http_send(c, status, "text/plain; charset=utf-8", r, strlen(r), NULL);
}

const char *bearer(Conn *c) {
    return str_starts(c->auth, "Bearer ") ? c->auth + 7 : "";
}

int conn_is_host(Conn *c) {
    return c->from_loopback && app.host_token[0] && secure_streq(bearer(c), app.host_token);
}

/* Host must name this server, which blocks DNS-rebinding pages from reaching the API. */
static int host_allowed(Conn *c) {
    char want[128];
    const char *names[] = {"127.0.0.1", "localhost", c->lan ? app.selected : NULL};
    for (int i = 0; i < 3; i++) {
        if (!names[i] || !names[i][0]) continue;
        snprintf(want, sizeof want, "%s:%d", names[i], app.port);
        if (str_ieq(c->host, want)) return 1;
        if (app.port == 80 && str_ieq(c->host, names[i])) return 1;
    }
    return 0;
}

static int same_origin(Conn *c) {
    char want[300];
    snprintf(want, sizeof want, "http://%s", c->host);
    return str_ieq(c->origin, want);
}

static int hexval(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = tolower(ch);
    return ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}

/* Decode a query parameter (application/x-www-form-urlencoded). */
static int query_value(const char *query, const char *key, char *out, size_t n) {
    size_t kl = strlen(key);
    for (const char *p = query; *p;) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) > kl && !strncmp(p, key, kl) && p[kl] == '=') {
            size_t o = 0;
            for (const char *q = p + kl + 1; q < end; q++) {
                int ch = (unsigned char)*q;
                if (ch == '+') ch = ' ';
                else if (ch == '%') {
                    if (end - q < 3 || hexval(q[1]) < 0 || hexval(q[2]) < 0) return 0;
                    ch = hexval(q[1]) << 4 | hexval(q[2]);
                    q += 2;
                }
                if (o + 1 >= n || ch == 0) return 0;
                out[o++] = (char)ch;
            }
            out[o] = 0;
            return 1;
        }
        p = *end ? end + 1 : end;
    }
    return 0;
}

void net_info_json(Buf *b) {
    Iface list[32];
    int n = net_list(list, 32);
    buf_puts(b, "{\"interfaces\":[");
    for (int i = 0; i < n; i++) {
        buf_puts(b, i ? ",{\"name\":" : "{\"name\":");
        buf_json_str(b, list[i].name);
        buf_puts(b, ",\"address\":");
        buf_json_str(b, list[i].address);
        buf_puts(b, "}");
    }
    buf_puts(b, "],\"selectedAddress\":");
    if (app.selected[0]) buf_json_str(b, app.selected);
    else buf_puts(b, "null");
    buf_printf(b, ",\"port\":%d}", app.port);
}

/* ---------- routes ---------- */

static void route_json_post(Conn *c, const char *body, size_t n) {
    JVal *j = json_parse(body, n);
    if (!strcmp(c->path, "/api/host/auth")) {
        if (!c->from_loopback || (c->origin[0] && !same_origin(c))) { status_only(c, 403); goto out; }
        const char *secret = j ? json_str(j, "secret") : NULL;
        if (!secret || !app.bootstrap[0] || now_mono_ms() > app.bootstrap_expires || !secure_streq(secret, app.bootstrap)) {
            http_error(c, 403, "Invalid or expired launcher link. Restart the server for a new link.");
            goto out;
        }
        memset(app.bootstrap, 0, sizeof app.bootstrap);
        random_hex(app.host_token, 32);
        char resp[100];
        snprintf(resp, sizeof resp, "{\"token\":\"%s\"}", app.host_token);
        http_json(c, 200, resp);
    } else { /* /api/network/select */
        if (!conn_is_host(c)) { status_only(c, 403); goto out; }
        const char *address = j ? json_str(j, "address") : NULL;
        if (!address || strlen(address) >= sizeof app.selected || !net_has(address)) { http_error(c, 400, "Adapter is unavailable"); goto out; }
        if (strcmp(address, app.selected)) {
            sig_end();
            if (lan_bind(address) != 0) { http_error(c, 500, "Could not listen on that adapter"); goto out; }
        }
        Buf b;
        buf_init(&b);
        buf_printf(&b, "{\"selectedAddress\":");
        if (app.selected[0]) buf_json_str(&b, app.selected);
        else buf_puts(&b, "null");
        buf_printf(&b, ",\"port\":%d}", app.port);
        http_json(c, 200, b.d);
        buf_free(&b);
    }
out:
    json_free(j);
}

static void route(Conn *c) {
    int get = !strcmp(c->method, "GET") || !strcmp(c->method, "HEAD");
    int post = !strcmp(c->method, "POST");
    if (!host_allowed(c)) { status_only(c, 421); return; }
    if (c->te[0]) { http_error(c, 501, "Chunked uploads are not supported"); return; }

    if (!strcmp(c->path, "/ws")) {
        if (!get || !str_ieq(c->upgrade, "websocket")) { status_only(c, 426); return; }
        if (!c->origin[0] || !same_origin(c) || !ws_handshake(c)) { status_only(c, 403); return; }
        return;
    }
    if (str_starts(c->path, "/api/")) {
        if (get && !strcmp(c->path, "/api/network/interfaces")) {
            Buf b;
            buf_init(&b);
            net_info_json(&b);
            http_json(c, 200, b.d);
            buf_free(&b);
            return;
        }
        if (get && !strcmp(c->path, "/api/session")) {
            if (!app.session.active) { http_error(c, 404, "No active session"); return; }
            Buf b;
            buf_init(&b);
            sig_session_json(&b);
            http_json(c, 200, b.d);
            buf_free(&b);
            return;
        }
        if (get && !strcmp(c->path, "/api/qr")) {
            char text[512];
            Buf b;
            buf_init(&b);
            if (!query_value(c->query, "text", text, sizeof text) || !qr_svg(text, &b)) { buf_free(&b); http_error(c, 400, "Text too long for a QR code"); return; }
            http_send(c, 200, "image/svg+xml", b.d, b.len, "Cache-Control: no-store\r\n");
            buf_free(&b);
            return;
        }
        if (get && !strcmp(c->path, "/api/shared-deck")) {
            /* Android app: a PDF shared to LocalStage waits here until the presenter page takes it. */
            if (!conn_is_host(c)) { status_only(c, 403); return; }
            FILE *f = ls_shared_path[0] ? fopen(ls_shared_path, "rb") : NULL;
            long size = -1;
            if (f && fseek(f, 0, SEEK_END) == 0) size = ftell(f);
            if (!f || size <= 0 || fseek(f, 0, SEEK_SET) != 0) { if (f) fclose(f); http_send(c, 204, "text/plain", "", 0, "Cache-Control: no-store\r\n"); return; }
            char *data = malloc((size_t)size);
            size_t got = data ? fread(data, 1, (size_t)size, f) : 0;
            fclose(f);
            remove(ls_shared_path);
            if (got != (size_t)size) { free(data); http_error(c, 500, "Could not read the shared file"); return; }
            http_send(c, 200, "application/pdf", data, got, "Cache-Control: no-store\r\n");
            free(data);
            return;
        }
        if (post && !strcmp(c->path, "/api/decks")) {
            if (!conn_is_host(c)) { status_only(c, 403); return; }
            decks_begin_upload(c);
            if (c->st == CS_UPLOAD && c->content_length == 0) decks_upload_done(c);
            return;
        }
        if (get && str_starts(c->path, "/api/decks/")) {
            char id[64], slide[32], tail[8];
            if (sscanf(c->path, "/api/decks/%63[^/]/slides/%31[^/]%7s", id, slide, tail) == 2) { decks_serve_slide(c, id, slide); return; }
        }
        if (post && (!strcmp(c->path, "/api/host/auth") || !strcmp(c->path, "/api/network/select"))) {
            if (c->content_length < 0) c->content_length = 0;
            if (c->content_length > MAX_JSON_BODY) { http_error(c, 413, "Request body too large"); return; }
            c->st = CS_BODY;
            return;
        }
        http_error(c, 404, "Unknown API endpoint");
        return;
    }
    if (!get) { status_only(c, 405); return; }
    if (!strcmp(c->path, "/vendor/pdf.min.js") || !strcmp(c->path, "/vendor/pdf.worker.min.js")) {
        int worker = c->path[12] == 'w';
        http_send(c, 200, "text/javascript", worker ? web_pdfworker : web_pdfjs, worker ? web_pdfworker_len : web_pdfjs_len,
                  "Cache-Control: public, max-age=86400\r\n");
        return;
    }
    http_send(c, 200, "text/html; charset=utf-8", web_index, web_index_len,
              "Cache-Control: no-cache\r\nContent-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; worker-src 'self'; style-src 'unsafe-inline'; "
              "img-src 'self' blob:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n");
}

/* ---------- parsing ---------- */

static void header(Conn *c, const char *name, const char *value) {
#define CAP(field) snprintf(c->field, sizeof c->field, "%s", value)
    if (str_ieq(name, "host")) CAP(host);
    else if (str_ieq(name, "origin")) CAP(origin);
    else if (str_ieq(name, "authorization")) CAP(auth);
    else if (str_ieq(name, "content-type")) CAP(ctype);
    else if (str_ieq(name, "upgrade")) CAP(upgrade);
    else if (str_ieq(name, "sec-websocket-key")) CAP(wskey);
    else if (str_ieq(name, "sec-websocket-version")) CAP(wsver);
    else if (str_ieq(name, "transfer-encoding")) CAP(te);
    else if (str_ieq(name, "content-length")) {
        char *end;
        long long v = strtoll(value, &end, 10);
        c->content_length = (*value && !*end && v >= 0 && isdigit((unsigned char)*value)) ? v : -2;
    }
#undef CAP
}

static int parse_head(Conn *c, char *head) {
    char *line = head, *next = strstr(line, "\r\n");
    if (!next) return 0;
    *next = 0;
    char target[2048], version[16];
    if (sscanf(line, "%7s %2047s %15s", c->method, target, version) != 3 || strncmp(version, "HTTP/1.", 7)) return 0;
    char *q = strchr(target, '?');
    if (q) *q++ = 0;
    if (strlen(target) >= sizeof c->path || (q && strlen(q) >= sizeof c->query) || target[0] != '/') return 0;
    snprintf(c->path, sizeof c->path, "%s", target);
    snprintf(c->query, sizeof c->query, "%s", q ? q : "");
    int hosts = 0;
    for (line = next + 2; *line; line = next + 2) {
        next = strstr(line, "\r\n");
        if (!next) return 0;
        *next = 0;
        if (!*line) break;
        char *colon = strchr(line, ':');
        if (!colon || colon == line) return 0;
        *colon = 0;
        char *v = colon + 1;
        while (*v == ' ' || *v == '\t') v++;
        size_t vl = strlen(v);
        while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t')) v[--vl] = 0;
        if (str_ieq(line, "host")) hosts++;
        header(c, line, v);
    }
    return hosts == 1 && c->content_length != -2;
}

static char *find_crlf2(char *d, size_t n) {
    for (size_t i = 0; i + 4 <= n; i++)
        if (d[i] == '\r' && !memcmp(d + i, "\r\n\r\n", 4)) return d + i;
    return NULL;
}

void http_on_readable(Conn *c) {
    if (c->st == CS_HEAD) {
        char *end = find_crlf2(c->in.d, c->in.len);
        if (!end) {
            if (c->in.len > MAX_HEADER_BYTES) status_only(c, 431);
            return;
        }
        size_t hl = (size_t)(end - c->in.d) + 4;
        if (hl > MAX_HEADER_BYTES) { status_only(c, 431); return; }
        char *head = malloc(hl + 1);
        memcpy(head, c->in.d, hl);
        head[hl] = 0;
        buf_consume(&c->in, hl);
        c->content_length = -1;
        int ok = memchr(head, 0, hl) == NULL && parse_head(c, head);
        free(head);
        if (!ok) { snprintf(c->method, sizeof c->method, "GET"); status_only(c, 400); return; }
        route(c);
        if (c->st == CS_WS && c->in.len) ws_on_data(c);
    }
    if (c->st == CS_BODY) {
        if ((long long)c->in.len < c->content_length) return;
        route_json_post(c, c->in.d ? c->in.d : "", (size_t)c->content_length);
        return;
    }
    if (c->st == CS_UPLOAD) {
        size_t take = c->in.len;
        if ((long long)take > c->content_length - c->received) take = (size_t)(c->content_length - c->received);
        if (take) decks_upload_data(c, c->in.d, take);
        buf_consume(&c->in, take);
        c->received += (long long)take;
        if (c->st == CS_UPLOAD && c->received == c->content_length) decks_upload_done(c);
        return;
    }
    if (c->st == CS_WS) { ws_on_data(c); return; }
    if (c->st == CS_DRAIN || c->st == CS_WAIT) c->in.len = 0; /* discard */
}
