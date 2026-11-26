/* Read-only HTTP state bridge and atomic JSON state file. */
#include "anvil.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct Bridge {
    Editor *ed;
    int listen_fd;
    int port;
    pthread_t thread;
    bool thread_started;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    char *json;
    size_t json_len;
    long seq;
    double last_timestamp;
    bool closed;
    bool stop;
    double started;
};

typedef struct {
    Bridge *bridge;
    int fd;
} ClientCtx;

static void mkdir_p(const char *dir) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    if (len == 0) return;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

void bridge_write_state_file(const char *path, const char *json, char *err,
                             size_t errlen) {
    if (!path || !*path) return;
    char expanded[PATH_MAX];
    path_expand(path, expanded, sizeof(expanded));
    char dir[PATH_MAX];
    path_dirname(expanded, dir, sizeof(dir));
    mkdir_p(dir);

    char tmp[PATH_MAX * 2];
    snprintf(tmp, sizeof(tmp), "%s/.%s.XXXXXX", dir, path_basename(expanded));
    int fd = mkstemp(tmp);
    if (fd < 0) {
        if (err) snprintf(err, errlen, "%s", strerror(errno));
        return;
    }
    size_t len = strlen(json);
    size_t done = 0;
    bool ok = true;
    while (done < len) {
        ssize_t n = write(fd, json + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        done += (size_t)n;
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok && rename(tmp, expanded) != 0) ok = false;
    if (!ok) {
        if (err) snprintf(err, errlen, "%s", strerror(errno));
        unlink(tmp);
    }
}

void bridge_publish(Bridge *b, const char *json, long seq) {
    if (!b) return;
    pthread_mutex_lock(&b->lock);
    free(b->json);
    b->json = xstrdup(json ? json : "{}");
    b->json_len = strlen(b->json);
    b->seq = seq;
    b->last_timestamp = now_sec();
    pthread_cond_broadcast(&b->cond);
    pthread_mutex_unlock(&b->lock);
}

long bridge_seq(Bridge *b) {
    if (!b) return 0;
    pthread_mutex_lock(&b->lock);
    long n = b->seq;
    pthread_mutex_unlock(&b->lock);
    return n;
}

int bridge_port(Bridge *b) {
    return b ? b->port : 0;
}

bool bridge_closed(Bridge *b) {
    if (!b) return true;
    pthread_mutex_lock(&b->lock);
    bool closed = b->closed;
    pthread_mutex_unlock(&b->lock);
    return closed;
}

static char *state_copy(Bridge *b, long wait_since, double timeout,
                        long *out_seq, double *out_ts) {
    pthread_mutex_lock(&b->lock);
    if (wait_since >= 0 && timeout > 0.0 && b->seq <= wait_since && !b->closed) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        double seconds = timeout;
        long whole = (long)seconds;
        long nanos = (long)((seconds - (double)whole) * 1e9);
        deadline.tv_sec += whole;
        deadline.tv_nsec += nanos;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        while (b->seq <= wait_since && !b->closed) {
            int rc = pthread_cond_timedwait(&b->cond, &b->lock, &deadline);
            if (rc == ETIMEDOUT) break;
        }
    }
    char *copy = xstrdup(b->json ? b->json : "{}");
    if (out_seq) *out_seq = b->seq;
    if (out_ts) *out_ts = b->last_timestamp;
    pthread_mutex_unlock(&b->lock);
    return copy;
}

/* ---------------------------------------------------------------- JSON scan */

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *skip_string(const char *p) {
    if (!p || *p != '"') return p;
    p++;
    while (*p) {
        if (*p == '\\' && p[1]) { p += 2; continue; }
        if (*p == '"') return p + 1;
        p++;
    }
    return p;
}

static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (!p || !*p) return p;
    if (*p == '"') return skip_string(p);
    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = open == '{' ? '}' : ']';
        int depth = 0;
        while (*p) {
            if (*p == '"') { p = skip_string(p); continue; }
            if (*p == open) depth++;
            else if (*p == close) {
                depth--;
                if (depth == 0) return p + 1;
            }
            p++;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}

static char *json_extract(const char *json, const char *key) {
    if (!json || !key) return NULL;
    const char *p = skip_ws(json);
    if (*p != '{') return NULL;
    p++;
    size_t keylen = strlen(key);
    while (*p) {
        p = skip_ws(p);
        if (*p == '}') break;
        if (*p != '"') break;
        const char *key_start = p + 1;
        const char *key_end = skip_string(p);
        if (!key_end) break;
        size_t klen = (size_t)(key_end - key_start - 1);
        p = skip_ws(key_end);
        if (*p != ':') break;
        p++;
        p = skip_ws(p);
        const char *value_start = p;
        const char *value_end = skip_value(p);
        if (klen == keylen && strncmp(key_start, key, keylen) == 0) {
            return xstrndup(value_start, (size_t)(value_end - value_start));
        }
        p = skip_ws(value_end);
        if (*p == ',') { p++; continue; }
        break;
    }
    return NULL;
}

/* ---------------------------------------------------------------- HTTP I/O */

static void send_all(int fd, const char *data, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = send(fd, data + done, len - done, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        done += (size_t)n;
    }
}

static void send_response(int fd, int status, const char *content_type,
                          const char *body, size_t body_len) {
    const char *reason = status == 200 ? "OK" : status == 204 ? "No Content" :
                         status == 404 ? "Not Found" : status == 400 ? "Bad Request" :
                         "Internal Server Error";
    StrBuf head;
    sb_init(&head);
    sb_addf(&head, "HTTP/1.1 %d %s\r\n", status, reason);
    if (body_len > 0) sb_addf(&head, "Content-Type: %s\r\n", content_type);
    sb_addf(&head, "Content-Length: %zu\r\n", body_len);
    sb_adds(&head, "Cache-Control: no-store\r\n");
    sb_adds(&head, "Access-Control-Allow-Origin: *\r\n");
    sb_adds(&head, "Access-Control-Allow-Methods: GET, OPTIONS\r\n");
    sb_adds(&head, "Access-Control-Allow-Headers: Content-Type\r\n");
    sb_adds(&head, "Connection: close\r\n\r\n");
    send_all(fd, head.data ? head.data : "", head.len);
    if (body_len > 0) send_all(fd, body, body_len);
    sb_free(&head);
}

static void send_json(int fd, const char *json) {
    send_response(fd, 200, "application/json; charset=utf-8", json, strlen(json));
}

static int query_int(const char *query, const char *key, int def) {
    if (!query) return def;
    size_t klen = strlen(key);
    const char *p = query;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        const char *eq = strchr(p, '=');
        if (eq && (!amp || eq < amp)) {
            size_t n = (size_t)(eq - p);
            if (n == klen && strncmp(p, key, klen) == 0) {
                return atoi(eq + 1);
            }
        }
        p = amp ? amp + 1 : NULL;
    }
    return def;
}

static double query_double(const char *query, const char *key, double def) {
    if (!query) return def;
    size_t klen = strlen(key);
    const char *p = query;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        const char *eq = strchr(p, '=');
        if (eq && (!amp || eq < amp)) {
            size_t n = (size_t)(eq - p);
            if (n == klen && strncmp(p, key, klen) == 0) {
                return atof(eq + 1);
            }
        }
        p = amp ? amp + 1 : NULL;
    }
    return def;
}

static bool query_bool(const char *query, const char *key) {
    if (!query) return false;
    size_t klen = strlen(key);
    const char *p = query;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        const char *eq = strchr(p, '=');
        if (eq && (!amp || eq < amp)) {
            size_t n = (size_t)(eq - p);
            if (n == klen && strncmp(p, key, klen) == 0) {
                const char *v = eq + 1;
                return strncmp(v, "1", 1) == 0 || strncasecmp(v, "true", 4) == 0 ||
                       strncasecmp(v, "yes", 3) == 0 || strncasecmp(v, "on", 2) == 0;
            }
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

static void handle_subset(int fd, Bridge *b, const char *path) {
    long seq = 0;
    double ts = 0;
    char *state = state_copy(b, -1, 0, &seq, &ts);
    const char *key = NULL;
    bool redo = false;
    if (strcmp(path, "/undo") == 0) key = "undo";
    else if (strcmp(path, "/redo") == 0) { key = "undo"; redo = true; }
    else if (strcmp(path, "/clipboard") == 0) key = "clipboard";
    else if (strcmp(path, "/stats") == 0) key = "stats";
    else if (strcmp(path, "/file") == 0) key = "file";
    else if (strcmp(path, "/autosave") == 0) key = "autosave";
    else if (strcmp(path, "/ui") == 0) key = "ui";
    else if (strcmp(path, "/document") == 0) key = "document";

    char *value = key ? json_extract(state, key) : NULL;
    if (redo && value) {
        char *nested = json_extract(value, "redo_stack");
        free(value);
        value = nested;
    }
    StrBuf out;
    sb_init(&out);
    sb_addf(&out, "{\"seq\":%ld,\"timestamp\":%.3f,", seq, ts);
    const char *label = path + 1;
    sb_json_str(&out, label);
    sb_addc(&out, ':');
    if (value) sb_adds(&out, value);
    else sb_adds(&out, "{}");
    sb_addc(&out, '}');
    send_json(fd, out.data ? out.data : "{}");
    sb_free(&out);
    free(value);
    free(state);
}

static void handle_index(int fd, Bridge *b) {
    long seq = 0;
    double ts = 0;
    char *state = state_copy(b, -1, 0, &seq, &ts);
    char *name = json_extract(state, "file");
    char *file_name = name ? json_extract(name, "name") : NULL;
    StrBuf html;
    sb_init(&html);
    sb_adds(&html, "<!doctype html><html><head><meta charset=\"utf-8\">"
                   "<title>Anvil state bridge</title><style>"
                   "body{font-family:ui-monospace,monospace;background:#0b1220;"
                   "color:#e5e7eb;padding:2rem}a{color:#38bdf8}"
                   "code{background:#111827;padding:.15rem .35rem;border-radius:4px}"
                   "</style></head><body><h1>Anvil &middot; state bridge</h1><p>Live file: <b>");
    sb_adds(&html, file_name ? file_name : "\"untitled\"");
    sb_addf(&html, "</b> &middot; seq %ld</p>"
                   "<p>This server is read-only. Poll it from the visualizer.</p><ul>"
                   "<li><a href=\"/state\">/state</a> &mdash; complete snapshot</li>"
                   "<li><a href=\"/state?wait=1&amp;since=0&amp;timeout=2\">/state?wait=1&amp;since=0</a> &mdash; long-poll</li>"
                   "<li><a href=\"/trie?depth=5\">/trie?depth=5</a> &mdash; trie tree</li>"
                   "<li><a href=\"/undo\">/undo</a> &middot; <a href=\"/clipboard\">/clipboard</a> &middot; <a href=\"/stats\">/stats</a></li>"
                   "<li><a href=\"/health\">/health</a></li></ul>"
                   "<p>Python: <code>anvil-viz</code></p></body></html>", seq);
    send_response(fd, 200, "text/html; charset=utf-8", html.data ? html.data : "", html.len);
    sb_free(&html);
    free(file_name);
    free(name);
    free(state);
}

static void handle_trie(int fd, Bridge *b, const char *query) {
    int depth = query_int(query, "depth", 4);
    int limit = query_int(query, "limit", 1200);
    if (depth < 1) depth = 1;
    if (depth > 12) depth = 12;
    if (limit < 10) limit = 10;
    if (limit > 5000) limit = 5000;

    char *trie = NULL;
    if (b->ed) trie = editor_trie_json(b->ed, depth, limit);
    if (!trie) {
        long seq = 0;
        double ts = 0;
        char *state = state_copy(b, -1, 0, &seq, &ts);
        trie = json_extract(state, "trie");
        free(state);
    }
    if (!trie) trie = xstrdup("{}");
    long seq = bridge_seq(b);
    StrBuf out;
    sb_init(&out);
    sb_addf(&out, "{\"seq\":%ld,", seq);
    const char *body = trie;
    while (*body && isspace((unsigned char)*body)) body++;
    if (*body == '{') body++;
    sb_adds(&out, body);
    send_json(fd, out.data ? out.data : "{}");
    sb_free(&out);
    free(trie);
}

static void handle_health(int fd, Bridge *b) {
    long seq = bridge_seq(b);
    bool closed = bridge_closed(b);
    double uptime = now_sec() - b->started;
    StrBuf out;
    sb_init(&out);
    sb_addf(&out,
            "{\"ok\":%s,\"schema\":\"%s\",\"version\":%d,\"seq\":%ld,"
            "\"closed\":%s,\"uptime_seconds\":%.1f,\"port\":%d,\"pid\":%ld}",
            closed ? "false" : "true", ANVIL_SCHEMA, ANVIL_SCHEMA_VERSION,
            seq, closed ? "true" : "false", uptime, b->port, (long)getpid());
    send_json(fd, out.data ? out.data : "{}");
    sb_free(&out);
}

static void *client_thread(void *arg) {
    ClientCtx *ctx = arg;
    Bridge *b = ctx->bridge;
    int fd = ctx->fd;
    free(ctx);

    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char req[16384];
    size_t used = 0;
    while (used < sizeof(req) - 1) {
        ssize_t n = recv(fd, req + used, sizeof(req) - 1 - used, 0);
        if (n <= 0) break;
        used += (size_t)n;
        req[used] = '\0';
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[used] = '\0';

    char method[16] = "";
    char target[4096] = "";
    if (sscanf(req, "%15s %4095s", method, target) != 2) {
        close(fd);
        return NULL;
    }

    if (strcmp(method, "OPTIONS") == 0) {
        send_response(fd, 204, "text/plain", "", 0);
        close(fd);
        return NULL;
    }
    if (strcmp(method, "GET") != 0) {
        send_response(fd, 400, "application/json", "{\"error\":\"GET only\"}", 22);
        close(fd);
        return NULL;
    }

    char path[4096];
    char *query = NULL;
    char *qmark = strchr(target, '?');
    if (qmark) {
        *qmark = '\0';
        query = qmark + 1;
    }
    snprintf(path, sizeof(path), "%s", target);
    size_t plen = strlen(path);
    while (plen > 1 && path[plen - 1] == '/') path[--plen] = '\0';
    if (plen == 0) snprintf(path, sizeof(path), "/");

    if (strcmp(path, "/favicon.ico") == 0) {
        send_response(fd, 204, "text/plain", "", 0);
    } else if (strcmp(path, "/") == 0) {
        handle_index(fd, b);
    } else if (strcmp(path, "/health") == 0) {
        handle_health(fd, b);
    } else if (strcmp(path, "/state") == 0 || strcmp(path, "/state.json") == 0 ||
               strcmp(path, "/snapshot") == 0) {
        bool wait = query_bool(query, "wait");
        long since = (long)query_int(query, "since", -1);
        double timeout = query_double(query, "timeout", 0.0);
        if (timeout < 0) timeout = 0;
        if (timeout > 5.0) timeout = 5.0;
        long seq = 0;
        double ts = 0;
        char *state = state_copy(b, wait ? since : -1, wait ? timeout : 0, &seq, &ts);
        send_json(fd, state ? state : "{}");
        free(state);
    } else if (strcmp(path, "/trie") == 0) {
        handle_trie(fd, b, query);
    } else if (strcmp(path, "/undo") == 0 || strcmp(path, "/redo") == 0 ||
               strcmp(path, "/clipboard") == 0 || strcmp(path, "/stats") == 0 ||
               strcmp(path, "/file") == 0 || strcmp(path, "/autosave") == 0 ||
               strcmp(path, "/ui") == 0 || strcmp(path, "/document") == 0) {
        handle_subset(fd, b, path);
    } else {
        StrBuf out;
        sb_init(&out);
        sb_adds(&out, "{\"error\":\"not found\",\"path\":");
        sb_json_str(&out, path);
        sb_addc(&out, '}');
        send_response(fd, 404, "application/json; charset=utf-8",
                      out.data ? out.data : "{}", out.len);
        sb_free(&out);
    }

    close(fd);
    return NULL;
}

static void *accept_thread(void *arg) {
    Bridge *b = arg;
    while (!b->stop) {
        struct sockaddr_in addr;
        socklen_t alen = sizeof(addr);
        int fd = accept(b->listen_fd, (struct sockaddr *)&addr, &alen);
        if (fd < 0) {
            if (b->stop) break;
            if (errno == EINTR) continue;
            break;
        }
        ClientCtx *ctx = xmalloc(sizeof(*ctx));
        ctx->bridge = b;
        ctx->fd = fd;
        pthread_t th;
        if (pthread_create(&th, NULL, client_thread, ctx) == 0) {
            pthread_detach(th);
        } else {
            close(fd);
            free(ctx);
        }
    }
    return NULL;
}

Bridge *bridge_start(Editor *ed, int requested_port, bool enable_server,
                     const char *state_file, bool state_file_enabled) {
    (void)state_file;
    (void)state_file_enabled;
    if (!enable_server) return NULL;

    Bridge *b = xmalloc(sizeof(*b));
    memset(b, 0, sizeof(*b));
    b->ed = ed;
    b->listen_fd = -1;
    b->started = now_sec();
    pthread_mutex_init(&b->lock, NULL);
    pthread_cond_init(&b->cond, NULL);

    StrBuf init;
    sb_init(&init);
    sb_addf(&init,
            "{\"schema\":\"%s\",\"version\":%d,\"seq\":0,\"timestamp\":%.3f,"
            "\"status\":\"starting\"}", ANVIL_SCHEMA, ANVIL_SCHEMA_VERSION, now_sec());
    b->json = init.data ? init.data : xstrdup("{}");
    b->json_len = strlen(b->json);
    b->last_timestamp = now_sec();

    int candidates[12];
    int ncand = 0;
    if (requested_port <= 0) {
        candidates[ncand++] = 0;
    } else {
        for (int i = 0; i < 10; i++) candidates[ncand++] = requested_port + i;
        candidates[ncand++] = 0;
    }

    int last_errno = 0;
    for (int i = 0; i < ncand; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { last_errno = errno; continue; }
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((uint16_t)candidates[i]);
        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            last_errno = errno;
            close(fd);
            continue;
        }
        if (listen(fd, 64) != 0) {
            last_errno = errno;
            close(fd);
            continue;
        }
        socklen_t alen = sizeof(addr);
        getsockname(fd, (struct sockaddr *)&addr, &alen);
        b->port = ntohs(addr.sin_port);
        b->listen_fd = fd;
        break;
    }

    if (b->listen_fd < 0) {
        (void)last_errno;
        pthread_mutex_destroy(&b->lock);
        pthread_cond_destroy(&b->cond);
        free(b->json);
        free(b);
        return NULL;
    }

    if (pthread_create(&b->thread, NULL, accept_thread, b) == 0) {
        b->thread_started = true;
    }
    return b;
}

void bridge_stop(Bridge *b) {
    if (!b) return;
    pthread_mutex_lock(&b->lock);
    b->stop = true;
    b->closed = true;
    pthread_cond_broadcast(&b->cond);
    int fd = b->listen_fd;
    b->listen_fd = -1;
    pthread_mutex_unlock(&b->lock);

    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    if (b->thread_started) pthread_join(b->thread, NULL);

    pthread_mutex_destroy(&b->lock);
    pthread_cond_destroy(&b->cond);
    free(b->json);
    free(b);
}
