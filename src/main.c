/*
 * proofgate: the socket shell around the verified core.
 *
 * Everything in this file is ordinary, unverified C. It owns sockets, timers
 * and connection state, and hands every untrusted byte to the verified
 * parsers in http.c, api.c and modbus.c.
 */
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "proofgate/api.h"
#include "proofgate/http.h"
#include "proofgate/modbus.h"

#define MAX_CONNS 64
#define TX_CAP 4096u
#define IDLE_TIMEOUT_MS 10000
#define UPSTREAM_TIMEOUT_MS 1000

typedef struct {
    int fd;
    size_t rx_len;
    size_t tx_len;
    size_t tx_off;
    bool close_after;
    int64_t last_active_ms;
    uint8_t rx[HTTP_MAX_HEAD];
    uint8_t tx[TX_CAP];
} conn;

typedef struct {
    const char *bind_addr;
    const char *port;
    const char *upstream_host;
    const char *upstream_port;
} config;

static conn g_conns[MAX_CONNS];
static uint16_t g_txid;
static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void respond(conn *c, const char *status, const char *extra, const void *body,
                    size_t body_len, bool close_after)
{
    int n = snprintf((char *)c->tx, TX_CAP,
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: %s\r\n"
                     "Cache-Control: no-store\r\n"
                     "X-Content-Type-Options: nosniff\r\n"
                     "%s\r\n",
                     status, body_len, close_after ? "close" : "keep-alive", extra);
    if (n < 0 || (size_t)n > TX_CAP || TX_CAP - (size_t)n < body_len) {
        c->tx_len = 0;
        c->tx_off = 0;
        c->close_after = true;
        return;
    }
    memcpy(c->tx + (size_t)n, body, body_len);
    c->tx_len = (size_t)n + body_len;
    c->tx_off = 0;
    c->close_after = close_after;
}

static void respond_error(conn *c, const char *status, const char *code, bool close_after)
{
    char body[96];
    int n = snprintf(body, sizeof body, "{\"error\":\"%s\"}", code);
    respond(c, status, "", body, n > 0 ? (size_t)n : 0u, close_after);
}

/* Waits for fd to become ready, sharing one deadline across a whole exchange. */
static bool wait_fd(int fd, short events, int64_t deadline)
{
    for (;;) {
        int64_t left = deadline - now_ms();
        if (left <= 0) {
            return false;
        }
        struct pollfd p = { .fd = fd, .events = events, .revents = 0 };
        int r = poll(&p, 1, (int)left);
        if (r > 0) {
            return true;
        }
        if (r == 0 || errno != EINTR) {
            return false;
        }
    }
}

static int upstream_connect(const config *cfg, int64_t deadline)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(cfg->upstream_host, cfg->upstream_port, &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *a = res; a != NULL; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (set_nonblocking(fd) == 0) {
            int r = connect(fd, a->ai_addr, a->ai_addrlen);
            if (r == 0) {
                break;
            }
            if (errno == EINPROGRESS && wait_fd(fd, POLLOUT, deadline)) {
                int err = 0;
                socklen_t len = sizeof err;
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                    break;
                }
            }
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

typedef enum {
    UP_OK,
    UP_EXCEPTION,
    UP_FAILED,
    UP_TIMEOUT
} upstream_result;

static upstream_result upstream_read(const config *cfg, const api_route *route,
                                     uint16_t *regs, uint8_t *exception)
{
    int64_t deadline = now_ms() + UPSTREAM_TIMEOUT_MS;
    uint8_t req[MB_READ_REQ_LEN];
    uint8_t buf[MB_MAX_ADU];
    size_t have = 0;
    size_t sent = 0;
    uint16_t txid = ++g_txid;

    if (mb_encode_read(req, sizeof req, txid, route->unit, route->fc, route->addr,
                       route->count) != MB_OK) {
        return UP_FAILED;
    }

    int fd = upstream_connect(cfg, deadline);
    if (fd < 0) {
        return now_ms() >= deadline ? UP_TIMEOUT : UP_FAILED;
    }

    upstream_result result = UP_FAILED;
    while (sent < sizeof req) {
        ssize_t n = send(fd, req + sent, sizeof req - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_fd(fd, POLLOUT, deadline)) {
                result = UP_TIMEOUT;
                goto done;
            }
        } else if (!(n < 0 && errno == EINTR)) {
            goto done;
        }
    }

    for (;;) {
        size_t consumed = 0;
        mb_status st = mb_decode_read(buf, have, txid, route->unit, route->fc, route->count,
                                      regs, MB_MAX_REGS, exception, &consumed);
        if (st == MB_OK) {
            result = UP_OK;
            goto done;
        }
        if (st == MB_EXCEPTION) {
            result = UP_EXCEPTION;
            goto done;
        }
        if (st != MB_INCOMPLETE || have == sizeof buf) {
            goto done;
        }
        if (!wait_fd(fd, POLLIN, deadline)) {
            result = UP_TIMEOUT;
            goto done;
        }
        ssize_t n = recv(fd, buf + have, sizeof buf - have, 0);
        if (n > 0) {
            have += (size_t)n;
        } else if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
            goto done;
        }
    }

done:
    close(fd);
    return result;
}

static void handle_request(conn *c, const config *cfg, const http_request *req)
{
    bool close_after = !req->keep_alive;
    api_route route;

    if (req->content_length != 0u) {
        respond_error(c, "413 Content Too Large", "body_not_accepted", true);
        return;
    }
    if (req->method != HTTP_M_GET) {
        respond(c, "405 Method Not Allowed", "Allow: GET\r\n", "{\"error\":\"method\"}", 18u,
                close_after);
        return;
    }

    switch (api_parse_route(req->path, req->query, &route)) {
    case API_OK:
        break;
    case API_NOT_FOUND:
        respond_error(c, "404 Not Found", "not_found", close_after);
        return;
    case API_BAD_REQUEST:
    default:
        respond_error(c, "400 Bad Request", "bad_request", close_after);
        return;
    }

    if (route.kind == API_ROUTE_HEALTH) {
        respond(c, "200 OK", "", "{\"status\":\"ok\"}", 15u, close_after);
        return;
    }
    if (cfg->upstream_host == NULL) {
        respond_error(c, "503 Service Unavailable", "no_upstream", close_after);
        return;
    }

    uint16_t regs[MB_MAX_REGS];
    uint8_t exception = 0;
    switch (upstream_read(cfg, &route, regs, &exception)) {
    case UP_OK: {
        uint8_t body[TX_CAP / 2u];
        size_t n = api_render_registers(body, sizeof body, &route, regs);
        if (n == 0u) {
            respond_error(c, "500 Internal Server Error", "render", true);
        } else {
            respond(c, "200 OK", "", body, n, close_after);
        }
        return;
    }
    case UP_EXCEPTION: {
        char body[64];
        int n = snprintf(body, sizeof body,
                         "{\"error\":\"modbus_exception\",\"code\":%u}", exception);
        respond(c, "502 Bad Gateway", "", body, n > 0 ? (size_t)n : 0u, close_after);
        return;
    }
    case UP_TIMEOUT:
        respond_error(c, "504 Gateway Timeout", "upstream_timeout", close_after);
        return;
    case UP_FAILED:
    default:
        respond_error(c, "502 Bad Gateway", "upstream_failed", close_after);
        return;
    }
}

static void process_rx(conn *c, const config *cfg)
{
    while (c->tx_len == 0u && !c->close_after) {
        http_request req;
        switch (http_parse_request(c->rx, c->rx_len, &req)) {
        case HTTP_OK: {
            size_t used = req.head_len;
            handle_request(c, cfg, &req);
            memmove(c->rx, c->rx + used, c->rx_len - used);
            c->rx_len -= used;
            break;
        }
        case HTTP_INCOMPLETE:
            return;
        case HTTP_TOO_LARGE:
            respond_error(c, "431 Request Header Fields Too Large", "too_large", true);
            return;
        case HTTP_UNSUPPORTED:
            respond_error(c, "501 Not Implemented", "transfer_encoding", true);
            return;
        case HTTP_BAD:
        default:
            respond_error(c, "400 Bad Request", "malformed", true);
            return;
        }
    }
}

static void conn_close(conn *c)
{
    close(c->fd);
    c->fd = -1;
    c->rx_len = 0;
    c->tx_len = 0;
    c->tx_off = 0;
    c->close_after = false;
}

/* Returns false when the connection has been closed. */
static bool conn_flush(conn *c)
{
    while (c->tx_off < c->tx_len) {
        ssize_t n = send(c->fd, c->tx + c->tx_off, c->tx_len - c->tx_off, MSG_NOSIGNAL);
        if (n > 0) {
            c->tx_off += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        } else {
            conn_close(c);
            return false;
        }
    }
    c->tx_len = 0;
    c->tx_off = 0;
    if (c->close_after) {
        conn_close(c);
        return false;
    }
    return true;
}

/* Alternates flushing and parsing so pipelined requests already in rx are served. */
static void conn_pump(conn *c, const config *cfg)
{
    while (c->fd >= 0 && c->tx_len > 0u) {
        if (!conn_flush(c) || c->tx_len > 0u) {
            return;
        }
        process_rx(c, cfg);
    }
}

static void conn_readable(conn *c, const config *cfg)
{
    if (c->rx_len == sizeof c->rx) {
        return; /* a response is still queued; poll will come back */
    }
    ssize_t n = recv(c->fd, c->rx + c->rx_len, sizeof c->rx - c->rx_len, 0);
    if (n > 0) {
        c->rx_len += (size_t)n;
        c->last_active_ms = now_ms();
        process_rx(c, cfg);
    } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        conn_close(c);
    }
}

static int listen_on(const config *cfg)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    int rc = getaddrinfo(cfg->bind_addr, cfg->port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "proofgate: %s\n", gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *a = res; a != NULL; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) {
            continue;
        }
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        if (bind(fd, a->ai_addr, a->ai_addrlen) == 0 && listen(fd, 64) == 0 &&
            set_nonblocking(fd) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        perror("proofgate: listen");
    }
    return fd;
}

static void accept_all(int lfd)
{
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) {
            return;
        }
        conn *slot = NULL;
        for (size_t i = 0; i < MAX_CONNS; i++) {
            if (g_conns[i].fd < 0) {
                slot = &g_conns[i];
                break;
            }
        }
        if (slot == NULL || set_nonblocking(fd) != 0) {
            close(fd);
            continue;
        }
        slot->fd = fd;
        slot->rx_len = 0;
        slot->tx_len = 0;
        slot->tx_off = 0;
        slot->close_after = false;
        slot->last_active_ms = now_ms();
    }
}

static bool split_host_port(char *arg, const char **host, const char **port)
{
    char *colon = strrchr(arg, ':');
    if (colon == NULL || colon == arg || colon[1] == '\0') {
        return false;
    }
    *colon = '\0';
    *host = arg;
    *port = colon + 1;
    return true;
}

static void usage(void)
{
    fputs("usage: proofgate [--bind ADDR] [--port PORT] [--modbus HOST:PORT]\n"
          "  --bind    listen address (default 127.0.0.1)\n"
          "  --port    listen port (default 8080)\n"
          "  --modbus  Modbus TCP device to read from\n",
          stderr);
}

int main(int argc, char **argv)
{
    config cfg = { "127.0.0.1", "8080", NULL, NULL };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            cfg.bind_addr = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            cfg.port = argv[++i];
        } else if (strcmp(argv[i], "--modbus") == 0 && i + 1 < argc) {
            if (!split_host_port(argv[++i], &cfg.upstream_host, &cfg.upstream_port)) {
                usage();
                return 2;
            }
        } else {
            usage();
            return 2;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    for (size_t i = 0; i < MAX_CONNS; i++) {
        g_conns[i].fd = -1;
    }

    int lfd = listen_on(&cfg);
    if (lfd < 0) {
        return 1;
    }
    fprintf(stderr, "proofgate: listening on %s:%s\n", cfg.bind_addr, cfg.port);

    while (!g_stop) {
        struct pollfd fds[MAX_CONNS + 1];
        size_t owner[MAX_CONNS + 1];
        nfds_t n = 1;
        fds[0].fd = lfd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        owner[0] = 0;

        for (size_t i = 0; i < MAX_CONNS; i++) {
            if (g_conns[i].fd >= 0) {
                fds[n].fd = g_conns[i].fd;
                fds[n].events = g_conns[i].tx_len > 0u ? POLLOUT : POLLIN;
                fds[n].revents = 0;
                owner[n] = i;
                n++;
            }
        }

        int ready = poll(fds, n, 250);
        if (ready < 0 && errno != EINTR) {
            perror("proofgate: poll");
            break;
        }

        if (ready > 0) {
            for (nfds_t k = 1; k < n; k++) {
                conn *c = &g_conns[owner[k]];
                if (c->fd < 0 || fds[k].revents == 0) {
                    continue;
                }
                if ((fds[k].revents & POLLOUT) != 0) {
                    c->last_active_ms = now_ms();
                } else {
                    conn_readable(c, &cfg);
                }
                conn_pump(c, &cfg);
            }
            if ((fds[0].revents & POLLIN) != 0) {
                accept_all(lfd);
            }
        }

        int64_t now = now_ms();
        for (size_t i = 0; i < MAX_CONNS; i++) {
            if (g_conns[i].fd >= 0 && now - g_conns[i].last_active_ms > IDLE_TIMEOUT_MS) {
                conn_close(&g_conns[i]);
            }
        }
    }

    for (size_t i = 0; i < MAX_CONNS; i++) {
        if (g_conns[i].fd >= 0) {
            conn_close(&g_conns[i]);
        }
    }
    close(lfd);
    return 0;
}
