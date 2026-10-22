/* Platform layer implementation. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "plat.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static volatile sig_atomic_t stopping;
int plat_stopping(void) { return stopping; }

#ifdef _WIN32
/* ======================= Windows ======================= */
#include <direct.h>
#include <io.h>
#include <shellapi.h>

static BOOL WINAPI on_ctrl(DWORD type) {
    (void)type;
    stopping = 1;
    return TRUE;
}

void plat_init(const char *argv0) {
    (void)argv0;
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) { fprintf(stderr, "Winsock unavailable\n"); exit(1); }
    SetConsoleCtrlHandler(on_ctrl, TRUE);
    SetConsoleOutputCP(CP_UTF8);
    /* Let a poppler folder shipped next to the program provide pdfinfo/pdftoppm. */
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe);
    if (n == 0 || n >= sizeof exe) return;
    char *slash = strrchr(exe, '\\');
    if (!slash) return;
    *slash = 0;
    const char *old = getenv("PATH");
    size_t len = strlen(exe) * 3 + (old ? strlen(old) : 0) + 128;
    char *path = malloc(len);
    if (!path) return;
    snprintf(path, len, "%s\\poppler\\Library\\bin;%s\\poppler\\bin;%s;%s", exe, exe, exe, old ? old : "");
    SetEnvironmentVariableA("PATH", path);
    _putenv_s("PATH", path);
    free(path);
}

sock_t sock_listen(const char *address, int port, int *err) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { *err = WSAGetLastError(); return SOCK_INVALID; }
    /* Exclusive use: Windows SO_REUSEADDR would let another program steal the port. */
    BOOL one = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&one, sizeof one);
    SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)port);
    u_long nb = 1;
    if (inet_pton(AF_INET, address, &sa.sin_addr) != 1 || bind(s, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(s, 128) != 0 ||
        ioctlsocket(s, FIONBIO, &nb) != 0) {
        *err = WSAGetLastError();
        closesocket(s);
        return SOCK_INVALID;
    }
    return s;
}

int sock_err_in_use(int err) { return err == WSAEADDRINUSE || err == WSAEACCES; }

const char *sock_strerror(int err) {
    static char msg[256];
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)err, 0, msg, sizeof msg, NULL))
        snprintf(msg, sizeof msg, "error %d", err);
    size_t l = strlen(msg);
    while (l && (msg[l - 1] == '\n' || msg[l - 1] == '\r' || msg[l - 1] == '.')) msg[--l] = 0;
    return msg;
}

sock_t sock_accept(sock_t lfd, char *ip) {
    struct sockaddr_in sa;
    int sl = sizeof sa;
    SOCKET s = accept(lfd, (struct sockaddr *)&sa, &sl);
    if (s == INVALID_SOCKET) return SOCK_INVALID;
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    BOOL one = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
    inet_ntop(AF_INET, &sa.sin_addr, ip, 64);
    return s;
}

long sock_read(sock_t s, void *buf, size_t n) {
    int r = recv(s, buf, n > 1 << 30 ? 1 << 30 : (int)n, 0);
    if (r >= 0) return r;
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR ? -1 : -2;
}

long sock_write(sock_t s, const void *buf, size_t n) {
    int r = send(s, buf, n > 1 << 30 ? 1 << 30 : (int)n, 0);
    if (r >= 0) return r;
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR ? -1 : -2;
}

void sock_shutdown_write(sock_t s) { shutdown(s, SD_SEND); }
void sock_close(sock_t s) { closesocket(s); }

int plat_poll(plat_pollfd *fds, int n, int timeout_ms) {
    if (n == 0) { Sleep((DWORD)timeout_ms); return 0; }
    return WSAPoll(fds, (ULONG)n, timeout_ms);
}

/* Quote one argument for CreateProcess (MSVC runtime rules). */
static void quote_arg(Buf *b, const char *a) {
    if (*a && !strpbrk(a, " \t\n\v\"")) { buf_puts(b, a); return; }
    buf_puts(b, "\"");
    for (const char *p = a;; p++) {
        size_t bs = 0;
        while (*p == '\\') { bs++; p++; }
        if (!*p) { for (size_t i = 0; i < bs * 2; i++) buf_puts(b, "\\"); break; }
        if (*p == '"') { for (size_t i = 0; i < bs * 2 + 1; i++) buf_puts(b, "\\"); }
        else for (size_t i = 0; i < bs; i++) buf_puts(b, "\\");
        buf_add(b, p, 1);
    }
    buf_puts(b, "\"");
}

static HANDLE open_inheritable(const char *path, int write) {
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    if (!path) path = "NUL";
    return CreateFileA(path, write ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                       write ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* Start a process with redirected stdio; returns 0 on success, 127 if not found. */
static int start(char *const argv[], const char *out_path, const char *err_path, DWORD flags, PROCESS_INFORMATION *pi) {
    Buf cmd = {0};
    for (int i = 0; argv[i]; i++) {
        if (i) buf_puts(&cmd, " ");
        quote_arg(&cmd, argv[i]);
    }
    /* Resolve the program on PATH ourselves so "not found" is reported clearly. */
    char exe[MAX_PATH];
    char *file;
    const char *prog = argv[0];
    int found = SearchPathA(NULL, prog, ".exe", sizeof exe, exe, &file) > 0;
    STARTUPINFOA si = {0};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = open_inheritable(NULL, 0);
    si.hStdOutput = open_inheritable(out_path, 1);
    si.hStdError = err_path ? open_inheritable(err_path, 1) : open_inheritable(NULL, 1);
    int rc = 0;
    if (!found) rc = 127;
    else if (!CreateProcessA(exe, cmd.d, NULL, NULL, TRUE, flags | CREATE_NO_WINDOW, NULL, NULL, &si, pi)) rc = -1;
    CloseHandle(si.hStdInput);
    CloseHandle(si.hStdOutput);
    CloseHandle(si.hStdError);
    buf_free(&cmd);
    return rc;
}

int plat_run(char *const argv[], const char *out_path, const char *err_path) {
    PROCESS_INFORMATION pi;
    int rc = start(argv, out_path, err_path, 0, &pi);
    if (rc) {
        if (rc == 127 && err_path) {
            FILE *f = fopen(err_path, "w");
            if (f) { fprintf(f, "Could not run %s: not found", argv[0]); fclose(f); }
        }
        return rc;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

int plat_spawn_convert(PlatProc *p, void (*fn)(const char *, const char *, int), const char *dir, const char *format, int max_pages) {
    (void)fn;
    char self[MAX_PATH], pages[16];
    DWORD n = GetModuleFileNameA(NULL, self, sizeof self);
    if (n == 0 || n >= sizeof self) return -1;
    snprintf(pages, sizeof pages, "%d", max_pages);
    char *argv[] = {self, "--convert", (char *)dir, (char *)format, pages, NULL};
    /* A job object lets a timeout kill LibreOffice and other helpers too. */
    p->job = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {0};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (p->job) SetInformationJobObject(p->job, JobObjectExtendedLimitInformation, &li, sizeof li);
    PROCESS_INFORMATION pi;
    if (start(argv, NULL, NULL, CREATE_SUSPENDED, &pi) != 0) {
        if (p->job) CloseHandle(p->job);
        return -1;
    }
    if (p->job) AssignProcessToJobObject(p->job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    p->proc = pi.hProcess;
    p->running = 1;
    return 0;
}

int plat_proc_poll(PlatProc *p, int *code) {
    if (!p->running) return 1;
    if (WaitForSingleObject(p->proc, 0) == WAIT_TIMEOUT) return 0;
    DWORD c = 1;
    GetExitCodeProcess(p->proc, &c);
    *code = (int)c;
    CloseHandle(p->proc);
    if (p->job) { TerminateJobObject(p->job, 1); CloseHandle(p->job); }
    p->running = 0;
    return 1;
}

void plat_proc_kill(PlatProc *p) {
    if (!p->running) return;
    if (p->job) TerminateJobObject(p->job, 1);
    else TerminateProcess(p->proc, 1);
    WaitForSingleObject(p->proc, 5000);
    CloseHandle(p->proc);
    if (p->job) CloseHandle(p->job);
    p->running = 0;
}

int plat_mkdtemp(char *out, size_t n, const char *prefix) {
    char base[MAX_PATH];
    DWORD l = GetTempPathA(sizeof base, base);
    if (l == 0 || l >= sizeof base) return -1;
    for (int tries = 0; tries < 16; tries++) {
        char rnd[13];
        random_hex(rnd, 6);
        snprintf(out, n, "%s%s%s", base, prefix, rnd);
        if (CreateDirectoryA(out, NULL)) return 0;
        if (GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    out[0] = 0;
    return -1;
}

int plat_mkdir(const char *path) { return CreateDirectoryA(path, NULL) ? 0 : -1; }
int plat_exists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

void plat_file_url(const char *path, char *out, size_t n) {
    size_t o = (size_t)snprintf(out, n, "file:///");
    for (const char *p = path; *p && o + 1 < n; p++) out[o++] = *p == '\\' ? '/' : *p;
    out[o < n ? o : n - 1] = 0;
}

void plat_open_browser(const char *url) {
    if ((INT_PTR)ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL) <= 32) printf("Open the presenter link above manually.\n");
}

#else
/* ======================= POSIX ======================= */
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void on_signal(int sig) { (void)sig; stopping = 1; }

void plat_init(const char *argv0) {
    (void)argv0;
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

sock_t sock_listen(const char *address, int port, int *err) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { *err = errno; return -1; }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, address, &sa.sin_addr) != 1 || bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(fd, 128) != 0) {
        *err = errno;
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

int sock_err_in_use(int err) { return err == EADDRINUSE; }
const char *sock_strerror(int err) { return strerror(err); }

sock_t sock_accept(sock_t lfd, char *ip) {
    struct sockaddr_in sa;
    socklen_t sl = sizeof sa;
    int fd = accept(lfd, (struct sockaddr *)&sa, &sl);
    if (fd < 0) return -1;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    inet_ntop(AF_INET, &sa.sin_addr, ip, 64);
    return fd;
}

long sock_read(sock_t s, void *buf, size_t n) {
    ssize_t r = read(s, buf, n);
    if (r >= 0) return (long)r;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? -1 : -2;
}

long sock_write(sock_t s, const void *buf, size_t n) {
    ssize_t r = write(s, buf, n);
    if (r >= 0) return (long)r;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? -1 : -2;
}

void sock_shutdown_write(sock_t s) { shutdown(s, SHUT_WR); }
void sock_close(sock_t s) { close(s); }
int plat_poll(plat_pollfd *fds, int n, int timeout_ms) { return poll(fds, (nfds_t)n, timeout_ms); }

int plat_run(char *const argv[], const char *out_path, const char *err_path) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int in = open("/dev/null", O_RDONLY);
        int out = out_path ? open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600) : open("/dev/null", O_WRONLY);
        int err = err_path ? open(err_path, O_WRONLY | O_CREAT | O_TRUNC, 0600) : open("/dev/null", O_WRONLY);
        if (in < 0 || out < 0 || err < 0) _exit(126);
        dup2(in, 0);
        dup2(out, 1);
        dup2(err, 2);
        execvp(argv[0], argv);
        dprintf(2, "Could not run %s: %s", argv[0], strerror(errno));
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

int plat_spawn_convert(PlatProc *p, void (*fn)(const char *, const char *, int), const char *dir, const char *format, int max_pages) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setpgid(0, 0); /* own group, so a timeout kills LibreOffice helpers too */
        signal(SIGPIPE, SIG_DFL);
        signal(SIGINT, SIG_IGN);
        fn(dir, format, max_pages);
        _exit(0);
    }
    setpgid(pid, pid);
    p->pid = pid;
    p->running = 1;
    return 0;
}

int plat_proc_poll(PlatProc *p, int *code) {
    if (!p->running) return 1;
    int status;
    pid_t r = waitpid(p->pid, &status, WNOHANG);
    if (r == 0 || (r < 0 && errno == EINTR)) return 0;
    kill(-p->pid, SIGKILL); /* reap anything left in the group */
    *code = r > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    p->running = 0;
    return 1;
}

void plat_proc_kill(PlatProc *p) {
    if (!p->running) return;
    int status;
    kill(-p->pid, SIGKILL);
    waitpid(p->pid, &status, 0);
    p->running = 0;
}

int plat_mkdtemp(char *out, size_t n, const char *prefix) {
    const char *tmp = getenv("TMPDIR");
    snprintf(out, n, "%s/%sXXXXXX", tmp && *tmp ? tmp : "/tmp", prefix);
    if (mkdtemp(out)) return 0;
    out[0] = 0;
    return -1;
}

int plat_mkdir(const char *path) { return mkdir(path, 0700); }
int plat_exists(const char *path) { return access(path, F_OK) == 0; }
void plat_file_url(const char *path, char *out, size_t n) { snprintf(out, n, "file://%s", path); }

void plat_open_browser(const char *url) {
    /* Double fork so the launcher is never left as a zombie. */
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
#endif
