/* Encoding-aware, newline-preserving, atomic file I/O. */
#include "anvil.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define WARN_SIZE (5u * 1024u * 1024u)
#define MAX_SIZE  (64u * 1024u * 1024u)

const char *newline_name(Newline nl) {
    switch (nl) {
    case NL_CRLF: return "CRLF";
    case NL_CR: return "CR";
    default: return "LF";
    }
}

static bool utf8_valid(const unsigned char *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        unsigned char c = s[i];
        if (c < 0x80) { i++; continue; }
        int need;
        uint32_t cp;
        if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
        else return false;
        if (i + (size_t)need >= len) return false;
        for (int k = 1; k <= need; k++) {
            unsigned char cc = s[i + (size_t)k];
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        i += (size_t)need + 1;
    }
    return true;
}

static void sb_add_cp(StrBuf *sb, uint32_t cp) {
    char tmp[4];
    int n = utf8_encode(cp, tmp);
    sb_addn(sb, tmp, (size_t)n);
}

static char *utf16_to_utf8(const unsigned char *raw, size_t len, bool be, size_t *out_len) {
    StrBuf sb;
    sb_init(&sb);
    size_t i = 0;
    while (i + 1 < len) {
        uint32_t unit = be ? ((uint32_t)raw[i] << 8 | raw[i + 1])
                           : ((uint32_t)raw[i + 1] << 8 | raw[i]);
        i += 2;
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < len) {
            uint32_t low = be ? ((uint32_t)raw[i] << 8 | raw[i + 1])
                              : ((uint32_t)raw[i + 1] << 8 | raw[i]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                i += 2;
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
            } else {
                unit = 0xFFFD;
            }
        } else if (unit >= 0xD800 && unit <= 0xDFFF) {
            unit = 0xFFFD;
        }
        sb_add_cp(&sb, unit);
    }
    if (!sb.data) { sb_init(&sb); sb_addc(&sb, '\0'); sb.len = 0; }
    *out_len = sb.len;
    return sb.data;
}

static const uint32_t cp1252_high[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178
};

static bool cp1252_ok(const unsigned char *raw, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = raw[i];
        if (c >= 0x81 && c <= 0x9F) {
            if (cp1252_high[c - 0x80] == 0xFFFD) return false;
        }
    }
    return true;
}

static char *cp1252_to_utf8(const unsigned char *raw, size_t len, size_t *out_len) {
    StrBuf sb;
    sb_init(&sb);
    for (size_t i = 0; i < len; i++) {
        unsigned char c = raw[i];
        if (c >= 0x80 && c <= 0x9F) sb_add_cp(&sb, cp1252_high[c - 0x80]);
        else sb_add_cp(&sb, c);
    }
    if (!sb.data) { sb_init(&sb); sb_addc(&sb, '\0'); sb.len = 0; }
    *out_len = sb.len;
    return sb.data;
}

static char *latin1_to_utf8(const unsigned char *raw, size_t len, size_t *out_len) {
    StrBuf sb;
    sb_init(&sb);
    for (size_t i = 0; i < len; i++) sb_add_cp(&sb, raw[i]);
    if (!sb.data) { sb_init(&sb); sb_addc(&sb, '\0'); sb.len = 0; }
    *out_len = sb.len;
    return sb.data;
}

static Newline detect_newline(const char *text, size_t len) {
    size_t crlf = 0, lf = 0, cr = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\r') {
            if (i + 1 < len && text[i + 1] == '\n') { crlf++; i++; }
            else cr++;
        } else if (text[i] == '\n') {
            lf++;
        }
    }
    if (crlf >= lf && crlf >= cr && crlf > 0) return NL_CRLF;
    if (cr > lf && cr > 0) return NL_CR;
    return NL_LF;
}

static char *normalize_newlines(const char *text, size_t len, size_t *out_len) {
    char *out = xmalloc(len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\r') {
            if (i + 1 < len && text[i + 1] == '\n') i++;
            out[j++] = '\n';
        } else {
            out[j++] = text[i];
        }
    }
    out[j] = '\0';
    *out_len = j;
    return out;
}

int fileio_load(const char *path, LoadedFile *out, char *err, size_t errlen) {
    memset(out, 0, sizeof(*out));
    snprintf(out->encoding, sizeof(out->encoding), "utf-8");
    out->newline = NL_LF;

    char expanded[PATH_MAX];
    path_expand(path, expanded, sizeof(expanded));

    struct stat st;
    if (stat(expanded, &st) != 0) {
        if (err) snprintf(err, errlen, "%s", strerror(errno));
        return -1;
    }
    if ((unsigned long long)st.st_size > MAX_SIZE) {
        if (err) snprintf(err, errlen,
                          "File is too large to open safely (%.1f MiB; limit 64 MiB)",
                          (double)st.st_size / 1048576.0);
        return -1;
    }
    size_t size = (size_t)st.st_size;
    unsigned char *raw = xmalloc(size + 1);
    FILE *fp = fopen(expanded, "rb");
    if (!fp) {
        free(raw);
        if (err) snprintf(err, errlen, "%s", strerror(errno));
        return -1;
    }
    size_t got = fread(raw, 1, size, fp);
    fclose(fp);
    raw[got] = '\0';
    size = got;

    char *text = NULL;
    size_t text_len = 0;
    bool has_utf_bom = false;

    if (size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        has_utf_bom = true;
        text = xstrndup((const char *)raw + 3, size - 3);
        text_len = size - 3;
        snprintf(out->encoding, sizeof(out->encoding), "utf-8");
        memcpy(out->bom, raw, 3);
        out->bom_len = 3;
    } else if (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        has_utf_bom = true;
        text = utf16_to_utf8(raw + 2, size - 2, false, &text_len);
        snprintf(out->encoding, sizeof(out->encoding), "utf-16-le");
        memcpy(out->bom, raw, 2);
        out->bom_len = 2;
    } else if (size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        has_utf_bom = true;
        text = utf16_to_utf8(raw + 2, size - 2, true, &text_len);
        snprintf(out->encoding, sizeof(out->encoding), "utf-16-be");
        memcpy(out->bom, raw, 2);
        out->bom_len = 2;
    }

    if (!text) {
        size_t probe = size < 8192 ? size : 8192;
        if (!has_utf_bom) {
            for (size_t i = 0; i < probe; i++) {
                if (raw[i] == 0) {
                    free(raw);
                    if (err) snprintf(err, errlen,
                                      "This looks like a binary file; the editor only opens text files");
                    return -1;
                }
            }
        }
        if (utf8_valid(raw, size)) {
            text = xstrndup((const char *)raw, size);
            text_len = size;
            snprintf(out->encoding, sizeof(out->encoding), "utf-8");
        } else if (cp1252_ok(raw, size)) {
            text = cp1252_to_utf8(raw, size, &text_len);
            snprintf(out->encoding, sizeof(out->encoding), "cp1252");
        } else {
            text = latin1_to_utf8(raw, size, &text_len);
            snprintf(out->encoding, sizeof(out->encoding), "latin-1");
        }
    }

    out->newline = detect_newline(text, text_len);
    size_t norm_len = 0;
    char *norm = normalize_newlines(text, text_len, &norm_len);
    free(text);
    free(raw);

    out->text = norm;
    out->len = norm_len;
    if (size > WARN_SIZE) {
        snprintf(out->warning, sizeof(out->warning),
                 "Large file (%.1f MiB); editing may be slow",
                 (double)size / 1048576.0);
    }
    return 0;
}

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

static int cp1252_from_cp(uint32_t cp) {
    if (cp < 0x80) return (int)cp;
    if (cp >= 0xA0 && cp <= 0xFF) return (int)cp;
    for (int i = 0; i < 32; i++) {
        if (cp1252_high[i] == cp) return 0x80 + i;
    }
    return -1;
}

static int encode_text(const char *text, size_t len, const char *encoding,
                       StrBuf *out) {
    if (strcmp(encoding, "utf-16-le") == 0 || strcmp(encoding, "utf-16-be") == 0) {
        bool be = strcmp(encoding, "utf-16-be") == 0;
        size_t i = 0;
        while (i < len) {
            uint32_t cp;
            int used = utf8_decode(text + i, len - i, &cp);
            if (used <= 0) used = 1;
            i += (size_t)used;
            if (cp >= 0x10000) {
                uint32_t v = cp - 0x10000;
                uint32_t hi = 0xD800 + (v >> 10);
                uint32_t lo = 0xDC00 + (v & 0x3FF);
                if (be) { sb_addc(out, (char)(hi >> 8)); sb_addc(out, (char)(hi & 0xFF)); }
                else { sb_addc(out, (char)(hi & 0xFF)); sb_addc(out, (char)(hi >> 8)); }
                if (be) { sb_addc(out, (char)(lo >> 8)); sb_addc(out, (char)(lo & 0xFF)); }
                else { sb_addc(out, (char)(lo & 0xFF)); sb_addc(out, (char)(lo >> 8)); }
            } else {
                if (be) { sb_addc(out, (char)(cp >> 8)); sb_addc(out, (char)(cp & 0xFF)); }
                else { sb_addc(out, (char)(cp & 0xFF)); sb_addc(out, (char)(cp >> 8)); }
            }
        }
        return 0;
    }
    if (strcmp(encoding, "cp1252") == 0) {
        size_t i = 0;
        while (i < len) {
            uint32_t cp;
            int used = utf8_decode(text + i, len - i, &cp);
            if (used <= 0) used = 1;
            i += (size_t)used;
            int b = cp1252_from_cp(cp);
            sb_addc(out, (char)(b >= 0 ? b : '?'));
        }
        return 0;
    }
    if (strcmp(encoding, "latin-1") == 0) {
        size_t i = 0;
        while (i < len) {
            uint32_t cp;
            int used = utf8_decode(text + i, len - i, &cp);
            if (used <= 0) used = 1;
            i += (size_t)used;
            sb_addc(out, (char)(cp <= 0xFF ? cp : '?'));
        }
        return 0;
    }
    sb_addn(out, text, len);
    return 0;
}

int fileio_save(const char *path, const char *text, size_t len,
                const char *encoding, const unsigned char *bom, int bom_len,
                Newline newline, char *err, size_t errlen) {
    char expanded[PATH_MAX];
    path_expand(path, expanded, sizeof(expanded));

    char dir[PATH_MAX];
    path_dirname(expanded, dir, sizeof(dir));
    mkdir_p(dir);

    StrBuf normalized;
    sb_init(&normalized);
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == '\r') {
            if (i + 1 < len && text[i + 1] == '\n') i++;
            c = '\n';
        }
        if (c == '\n') {
            if (newline == NL_CRLF) sb_adds(&normalized, "\r\n");
            else if (newline == NL_CR) sb_addc(&normalized, '\r');
            else sb_addc(&normalized, '\n');
        } else {
            sb_addc(&normalized, c);
        }
    }

    StrBuf encoded;
    sb_init(&encoded);
    if (bom && bom_len > 0) sb_addn(&encoded, (const char *)bom, (size_t)bom_len);
    encode_text(normalized.data ? normalized.data : "", normalized.len,
                encoding ? encoding : "utf-8", &encoded);

    mode_t mode = 0;
    bool have_mode = false;
    struct stat st;
    if (stat(expanded, &st) == 0) {
        mode = st.st_mode & 07777;
        have_mode = true;
    }

    char tmp[PATH_MAX * 2];
    snprintf(tmp, sizeof(tmp), "%s/.%s.XXXXXX", dir, path_basename(expanded));
    int fd = mkstemp(tmp);
    if (fd < 0) {
        if (err) snprintf(err, errlen, "%s", strerror(errno));
        sb_free(&normalized);
        sb_free(&encoded);
        return -1;
    }

    size_t total = 0;
    const char *data = encoded.data ? encoded.data : "";
    size_t data_len = encoded.len;
    bool ok = true;
    while (total < data_len) {
        ssize_t n = write(fd, data + total, data_len - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        total += (size_t)n;
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (ok && have_mode) fchmod(fd, mode);
    if (close(fd) != 0) ok = false;
    if (ok && rename(tmp, expanded) != 0) ok = false;

    if (!ok) {
        if (err) snprintf(err, errlen, "%s", strerror(errno));
        unlink(tmp);
    }

    sb_free(&normalized);
    sb_free(&encoded);
    return ok ? (int)total : -1;
}
