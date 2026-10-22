/* LocalStage server entry point: listeners and the single-threaded poll loop. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "server.h"

App app;

#define HTTP_IDLE_MS 30000
#define LINGER_MS 2000
#define NETWORK_CHECK_MS 5000

static volatile sig_atomic_t stopping;

static void on_signal(int sig) { (void)sig; stopping = 1; }

static int listen_on(const char *address, int *err) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) { *err = errno; return -1; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)app.port);
    if (inet_pton(AF_INET, address, &sa.sin_addr) != 1 || bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(fd, 128) != 0) {
        *err = errno;
        close(fd);
        return -1;
    }
    return fd;
}

static void conn_free(Conn *c) {
    if (c->st == CS_WS) sig_on_close(c);
    decks_conn_gone(c);
    if (c->upload_fd >= 0) close(c->upload_fd);
    close(c->fd);
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
    if (app.lan_fd >= 0) { close(app.lan_fd); app.lan_fd = -1; }
    conn_close_lan();
    app.selected[0] = 0;
    if (!address || !*address) return 0;
    int err;
    app.lan_fd = listen_on(address, &err);
    if (app.lan_fd < 0) {
        fprintf(stderr, "Could not listen on %s:%d: %s\n", address, app.port, strerror(err));
        return -1;
    }
    snprintf(app.selected, sizeof app.selected, "%s", address);
    return 0;
}

static void accept_all(int lfd, int lan) {
    for (;;) {
        struct sockaddr_in sa;
        socklen_t sl = sizeof sa;
        int fd = accept4(lfd, (struct sockaddr *)&sa, &sl, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        if (app.nconns >= 4096) { close(fd); continue; }
        Conn *c = calloc(1, sizeof *c);
        if (!c) { close(fd); continue; }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        c->fd = fd;
        c->lan = lan;
        c->upload_fd = -1;
        c->content_length = -1;
        inet_ntop(AF_INET, &sa.sin_addr, c->ip, sizeof c->ip);
        c->from_loopback = !lan && !strcmp(c->ip, "127.0.0.1");
        c->last_activity = now_mono_ms();
        if (app.nconns == app.capconns) {
            app.capconns = app.capconns ? app.capconns * 2 : 64;
            app.conns = realloc(app.conns, (size_t)app.capconns * sizeof *app.conns);
        }
        app.conns[app.nconns++] = c;
    }
}

static void on_readable(Conn *c) {
    char tmp[65536];
    for (int rounds = 0; rounds < 16; rounds++) {
        ssize_t r = read(c->fd, tmp, sizeof tmp);
        if (r == 0) { c->dead = 1; return; }
        if (r < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) c->dead = 1;
            return;
        }
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
        ssize_t w = write(c->fd, c->out.d, c->out.len);
        if (w < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) c->dead = 1;
            return;
        }
        buf_consume(&c->out, (size_t)w);
    }
    if (c->close_after_write == 1) {
        if (c->st == CS_WS) { c->dead = 1; return; }
        /* Lingering close: let the peer read the response before we drop unread body bytes. */
        shutdown(c->fd, SHUT_WR);
        c->close_after_write = 2;
        c->last_activity = now_mono_ms();
    }
}

static void open_browser(const char *url) {
    /* Double fork so the browser launcher is never left as a zombie. */
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() != 0) _exit(0);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); }
#ifdef __APPLE__
        execlp("open", "open", url, (char *)NULL);
#else
        execlp("xdg-open", "xdg-open", url, (char *)NULL);
#endif
        _exit(127);
    }
    if (pid < 0) printf("Open the presenter link above manually.\n");
    else waitpid(pid, NULL, 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *port = getenv("PORT");
    char *end;
    long p = port && *port ? strtol(port, &end, 10) : 3000;
    if ((port && *port && *end) || p < 1 || p > 65535) { fprintf(stderr, "Invalid PORT\n"); return 1; }
    app.port = (int)p;
    app.lan_fd = app.loop_fd = -1;
    decks_init();

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int err;
    app.loop_fd = listen_on("127.0.0.1", &err);
    if (app.loop_fd < 0) {
        if (err == EADDRINUSE)
            fprintf(stderr, "Port %d is already in use. Another LocalStage instance may be running — close it or start with a different PORT.\n", app.port);
        else fprintf(stderr, "Could not listen on 127.0.0.1:%d: %s\n", app.port, strerror(err));
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
    if (ob && !strcmp(ob, "1")) open_browser(url);

    int64_t next_heartbeat = now_mono_ms() + HEARTBEAT_MS, next_net = now_mono_ms() + NETWORK_CHECK_MS;
    struct pollfd *pfds = NULL;
    int cap = 0;
    while (!stopping) {
        if (cap < app.nconns + 2) {
            cap = app.nconns + 66;
            pfds = realloc(pfds, (size_t)cap * sizeof *pfds);
        }
        int np = 0;
        pfds[np++] = (struct pollfd){app.loop_fd, POLLIN, 0};
        pfds[np++] = (struct pollfd){app.lan_fd, POLLIN, 0}; /* -1 is ignored by poll */
        int base = np;
        for (int i = 0; i < app.nconns; i++) {
            Conn *c = app.conns[i];
            short ev = POLLIN;
            if (c->out.len || c->close_after_write == 1) ev |= POLLOUT;
            pfds[np++] = (struct pollfd){c->fd, ev, 0};
        }
        int nconns = app.nconns;
        if (poll(pfds, (nfds_t)np, 250) < 0 && errno != EINTR) { perror("poll"); break; }

        for (int i = 0; i < nconns; i++) {
            Conn *c = app.conns[i];
            short re = pfds[base + i].revents;
            if (c->dead) continue;
            if (re & (POLLIN | POLLHUP | POLLERR)) on_readable(c);
            if (!c->dead && (c->out.len || c->close_after_write == 1)) on_writable(c);
        }
        if (pfds[0].revents & POLLIN) accept_all(app.loop_fd, 0);
        if (app.lan_fd >= 0 && (pfds[1].revents & POLLIN)) accept_all(app.lan_fd, 1);

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
