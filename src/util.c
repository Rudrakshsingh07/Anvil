/* Small utilities: allocation, string buffers, UTF-8 and paths. */
#include "anvil.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void *xmalloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) {
        fputs("anvil: out of memory\n", stderr);
        abort();
    }
    return p;
}

void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size ? size : 1);
    if (!p) {
        fputs("anvil: out of memory\n", stderr);
        abort();
    }
    return p;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = xmalloc(n + 1);
    memcpy(out, s, n + 1);
    return out;
}

char *xstrndup(const char *s, size_t n) {
    char *out = xmalloc(n + 1);
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_clear(StrBuf *sb) {
    sb->len = 0;
    if (sb->data) sb->data[0] = '\0';
}

void sb_reserve(StrBuf *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return;
    size_t cap = sb->cap ? sb->cap : 64;
    while (cap < need) cap *= 2;
    sb->data = xrealloc(sb->data, cap);
    sb->cap = cap;
}

void sb_addc(StrBuf *sb, char c) {
    sb_reserve(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void sb_addn(StrBuf *sb, const char *s, size_t n) {
    if (!s || n == 0) return;
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_adds(StrBuf *sb, const char *s) {
    if (s) sb_addn(sb, s, strlen(s));
}

void sb_addf(StrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) {
        va_end(ap);
        return;
    }
    sb_reserve(sb, (size_t)n);
    vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, ap);
    sb->len += (size_t)n;
    va_end(ap);
}

static void json_escape(StrBuf *sb, const char *s, size_t n) {
    sb_addc(sb, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': sb_adds(sb, "\\\""); break;
        case '\\': sb_adds(sb, "\\\\"); break;
        case '\b': sb_adds(sb, "\\b"); break;
        case '\f': sb_adds(sb, "\\f"); break;
        case '\n': sb_adds(sb, "\\n"); break;
        case '\r': sb_adds(sb, "\\r"); break;
        case '\t': sb_adds(sb, "\\t"); break;
        default:
            if (c < 0x20) {
                sb_addf(sb, "\\u%04x", c);
            } else {
                sb_addc(sb, (char)c);
            }
        }
    }
    sb_addc(sb, '"');
}

void sb_json_str(StrBuf *sb, const char *s) {
    json_escape(sb, s ? s : "", s ? strlen(s) : 0);
}

void sb_json_nstr(StrBuf *sb, const char *s, size_t n) {
    json_escape(sb, s ? s : "", n);
}

char *preview_collapse(const char *s, size_t len, int limit) {
    StrBuf out;
    sb_init(&out);
    size_t i = 0;
    int emitted = 0;
    bool pending_space = false;
    bool truncated = false;

    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            if (emitted) pending_space = true;
            i++;
            continue;
        }
        if (pending_space) {
            if (emitted >= limit) { truncated = true; break; }
            sb_addc(&out, ' ');
            emitted++;
            pending_space = false;
        }
        if (emitted >= limit) { truncated = true; break; }
        sb_addc(&out, (char)c);
        emitted++;
        i++;
    }
    if (truncated) sb_adds(&out, "\xe2\x80\xa6");
    if (!out.data) return xstrdup("");
    return out.data;
}

char *preview_clip(const char *s, size_t len, int limit) {
    StrBuf out;
    sb_init(&out);
    int emitted = 0;
    bool truncated = false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        const char *rep = NULL;
        if (c == '\r') rep = "\\r";
        else if (c == '\n') rep = "\\n";
        else if (c == '\t') rep = "\\t";
        if (rep) {
            if (emitted + 2 > limit) { truncated = true; break; }
            sb_adds(&out, rep);
            emitted += 2;
        } else {
            if (emitted >= limit) { truncated = true; break; }
            sb_addc(&out, c);
            emitted++;
        }
    }
    if (truncated) sb_adds(&out, "\xe2\x80\xa6");
    if (!out.data) return xstrdup("");
    return out.data;
}

double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int utf8_decode(const char *s, size_t len, uint32_t *cp) {
    if (len == 0) return 0;
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { *cp = c; return 1; }
    int need;
    uint32_t value;
    if ((c & 0xE0) == 0xC0) { need = 1; value = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { need = 2; value = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { need = 3; value = c & 0x07; }
    else { *cp = 0xFFFD; return 1; }
    if ((size_t)need + 1 > len) { *cp = 0xFFFD; return 1; }
    for (int i = 1; i <= need; i++) {
        unsigned char cc = (unsigned char)s[i];
        if ((cc & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        value = (value << 6) | (cc & 0x3F);
    }
    *cp = value;
    return need + 1;
}

int utf8_encode(uint32_t cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int utf8_len_n(const char *s, size_t n) {
    size_t i = 0;
    int count = 0;
    while (i < n) {
        uint32_t cp;
        int used = utf8_decode(s + i, n - i, &cp);
        if (used <= 0) used = 1;
        i += (size_t)used;
        count++;
    }
    return count;
}

int utf8_strlen(const char *s) {
    return s ? utf8_len_n(s, strlen(s)) : 0;
}

size_t utf8_prev(const char *s, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

size_t utf8_next(const char *s, size_t pos, size_t len) {
    if (pos >= len) return len;
    pos++;
    while (pos < len && ((unsigned char)s[pos] & 0xC0) == 0x80) pos++;
    return pos;
}

int cp_width(uint32_t cp) {
    if (cp == 0) return 0;
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;
    if (cp >= 0x0300 && cp <= 0x036F) return 0;
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return 0;
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return 0;
    if (cp >= 0x20D0 && cp <= 0x20FF) return 0;
    if (cp >= 0xFE20 && cp <= 0xFE2F) return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) ||
        (cp >= 0x2E80 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE19) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1F64F) ||
        (cp >= 0x1F900 && cp <= 0x1F9FF) ||
        (cp >= 0x20000 && cp <= 0x3FFFD)) {
        return 2;
    }
    return 1;
}

int text_width(const char *s, size_t len) {
    int width = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        int used = utf8_decode(s + i, len - i, &cp);
        if (used <= 0) used = 1;
        width += cp_width(cp);
        i += (size_t)used;
    }
    return width;
}

size_t display_col(const char *line, size_t len, size_t byte_off, int tab_width) {
    if (byte_off > len) byte_off = len;
    if (tab_width < 1) tab_width = 4;
    size_t col = 0;
    size_t i = 0;
    while (i < byte_off) {
        uint32_t cp;
        int used = utf8_decode(line + i, len - i, &cp);
        if (used <= 0) used = 1;
        if (cp == '\t') {
            int next = (int)(col + (size_t)tab_width - (col % (size_t)tab_width));
            col = (size_t)next;
        } else {
            col += (size_t)cp_width(cp);
        }
        i += (size_t)used;
    }
    return col;
}

size_t byte_at_display_col(const char *line, size_t len, size_t col, int tab_width) {
    if (tab_width < 1) tab_width = 4;
    size_t i = 0;
    size_t cur = 0;
    while (i < len) {
        uint32_t cp;
        int used = utf8_decode(line + i, len - i, &cp);
        if (used <= 0) used = 1;
        size_t advance;
        if (cp == '\t') {
            advance = (size_t)tab_width - (cur % (size_t)tab_width);
        } else {
            advance = (size_t)cp_width(cp);
        }
        if (cur + advance > col) return i;
        cur += advance;
        i += (size_t)used;
    }
    return len;
}

const char *path_basename(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void path_dirname(const char *path, char *out, size_t outlen) {
    if (!path || !*path) { snprintf(out, outlen, "."); return; }
    const char *slash = strrchr(path, '/');
    if (!slash) { snprintf(out, outlen, "."); return; }
    size_t n = (size_t)(slash - path);
    if (n == 0) n = 1;
    if (n >= outlen) n = outlen - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

bool file_exists(const char *path) {
    struct stat st;
    return path && *path && stat(path, &st) == 0;
}

void path_expand(const char *in, char *out, size_t outlen) {
    if (!in) { out[0] = '\0'; return; }
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(out, outlen, "%s%s", home, in + 1);
            return;
        }
    }
    snprintf(out, outlen, "%s", in);
}

void path_display(const char *path, char *out, size_t outlen) {
    if (!path || !*path) {
        snprintf(out, outlen, "untitled");
        return;
    }
    const char *home = getenv("HOME");
    if (home && strncmp(path, home, strlen(home)) == 0 &&
        (path[strlen(home)] == '/' || path[strlen(home)] == '\0')) {
        snprintf(out, outlen, "~%s", path + strlen(home));
        return;
    }
    snprintf(out, outlen, "%s", path);
}
