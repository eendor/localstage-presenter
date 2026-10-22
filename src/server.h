/* LocalStage — shared server state and module interfaces. */
#ifndef LS_SERVER_H
#define LS_SERVER_H

#include <stdint.h>
#include <stdio.h>

#include "plat.h"

#include "json.h"
#include "util.h"

#define MAX_MESSAGE_SIZE (64 * 1024)
#define MAX_HEADER_BYTES (16 * 1024)
#define MAX_JSON_BODY 2048
#define DEFAULT_MAX_VIEWERS 5
#define PIN_LIMIT_ATTEMPTS 5
#define PIN_LIMIT_WINDOW_MS 60000
#define HEARTBEAT_MS 15000
#define MAX_DECKS 4
#define MAX_TOKENS 1024

typedef enum { CS_HEAD, CS_BODY, CS_UPLOAD, CS_WAIT, CS_WS, CS_DRAIN } ConnState;
typedef enum { ROLE_NONE, ROLE_HOST, ROLE_VIEWER } Role;

typedef struct Conn {
    sock_t fd;
    int lan;           /* accepted on the LAN listener */
    int from_loopback; /* peer address is 127.0.0.1 */
    char ip[64];
    ConnState st;
    Buf in, out;
    int close_after_write; /* close once out drains */
    int dead;              /* tear down at end of loop iteration */
    int64_t last_activity;

    /* Current HTTP request. */
    char method[8];
    char path[1024];
    char query[1024];
    char host[256], origin[256], auth[256], ctype[128], upgrade[64], wskey[64], wsver[8], te[64];
    long long content_length; /* -1 when absent */
    long long received;
    FILE *upload_f;

    /* WebSocket state. */
    Buf frag;
    int frag_op;
    int alive;
    Role role;
    char session_id[37];
    char viewer_id[37];
    char display_name[256];
    char member_token[65];
    char vstate[16];
    int64_t joined_at;
    double generation; /* last accepted rtc:offer generation for this viewer */
} Conn;

typedef struct {
    int active;
    char id[37];
    char title[512];
    char source[8]; /* "screen" | "deck" */
    char status[8]; /* waiting | live | paused | ended */
    int join_locked, review_locked, max_viewers;
    int64_t created_at;
    char pin[8]; /* empty when no PIN */
    int viewer_serial;
} Session;

typedef struct {
    char id[37];
    char dir[512];
    int total;
} Deck;

typedef struct {
    int active;
    char deck_id[37];
    long long revision;
    int total, slide;
    long long sequence;
} DeckState;

typedef struct {
    char token[65];
    char session_id[37];
    char display_name[256];
} Membership;

typedef struct {
    int port;
    sock_t loop_fd, lan_fd;
    char selected[64]; /* empty: no LAN listener */
    Conn **conns;
    int nconns, capconns;

    char bootstrap[65];
    int64_t bootstrap_expires;
    char host_token[65];

    Session session;
    Conn *host;
    Membership tokens[MAX_TOKENS];
    int ntokens;

    struct { char ip[64]; int count; int64_t first; } limiter[256];
    int nlimiter;

    Deck decks[MAX_DECKS];
    int ndecks;
    DeckState deck;
    long long deck_revision;
    long long max_upload;
    char max_upload_label[32];
    int max_pages;
    /* Running conversion. */
    PlatProc conv; /* conv.running: a conversion process is active */
    int64_t conv_deadline;
    Conn *conv_conn;
    char conv_dir[512];
    int conv_epoch, epoch;
} App;

extern App app;

/* http.c */
void http_on_readable(Conn *c);
void http_send(Conn *c, int status, const char *ctype, const void *body, size_t n, const char *extra);
void http_json(Conn *c, int status, const char *json);
void http_error(Conn *c, int status, const char *message);
int conn_is_host(Conn *c);
const char *bearer(Conn *c);

/* ws.c */
int ws_handshake(Conn *c);
void ws_on_data(Conn *c);
void ws_send_text(Conn *c, const char *s, size_t n);
void ws_send_close(Conn *c, int code, const char *reason);
void ws_ping(Conn *c);

/* signaling.c */
void sig_on_message(Conn *c, const char *text, size_t n);
void sig_on_close(Conn *c);
void sig_end(void);
void sig_heartbeat(void);
int sig_viewer_authorized(const char *token);
void sig_session_json(Buf *b);

/* decks.c */
void decks_init(void);
void decks_begin_upload(Conn *c);
void decks_upload_data(Conn *c, const char *data, size_t n);
void decks_upload_done(Conn *c);
void decks_poll(void);
void decks_conn_gone(Conn *c);
void decks_clear(void);
int decks_activate(const char *id);
int decks_navigate(const char *id, long long revision, long long sequence, long long slide);
void decks_serve_slide(Conn *c, const char *id, const char *slide);
void deck_state_json(Buf *b);
void decks_shutdown(void);
/* Conversion worker entry point (runs in the child process, never returns). */
void decks_convert_child(const char *dir, const char *format, int max_pages);

/* net.c */
typedef struct { char name[64]; char address[64]; } Iface;
int net_list(Iface *out, int max);
int net_default(char *out, size_t n);
int net_has(const char *address);

/* main.c */
int localstage_main(int argc, char **argv);
extern volatile int ls_link_request, ls_link_ready;
extern char ls_link[200];
extern char ls_shared_path[600]; /* Android: where a shared PDF is dropped */
int lan_bind(const char *address);
void conn_close_lan(void);
void net_info_json(Buf *b);

/* web.c (generated) */
extern const unsigned char web_index[];
extern const unsigned long web_index_len;
extern const unsigned char web_pdfjs[], web_pdfworker[];
extern const unsigned long web_pdfjs_len, web_pdfworker_len;

#endif
