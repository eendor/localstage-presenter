/* LocalStage server entry point: listeners and the single-threaded poll loop. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"

App app;

#define HTTP_IDLE_MS 30000
#define LINGER_MS 2000
#define NETWORK_CHECK_MS 5000

static void conn_free(Conn *c) {
    if (c->st == CS_WS) sig_on_close(c);
    decks_conn_gone(c);
    if (c->upload_f) fclose(c->upload_f);
    sock_close(c->fd);
    buf_free(&c->in);
    buf_free(&c->out);
    buf_free(&c->frag);
    free(c);
}

void conn_close_lan(void) {
    for (int i = 0; i < app.nconns; i++)
        if (app.conns[i]->lan) app.conns[i]->dead = 1;
}

int lan_bind(const char *address) {
    if (app.lan_fd != SOCK_INVALID) { sock_close(app.lan_fd); app.lan_fd = SOCK_INVALID; }
    conn_close_lan();
    app.selected[0] = 0;
    if (!address || !*address) return 0;
    int err = 0;
    app.lan_fd = sock_listen(address, app.port, &err);
    if (app.lan_fd == SOCK_INVALID) {
        fprintf(stderr, "Could not listen on %s:%d: %s\n", address, app.port, sock_strerror(err));
        return -1;
    }
    snprintf(app.selected, sizeof app.selected, "%s", address);
    return 0;
}

static void accept_all(sock_t lfd, int lan) {
    for (;;) {
        char ip[64];
        sock_t fd = sock_accept(lfd, ip);
        if (fd == SOCK_INVALID) return;
        if (app.nconns >= 4096) { sock_close(fd); continue; }
        Conn *c = calloc(1, sizeof *c);
        if (!c) { sock_close(fd); continue; }
        c->fd = fd;
        c->lan = lan;
        c->content_length = -1;
        snprintf(c->ip, sizeof c->ip, "%s", ip);
        c->from_loopback = !lan && !strcmp(c->ip, "127.0.0.1");
        c->last_activity = now_mono_ms();
        if (app.nconns == app.capconns) {
            app.capconns = app.capconns ? app.capconns * 2 : 64;
            Conn **grown = realloc(app.conns, (size_t)app.capconns * sizeof *app.conns);
            if (!grown) { fputs("Out of memory\n", stderr); abort(); }
            app.conns = grown;
        }
        app.conns[app.nconns++] = c;
    }
}

static void on_readable(Conn *c) {
    static char tmp[65536];
    for (int rounds = 0; rounds < 16; rounds++) {
        long r = sock_read(c->fd, tmp, sizeof tmp);
        if (r == 0 || r == -2) { c->dead = 1; return; }
        if (r < 0) return;
        c->last_activity = now_mono_ms();
        if (c->st == CS_DRAIN || c->st == CS_WAIT) continue; /* discard */
        if (c->st == CS_WS && c->close_after_write) continue;
        buf_add(&c->in, tmp, (size_t)r);
        http_on_readable(c);
        if (c->dead) return;
        if (c->st == CS_HEAD && c->in.len > MAX_HEADER_BYTES) return;
    }
}

static void on_writable(Conn *c) {
    while (c->out.len) {
        long w = sock_write(c->fd, c->out.d, c->out.len);
        if (w == -2) { c->dead = 1; return; }
        if (w < 0) return;
        buf_consume(&c->out, (size_t)w);
    }
    if (c->close_after_write == 1) {
        if (c->st == CS_WS) { c->dead = 1; return; }
        /* Lingering close: let the peer read the response before we drop unread body bytes. */
        sock_shutdown_write(c->fd);
        c->close_after_write = 2;
        c->last_activity = now_mono_ms();
    }
}

/* Lets an embedding app (Android) ask for a fresh one-use presenter link. */
volatile int ls_link_request, ls_link_ready;
char ls_link[200];
char ls_shared_path[600];

int localstage_main(int argc, char **argv) {
    /* Windows runs deck conversion by re-launching this program. */
    if (argc == 5 && !strcmp(argv[1], "--convert")) {
        decks_convert_child(argv[2], argv[3], atoi(argv[4]));
        return 0;
    }
#ifdef _WIN32
    setvbuf(stdout, NULL, _IONBF, 0); /* Windows has no line buffering */
#else
    setvbuf(stdout, NULL, _IOLBF, 4096);
#endif
    plat_init(argv[0]);
    const char *port = getenv("PORT");
    char *end = NULL;
    long p = port && *port ? strtol(port, &end, 10) : 3000;
    if ((port && *port && *end) || p < 1 || p > 65535) { fprintf(stderr, "Invalid PORT\n"); return 1; }
    app.port = (int)p;
    app.lan_fd = app.loop_fd = SOCK_INVALID;
    decks_init();

    int err = 0;
    app.loop_fd = sock_listen("127.0.0.1", app.port, &err);
    if (app.loop_fd == SOCK_INVALID) {
        if (sock_err_in_use(err))
            fprintf(stderr, "Port %d is already in use. Another LocalStage instance may be running — close it or start with a different PORT.\n", app.port);
        else fprintf(stderr, "Could not listen on 127.0.0.1:%d: %s\n", app.port, sock_strerror(err));
        return 1;
    }
    char lan[64];
    if (net_default(lan, sizeof lan)) lan_bind(lan); /* retried by the network check if it fails */

    random_hex(app.bootstrap, 32);
    app.bootstrap_expires = now_mono_ms() + 5 * 60000;
    char url[160];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/host#%s", app.port, app.bootstrap);
    printf("Presenter (one-use link, expires in 5 minutes): %s\n", url);
    Iface list[32];
    int n = net_list(list, 32);
    for (int i = 0; i < n; i++) printf("%s: http://%s:%d/join\n", list[i].name, list[i].address, app.port);
    if (!n) printf("No network adapter found yet. Viewers can join once one connects.\n");
    const char *ob = getenv("OPEN_BROWSER");
#ifdef _WIN32
    /* Double-clicking the .exe should just work: open the browser unless told not to. */
    if (!ob || strcmp(ob, "0")) plat_open_browser(url);
    printf("\nLeave this window open while you present. Close it to stop LocalStage.\n");
#else
    if (ob && !strcmp(ob, "1")) plat_open_browser(url);
#endif

    int64_t next_heartbeat = now_mono_ms() + HEARTBEAT_MS, next_net = now_mono_ms() + NETWORK_CHECK_MS;
    plat_pollfd *pfds = NULL;
    int cap = 0;
    while (!plat_stopping()) {
        if (__atomic_load_n(&ls_link_request, __ATOMIC_ACQUIRE)) {
            random_hex(app.bootstrap, 32);
            app.bootstrap_expires = now_mono_ms() + 5 * 60000;
            snprintf(ls_link, sizeof ls_link, "http://127.0.0.1:%d/host#%s", app.port, app.bootstrap);
            __atomic_store_n(&ls_link_request, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&ls_link_ready, 1, __ATOMIC_RELEASE);
        }
        if (cap < app.nconns + 2) {
            cap = app.nconns + 66;
            pfds = realloc(pfds, (size_t)cap * sizeof *pfds);
            if (!pfds) { fputs("Out of memory\n", stderr); abort(); }
        }
        int np = 0;
        memset(pfds, 0, (size_t)cap * sizeof *pfds);
        pfds[np].fd = app.loop_fd; pfds[np++].events = POLLIN;
        int lan_slot = -1;
        if (app.lan_fd != SOCK_INVALID) { lan_slot = np; pfds[np].fd = app.lan_fd; pfds[np++].events = POLLIN; }
        int base = np;
        for (int i = 0; i < app.nconns; i++) {
            Conn *c = app.conns[i];
            pfds[np].fd = c->fd;
            pfds[np].events = POLLIN;
            if (c->out.len || c->close_after_write == 1) pfds[np].events |= POLLOUT;
            np++;
        }
        int nconns = app.nconns;
        if (plat_poll(pfds, np, 250) < 0 && errno != EINTR && !plat_stopping()) {
#ifdef _WIN32
            Sleep(10); /* WSAPoll can fail transiently on a socket closed mid-loop */
#else
            perror("poll");
            break;
#endif
        }

        for (int i = 0; i < nconns; i++) {
            Conn *c = app.conns[i];
            short re = pfds[base + i].revents;
            if (c->dead) continue;
            if (re & (POLLIN | POLLHUP | POLLERR)) on_readable(c);
            if (!c->dead && (c->out.len || c->close_after_write == 1)) on_writable(c);
        }
        if (pfds[0].revents & POLLIN) accept_all(app.loop_fd, 0);
        if (lan_slot >= 0 && app.lan_fd == pfds[lan_slot].fd && (pfds[lan_slot].revents & POLLIN)) accept_all(app.lan_fd, 1);

        decks_poll();
        int64_t now = now_mono_ms();
        if (now >= next_heartbeat) { sig_heartbeat(); next_heartbeat = now + HEARTBEAT_MS; }
        if (now >= next_net) {
            next_net = now + NETWORK_CHECK_MS;
            if (app.selected[0] && !net_has(app.selected)) {
                printf("Adapter %s went away.\n", app.selected);
                sig_end();
                if (net_default(lan, sizeof lan)) lan_bind(lan);
                else lan_bind(NULL);
                if (app.selected[0]) printf("Now listening on http://%s:%d/join\n", app.selected, app.port);
            } else if (!app.selected[0] && net_default(lan, sizeof lan) && lan_bind(lan) == 0) {
                printf("Viewers can join at http://%s:%d/join\n", app.selected, app.port);
            } else if (!app.manual_adapter && !app.session.active && net_default(lan, sizeof lan) &&
                       strcmp(lan, app.selected) && lan_bind(lan) == 0) {
                /* A hotspot or tether was turned on (or a better adapter appeared): follow it. */
                printf("Network changed. Viewers can join at http://%s:%d/join\n", app.selected, app.port);
            }
        }
        /* Timeouts and teardown. */
        for (int i = 0; i < app.nconns;) {
            Conn *c = app.conns[i];
            int64_t idle = now - c->last_activity;
            if (c->close_after_write == 2 && idle > LINGER_MS) c->dead = 1;
            else if ((c->st == CS_HEAD || c->st == CS_BODY || c->st == CS_UPLOAD || c->st == CS_DRAIN) && idle > HTTP_IDLE_MS) c->dead = 1;
            if (c->dead) {
                app.conns[i] = app.conns[--app.nconns];
                conn_free(c);
            } else i++;
        }
    }
    printf("\nShutting down.\n");
    sig_end();
    for (int i = 0; i < app.nconns; i++) on_writable(app.conns[i]);
    while (app.nconns) conn_free(app.conns[--app.nconns]);
    decks_shutdown();
    free(pfds);
    free(app.conns);
    return 0;
}

#ifndef LOCALSTAGE_NO_MAIN
int main(int argc, char **argv) {
    int rc = localstage_main(argc, argv);
#ifdef _WIN32
    /* Started by double-click: keep the window open so the error can be read. */
    if (rc != 0) { fputs("\nPress Enter to close.", stderr); getchar(); }
#endif
    return rc;
}
#endif
