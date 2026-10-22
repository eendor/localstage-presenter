/* Deck upload, conversion (pdfinfo/pdftoppm, optional LibreOffice) and slide access. */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "server.h"

#define CONVERT_TIMEOUT_MS 120000

static char conv_format[16];

static long long parse_size(const char *s, char *label, size_t ln) {
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno || end == s || v <= 0) return -1;
    long long mul = 1;
    if (!*end || str_ieq(end, "b")) mul = 1;
    else if (str_ieq(end, "kb")) mul = 1024LL;
    else if (str_ieq(end, "mb")) mul = 1024LL * 1024;
    else if (str_ieq(end, "gb")) mul = 1024LL * 1024 * 1024;
    else return -1;
    if (v > (1LL << 40) / mul) return -1;
    if (*end) {
        char unit[4] = {0};
        for (int i = 0; i < 2 && end[i]; i++) unit[i] = (char)toupper((unsigned char)end[i]);
        snprintf(label, ln, "%lld %s", v, unit);
    } else snprintf(label, ln, "%lld bytes", v);
    return v * mul;
}

void decks_init(void) {
    const char *size = getenv("MAX_DECK_BYTES");
    if (!size || !*size) size = "150mb";
    app.max_upload = parse_size(size, app.max_upload_label, sizeof app.max_upload_label);
    if (app.max_upload < 0) { fprintf(stderr, "MAX_DECK_BYTES must look like 150mb, 512kb or 1gb\n"); exit(1); }
    const char *pages = getenv("MAX_DECK_PAGES");
    char *end;
    long p = pages && *pages ? strtol(pages, &end, 10) : 200;
    if ((pages && *pages && *end) || p < 1 || p > 1000) { fprintf(stderr, "MAX_DECK_PAGES must be between 1 and 1000\n"); exit(1); }
    app.max_pages = (int)p;
}

/* ---------- upload ---------- */

static void query_param(const char *query, const char *key, char *out, size_t n) {
    size_t kl = strlen(key);
    out[0] = 0;
    for (const char *p = query; p && *p;) {
        const char *amp = strchr(p, '&');
        size_t len = amp ? (size_t)(amp - p) : strlen(p);
        if (len > kl && !strncmp(p, key, kl) && p[kl] == '=') {
            size_t vl = len - kl - 1;
            if (vl >= n) vl = n - 1;
            memcpy(out, p + kl + 1, vl);
            out[vl] = 0;
            return;
        }
        p = amp ? amp + 1 : NULL;
    }
}

static void fail_upload(Conn *c, int status, const char *message) {
    if (c->upload_f) { fclose(c->upload_f); c->upload_f = NULL; }
    if (app.conv_dir[0] && app.conv_conn == c && !app.conv.running) { remove_tree(app.conv_dir); app.conv_dir[0] = 0; app.conv_conn = NULL; }
    http_error(c, status, message);
}

void decks_begin_upload(Conn *c) {
    char format[16];
    query_param(c->query, "format", format, sizeof format);
    for (char *p = format; *p; p++) *p = (char)tolower((unsigned char)*p);
    char msg[128];
    c->upload_f = NULL;
    if (c->content_length < 0) { http_error(c, 411, "Content-Length required"); return; }
    /* Pre-rendered PNGs are bigger than the PDF they came from. */
    long long limit = !strcmp(format, "slides") ? app.max_upload * 4 : app.max_upload;
    if (c->content_length > limit) {
        snprintf(msg, sizeof msg, "Upload too large. Maximum deck size is %s.", app.max_upload_label);
        http_error(c, 413, msg);
        return;
    }
    if (app.conv_conn || app.conv.running) { http_error(c, 409, "A conversion is already running"); return; }
    if (app.ndecks >= MAX_DECKS) { http_error(c, 409, "Four decks loaded. End the session before uploading more."); return; }
    if ((strcmp(format, "pdf") && strcmp(format, "ppt") && strcmp(format, "pptx") && strcmp(format, "slides")) || c->content_length == 0) {
        http_error(c, 400, "Upload a PDF, PPTX or PPT file");
        return;
    }
    if (plat_mkdtemp(app.conv_dir, sizeof app.conv_dir, "localstage-deck-") != 0) { http_error(c, 500, "Could not create a temporary folder"); return; }
    char path[600];
    snprintf(path, sizeof path, "%s/original.%s", app.conv_dir, format);
    c->upload_f = fopen(path, "wb");
    app.conv_conn = c;
    if (!c->upload_f) { fail_upload(c, 500, "Could not store the upload"); return; }
    snprintf(conv_format, sizeof conv_format, "%s", format);
    app.conv_epoch = app.epoch;
    c->received = 0;
    c->st = CS_UPLOAD;
}

void decks_upload_data(Conn *c, const char *data, size_t n) {
    if (n && fwrite(data, 1, n, c->upload_f) != n) fail_upload(c, 500, "Could not store the upload");
}

/* ---------- conversion child ---------- */

static void child_fail(const char *dir, const char *message) {
    char path[600];
    snprintf(path, sizeof path, "%s/error.txt", dir);
    FILE *f = fopen(path, "w");
    if (f) { fputs(message, f); fclose(f); }
    _exit(1);
}

/* Tail of a log file, trimmed, for error messages. */
static void read_tail(const char *path, char *out, size_t n) {
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long start = size > (long)n - 1 ? size - (long)n + 1 : 0;
    fseek(f, start, SEEK_SET);
    size_t r = fread(out, 1, n - 1, f);
    out[r] = 0;
    fclose(f);
    while (r && isspace((unsigned char)out[r - 1])) out[--r] = 0;
}

void decks_convert_child(const char *dir, const char *format, int max_pages) {
    char pdf[600], errlog[600], info[600];
    snprintf(errlog, sizeof errlog, "%s/stderr.log", dir);
    snprintf(pdf, sizeof pdf, "%s/original.%s", dir, format);
    if (strcmp(format, "pdf")) {
        const char *office = getenv("LIBREOFFICE_PATH");
        if (!office || !*office) {
            /* Standard install locations, so a fresh LibreOffice install works without PATH changes. */
            static const char *known[] = {
#ifdef _WIN32
                "C:\\Program Files\\LibreOffice\\program\\soffice.exe",
                "C:\\Program Files (x86)\\LibreOffice\\program\\soffice.exe",
#elif defined(__APPLE__)
                "/Applications/LibreOffice.app/Contents/MacOS/soffice",
#endif
                NULL};
            office = "soffice";
            for (int i = 0; known[i]; i++) if (plat_exists(known[i])) { office = known[i]; break; }
        }
        char profile[700], env[800], xcu[800];
        snprintf(profile, sizeof profile, "%s/office-profile/user", dir);
        snprintf(xcu, sizeof xcu, "%s/registrymodifications.xcu", profile);
        char p1[700];
        snprintf(p1, sizeof p1, "%s/office-profile", dir);
        plat_mkdir(p1);
        plat_mkdir(profile);
        FILE *f = fopen(xcu, "w");
        if (f) {
            /* Disable macros and link updates in the throwaway profile. */
            fputs("<?xml version=\"1.0\"?><oor:items xmlns:oor=\"http://openoffice.org/2001/registry\"><item oor:path=\"/org.openoffice.Office.Common/Security/Scripting\"><prop oor:name=\"MacroSecurityLevel\" oor:op=\"fuse\"><value>3</value></prop></item><item oor:path=\"/org.openoffice.Office.Common/Load\"><prop oor:name=\"UpdateDocMode\" oor:op=\"fuse\"><value>0</value></prop></item></oor:items>", f);
            fclose(f);
        }
        char purl[760];
        plat_file_url(p1, purl, sizeof purl);
        snprintf(env, sizeof env, "-env:UserInstallation=%s", purl);
        char *argv[] = {(char *)office, env, "--headless", "--nologo", "--nodefault", "--norestore", "--convert-to",
                        "pdf:impress_pdf_Export:{\"ExportNotesPages\":{\"type\":\"boolean\",\"value\":\"false\"},\"ExportHiddenSlides\":{\"type\":\"boolean\",\"value\":\"false\"}}",
                        "--outdir", (char *)dir, pdf, NULL};
        int rc = plat_run(argv, NULL, errlog);
        snprintf(pdf, sizeof pdf, "%s/original.pdf", dir);
        if (rc != 0 || !plat_exists(pdf)) {
            char tail[512];
            read_tail(errlog, tail, sizeof tail);
            if (rc == 127) child_fail(dir, "Office conversion requires LibreOffice. Install it, set LIBREOFFICE_PATH to soffice, or export your deck to PDF.");
            child_fail(dir, tail[0] ? tail : "LibreOffice could not convert the file");
        }
    }

    snprintf(info, sizeof info, "%s/info.txt", dir);
    char *iargv[] = {"pdfinfo", pdf, NULL};
    int rc = plat_run(iargv, info, errlog);
    if (rc != 0) {
        char tail[512];
        read_tail(errlog, tail, sizeof tail);
        if (rc == 127) child_fail(dir, "pdfinfo was not found. Install poppler-utils.");
        child_fail(dir, tail[0] ? tail : "Unreadable PDF");
    }
    FILE *f = fopen(info, "r");
    if (!f) child_fail(dir, "Unreadable PDF");
    char line[512];
    long pages = -1;
    double w = 0, h = 0;
    int encrypted = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "Pages:", 6)) pages = strtol(line + 6, NULL, 10);
        else if (!strncmp(line, "Encrypted:", 10)) encrypted = strstr(line, "yes") != NULL;
        else if (!strncmp(line, "Page size:", 10) && w == 0) sscanf(line + 10, " %lf x %lf", &w, &h);
    }
    fclose(f);
    if (encrypted) child_fail(dir, "Encrypted or password-protected PDFs are unsupported");
    if (pages < 1) child_fail(dir, "PDF has no pages");
    if (pages > max_pages) {
        char m[64];
        snprintf(m, sizeof m, "Deck exceeds %d slides", max_pages);
        child_fail(dir, m);
    }
    (void)w; (void)h;
    /* Scale each page so its long side is 1920 px: landscape pages get 1920 wide,
       portrait pages 1920 tall, and mixed-orientation PDFs are handled per page. */
    char prefix[600];
    snprintf(prefix, sizeof prefix, "%s/page", dir);
    char *rargv[] = {"pdftoppm", "-png", "-scale-to", "1920", pdf, prefix, NULL};
    rc = plat_run(rargv, NULL, errlog);
    if (rc != 0) {
        char tail[512];
        read_tail(errlog, tail, sizeof tail);
        if (rc == 127) child_fail(dir, "pdftoppm was not found. Install poppler-utils.");
        child_fail(dir, tail[0] ? tail : "Rendering failed");
    }
    /* pdftoppm zero-pads page numbers to the digit count of the page total. */
    int digits = snprintf(NULL, 0, "%ld", pages);
    for (long i = 1; i <= pages; i++) {
        char from[700], to[700];
        snprintf(from, sizeof from, "%s-%0*ld.png", prefix, digits, i);
        snprintf(to, sizeof to, "%s/%ld.png", dir, i);
        if (rename(from, to) != 0) child_fail(dir, "Rendered slide is missing");
    }
    char count[600];
    snprintf(count, sizeof count, "%s/count.txt", dir);
    f = fopen(count, "w");
    if (!f) child_fail(dir, "Could not write output");
    fprintf(f, "%ld", pages);
    fclose(f);
    _exit(0);
}

/* "slides" upload: the presenter's browser already rendered the pages (PDF.js), so no
   external tools are needed. Body = repeated [4-byte big-endian length][PNG file]. */
static const char *split_slides(int *total) {
    char path[600];
    snprintf(path, sizeof path, "%s/original.slides", app.conv_dir);
    FILE *in = fopen(path, "rb");
    if (!in) return "Could not read the upload";
    const char *err = NULL;
    int n = 0;
    unsigned char hdr[4];
    char *buf = NULL;
    while (!err) {
        size_t r = fread(hdr, 1, 4, in);
        if (r == 0) break;
        if (r != 4) { err = "Damaged slide bundle"; break; }
        unsigned long len = (unsigned long)hdr[0] << 24 | (unsigned long)hdr[1] << 16 | (unsigned long)hdr[2] << 8 | hdr[3];
        if (len < 8 || len > (unsigned long)app.max_upload) { err = "Damaged slide bundle"; break; }
        if (++n > app.max_pages) { err = "Too many slides"; break; }
        free(buf);
        buf = malloc(len);
        if (!buf || fread(buf, 1, len, in) != len) { err = "Damaged slide bundle"; break; }
        if (memcmp(buf, "\x89PNG\r\n\x1a\n", 8)) { err = "Slides must be PNG images"; break; }
        char out[700];
        snprintf(out, sizeof out, "%s/%d.png", app.conv_dir, n);
        FILE *o = fopen(out, "wb");
        if (!o || fwrite(buf, 1, len, o) != len) err = "Could not store a slide";
        if (o && fclose(o) != 0) err = "Could not store a slide";
    }
    free(buf);
    fclose(in);
    remove(path);
    if (!err && n == 0) err = "The deck has no slides";
    if (!err) {
        snprintf(path, sizeof path, "%s/count.txt", app.conv_dir);
        FILE *f = fopen(path, "w");
        if (!f) return "Could not write output";
        fprintf(f, "%d", n);
        fclose(f);
    }
    *total = n;
    return err;
}

static void finish(int ok, const char *error);

static int has_pdf_magic(void) {
    char path[600], head[5];
    snprintf(path, sizeof path, "%s/original.pdf", app.conv_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t r = fread(head, 1, 5, f);
    fclose(f);
    return r == 5 && !memcmp(head, "%PDF-", 5);
}

void decks_upload_done(Conn *c) {
    if (c->upload_f && fclose(c->upload_f) != 0) { c->upload_f = NULL; fail_upload(c, 500, "Could not store the upload"); return; }
    c->upload_f = NULL;
    if (!strcmp(conv_format, "pdf") && !has_pdf_magic()) { fail_upload(c, 400, "Invalid PDF file"); return; }
    if (!strcmp(conv_format, "slides")) {
        int total = 0;
        const char *err = split_slides(&total);
        c->st = CS_WAIT;
        finish(err == NULL, err);
        return;
    }
    c->st = CS_WAIT;
    if (plat_spawn_convert(&app.conv, decks_convert_child, app.conv_dir, conv_format, app.max_pages) != 0) {
        fail_upload(c, 500, "Could not start conversion");
        return;
    }
    app.conv_deadline = now_mono_ms() + CONVERT_TIMEOUT_MS;
}

static void finish(int ok, const char *error) {
    Conn *c = app.conv_conn;
    int stale = app.conv_epoch != app.epoch;
    app.conv_conn = NULL;
    if (ok && !stale && c && app.ndecks < MAX_DECKS) {
        char count[600];
        snprintf(count, sizeof count, "%s/count.txt", app.conv_dir);
        FILE *f = fopen(count, "r");
        int total = 0;
        if (f) { if (fscanf(f, "%d", &total) != 1) total = 0; fclose(f); }
        if (total > 0) {
            Deck *d = &app.decks[app.ndecks++];
            random_uuid(d->id);
            snprintf(d->dir, sizeof d->dir, "%s", app.conv_dir);
            d->total = total;
            app.conv_dir[0] = 0;
            char json[128];
            snprintf(json, sizeof json, "{\"id\":\"%s\",\"totalSlides\":%d}", d->id, total);
            http_json(c, 200, json);
            return;
        }
        error = "Converter produced no slides";
    }
    remove_tree(app.conv_dir);
    app.conv_dir[0] = 0;
    if (!c) return; /* client went away: discard */
    if (stale) error = "Session ended during conversion";
    char msg[700];
    snprintf(msg, sizeof msg, "Conversion failed: %s", error && *error ? error : "unknown error");
    http_error(c, 400, msg);
}

void decks_poll(void) {
    if (!app.conv.running) return;
    int code = 1;
    if (!plat_proc_poll(&app.conv, &code)) {
        if (now_mono_ms() > app.conv_deadline) {
            plat_proc_kill(&app.conv);
            finish(0, "Conversion exceeded two-minute time limit");
        }
        return;
    }
    if (code == 0) { finish(1, NULL); return; }
    char err[600], msg[512];
    snprintf(err, sizeof err, "%s/error.txt", app.conv_dir);
    read_tail(err, msg, sizeof msg);
    finish(0, msg[0] ? msg : "Converter crashed");
}

void decks_conn_gone(Conn *c) {
    if (app.conv_conn != c) return;
    if (c->upload_f) { fclose(c->upload_f); c->upload_f = NULL; }
    app.conv_conn = NULL;
    if (!app.conv.running && app.conv_dir[0]) { remove_tree(app.conv_dir); app.conv_dir[0] = 0; }
}

void decks_clear(void) {
    app.epoch++;
    app.deck.active = 0;
    for (int i = 0; i < app.ndecks; i++) remove_tree(app.decks[i].dir);
    app.ndecks = 0;
}

void decks_shutdown(void) {
    plat_proc_kill(&app.conv);
    if (app.conv_dir[0]) remove_tree(app.conv_dir);
    decks_clear();
}

/* ---------- presenter state ---------- */

static Deck *find_deck(const char *id) {
    for (int i = 0; i < app.ndecks; i++)
        if (!strcmp(app.decks[i].id, id)) return &app.decks[i];
    return NULL;
}

int decks_activate(const char *id) {
    Deck *d = find_deck(id);
    if (!d) return 0;
    app.deck.active = 1;
    snprintf(app.deck.deck_id, sizeof app.deck.deck_id, "%s", id);
    app.deck.revision = ++app.deck_revision;
    app.deck.total = d->total;
    app.deck.slide = 1;
    app.deck.sequence = 0;
    return 1;
}

int decks_navigate(const char *id, long long revision, long long sequence, long long slide) {
    DeckState *s = &app.deck;
    if (!s->active || strcmp(s->deck_id, id) || s->revision != revision || s->sequence != sequence || slide < 1 || slide > s->total || slide == s->slide) return 0;
    s->slide = (int)slide;
    s->sequence++;
    return 1;
}

void deck_state_json(Buf *b) {
    if (!app.deck.active) { buf_puts(b, "null"); return; }
    buf_printf(b, "{\"deckId\":\"%s\",\"deckRevision\":%lld,\"totalSlides\":%d,\"presenterSlide\":%d,\"stateSequence\":%lld}", app.deck.deck_id,
               app.deck.revision, app.deck.total, app.deck.slide, app.deck.sequence);
}

static long long strict_int(const char *s) {
    if (!*s || strlen(s) > 9) return -1;
    for (const char *p = s; *p; p++) if (!isdigit((unsigned char)*p)) return -1;
    return strtoll(s, NULL, 10);
}

void decks_serve_slide(Conn *c, const char *id, const char *slide_s) {
    static const char *nostore = "Cache-Control: no-store\r\n";
    const char *token = bearer(c);
    int host = conn_is_host(c);
    long long slide = strict_int(slide_s);
    char rev_s[32];
    query_param(c->query, "revision", rev_s, sizeof rev_s);
    long long revision = strict_int(rev_s);
    DeckState *s = &app.deck;
    if (!host) {
        int allowed = s->active && (app.session.review_locked ? slide == s->slide : slide <= s->slide);
        if (!sig_viewer_authorized(token) || !s->active || strcmp(s->deck_id, id) || revision != s->revision || !allowed) {
            http_send(c, 403, "text/plain", "Forbidden", 9, nostore);
            return;
        }
    }
    Deck *d = find_deck(id);
    if (!d || slide < 1 || slide > d->total) { http_send(c, 404, "text/plain", "Not Found", 9, nostore); return; }
    char path[600];
    snprintf(path, sizeof path, "%s/%lld.png", d->dir, slide);
    FILE *f = fopen(path, "rb");
    long size = -1;
    if (f && fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (!f || size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        if (f) fclose(f);
        http_send(c, 404, "text/plain", "Not Found", 9, nostore);
        return;
    }
    char *data = malloc((size_t)size + 1);
    size_t got = data ? fread(data, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!data || got != (size_t)size) { free(data); http_error(c, 500, "Could not read slide"); return; }
    http_send(c, 200, "image/png", data, got, nostore);
    free(data);
}
