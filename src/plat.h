/* Platform layer: sockets, polling, processes and file-system helpers for POSIX and Windows. */
#ifndef LS_PLAT_H
#define LS_PLAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
typedef WSAPOLLFD plat_pollfd;
#else
#include <poll.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
typedef struct pollfd plat_pollfd;
#endif

/* Process start-up: sockets, signals, PATH for bundled tools. argv0 is the program path. */
void plat_init(const char *argv0);
int plat_stopping(void); /* Ctrl+C / SIGTERM received */

/* Listening socket on IPv4 address:port. Returns SOCK_INVALID and sets *err (an errno-like code) on failure. */
sock_t sock_listen(const char *address, int port, int *err);
int sock_err_in_use(int err);
const char *sock_strerror(int err);
/* Non-blocking accept with TCP_NODELAY. Fills ip (64 bytes). */
sock_t sock_accept(sock_t lfd, char *ip);
/* > 0 bytes, 0 closed, -1 would block, -2 error. */
long sock_read(sock_t s, void *buf, size_t n);
long sock_write(sock_t s, const void *buf, size_t n);
void sock_shutdown_write(sock_t s);
void sock_close(sock_t s);
int plat_poll(plat_pollfd *fds, int n, int timeout_ms);

/* Run a program and wait. stdout goes to out_path (or is discarded), stderr to err_path.
   Returns its exit code, 127 when the program cannot be found, -1 on other failures. */
int plat_run(char *const argv[], const char *out_path, const char *err_path);

/* Deck conversion process (killed with all its children on timeout). */
typedef struct {
#ifdef _WIN32
    HANDLE proc, job;
#else
    int pid;
#endif
    int running;
} PlatProc;
/* Start "convert" in a child process. On POSIX it calls fn directly after fork; on Windows
   the program re-runs itself with --convert dir format max_pages and main() calls fn. */
int plat_spawn_convert(PlatProc *p, void (*fn)(const char *, const char *, int), const char *dir, const char *format, int max_pages);
/* 0 still running, 1 exited (sets *code). */
int plat_proc_poll(PlatProc *p, int *code);
void plat_proc_kill(PlatProc *p);

/* Create a fresh private directory under the system temp folder. */
int plat_mkdtemp(char *out, size_t n, const char *prefix);
int plat_mkdir(const char *path);
int plat_exists(const char *path);
/* file:// URL for a local path (LibreOffice profile). */
void plat_file_url(const char *path, char *out, size_t n);

void plat_open_browser(const char *url);

#endif
