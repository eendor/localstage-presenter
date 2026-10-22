/* Signaling: session lifecycle, admission, roles and WebRTC relay. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "server.h"

/* ---------- validation (mirrors the original protocol package) ---------- */

static int text(const JVal *m, const char *key, size_t max) {
    const JVal *v = json_get(m, key);
    return v && v->type == J_STR && v->u16len <= max;
}
static int optional(const JVal *m, const char *key, size_t max) { return !json_get(m, key) || text(m, key, max); }
static int integer(const JVal *m, const char *key) {
    const JVal *v = json_get(m, key);
    return json_is_safe_int(v) && v->num >= 1;
}
static double num(const JVal *m, const char *key) { const JVal *v = json_get(m, key); return v ? v->num : 0; }

static int six_digits(const char *s) {
    if (strlen(s) != 6) return 0;
    for (int i = 0; i < 6; i++) if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static int valid_candidate(const JVal *c) {
    if (!c) return 0;
    if (c->type == J_NULL) return 1;
    if (c->type != J_OBJ) return 0;
    const JVal *cand = json_get(c, "candidate"), *mid = json_get(c, "sdpMid"), *idx = json_get(c, "sdpMLineIndex"), *uf = json_get(c, "usernameFragment");
    if (!cand || cand->type != J_STR || cand->u16len >= 8192) return 0;
    if (mid && mid->type != J_NULL && (mid->type != J_STR || mid->u16len > 256)) return 0;
    if (idx && idx->type != J_NULL && !json_is_safe_int(idx)) return 0;
    if (uf && uf->type != J_NULL && (uf->type != J_STR || uf->u16len > 256)) return 0;
    return 1;
}

static int validate(const JVal *m) {
    if (!m || m->type != J_OBJ) return 0;
    const char *t = json_str(m, "type");
    if (!t) return 0;
    int b;
    if (!strcmp(t, "host:create")) {
        const char *src = json_str(m, "source"), *pin = json_str(m, "pin");
        if (json_get(m, "source") && (!src || (strcmp(src, "screen") && strcmp(src, "deck")))) return 0;
        if (!text(m, "token", 128) || !text(m, "title", 128) || !optional(m, "pin", 6)) return 0;
        if (pin && *pin && !six_digits(pin)) return 0;
        return integer(m, "maxViewers") && num(m, "maxViewers") <= 50;
    }
    if (!strcmp(t, "viewer:join")) return text(m, "sessionId", 128) && optional(m, "displayName", 64) && optional(m, "pin", 6) && optional(m, "token", 128);
    if (!strcmp(t, "host:lock") || !strcmp(t, "host:review-lock")) return json_bool(m, "locked", &b);
    if (!strcmp(t, "host:remove-viewer")) return text(m, "viewerId", 128);
    if (!strcmp(t, "host:status")) {
        const char *s = json_str(m, "status");
        return s && (!strcmp(s, "live") || !strcmp(s, "paused"));
    }
    if (!strcmp(t, "host:end") || !strcmp(t, "viewer:leave")) return 1;
    if (!strcmp(t, "deck:activate")) return text(m, "deckId", 128) && integer(m, "deckRevision") && integer(m, "totalSlides");
    if (!strcmp(t, "presenter:navigate"))
        return text(m, "deckId", 128) && integer(m, "deckRevision") && integer(m, "targetSlide") && json_is_safe_int(json_get(m, "stateSequence")) && num(m, "stateSequence") >= 0;
    if (!strcmp(t, "rtc:offer") || !strcmp(t, "rtc:answer"))
        return text(m, "sessionId", 128) && integer(m, "connectionGeneration") && text(m, !strcmp(t, "rtc:offer") ? "targetViewerId" : "viewerId", 128) && text(m, "sdp", 60000);
    if (!strcmp(t, "rtc:ice")) return text(m, "sessionId", 128) && text(m, "targetId", 128) && integer(m, "connectionGeneration") && valid_candidate(json_get(m, "candidate"));
    return 0;
}

/* ---------- sending ---------- */

static void send_buf(Conn *c, Buf *b) {
    if (c && b->len) ws_send_text(c, b->d, b->len);
}

static void send_error(Conn *c, const char *message) {
    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"type\":\"error\",\"code\":\"REJECTED\",\"message\":");
    buf_json_str(&b, message);
    buf_puts(&b, "}");
    send_buf(c, &b);
    buf_free(&b);
}

static void session_obj(Buf *b) {
    Session *s = &app.session;
    buf_puts(b, "{\"id\":");
    buf_json_str(b, s->id);
    buf_puts(b, ",\"source\":");
    buf_json_str(b, s->source);
    buf_puts(b, ",\"title\":");
    buf_json_str(b, s->title);
    buf_printf(b, ",\"status\":\"%s\",\"joinLocked\":%s,\"reviewLocked\":%s,\"maxViewers\":%d,\"createdAt\":%lld}", s->status,
               s->join_locked ? "true" : "false", s->review_locked ? "true" : "false", s->max_viewers, (long long)s->created_at);
}

static int is_viewer(Conn *c) {
    return c->st == CS_WS && c->role == ROLE_VIEWER && c->viewer_id[0] && app.session.active && !strcmp(c->session_id, app.session.id);
}

static int viewer_count(void) {
    int n = 0;
    for (int i = 0; i < app.nconns; i++) if (is_viewer(app.conns[i])) n++;
    return n;
}

static Conn *find_viewer(const char *id) {
    for (int i = 0; i < app.nconns; i++)
        if (is_viewer(app.conns[i]) && !strcmp(app.conns[i]->viewer_id, id)) return app.conns[i];
    return NULL;
}

void sig_session_json(Buf *b) {
    Session *s = &app.session;
    buf_puts(b, "{\"id\":");
    buf_json_str(b, s->id);
    buf_puts(b, ",\"title\":");
    buf_json_str(b, s->title);
    buf_printf(b, ",\"status\":\"%s\",\"joinLocked\":%s,\"viewerCount\":%d,\"hasPin\":%s}", s->status, s->join_locked ? "true" : "false",
               viewer_count(), s->pin[0] ? "true" : "false");
}

static void broadcast_state(void) {
    if (!app.session.active) return;
    for (int i = 0; i < app.nconns; i++) {
        Conn *c = app.conns[i];
        if (c->st != CS_WS || strcmp(c->session_id, app.session.id)) continue;
        Buf b;
        buf_init(&b);
        buf_puts(&b, "{\"type\":\"session:state\",\"sessionId\":");
        buf_json_str(&b, app.session.id);
        buf_printf(&b, ",\"status\":\"%s\",\"session\":", app.session.status);
        session_obj(&b);
        buf_printf(&b, ",\"viewerCount\":%d", viewer_count());
        if (c == app.host) {
            buf_puts(&b, ",\"viewers\":[");
            int first = 1;
            for (int j = 0; j < app.nconns; j++) {
                Conn *v = app.conns[j];
                if (!is_viewer(v)) continue;
                buf_puts(&b, first ? "{\"id\":" : ",{\"id\":");
                first = 0;
                buf_json_str(&b, v->viewer_id);
                buf_puts(&b, ",\"sessionId\":");
                buf_json_str(&b, v->session_id);
                buf_puts(&b, ",\"displayName\":");
                buf_json_str(&b, v->display_name);
                buf_printf(&b, ",\"state\":\"%s\",\"joinedAt\":%lld}", v->vstate, (long long)v->joined_at);
            }
            buf_puts(&b, "]");
        }
        buf_puts(&b, "}");
        send_buf(c, &b);
        buf_free(&b);
    }
}

static void broadcast_deck(void) {
    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"type\":\"deck:state\",\"state\":");
    deck_state_json(&b);
    buf_puts(&b, "}");
    for (int i = 0; i < app.nconns; i++) {
        Conn *c = app.conns[i];
        if (c->st == CS_WS && app.session.active && !strcmp(c->session_id, app.session.id)) send_buf(c, &b);
    }
    buf_free(&b);
}

/* ---------- memberships and PIN limiter ---------- */

static Membership *find_token(const char *token) {
    for (int i = 0; i < app.ntokens; i++)
        if (secure_streq(app.tokens[i].token, token)) return &app.tokens[i];
    return NULL;
}

static void delete_token(const char *token) {
    for (int i = 0; i < app.ntokens; i++)
        if (!strcmp(app.tokens[i].token, token)) {
            app.tokens[i] = app.tokens[--app.ntokens];
            return;
        }
}

int sig_viewer_authorized(const char *token) {
    if (!*token || !app.session.active) return 0;
    Membership *m = find_token(token);
    if (!m || strcmp(m->session_id, app.session.id)) return 0;
    for (int i = 0; i < app.nconns; i++)
        if (is_viewer(app.conns[i]) && secure_streq(app.conns[i]->member_token, token)) return 1;
    return 0;
}

static void limiter_cleanup(void) {
    int64_t now = now_mono_ms();
    for (int i = 0; i < app.nlimiter;)
        if (now - app.limiter[i].first > PIN_LIMIT_WINDOW_MS) app.limiter[i] = app.limiter[--app.nlimiter];
        else i++;
}

static int limiter_check(const char *ip) {
    int64_t now = now_mono_ms();
    for (int i = 0; i < app.nlimiter; i++) {
        if (strcmp(app.limiter[i].ip, ip)) continue;
        if (now - app.limiter[i].first > PIN_LIMIT_WINDOW_MS) { app.limiter[i].count = 1; app.limiter[i].first = now; return 1; }
        if (app.limiter[i].count >= PIN_LIMIT_ATTEMPTS) return 0;
        app.limiter[i].count++;
        return 1;
    }
    if (app.nlimiter == (int)(sizeof app.limiter / sizeof app.limiter[0])) limiter_cleanup();
    if (app.nlimiter == (int)(sizeof app.limiter / sizeof app.limiter[0])) return 0; /* table full: fail closed */
    snprintf(app.limiter[app.nlimiter].ip, sizeof app.limiter[0].ip, "%s", ip);
    app.limiter[app.nlimiter].count = 1;
    app.limiter[app.nlimiter].first = now;
    app.nlimiter++;
    return 1;
}

static void limiter_reset(const char *ip) {
    for (int i = 0; i < app.nlimiter; i++)
        if (!strcmp(app.limiter[i].ip, ip)) { app.limiter[i] = app.limiter[--app.nlimiter]; return; }
}

/* ---------- lifecycle ---------- */

static void remove_viewer(Conn *s, const char *reason) {
    if (!s->viewer_id[0] || !s->session_id[0]) return;
    char id[37];
    snprintf(id, sizeof id, "%s", s->viewer_id);
    if (strcmp(reason, "disconnected") && s->member_token[0]) delete_token(s->member_token);
    s->member_token[0] = 0;
    s->generation = 0;
    s->viewer_id[0] = 0;
    s->session_id[0] = 0;
    if (app.host) {
        Buf b;
        buf_init(&b);
        buf_puts(&b, "{\"type\":\"viewer:left\",\"viewerId\":");
        buf_json_str(&b, id);
        buf_printf(&b, ",\"reason\":\"%s\"}", reason);
        send_buf(app.host, &b);
        buf_free(&b);
    }
    broadcast_state();
}

void sig_end(void) {
    if (app.session.active) {
        snprintf(app.session.status, sizeof app.session.status, "ended");
        broadcast_state();
        app.session.active = 0;
        memset(app.session.pin, 0, sizeof app.session.pin);
    }
    app.host = NULL;
    decks_clear();
    memset(app.tokens, 0, sizeof app.tokens);
    app.ntokens = 0;
    for (int i = 0; i < app.nconns; i++) {
        Conn *c = app.conns[i];
        if (c->st != CS_WS) continue;
        c->session_id[0] = c->viewer_id[0] = c->member_token[0] = 0;
        c->role = ROLE_NONE;
        ws_send_close(c, 1000, "Session ended");
    }
}

void sig_on_close(Conn *c) {
    if (c == app.host) sig_end();
    else remove_viewer(c, "disconnected");
}

void sig_heartbeat(void) {
    limiter_cleanup();
    for (int i = 0; i < app.nconns; i++) {
        Conn *c = app.conns[i];
        if (c->st != CS_WS) continue;
        if (!c->alive) c->dead = 1;
        else { c->alive = 0; ws_ping(c); }
    }
}

static void trim_copy(char *out, size_t size, const char *in) {
    while (*in && isspace((unsigned char)*in)) in++;
    size_t n = strlen(in);
    while (n && isspace((unsigned char)in[n - 1])) n--;
    if (n >= size) n = size - 1;
    memcpy(out, in, n);
    out[n] = 0;
}

/* ---------- message handlers ---------- */

static void host_create(Conn *s, const JVal *m) {
    const char *token = json_str(m, "token");
    if (s->role != ROLE_NONE || app.host || !s->from_loopback || !app.host_token[0] || !secure_streq(token, app.host_token)) {
        send_error(s, "Host authentication required or session already active");
        return;
    }
    Session *ss = &app.session;
    memset(ss, 0, sizeof *ss);
    ss->active = 1;
    random_uuid(ss->id);
    trim_copy(ss->title, sizeof ss->title, json_str(m, "title"));
    if (!ss->title[0]) snprintf(ss->title, sizeof ss->title, "Presentation");
    const char *src = json_str(m, "source");
    snprintf(ss->source, sizeof ss->source, "%s", src ? src : "screen");
    snprintf(ss->status, sizeof ss->status, "live");
    ss->max_viewers = (int)num(m, "maxViewers");
    ss->created_at = now_wall_ms();
    const char *pin = json_str(m, "pin");
    if (pin && *pin) snprintf(ss->pin, sizeof ss->pin, "%s", pin);
    s->role = ROLE_HOST;
    snprintf(s->session_id, sizeof s->session_id, "%s", ss->id);
    app.host = s;
    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"type\":\"host:created\",\"session\":");
    session_obj(&b);
    buf_puts(&b, "}");
    send_buf(s, &b);
    buf_free(&b);
    broadcast_state();
}

static void viewer_join(Conn *s, const JVal *m) {
    Session *ss = &app.session;
    const char *sid = json_str(m, "sessionId"), *token = json_str(m, "token"), *pin = json_str(m, "pin"), *name = json_str(m, "displayName");
    if (token && !*token) token = NULL;
    if (s->role != ROLE_NONE || !app.host || !ss->active || strcmp(ss->id, sid)) { send_error(s, "Host unavailable"); return; }
    Membership *previous = token ? find_token(token) : NULL;
    if (token && (!previous || strcmp(previous->session_id, ss->id))) { send_error(s, "Membership expired. Join again."); return; }
    if (ss->join_locked) { send_error(s, "Joining is locked"); return; }
    if (!previous && ss->pin[0]) {
        if (!limiter_check(s->ip)) { send_error(s, "Too many PIN attempts. Wait one minute."); return; }
        if (!secure_streq(pin ? pin : "", ss->pin)) { send_error(s, "Wrong PIN"); return; }
        limiter_reset(s->ip);
    }
    if (token)
        for (int i = 0; i < app.nconns; i++) {
            Conn *old = app.conns[i];
            if (old != s && is_viewer(old) && secure_streq(old->member_token, token)) {
                remove_viewer(old, "disconnected");
                ws_send_close(old, 1000, "Replaced by a newer connection");
            }
        }
    if (viewer_count() >= ss->max_viewers) { send_error(s, "Session is full"); return; }
    if (!token && app.ntokens >= MAX_TOKENS) { send_error(s, "Session is full"); return; }

    char display[256] = "";
    if (previous) snprintf(display, sizeof display, "%s", previous->display_name);
    else if (name) trim_copy(display, sizeof display, name);
    if (!display[0]) snprintf(display, sizeof display, "Viewer %d", ++ss->viewer_serial);

    char fresh[65];
    if (!token) {
        random_hex(fresh, 32);
        Membership *t = &app.tokens[app.ntokens++];
        snprintf(t->token, sizeof t->token, "%s", fresh);
        snprintf(t->session_id, sizeof t->session_id, "%s", ss->id);
        snprintf(t->display_name, sizeof t->display_name, "%s", display);
        token = fresh;
    }
    s->role = ROLE_VIEWER;
    snprintf(s->session_id, sizeof s->session_id, "%s", ss->id);
    random_uuid(s->viewer_id);
    snprintf(s->display_name, sizeof s->display_name, "%s", display);
    snprintf(s->member_token, sizeof s->member_token, "%s", token);
    snprintf(s->vstate, sizeof s->vstate, "connected");
    s->joined_at = now_wall_ms();
    s->generation = 0;

    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"type\":\"viewer:accepted\",\"viewerId\":");
    buf_json_str(&b, s->viewer_id);
    buf_puts(&b, ",\"sessionId\":");
    buf_json_str(&b, ss->id);
    buf_puts(&b, ",\"token\":");
    buf_json_str(&b, token);
    buf_puts(&b, "}");
    send_buf(s, &b);
    b.len = 0;
    buf_puts(&b, "{\"type\":\"viewer:ready\",\"viewerId\":");
    buf_json_str(&b, s->viewer_id);
    buf_puts(&b, ",\"displayName\":");
    buf_json_str(&b, display);
    buf_puts(&b, "}");
    send_buf(app.host, &b);
    buf_free(&b);
    broadcast_state();
    if (app.deck.active) {
        buf_init(&b);
        buf_puts(&b, "{\"type\":\"deck:state\",\"state\":");
        deck_state_json(&b);
        buf_puts(&b, "}");
        send_buf(s, &b);
        buf_free(&b);
    }
}

static void put_opt_str(Buf *b, const JVal *o, const char *key) {
    const JVal *v = json_get(o, key);
    if (!v) return;
    buf_printf(b, ",\"%s\":", key);
    if (v->type == J_STR) buf_json_str(b, v->str);
    else buf_puts(b, "null");
}

static void relay(Conn *s, const JVal *m, const char *type) {
    const char *sid = json_str(m, "sessionId");
    if (strcmp(sid, s->session_id)) { send_error(s, "Wrong session"); return; }
    int is_host = s == app.host;
    const char *id = !strcmp(type, "rtc:offer") ? json_str(m, "targetViewerId")
                   : is_host && !strcmp(type, "rtc:ice") ? json_str(m, "targetId") : s->viewer_id;
    Conn *target = id && *id ? find_viewer(id) : NULL;
    if (!target) { send_error(s, "Unknown viewer"); return; }
    double gen = num(m, "connectionGeneration");
    if (!strcmp(type, "rtc:offer")) {
        if (!is_host || gen <= target->generation) { send_error(s, "Unauthorized or stale offer"); return; }
        target->generation = gen;
    } else if (gen != target->generation) { send_error(s, "Stale connection generation"); return; }
    if (!strcmp(type, "rtc:answer") && (s->role != ROLE_VIEWER || strcmp(json_str(m, "viewerId"), s->viewer_id))) { send_error(s, "Unauthorized answer"); return; }
    if (!strcmp(type, "rtc:ice") && !is_host && strcmp(json_str(m, "targetId"), "host")) { send_error(s, "Invalid ICE target"); return; }

    /* Rebuild from validated fields only; unknown fields are never relayed. */
    Buf b;
    buf_init(&b);
    buf_printf(&b, "{\"type\":\"%s\",\"sessionId\":", type);
    buf_json_str(&b, sid);
    buf_printf(&b, ",\"connectionGeneration\":%.0f", gen);
    if (!strcmp(type, "rtc:offer")) { buf_puts(&b, ",\"targetViewerId\":"); buf_json_str(&b, id); }
    else if (!strcmp(type, "rtc:answer")) { buf_puts(&b, ",\"viewerId\":"); buf_json_str(&b, id); }
    else { buf_puts(&b, ",\"targetId\":"); buf_json_str(&b, id); }
    if (strcmp(type, "rtc:ice")) { buf_puts(&b, ",\"sdp\":"); buf_json_str(&b, json_str(m, "sdp")); }
    else {
        const JVal *c = json_get(m, "candidate");
        if (c->type == J_NULL) buf_puts(&b, ",\"candidate\":null");
        else {
            buf_puts(&b, ",\"candidate\":{\"candidate\":");
            buf_json_str(&b, json_str(c, "candidate"));
            put_opt_str(&b, c, "sdpMid");
            put_opt_str(&b, c, "usernameFragment");
            const JVal *idx = json_get(c, "sdpMLineIndex");
            if (idx) {
                if (idx->type == J_NULL) buf_puts(&b, ",\"sdpMLineIndex\":null");
                else buf_printf(&b, ",\"sdpMLineIndex\":%.0f", idx->num);
            }
            buf_puts(&b, "}");
        }
    }
    buf_puts(&b, "}");
    send_buf(is_host ? target : app.host, &b);
    buf_free(&b);
}

void sig_on_message(Conn *s, const char *data, size_t n) {
    JVal *m = json_parse(data, n);
    if (!m) { send_error(s, "Invalid JSON"); return; }
    if (!validate(m)) { send_error(s, "Invalid message"); json_free(m); return; }
    const char *type = json_str(m, "type");
    int b;

    if (!strcmp(type, "host:create")) { host_create(s, m); goto done; }
    if (!strcmp(type, "viewer:join")) { viewer_join(s, m); goto done; }
    if (!s->session_id[0] || !app.session.active || strcmp(s->session_id, app.session.id)) { send_error(s, "Session membership required"); goto done; }

    if (!strcmp(type, "deck:activate") || !strcmp(type, "presenter:navigate")) {
        if (s != app.host) { send_error(s, "Presenter only"); goto done; }
        if (!strcmp(type, "deck:activate")) {
            if (!decks_activate(json_str(m, "deckId"))) { send_error(s, "Unknown converted deck"); goto done; }
        } else if (!decks_navigate(json_str(m, "deckId"), (long long)num(m, "deckRevision"), (long long)num(m, "stateSequence"), (long long)num(m, "targetSlide")))
            goto done;
        broadcast_deck();
        goto done;
    }
    if (!strcmp(type, "viewer:leave")) {
        if (s->role == ROLE_VIEWER) { remove_viewer(s, "left"); ws_send_close(s, 1000, "Left"); }
        goto done;
    }
    if (!strncmp(type, "host:", 5)) {
        if (s != app.host) { send_error(s, "Presenter only"); goto done; }
        if (!strcmp(type, "host:end")) sig_end();
        else if (!strcmp(type, "host:lock")) { json_bool(m, "locked", &b); app.session.join_locked = b; broadcast_state(); }
        else if (!strcmp(type, "host:review-lock")) { json_bool(m, "locked", &b); app.session.review_locked = b; broadcast_state(); }
        else if (!strcmp(type, "host:status")) { snprintf(app.session.status, sizeof app.session.status, "%s", json_str(m, "status")); broadcast_state(); }
        else if (!strcmp(type, "host:remove-viewer")) {
            Conn *target = find_viewer(json_str(m, "viewerId"));
            if (target) {
                send_error(target, "Removed by presenter");
                remove_viewer(target, "removed");
                ws_send_close(target, 4003, "Removed");
            }
        }
        goto done;
    }
    if (!strncmp(type, "rtc:", 4)) relay(s, m, type);
done:
    json_free(m);
}
