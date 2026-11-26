/* End-to-end smoke test for the C editor.
 *
 * Runs build/anvil inside a pseudo-terminal, types, saves, undoes, copies and
 * quits, then checks the file on disk, the HTTP bridge and the JSON state
 * file.  This is the editor-side equivalent of the visualizer's smoke test.
 *
 *   make test        (builds and runs this file)
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int failures = 0;

static void ok(const char *name) { printf("[PASS] %s\n", name); }

static void fail(const char *name, const char *detail) {
    printf("[FAIL] %s%s%s\n", name, detail && *detail ? ": " : "",
           detail ? detail : "");
    failures++;
}

static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

/* Sleep while continuously draining the pty, so the editor never blocks on a
 * full output buffer before it has processed our keystrokes. */
static void sleep_drain(int fd, int ms) {
    int elapsed = 0;
    while (elapsed < ms) {
        struct pollfd pfd = {fd, POLLIN, 0};
        int rc = poll(&pfd, 1, 20);
        if (rc > 0) {
            char buf[8192];
            ssize_t n = read(fd, buf, sizeof(buf));
            (void)n;
        }
        elapsed += 20;
    }
}

static int send_all(int fd, const char *data, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, data + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    char *data = malloc((size_t)size + 1);
    if (!data) {
        fclose(fp);
        return NULL;
    }
    size_t got = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    data[got] = '\0';
    return data;
}

static int parse_port(const char *json) {
    const char *p = json ? strstr(json, "\"port\":") : NULL;
    if (!p) return 0;
    p += 7;
    while (*p == ' ') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    return atoi(p);
}

static char *http_get(int port, const char *path) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    struct timeval tv = {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return NULL;
    }
    char req[512];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                     "Connection: close\r\n\r\n", path);
    if (send_all(fd, req, (size_t)n) != 0) {
        close(fd);
        return NULL;
    }
    size_t cap = 8192, len = 0;
    char *out = malloc(cap);
    if (!out) {
        close(fd);
        return NULL;
    }
    for (;;) {
        if (len + 4096 + 1 > cap) {
            cap *= 2;
            char *grown = realloc(out, cap);
            if (!grown) {
                free(out);
                close(fd);
                return NULL;
            }
            out = grown;
        }
        ssize_t got = recv(fd, out + len, cap - len - 1, 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (got == 0) break;
        len += (size_t)got;
    }
    out[len] = '\0';
    close(fd);
    return out;
}

static int wait_for_file(const char *path, char **out, int timeout_ms) {
    int waited = 0;
    while (waited < timeout_ms) {
        char *data = read_file(path);
        if (data) {
            *out = data;
            return 0;
        }
        sleep_ms(50);
        waited += 50;
    }
    return -1;
}

int main(void) {
    char tmpdir[] = "/tmp/anvil-pty-XXXXXX";
    if (!mkdtemp(tmpdir)) {
        perror("mkdtemp");
        return 2;
    }
    char file[512], state[512];
    snprintf(file, sizeof(file), "%s/smoke.txt", tmpdir);
    snprintf(state, sizeof(state), "%s/state.json", tmpdir);
    FILE *seed = fopen(file, "wb");
    if (!seed) {
        perror("seed file");
        return 2;
    }
    fputs("seed line\n", seed);
    fclose(seed);

    int master = -1;
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_row = 40;
    ws.ws_col = 120;
    pid_t pid = forkpty(&master, NULL, NULL, &ws);
    if (pid < 0) {
        perror("forkpty");
        return 2;
    }
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        execl("./build/anvil", "anvil", "--port", "0", "--state-file", state,
              "--no-autosave", file, (char *)NULL);
        perror("exec anvil");
        _exit(127);
    }

    sleep_drain(master, 700);

    /* Wait for the first state-file snapshot and discover the bridge port. */
    char *state_json = NULL;
    int port = 0;
    for (int attempt = 0; attempt < 100 && port == 0; attempt++) {
        free(state_json);
        state_json = NULL;
        if (wait_for_file(state, &state_json, 200) == 0) {
            port = parse_port(state_json);
        }
        if (port == 0) sleep_ms(50);
    }
    if (port > 0) ok("state file published with an ephemeral port");
    else fail("state file published with an ephemeral port", "no port found");

    if (port > 0) {
        char *health = http_get(port, "/health");
        if (health && strstr(health, "\"ok\":true") &&
            strstr(health, "\"schema\":\"dsa-editor/state\"")) {
            ok("HTTP /health responds");
        } else {
            fail("HTTP /health responds", health ? health : "no response");
        }
        free(health);

        char *snapshot = http_get(port, "/state");
        if (snapshot && strstr(snapshot, "\"schema\":\"dsa-editor/state\"") &&
            strstr(snapshot, "\"stats\"")) {
            ok("HTTP /state returns a complete snapshot");
        } else {
            fail("HTTP /state returns a complete snapshot",
                 snapshot ? "missing fields" : "no response");
        }
        free(snapshot);

        char *trie = http_get(port, "/trie?depth=3&limit=200");
        if (trie && strstr(trie, "\"stats\"") && strstr(trie, "\"tree\"")) {
            ok("HTTP /trie returns the trie tree");
        } else {
            fail("HTTP /trie returns the trie tree",
                 trie ? "missing fields" : "no response");
        }
        free(trie);
    }

    /* Type at the end of the document and save. */
    send_all(master, "\x1b[1;5F", 6);
    sleep_drain(master, 150);
    send_all(master, "\nhello world\n", 13);
    sleep_drain(master, 300);
    send_all(master, "\x13", 1); /* Ctrl-S */
    sleep_drain(master, 600);

    char *saved = read_file(file);
    if (saved && strstr(saved, "hello world")) ok("typing + Ctrl-S writes the file");
    else fail("typing + Ctrl-S writes the file", saved ? saved : "unreadable");
    free(saved);

    /* Typing is coalesced into one undo unit. */
    send_all(master, "XYZ", 3);
    sleep_drain(master, 300);
    send_all(master, "\x1a", 1); /* Ctrl-Z */
    sleep_drain(master, 400);
    send_all(master, "\x13", 1); /* Ctrl-S */
    sleep_drain(master, 600);

    saved = read_file(file);
    if (saved && !strstr(saved, "XYZ") && strstr(saved, "hello world")) {
        ok("Ctrl-Z removes the coalesced typing group");
    } else {
        fail("Ctrl-Z removes the coalesced typing group",
             saved ? "unexpected contents" : "unreadable");
    }
    free(saved);

    /* Select all + copy fills the clipboard FIFO. */
    send_all(master, "\x01", 1); /* Ctrl-A */
    sleep_drain(master, 200);
    send_all(master, "\x03", 1); /* Ctrl-C */
    sleep_drain(master, 600);

    if (port > 0) {
        char *clip = http_get(port, "/clipboard");
        if (clip && strstr(clip, "\"size\":1") && strstr(clip, "hello world")) {
            ok("Ctrl-A + Ctrl-C publishes one clipboard entry");
        } else {
            fail("Ctrl-A + Ctrl-C publishes one clipboard entry",
                 clip ? clip : "no response");
        }
        free(clip);
    }

    /* Quit (twice in case the buffer is still considered dirty). */
    send_all(master, "\x11", 1);
    sleep_drain(master, 400);
    send_all(master, "\x11", 1);

    int status = -1;
    int reaped = 0;
    for (int i = 0; i < 60; i++) {
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            reaped = 1;
            break;
        }
        sleep_ms(100);
    }
    if (!reaped) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        fail("Ctrl-Q exits cleanly", "process did not exit");
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        ok("Ctrl-Q exits cleanly");
    } else {
        fail("Ctrl-Q exits cleanly", "non-zero exit status");
    }

    free(state_json);
    state_json = read_file(state);
    if (state_json && strstr(state_json, "\"status\":\"closed\"")) {
        ok("state file ends with status=closed");
    } else {
        fail("state file ends with status=closed",
             state_json ? "missing status" : "unreadable");
    }
    free(state_json);

    close(master);
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir);
    if (system(cmd) != 0) {
        /* best-effort cleanup */
    }

    if (failures) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall editor smoke checks passed\n");
    return 0;
}
