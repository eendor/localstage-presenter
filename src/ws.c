/* WebSocket (RFC 6455) server framing. No extensions, 64 KiB message limit. */
#include <stdio.h>
#include <string.h>

#include "server.h"

#define MAX_OUT_BYTES (16 * 1024 * 1024)

int ws_handshake(Conn *c) {
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char key[128];
    if (!c->wskey[0] || strcmp(c->wsver, "13") != 0) return 0;
    snprintf(key, sizeof key, "%s%s", c->wskey, guid);
    uint8_t digest[20];
    char accept[32];
    sha1(key, strlen(key), digest);
    base64_encode(digest, 20, accept);
    buf_printf(&c->out, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
    c->st = CS_WS;
    c->alive = 1;
    buf_init(&c->frag);
    c->frag_op = 0;
    return 1;
}

static void send_frame(Conn *c, int op, const void *data, size_t n) {
    if (c->dead || c->close_after_write || c->st != CS_WS) return;
    if (c->out.len + n > MAX_OUT_BYTES) { c->dead = 1; return; } /* client stopped reading */
    unsigned char h[10];
    size_t hl;
    h[0] = (unsigned char)(0x80 | op);
    if (n < 126) { h[1] = (unsigned char)n; hl = 2; }
    else if (n < 65536) { h[1] = 126; h[2] = (unsigned char)(n >> 8); h[3] = (unsigned char)n; hl = 4; }
    else {
        h[1] = 127;
        for (int i = 0; i < 8; i++) h[2 + i] = (unsigned char)((unsigned long long)n >> (56 - 8 * i));
        hl = 10;
    }
    buf_add(&c->out, h, hl);
    if (n) buf_add(&c->out, data, n);
}

void ws_send_text(Conn *c, const char *s, size_t n) { send_frame(c, 1, s, n); }

void ws_ping(Conn *c) { send_frame(c, 9, NULL, 0); }

void ws_send_close(Conn *c, int code, const char *reason) {
    char p[128];
    size_t rl = strlen(reason);
    if (rl > 120) rl = 120;
    p[0] = (char)(code >> 8);
    p[1] = (char)code;
    memcpy(p + 2, reason, rl);
    send_frame(c, 8, p, rl + 2);
    c->close_after_write = 1;
}

void ws_on_data(Conn *c) {
    while (c->st == CS_WS && !c->close_after_write && !c->dead) {
        const unsigned char *p = (const unsigned char *)c->in.d;
        size_t avail = c->in.len;
        if (avail < 2) return;
        int fin = p[0] & 0x80, rsv = p[0] & 0x70, op = p[0] & 0x0F, masked = p[1] & 0x80;
        unsigned long long len = p[1] & 0x7F;
        size_t pos = 2;
        if (len == 126) {
            if (avail < 4) return;
            len = (unsigned long long)p[2] << 8 | p[3];
            pos = 4;
        } else if (len == 127) {
            if (avail < 10) return;
            len = 0;
            for (int i = 0; i < 8; i++) len = len << 8 | p[2 + i];
            pos = 10;
        }
        if (rsv || !masked) { ws_send_close(c, 1002, "Protocol error"); return; }
        if (len > MAX_MESSAGE_SIZE) { ws_send_close(c, 1009, "Message too big"); return; }
        if (avail < pos + 4 + len) return;
        unsigned char mask[4];
        memcpy(mask, p + pos, 4);
        pos += 4;
        unsigned char *payload = (unsigned char *)c->in.d + pos;
        for (unsigned long long i = 0; i < len; i++) payload[i] ^= mask[i & 3];
        c->last_activity = now_mono_ms();

        if (op >= 8) { /* control frames */
            if (!fin || len > 125) { ws_send_close(c, 1002, "Protocol error"); return; }
            if (op == 8) {
                int code = len >= 2 ? payload[0] << 8 | payload[1] : 1000;
                if (code < 1000 || code >= 5000) code = 1002;
                buf_consume(&c->in, pos + len);
                ws_send_close(c, code == 1002 ? 1002 : 1000, "");
                return;
            }
            if (op == 9) send_frame(c, 10, payload, len);
            else if (op == 10) c->alive = 1;
            else { ws_send_close(c, 1002, "Protocol error"); return; }
            buf_consume(&c->in, pos + len);
            continue;
        }
        if (op == 0) {
            if (!c->frag_op) { ws_send_close(c, 1002, "Protocol error"); return; }
        } else if (op == 1 || op == 2) {
            if (c->frag_op) { ws_send_close(c, 1002, "Protocol error"); return; }
            c->frag_op = op;
            c->frag.len = 0;
        } else { ws_send_close(c, 1002, "Protocol error"); return; }
        if (c->frag.len + len > MAX_MESSAGE_SIZE) { ws_send_close(c, 1009, "Message too big"); return; }
        buf_add(&c->frag, payload, (size_t)len);
        buf_consume(&c->in, pos + len);
        if (fin) {
            c->frag_op = 0;
            Buf msg = c->frag;
            buf_init(&c->frag);
            sig_on_message(c, msg.d ? msg.d : "", msg.len);
            buf_free(&msg);
        }
    }
}
