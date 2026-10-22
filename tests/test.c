/* End-to-end tests: start the server, then drive its HTTP API and WebSocket signaling. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/json.h"
#include "../src/qr.h"
#include "../src/util.h"

static int port, failures, checks;
static char host_hdr[64];

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int dial(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) { close(fd); return -1; }
    return fd;
}

static void write_all(int fd, const void *p, size_t n) {
    while (n) { ssize_t w = write(fd, p, n); if (w <= 0) return; p = (const char *)p + w; n -= (size_t)w; }
}

typedef struct { int status; Buf raw; char *body; } Resp;

/* Send a raw request and read until the server closes. */
static Resp raw(const char *req, const void *body, size_t n) {
    Resp r = {0};
    int fd = dial();
    if (fd < 0) return r;
    write_all(fd, req, strlen(req));
    if (body) write_all(fd, body, n);
    char tmp[65536];
    for (;;) {
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 150000) <= 0) break;
        ssize_t k = read(fd, tmp, sizeof tmp);
        if (k <= 0) break;
        buf_add(&r.raw, tmp, (size_t)k);
    }
    close(fd);
    buf_add(&r.raw, "", 1);
    sscanf(r.raw.d, "HTTP/1.1 %d", &r.status);
    char *e = strstr(r.raw.d, "\r\n\r\n");
    r.body = e ? e + 4 : r.raw.d + r.raw.len - 1;
    return r;
}

static Resp req(const char *method, const char *path, const char *extra, const char *body) {
    char head[4096];
    snprintf(head, sizeof head, "%s %s HTTP/1.1\r\nHost: %s\r\n%sContent-Length: %zu\r\n\r\n", method, path, host_hdr, extra ? extra : "", body ? strlen(body) : 0);
    return raw(head, body, body ? strlen(body) : 0);
}

/* ---- minimal WebSocket client ---- */
typedef struct { int fd; Buf in; } Ws;

static int ws_open(Ws *w) {
    memset(w, 0, sizeof *w);
    w->fd = dial();
    char head[512];
    snprintf(head, sizeof head, "GET /ws HTTP/1.1\r\nHost: %s\r\nOrigin: http://%s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", host_hdr, host_hdr);
    write_all(w->fd, head, strlen(head));
    char tmp[4096];
    for (;;) {
        struct pollfd p = {w->fd, POLLIN, 0};
        if (poll(&p, 1, 3000) <= 0) return 0;
        ssize_t k = read(w->fd, tmp, sizeof tmp);
        if (k <= 0) return 0;
        buf_add(&w->in, tmp, (size_t)k);
        char *e = memmem(w->in.d, w->in.len, "\r\n\r\n", 4);
        if (e) {
            int ok = !strncmp(w->in.d, "HTTP/1.1 101", 12) && memmem(w->in.d, w->in.len, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", 28);
            buf_consume(&w->in, (size_t)(e + 4 - w->in.d));
            return ok;
        }
    }
}

static void ws_send(Ws *w, const char *text) {
    size_t n = strlen(text);
    uint8_t h[14], mask[4] = {1, 2, 3, 4};
    size_t hl = 0;
    h[hl++] = 0x81;
    if (n < 126) h[hl++] = 0x80 | (uint8_t)n;
    else { h[hl++] = 0x80 | 126; h[hl++] = (uint8_t)(n >> 8); h[hl++] = (uint8_t)n; }
    memcpy(h + hl, mask, 4); hl += 4;
    char *p = malloc(hl + n);
    memcpy(p, h, hl);
    for (size_t i = 0; i < n; i++) p[hl + i] = (char)(text[i] ^ mask[i & 3]);
    write_all(w->fd, p, hl + n);
    free(p);
}

/* Next text message as parsed JSON (pings skipped). NULL on timeout/close. */
static JVal *ws_recv(Ws *w, int timeout_ms) {
    char tmp[65536];
    for (;;) {
        if (w->in.len >= 2) {
            uint8_t *d = (uint8_t *)w->in.d;
            size_t n = d[1] & 0x7f, off = 2;
            if (n == 126) { if (w->in.len < 4) goto more; n = ((size_t)d[2] << 8) | d[3]; off = 4; }
            if (w->in.len >= off + n) {
                int op = d[0] & 0x0f;
                JVal *v = op == 1 ? json_parse((char *)d + off, n) : NULL;
                buf_consume(&w->in, off + n);
                if (op == 8) return NULL;
                if (v) return v;
                continue;
            }
        }
    more:;
        struct pollfd p = {w->fd, POLLIN, 0};
        if (poll(&p, 1, timeout_ms) <= 0) return NULL;
        ssize_t k = read(w->fd, tmp, sizeof tmp);
        if (k <= 0) return NULL;
        buf_add(&w->in, tmp, (size_t)k);
    }
}

/* Wait for a message of the given type, discarding others. */
static JVal *ws_expect(Ws *w, const char *type) {
    for (;;) {
        JVal *v = ws_recv(w, 5000);
        if (!v) return NULL;
        const char *t = json_str(v, "type");
        if (t && !strcmp(t, type)) return v;
        json_free(v);
    }
}

static void ws_close(Ws *w) { close(w->fd); buf_free(&w->in); }

static const char *errmsg(JVal *v) { const char *m = v ? json_str(v, "message") : NULL; return m ? m : ""; }

/* ---- tests ---- */
static void test_qr(void) {
    QrCode q;
    CHECK(qr_encode("http://192.168.1.20:3000/join", -1, &q) && q.size == 29, "QR version 3 for a typical URL");
    char big[300]; memset(big, 'a', sizeof big - 1); big[299] = 0;
    CHECK(!qr_encode(big, -1, &q), "QR rejects overlong text");
}

static void test_json(void) {
    const char *sp = "{\"a\":\"\\ud83d\\ude00\",\"n\":1}";
    JVal *v = json_parse(sp, strlen(sp));
    CHECK(v && json_get(v, "a")->u16len == 2, "UTF-16 length of surrogate pair");
    json_free(v);
    CHECK(!json_parse("{\"a\":1,}", 8), "trailing comma rejected");
    CHECK(!json_parse("\"\\u0000\"", 8), "embedded NUL rejected");
}

static void test_http(void) {
    Resp r = req("GET", "/join", NULL, NULL);
    CHECK(r.status == 200 && strstr(r.body, "LocalStage"), "index served (%d)", r.status);
    CHECK(strstr(r.raw.d, "Content-Security-Policy"), "CSP header present");
    buf_free(&r.raw);
    r = raw("GET /join HTTP/1.1\r\nHost: evil.example\r\n\r\n", NULL, 0);
    CHECK(r.status == 421, "foreign Host rejected (%d)", r.status);
    buf_free(&r.raw);
    r = raw("GET /join HTTP/1.1\r\n\r\n", NULL, 0);
    CHECK(r.status == 400, "missing Host rejected (%d)", r.status);
    buf_free(&r.raw);
    r = req("GET", "/api/session", NULL, NULL);
    CHECK(r.status == 404, "no session yet (%d)", r.status);
    buf_free(&r.raw);
    r = req("GET", "/api/qr?text=hello%20world", NULL, NULL);
    CHECK(r.status == 200 && strstr(r.body, "<svg"), "QR SVG served");
    buf_free(&r.raw);
    r = req("GET", "/api/network/interfaces", NULL, NULL);
    CHECK(r.status == 200 && strstr(r.body, "\"interfaces\""), "interfaces listed");
    buf_free(&r.raw);
    r = req("POST", "/api/decks?format=pdf", NULL, "x");
    CHECK(r.status == 401 || r.status == 403, "upload needs host token (%d)", r.status);
    buf_free(&r.raw);
    r = raw("POST /api/decks HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n", NULL, 0);
    CHECK(r.status == 501 || r.status == 421, "chunked refused (%d)", r.status);
    buf_free(&r.raw);
    r = req("GET", "/api/nope", NULL, NULL);
    CHECK(r.status == 404, "unknown API 404");
    buf_free(&r.raw);
}

static char token[80];

static void test_auth(const char *secret) {
    Resp r = req("POST", "/api/host/auth", "Content-Type: application/json\r\n", "{\"secret\":\"wrong\"}");
    CHECK(r.status == 403, "bad secret rejected (%d)", r.status);
    buf_free(&r.raw);
    char body[128];
    snprintf(body, sizeof body, "{\"secret\":\"%s\"}", secret);
    r = req("POST", "/api/host/auth", "Content-Type: application/json\r\n", body);
    CHECK(r.status == 200, "secret exchanged (%d) %s", r.status, r.body);
    JVal *v = json_parse(r.body, strlen(r.body));
    const char *t = v ? json_str(v, "token") : NULL;
    if (t) snprintf(token, sizeof token, "%s", t);
    json_free(v);
    buf_free(&r.raw);
    r = req("POST", "/api/host/auth", "Content-Type: application/json\r\n", body);
    CHECK(r.status == 403, "secret is one-use (%d)", r.status);
    buf_free(&r.raw);
}

static const char *write_pdf(void) {
    static char path[] = "/tmp/localstage-test-XXXXXX.pdf";
    int fd = mkstemps(path, 4);
    const char *objs[] = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 320 180] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 320 180] >>",
    };
    Buf b = {0};
    buf_puts(&b, "%PDF-1.4\n");
    long off[4];
    for (int i = 0; i < 4; i++) { off[i] = (long)b.len; buf_printf(&b, "%d 0 obj\n%s\nendobj\n", i + 1, objs[i]); }
    long xref = (long)b.len;
    buf_puts(&b, "xref\n0 5\n0000000000 65535 f \n");
    for (int i = 0; i < 4; i++) buf_printf(&b, "%010ld 00000 n \n", off[i]);
    buf_printf(&b, "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", xref);
    write_all(fd, b.d, b.len);
    close(fd);
    buf_free(&b);
    return path;
}

static void test_session(void) {
    char auth[160];
    snprintf(auth, sizeof auth, "Authorization: Bearer %s\r\n", token);

    /* Deck upload: a non-PDF is rejected, a 2-page PDF converts. */
    Resp r = req("POST", "/api/decks?format=pdf", auth, "not a pdf");
    CHECK(r.status == 400, "non-PDF rejected (%d) %s", r.status, r.body);
    buf_free(&r.raw);
    const char *pdf = write_pdf();
    FILE *f = fopen(pdf, "rb");
    char data[4096];
    size_t n = fread(data, 1, sizeof data, f);
    fclose(f);
    unlink(pdf);
    char head[512];
    snprintf(head, sizeof head, "POST /api/decks?format=pdf HTTP/1.1\r\nHost: %s\r\n%sContent-Type: application/octet-stream\r\nContent-Length: %zu\r\n\r\n", host_hdr, auth, n);
    r = raw(head, data, n);
    CHECK(r.status == 200 || r.status == 201, "PDF converted (%d) %s", r.status, r.body);
    JVal *deck = json_parse(r.body, strlen(r.body));
    char deck_id[80] = "";
    if (deck && json_str(deck, "id")) snprintf(deck_id, sizeof deck_id, "%s", json_str(deck, "id"));
    const JVal *total = deck ? json_get(deck, "totalSlides") : NULL;
    CHECK(total && total->num == 2, "deck has 2 slides");
    json_free(deck);
    buf_free(&r.raw);

    /* Host creates a PIN-protected session. */
    Ws host, viewer, bad;
    CHECK(ws_open(&host), "host WebSocket handshake");
    char msg[512];
    snprintf(msg, sizeof msg, "{\"type\":\"host:create\",\"token\":\"%s\",\"title\":\"Demo\",\"pin\":\"123456\",\"maxViewers\":1,\"source\":\"deck\"}", token);
    ws_send(&host, msg);
    JVal *created = ws_expect(&host, "host:created");
    CHECK(created, "host:created received");
    const JVal *s = created ? json_get(created, "session") : NULL;
    char sid[64] = "";
    if (s && json_str(s, "id")) snprintf(sid, sizeof sid, "%s", json_str(s, "id"));
    json_free(created);

    r = req("GET", "/api/session", NULL, NULL);
    CHECK(r.status == 200 && strstr(r.body, "\"hasPin\":true") && !strstr(r.body, "123456"), "public session hides PIN: %s", r.body);
    buf_free(&r.raw);

    /* Wrong PIN is refused. */
    CHECK(ws_open(&bad), "bad viewer handshake");
    snprintf(msg, sizeof msg, "{\"type\":\"viewer:join\",\"sessionId\":\"%s\",\"displayName\":\"Eve\",\"pin\":\"000000\"}", sid);
    ws_send(&bad, msg);
    JVal *e = ws_expect(&bad, "error");
    CHECK(e, "wrong PIN gives error");
    json_free(e);
    ws_close(&bad);

    /* Correct PIN joins. */
    CHECK(ws_open(&viewer), "viewer handshake");
    snprintf(msg, sizeof msg, "{\"type\":\"viewer:join\",\"sessionId\":\"%s\",\"displayName\":\"Ann\",\"pin\":\"123456\"}", sid);
    ws_send(&viewer, msg);
    JVal *acc = ws_expect(&viewer, "viewer:accepted");
    CHECK(acc, "viewer accepted");
    char vtoken[80] = "", vid[64] = "";
    if (acc && json_str(acc, "token")) snprintf(vtoken, sizeof vtoken, "%s", json_str(acc, "token"));
    if (acc && json_str(acc, "viewerId")) snprintf(vid, sizeof vid, "%s", json_str(acc, "viewerId"));
    json_free(acc);
    JVal *ready = ws_expect(&host, "viewer:ready");
    CHECK(ready, "host told viewer is ready");
    json_free(ready);

    /* Viewer limit of 1 is enforced. */
    CHECK(ws_open(&bad), "second viewer handshake");
    snprintf(msg, sizeof msg, "{\"type\":\"viewer:join\",\"sessionId\":\"%s\",\"displayName\":\"Bob\",\"pin\":\"123456\"}", sid);
    ws_send(&bad, msg);
    e = ws_expect(&bad, "error");
    CHECK(e, "viewer limit enforced");
    json_free(e);
    ws_close(&bad);

    /* Offer relay: host -> viewer, answer viewer -> host. */
    snprintf(msg, sizeof msg, "{\"type\":\"rtc:offer\",\"sessionId\":\"%s\",\"targetViewerId\":\"%s\",\"connectionGeneration\":1,\"sdp\":\"v=0\\r\\n\"}", sid, vid);
    ws_send(&host, msg);
    JVal *offer = ws_expect(&viewer, "rtc:offer");
    CHECK(offer && json_str(offer, "sdp"), "offer relayed");
    json_free(offer);
    snprintf(msg, sizeof msg, "{\"type\":\"rtc:answer\",\"sessionId\":\"%s\",\"viewerId\":\"%s\",\"connectionGeneration\":1,\"sdp\":\"v=0\\r\\n\"}", sid, vid);
    ws_send(&viewer, msg);
    JVal *answer = ws_expect(&host, "rtc:answer");
    CHECK(answer, "answer relayed");
    json_free(answer);

    /* Deck: activate, navigate, and slide access rules. */
    snprintf(msg, sizeof msg, "{\"type\":\"deck:activate\",\"deckId\":\"%s\",\"deckRevision\":1,\"totalSlides\":2}", deck_id);
    ws_send(&host, msg);
    JVal *ds = ws_expect(&viewer, "deck:state");
    CHECK(ds && json_get(ds, "state") && json_get(ds, "state")->type == J_OBJ, "viewer gets deck state");
    const JVal *st = ds ? json_get(ds, "state") : NULL;
    int rev = st && json_get(st, "deckRevision") ? (int)json_get(st, "deckRevision")->num : 1;
    int seq = st && json_get(st, "stateSequence") ? (int)json_get(st, "stateSequence")->num : 0;
    json_free(ds);

    char path[256], vauth[160];
    snprintf(vauth, sizeof vauth, "Authorization: Bearer %s\r\n", vtoken);
    snprintf(path, sizeof path, "/api/decks/%s/slides/1?revision=%d", deck_id, rev);
    r = req("GET", path, vauth, NULL);
    CHECK(r.status == 200 && memmem(r.body, 8, "\x89PNG", 4), "viewer reads current slide (%d)", r.status);
    buf_free(&r.raw);
    snprintf(path, sizeof path, "/api/decks/%s/slides/2?revision=%d", deck_id, rev);
    r = req("GET", path, vauth, NULL);
    CHECK(r.status == 403, "future slide locked for viewer (%d)", r.status);
    buf_free(&r.raw);
    r = req("GET", path, auth, NULL);
    CHECK(r.status == 200, "host reads any slide (%d)", r.status);
    buf_free(&r.raw);
    r = req("GET", path, NULL, NULL);
    CHECK(r.status == 401 || r.status == 403, "anonymous slide refused (%d)", r.status);
    buf_free(&r.raw);

    snprintf(msg, sizeof msg, "{\"type\":\"presenter:navigate\",\"deckId\":\"%s\",\"deckRevision\":%d,\"stateSequence\":%d,\"targetSlide\":2}", deck_id, rev, seq);
    ws_send(&host, msg);
    ds = ws_expect(&viewer, "deck:state");
    st = ds ? json_get(ds, "state") : NULL;
    CHECK(st && json_get(st, "presenterSlide") && json_get(st, "presenterSlide")->num == 2, "navigate to slide 2");
    json_free(ds);
    r = req("GET", path, vauth, NULL);
    CHECK(r.status == 200, "slide 2 unlocked after navigation (%d)", r.status);
    buf_free(&r.raw);

    /* Review lock restricts earlier slides. */
    ws_send(&host, "{\"type\":\"host:review-lock\",\"locked\":true}");
    JVal *ss = ws_expect(&viewer, "session:state");
    json_free(ss);
    snprintf(path, sizeof path, "/api/decks/%s/slides/1?revision=%d", deck_id, rev);
    r = req("GET", path, vauth, NULL);
    CHECK(r.status == 403, "review lock hides earlier slide (%d)", r.status);
    buf_free(&r.raw);

    /* Garbage and oversized-field messages produce errors, not crashes. */
    ws_send(&viewer, "{not json");
    e = ws_expect(&viewer, "error");
    CHECK(e, "invalid JSON reported: %s", errmsg(e));
    json_free(e);

    /* Host removes viewer; viewer token then stops working. */
    snprintf(msg, sizeof msg, "{\"type\":\"host:remove-viewer\",\"viewerId\":\"%s\"}", vid);
    ws_send(&host, msg);
    JVal *left = ws_expect(&host, "viewer:left");
    CHECK(left, "viewer:left after removal");
    json_free(left);
    snprintf(path, sizeof path, "/api/decks/%s/slides/2?revision=%d", deck_id, rev);
    r = req("GET", path, vauth, NULL);
    CHECK(r.status == 403 || r.status == 401, "removed viewer loses slide access (%d)", r.status);
    buf_free(&r.raw);
    ws_close(&viewer);

    ws_send(&host, "{\"type\":\"host:end\"}");
    ws_close(&host);
    usleep(200000);
    r = req("GET", "/api/session", NULL, NULL);
    CHECK(r.status == 404, "session gone after end (%d)", r.status);
    buf_free(&r.raw);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s ./localstage\n", argv[0]); return 2; }
    port = 20000 + (getpid() % 20000);
    snprintf(host_hdr, sizeof host_hdr, "127.0.0.1:%d", port);
    int out[2];
    if (pipe(out)) return 2;
    pid_t pid = fork();
    if (pid == 0) {
        char p[16];
        snprintf(p, sizeof p, "%d", port);
        setenv("PORT", p, 1);
        unsetenv("OPEN_BROWSER");
        dup2(out[1], 1);
        close(out[0]);
        execl(argv[1], argv[1], (char *)NULL);
        _exit(127);
    }
    close(out[1]);
    FILE *so = fdopen(out[0], "r");
    char line[512], secret[80] = "";
    while (fgets(line, sizeof line, so)) {
        char *h = strstr(line, "/host#");
        if (h) { sscanf(h + 6, "%79[0-9a-f]", secret); break; }
    }
    CHECK(secret[0], "server printed presenter link");
    if (secret[0]) {
        test_qr();
        test_json();
        test_http();
        test_auth(secret);
        test_session();
    }
    kill(pid, SIGTERM);
    int status;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "clean shutdown");
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
