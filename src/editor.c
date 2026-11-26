/* Editor state, editing operations, commands and the state snapshot. */
#include "anvil.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static bool ident_char(unsigned char c) { return isalnum(c) || c == '_'; }
static bool ident_start(unsigned char c) { return isalpha(c) || c == '_'; }

static TextStats compute_stats(const char *text, size_t len) {
    TextStats st;
    memset(&st, 0, sizeof(st));
    bool in_word = false;
    size_t i = 0;
    int chars = 0, chars_no_ws = 0, lines = 1;
    while (i < len) {
        uint32_t cp;
        int used = utf8_decode(text + i, len - i, &cp);
        if (used <= 0) used = 1;
        chars++;
        bool space = cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
                     cp == '\f' || cp == '\v';
        if (!space) {
            chars_no_ws++;
            if (!in_word) { st.words++; in_word = true; }
        } else {
            in_word = false;
        }
        if (cp == '\n') lines++;
        i += (size_t)used;
    }
    st.chars = chars;
    st.chars_no_ws = chars_no_ws;
    st.lines = lines;
    return st;
}

static void resolve_path(const char *in, char *out, size_t outlen) {
    char expanded[PATH_MAX];
    path_expand(in ? in : "", expanded, sizeof(expanded));
    if (expanded[0] == '/') {
        snprintf(out, outlen, "%s", expanded);
        return;
    }
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        snprintf(out, outlen, "%s", cwd);
        size_t used = strlen(out);
        if (used + 1 < outlen) {
            out[used++] = '/';
            out[used] = '\0';
            size_t avail = outlen - used - 1;
            size_t el = strlen(expanded);
            if (el > avail) el = avail;
            memcpy(out + used, expanded, el);
            out[used + el] = '\0';
        }
    } else {
        snprintf(out, outlen, "%s", expanded);
    }
}

static void default_state_file(char *out, size_t outlen) {
    const char *xdg = getenv("XDG_STATE_HOME");
    if (xdg && *xdg) {
        snprintf(out, outlen, "%s/dsa-editor/state.json", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(out, outlen, "%s/.local/state/dsa-editor/state.json", home);
    } else {
        snprintf(out, outlen, "/tmp/dsa-editor-state.json");
    }
}

void editor_set_state_file(Editor *ed, const char *path, bool enabled) {
    if (path && *path) {
        path_expand(path, ed->state_file, sizeof(ed->state_file));
        ed->state_file_enabled = enabled;
    } else if (enabled) {
        default_state_file(ed->state_file, sizeof(ed->state_file));
        ed->state_file_enabled = true;
    } else {
        ed->state_file[0] = '\0';
        ed->state_file_enabled = false;
    }
}

void editor_set_message(Editor *ed, const char *text, bool error) {
    snprintf(ed->message, sizeof(ed->message), "%.*s",
             (int)(sizeof(ed->message) - 1), text ? text : "");
    ed->message_error = error;
    ed->message_until = now_sec() + 4.0;
}

static void set_message_duration(Editor *ed, const char *text, bool error, double seconds) {
    editor_set_message(ed, text, error);
    ed->message_until = now_sec() + seconds;
}

static void extract_prefix(Editor *ed, char *out, size_t outlen) {
    out[0] = '\0';
    if (!ed->buf.text) return;
    size_t i = ed->buf.cursor;
    size_t start = i;
    while (start > 0 && ident_char((unsigned char)ed->buf.text[start - 1])) start--;
    size_t n = i - start;
    if (n == 0) return;
    if (!ident_start((unsigned char)ed->buf.text[start])) return;
    if (n >= outlen) {
        start = i - (outlen - 1);
        n = outlen - 1;
    }
    memcpy(out, ed->buf.text + start, n);
    out[n] = '\0';
}

static void update_prefix(Editor *ed) {
    extract_prefix(ed, ed->prefix, sizeof(ed->prefix));
}

void editor_update_completion(Editor *ed) {
    update_prefix(ed);
    ed->completion_count = 0;
    ed->completion_selected = 0;
    ed->ghost[0] = '\0';
    if (strlen(ed->prefix) < 2) {
        ed->completion_active = false;
        return;
    }
    ed->completion_count = trie_complete(&ed->trie, ed->prefix, 12, ed->suggestions);
    ed->completion_active = ed->completion_count > 0;
    size_t plen = strlen(ed->prefix);
    for (int i = 0; i < ed->completion_count; i++) {
        const char *word = ed->suggestions[i].word;
        if (strncmp(word, ed->prefix, plen) == 0 && word[plen]) {
            snprintf(ed->ghost, sizeof(ed->ghost), "%s", word + plen);
            break;
        }
    }
}

void editor_rebuild_trie(Editor *ed) {
    trie_free(&ed->trie);
    trie_init(&ed->trie);
    keyword_seed_trie(&ed->trie, ed->language);
    trie_learn_text(&ed->trie, ed->buf.text ? ed->buf.text : "", 8, "word");
    editor_update_completion(ed);
}

void editor_learn(Editor *ed) {
    trie_learn_text(&ed->trie, ed->buf.text ? ed->buf.text : "", 8, "word");
    ed->last_learn = now_sec();
}

void editor_init(Editor *ed) {
    memset(ed, 0, sizeof(*ed));
    buf_init(&ed->buf);
    snprintf(ed->encoding, sizeof(ed->encoding), "utf-8");
    snprintf(ed->language, sizeof(ed->language), "text");
    snprintf(ed->language_label, sizeof(ed->language_label), "Plain Text");
    snprintf(ed->theme, sizeof(ed->theme), "monokai");
    ed->newline = NL_LF;
    ed->autosave_enabled = true;
    ed->autosave_interval = 30.0;
    ed->tab_width = 4;
    ed->running = true;
    ed->server_enabled = true;
    ed->state_file_enabled = true;
    ed->saved_text = xstrdup("");
    undo_init(&ed->undo, 150, 0.9, 4000000);
    trie_init(&ed->trie);
    clip_init(&ed->clip, 50);
    default_state_file(ed->state_file, sizeof(ed->state_file));
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)getpid() * 2654435761u;
    snprintf(ed->session, sizeof(ed->session), "%08x", seed);
    editor_set_message(ed, "F1 help · F9 commands · F3 save as · F4 open", false);
}

void editor_free(Editor *ed) {
    buf_free(&ed->buf);
    undo_free(&ed->undo);
    trie_free(&ed->trie);
    clip_free(&ed->clip);
    free(ed->saved_text);
    memset(ed, 0, sizeof(*ed));
}

static void reset_document(Editor *ed, const char *text, size_t len,
                           const char *path, const char *encoding,
                           const unsigned char *bom, int bom_len,
                           Newline newline, size_t size_bytes) {
    buf_set(&ed->buf, text ? text : "", len, 0);
    ed->has_path = path && *path;
    if (ed->has_path) snprintf(ed->path, sizeof(ed->path), "%s", path);
    else ed->path[0] = '\0';
    snprintf(ed->encoding, sizeof(ed->encoding), "%s", encoding ? encoding : "utf-8");
    memset(ed->bom, 0, sizeof(ed->bom));
    if (bom && bom_len > 0 && bom_len <= 4) memcpy(ed->bom, bom, (size_t)bom_len);
    ed->bom_len = bom_len > 0 && bom_len <= 4 ? bom_len : 0;
    ed->newline = newline;
    ed->size_bytes = size_bytes;
    free(ed->saved_text);
    ed->saved_text = xstrndup(text ? text : "", len);
    ed->saved_chars = len;
    ed->dirty = false;
    ed->ever_saved = ed->has_path;
    ed->last_saved = ed->has_path ? now_sec() : 0.0;
    ed->stats = compute_stats(ed->buf.text, ed->buf.len);
    const char *lang = ed->has_path ? language_from_path(ed->path) : "text";
    snprintf(ed->language, sizeof(ed->language), "%s", lang);
    snprintf(ed->language_label, sizeof(ed->language_label), "%s", language_label(lang));
    undo_reset(&ed->undo);
    editor_rebuild_trie(ed);
    ed->last_learn = now_sec();
}

void editor_open(Editor *ed, const char *path, bool force) {
    if (!path || !*path) {
        editor_set_message(ed, "Usage: :e <path>", true);
        return;
    }
    if (ed->dirty && !force) {
        editor_set_message(ed, "Unsaved changes — save first, or use :e! to discard", true);
        return;
    }
    char target[PATH_MAX];
    resolve_path(path, target, sizeof(target));
    if (!file_exists(target)) {
        reset_document(ed, "", 0, target, "utf-8", NULL, 0, NL_LF, 0);
        char disp[PATH_MAX];
        path_display(target, disp, sizeof(disp));
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof(msg), "New file %s — Ctrl-S to create it", disp);
        editor_set_message(ed, msg, false);
        editor_publish(ed, "ok");
        return;
    }
    LoadedFile lf;
    char err[512] = "";
    if (fileio_load(target, &lf, err, sizeof(err)) != 0) {
        char msg[600];
        snprintf(msg, sizeof(msg), "Open failed: %s", err);
        editor_set_message(ed, msg, true);
        return;
    }
    reset_document(ed, lf.text, lf.len, target, lf.encoding, lf.bom, lf.bom_len,
                   lf.newline, (size_t)0);
    struct stat st;
    if (stat(target, &st) == 0) ed->size_bytes = (size_t)st.st_size;
    free(lf.text);
    char disp[PATH_MAX];
    path_display(target, disp, sizeof(disp));
    char msg[PATH_MAX + 128];
    snprintf(msg, sizeof(msg), "Opened %s · %d chars · %s", disp,
             utf8_strlen(ed->buf.text), ed->encoding);
    if (lf.warning[0]) set_message_duration(ed, lf.warning, true, 6.0);
    else editor_set_message(ed, msg, false);
    editor_publish(ed, "ok");
}

void editor_new(Editor *ed, bool force) {
    if (ed->dirty && !force) {
        editor_set_message(ed, "Unsaved changes — save first, or use :new! to discard", true);
        return;
    }
    reset_document(ed, "", 0, NULL, "utf-8", NULL, 0, NL_LF, 0);
    editor_set_message(ed, "New file — F3 to choose a path", false);
    editor_publish(ed, "ok");
}

static int save_internal(Editor *ed, const char *raw_path, bool autosave) {
    char target[PATH_MAX];
    if (raw_path && *raw_path) {
        resolve_path(raw_path, target, sizeof(target));
    } else if (ed->has_path) {
        snprintf(target, sizeof(target), "%s", ed->path);
    } else {
        editor_set_message(ed, "No filename yet — F3 or :w <path>", true);
        return 0;
    }

    char err[512] = "";
    int written = fileio_save(target, ed->buf.text ? ed->buf.text : "", ed->buf.len,
                              ed->encoding, ed->bom, ed->bom_len, ed->newline,
                              err, sizeof(err));
    if (written < 0) {
        char msg[600];
        snprintf(msg, sizeof(msg), "%s failed: %s", autosave ? "Autosave" : "Save", err);
        editor_set_message(ed, msg, true);
        return 0;
    }

    bool path_changed = !ed->has_path || strcmp(ed->path, target) != 0;
    snprintf(ed->path, sizeof(ed->path), "%s", target);
    ed->has_path = true;
    if (path_changed) {
        const char *lang = language_from_path(ed->path);
        snprintf(ed->language, sizeof(ed->language), "%s", lang);
        snprintf(ed->language_label, sizeof(ed->language_label), "%s", language_label(lang));
        editor_rebuild_trie(ed);
    }

    free(ed->saved_text);
    ed->saved_text = xstrndup(ed->buf.text ? ed->buf.text : "", ed->buf.len);
    ed->saved_chars = ed->buf.len;
    ed->dirty = false;
    ed->ever_saved = true;
    ed->last_saved = now_sec();
    ed->size_bytes = (size_t)written;
    trie_learn_text(&ed->trie, ed->buf.text ? ed->buf.text : "", 8, "word");

    char disp[PATH_MAX];
    path_display(target, disp, sizeof(disp));
    char msg[PATH_MAX + 128];
    if (autosave) snprintf(msg, sizeof(msg), "Autosaved %s · %d bytes", path_basename(target), written);
    else snprintf(msg, sizeof(msg), "Saved %s · %d bytes", disp, written);
    set_message_duration(ed, msg, false, autosave ? 2.5 : 4.0);
    editor_publish(ed, "ok");
    return 1;
}

int editor_save(Editor *ed, const char *path) {
    return save_internal(ed, path, false);
}

void editor_apply(Editor *ed, size_t start, size_t end, const char *text, size_t len) {
    if (start > ed->buf.len) start = ed->buf.len;
    if (end > ed->buf.len) end = ed->buf.len;
    if (start > end) { size_t t = start; start = end; end = t; }
    if (start == end && len == 0) return;

    const char *insert = text;
    char *copy = NULL;
    if (text && text >= ed->buf.text && text < ed->buf.text + ed->buf.len) {
        copy = xstrndup(text, len);
        insert = copy;
    }

    undo_record_op(&ed->undo, ed->buf.text ? ed->buf.text : "", ed->buf.cursor,
                   start, ed->buf.text ? ed->buf.text + start : "", end - start,
                   insert, len, start + len);
    if (end > start) buf_delete(&ed->buf, start, end - start);
    if (len) buf_insert(&ed->buf, start, insert, len);
    ed->buf.cursor = start + len;
    ed->buf.anchor = SIZE_MAX;
    ed->buf.goal_col = 0;
    ed->dirty = true;
    ed->stats = compute_stats(ed->buf.text, ed->buf.len);
    editor_update_completion(ed);
    double now = now_sec();
    if (now - ed->last_learn >= 1.0) editor_learn(ed);
    editor_publish(ed, "ok");
    free(copy);
}

void editor_insert_text(Editor *ed, const char *text, size_t len) {
    if (!text || len == 0) return;
    if (buf_has_selection(&ed->buf)) {
        size_t start, end;
        buf_sel_range(&ed->buf, &start, &end);
        editor_apply(ed, start, end, text, len);
    } else {
        editor_apply(ed, ed->buf.cursor, ed->buf.cursor, text, len);
    }
}

void editor_delete_range(Editor *ed, size_t start, size_t end) {
    editor_apply(ed, start, end, NULL, 0);
}

void editor_replace_selection(Editor *ed, const char *text, size_t len) {
    if (buf_has_selection(&ed->buf)) {
        size_t start, end;
        buf_sel_range(&ed->buf, &start, &end);
        editor_apply(ed, start, end, text, len);
    } else {
        editor_apply(ed, ed->buf.cursor, ed->buf.cursor, text, len);
    }
}

void editor_copy(Editor *ed, bool cut) {
    if (!buf_has_selection(&ed->buf)) {
        editor_set_message(ed, "Nothing selected — use Shift+arrows", true);
        return;
    }
    size_t start, end;
    buf_sel_range(&ed->buf, &start, &end);
    char *text = buf_range_dup(&ed->buf, start, end);
    clip_enqueue(&ed->clip, text);
    int n = utf8_strlen(text);
    free(text);
    if (cut) {
        editor_apply(ed, start, end, NULL, 0);
        char msg[96];
        snprintf(msg, sizeof(msg), "Cut %d chars to clipboard history", n);
        set_message_duration(ed, msg, false, 3.0);
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "Copied %d chars to clipboard history", n);
        set_message_duration(ed, msg, false, 3.0);
    }
    editor_publish(ed, "ok");
}

void editor_paste_latest(Editor *ed) {
    const ClipEntry *entry = clip_get(&ed->clip, 0);
    if (!entry || !entry->text || !*entry->text) {
        editor_set_message(ed, "Clipboard history is empty", true);
        return;
    }
    char *text = xstrdup(entry->text);
    int n = utf8_strlen(text);
    editor_insert_text(ed, text, strlen(text));
    free(text);
    char msg[96];
    snprintf(msg, sizeof(msg), "Pasted %d chars (history %d)", n, clip_size(&ed->clip));
    set_message_duration(ed, msg, false, 3.0);
    editor_publish(ed, "ok");
}

void editor_paste_history(Editor *ed, int index) {
    const ClipEntry *entry = clip_get(&ed->clip, index);
    if (!entry) {
        char msg[64];
        snprintf(msg, sizeof(msg), "No clipboard entry #%d", index + 1);
        editor_set_message(ed, msg, true);
        return;
    }
    char *text = xstrdup(entry->text);
    editor_insert_text(ed, text, strlen(text));
    free(text);
    char msg[64];
    snprintf(msg, sizeof(msg), "Pasted clipboard entry #%d", index + 1);
    set_message_duration(ed, msg, false, 3.0);
    editor_publish(ed, "ok");
}

void editor_accept_completion(Editor *ed, bool ghost_only) {
    char word[128] = "";
    if (ghost_only && ed->ghost[0]) {
        size_t pl = strlen(ed->prefix);
        size_t gl = strlen(ed->ghost);
        if (pl > 63) pl = 63;
        if (gl > sizeof(word) - 1 - pl) gl = sizeof(word) - 1 - pl;
        memcpy(word, ed->prefix, pl);
        memcpy(word + pl, ed->ghost, gl);
        word[pl + gl] = '\0';
    } else if (ed->completion_count > 0) {
        snprintf(word, sizeof(word), "%s", ed->suggestions[ed->completion_selected].word);
    } else {
        return;
    }
    size_t plen = strlen(ed->prefix);
    if (plen > ed->buf.cursor) plen = ed->buf.cursor;
    size_t start = ed->buf.cursor - plen;
    editor_apply(ed, start, ed->buf.cursor, word, strlen(word));
    trie_bump(&ed->trie, word, 15);
    char msg[160];
    snprintf(msg, sizeof(msg), "Accepted %s", word);
    set_message_duration(ed, msg, false, 1.5);
    editor_update_completion(ed);
    editor_publish(ed, "ok");
}

void editor_undo(Editor *ed) {
    size_t cursor = 0;
    char *text = undo_undo(&ed->undo, ed->buf.text ? ed->buf.text : "", ed->buf.cursor, &cursor);
    if (!text) {
        editor_set_message(ed, "Nothing to undo", true);
        return;
    }
    buf_set(&ed->buf, text, strlen(text), cursor);
    free(text);
    ed->buf.anchor = SIZE_MAX;
    ed->dirty = !ed->saved_text || strcmp(ed->buf.text, ed->saved_text) != 0;
    ed->stats = compute_stats(ed->buf.text, ed->buf.len);
    editor_update_completion(ed);
    char msg[64];
    snprintf(msg, sizeof(msg), "Undo · %d left", undo_depth(&ed->undo));
    set_message_duration(ed, msg, false, 2.5);
    editor_publish(ed, "ok");
}

void editor_redo(Editor *ed) {
    size_t cursor = 0;
    char *text = undo_redo(&ed->undo, ed->buf.text ? ed->buf.text : "", ed->buf.cursor, &cursor);
    if (!text) {
        editor_set_message(ed, "Nothing to redo", true);
        return;
    }
    buf_set(&ed->buf, text, strlen(text), cursor);
    free(text);
    ed->buf.anchor = SIZE_MAX;
    ed->dirty = !ed->saved_text || strcmp(ed->buf.text, ed->saved_text) != 0;
    ed->stats = compute_stats(ed->buf.text, ed->buf.len);
    editor_update_completion(ed);
    char msg[64];
    snprintf(msg, sizeof(msg), "Redo · %d left", redo_depth(&ed->undo));
    set_message_duration(ed, msg, false, 2.5);
    editor_publish(ed, "ok");
}

void editor_find_next(Editor *ed, const char *needle) {
    const char *query = needle && *needle ? needle : ed->last_find;
    if (!query || !*query) {
        editor_set_message(ed, "Usage: :find <text>", true);
        return;
    }
    snprintf(ed->last_find, sizeof(ed->last_find), "%s", query);
    const char *hay = ed->buf.text ? ed->buf.text : "";
    size_t start = ed->buf.cursor + 1;
    if (start > ed->buf.len) start = ed->buf.len;
    const char *found = strstr(hay + start, query);
    bool wrapped = false;
    if (!found) {
        const char *limit = hay + (start < ed->buf.len ? start : ed->buf.len);
        for (const char *p = hay; p < limit; p++) {
            if (strncmp(p, query, strlen(query)) == 0) { found = p; break; }
        }
        wrapped = found != NULL;
    }
    if (!found) {
        char msg[320];
        snprintf(msg, sizeof(msg), "Not found: %s", query);
        editor_set_message(ed, msg, true);
        return;
    }
    ed->buf.cursor = (size_t)(found - hay);
    ed->buf.anchor = SIZE_MAX;
    int line, col;
    buf_line_col(&ed->buf, ed->buf.cursor, &line, &col);
    char msg[320];
    snprintf(msg, sizeof(msg), "Found at line %d, col %d%s", line, col,
             wrapped ? " (wrapped)" : "");
    editor_set_message(ed, msg, false);
    editor_update_completion(ed);
    editor_publish(ed, "ok");
}

void editor_goto_line(Editor *ed, int line_number) {
    int total = ed->stats.lines;
    if (line_number < 1) line_number = 1;
    if (line_number > total) line_number = total;
    ed->buf.cursor = buf_offset_at(&ed->buf, line_number, 1);
    ed->buf.anchor = SIZE_MAX;
    char msg[64];
    snprintf(msg, sizeof(msg), "Line %d of %d", line_number, total);
    editor_set_message(ed, msg, false);
    editor_update_completion(ed);
    editor_publish(ed, "ok");
}

void editor_set_language(Editor *ed, const char *name) {
    if (!name || !*name) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Language: %s · try :lang python / :lang auto", ed->language);
        set_message_duration(ed, msg, false, 6.0);
        return;
    }
    const char *key;
    if (strcasecmp(name, "auto") == 0) {
        key = ed->has_path ? language_from_path(ed->path) : "text";
    } else {
        key = language_from_name(name);
        if (!key) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Unknown language: %s", name);
            editor_set_message(ed, msg, true);
            return;
        }
    }
    snprintf(ed->language, sizeof(ed->language), "%s", key);
    snprintf(ed->language_label, sizeof(ed->language_label), "%s", language_label(key));
    editor_rebuild_trie(ed);
    char msg[160];
    snprintf(msg, sizeof(msg), "Language: %s (%s)", ed->language_label, key);
    editor_set_message(ed, msg, false);
    editor_publish(ed, "ok");
}

static void request_quit(Editor *ed) {
    if (ed->dirty) {
        double now = now_sec();
        if (now - ed->quit_pressed_at > 2.5) {
            ed->quit_pressed_at = now;
            set_message_duration(ed,
                "Unsaved changes — press Ctrl-Q again to discard, or Ctrl-S to save",
                true, 4.0);
            return;
        }
    }
    ed->running = false;
}

static void command_autosave(Editor *ed, const char *argument) {
    char arg[64];
    snprintf(arg, sizeof(arg), "%s", argument ? argument : "");
    for (char *p = arg; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (strcmp(arg, "on") == 0 || strcmp(arg, "yes") == 0 || strcmp(arg, "1") == 0) {
        ed->autosave_enabled = true;
    } else if (strcmp(arg, "off") == 0 || strcmp(arg, "no") == 0 || strcmp(arg, "0") == 0) {
        ed->autosave_enabled = false;
    } else if (*arg) {
        char *end = NULL;
        double seconds = strtod(arg, &end);
        if (!end || *end || seconds <= 0) {
            editor_set_message(ed, "Usage: :autosave on|off|<seconds>", true);
            return;
        }
        if (seconds < 1.0) seconds = 1.0;
        ed->autosave_interval = seconds;
        ed->autosave_enabled = true;
    } else {
        ed->autosave_enabled = !ed->autosave_enabled;
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "Autosave %s (%gs)",
             ed->autosave_enabled ? "on" : "off", ed->autosave_interval);
    editor_set_message(ed, msg, false);
    editor_publish(ed, "ok");
}

static void bridge_summary(Editor *ed, char *out, size_t outlen) {
    if (ed->port > 0) {
        snprintf(out, outlen, "Bridge http://127.0.0.1:%d/state · file %s",
                 ed->port, ed->state_file_enabled ? ed->state_file : "off");
    } else if (ed->state_file_enabled) {
        snprintf(out, outlen, "Bridge off · state file %s", ed->state_file);
    } else {
        snprintf(out, outlen, "Bridge off");
    }
}

void editor_run_command(Editor *ed) {
    char raw[512];
    size_t n = ed->command_len;
    if (n >= sizeof(raw)) n = sizeof(raw) - 1;
    memcpy(raw, ed->command, n);
    raw[n] = '\0';
    while (n > 0 && isspace((unsigned char)raw[n - 1])) raw[--n] = '\0';
    char *p = raw;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == ':') { p++; while (*p && isspace((unsigned char)*p)) p++; }
    if (!*p) return;

    char *arg = p;
    while (*arg && !isspace((unsigned char)*arg)) arg++;
    if (*arg) { *arg++ = '\0'; while (*arg && isspace((unsigned char)*arg)) arg++; }
    for (char *q = p; *q; q++) *q = (char)tolower((unsigned char)*q);

    size_t clen = strlen(p);
    bool force = false;
    if (clen > 0 && p[clen - 1] == '!') {
        p[clen - 1] = '\0';
        force = true;
    }

    if (strcmp(p, "w") == 0 || strcmp(p, "write") == 0 || strcmp(p, "save") == 0) {
        editor_save(ed, *arg ? arg : NULL);
    } else if (strcmp(p, "wq") == 0) {
        if (editor_save(ed, *arg ? arg : NULL)) ed->running = false;
    } else if (strcmp(p, "e") == 0 || strcmp(p, "edit") == 0 || strcmp(p, "open") == 0) {
        if (!*arg) editor_set_message(ed, "Usage: :e <path>", true);
        else editor_open(ed, arg, force);
    } else if (strcmp(p, "saveas") == 0) {
        if (!*arg) editor_set_message(ed, "Usage: :saveas <path>", true);
        else editor_save(ed, arg);
    } else if (strcmp(p, "new") == 0) {
        editor_new(ed, force);
    } else if (strcmp(p, "q") == 0 || strcmp(p, "quit") == 0) {
        if (force) ed->running = false;
        else request_quit(ed);
    } else if (strcmp(p, "help") == 0) {
        ed->help_visible = !ed->help_visible;
    } else if (strcmp(p, "autosave") == 0) {
        command_autosave(ed, arg);
    } else if (strcmp(p, "lang") == 0 || strcmp(p, "language") == 0 ||
               strcmp(p, "syntax") == 0) {
        editor_set_language(ed, arg);
    } else if (strcmp(p, "find") == 0) {
        editor_find_next(ed, arg);
    } else if (strcmp(p, "goto") == 0) {
        char *end = NULL;
        long line = strtol(arg, &end, 10);
        if (!*arg || (end && *end)) editor_set_message(ed, "Usage: :goto <line>", true);
        else editor_goto_line(ed, (int)line);
    } else if (strcmp(p, "clip") == 0 || strcmp(p, "clipboard") == 0) {
        ed->panel_visible = true;
        char msg[96];
        snprintf(msg, sizeof(msg), "Clipboard queue: %d/%d entries",
                 clip_size(&ed->clip), ed->clip.capacity);
        editor_set_message(ed, msg, false);
    } else if (strcmp(p, "stats") == 0 || strcmp(p, "panel") == 0) {
        ed->panel_visible = !ed->panel_visible;
        char msg[64];
        snprintf(msg, sizeof(msg), "Data panel %s", ed->panel_visible ? "on" : "off");
        editor_set_message(ed, msg, false);
    } else if (strcmp(p, "theme") == 0) {
        char msg[160];
        if (!*arg) {
            snprintf(msg, sizeof(msg), "Theme: %s (choose: %s)", ed->theme,
                     ui_theme_names());
            editor_set_message(ed, msg, false);
        } else {
            const char *canon = ui_theme_name(arg);
            if (!canon) {
                snprintf(msg, sizeof(msg), "Unknown theme '%s' (choose: %s)", arg,
                         ui_theme_names());
                editor_set_message(ed, msg, true);
            } else {
                snprintf(ed->theme, sizeof(ed->theme), "%s", canon);
                snprintf(msg, sizeof(msg), "Theme: %s", ed->theme);
                editor_set_message(ed, msg, false);
            }
        }
    } else if (strcmp(p, "tab") == 0) {
        char *end = NULL;
        long width = strtol(arg, &end, 10);
        if (!*arg || (end && *end) || width < 1 || width > 16) {
            editor_set_message(ed, "Usage: :tab <1-16>", true);
        } else {
            ed->tab_width = (int)width;
            char msg[64];
            snprintf(msg, sizeof(msg), "Tab width: %d", ed->tab_width);
            editor_set_message(ed, msg, false);
        }
    } else if (strcmp(p, "undo") == 0) {
        editor_undo(ed);
    } else if (strcmp(p, "redo") == 0) {
        editor_redo(ed);
    } else if (strcmp(p, "state") == 0 || strcmp(p, "bridge") == 0) {
        char msg[PATH_MAX + 128];
        bridge_summary(ed, msg, sizeof(msg));
        set_message_duration(ed, msg, false, 8.0);
    } else if (strcmp(p, "path") == 0) {
        char disp[PATH_MAX];
        path_display(ed->has_path ? ed->path : "", disp, sizeof(disp));
        set_message_duration(ed, disp, false, 6.0);
    } else if (strcmp(p, "languages") == 0) {
        StrBuf sb;
        sb_init(&sb);
        keyword_languages_json(&sb);
        for (size_t i = 0; i < sb.len; i++) {
            if (sb.data[i] == '[' || sb.data[i] == ']' || sb.data[i] == '"') sb.data[i] = ' ';
        }
        set_message_duration(ed, sb.data ? sb.data : "", false, 8.0);
        sb_free(&sb);
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Unknown command: %s", p);
        editor_set_message(ed, msg, true);
    }
    editor_publish(ed, "ok");
}

static void smart_enter(Editor *ed) {
    size_t line_start = buf_line_start(&ed->buf, ed->buf.cursor);
    StrBuf indent;
    sb_init(&indent);
    size_t i = line_start;
    while (i < ed->buf.cursor && (ed->buf.text[i] == ' ' || ed->buf.text[i] == '\t')) {
        sb_addc(&indent, ed->buf.text[i]);
        i++;
    }
    bool extra = false;
    size_t k = ed->buf.cursor;
    while (k > line_start && (ed->buf.text[k - 1] == ' ' || ed->buf.text[k - 1] == '\t')) k--;
    if (k > line_start) {
        char last = ed->buf.text[k - 1];
        extra = last == ':' || last == '{' || last == '[' || last == '(';
    }
    StrBuf ins;
    sb_init(&ins);
    sb_addc(&ins, '\n');
    if (indent.len) sb_addn(&ins, indent.data, indent.len);
    if (extra) {
        for (int t = 0; t < ed->tab_width; t++) sb_addc(&ins, ' ');
    }
    editor_insert_text(ed, ins.data ? ins.data : "\n", ins.len);
    sb_free(&indent);
    sb_free(&ins);
}

static void dedent_line(Editor *ed) {
    size_t start = buf_line_start(&ed->buf, ed->buf.cursor);
    size_t remove = 0;
    while (remove < (size_t)ed->tab_width && start + remove < ed->buf.len) {
        char c = ed->buf.text[start + remove];
        if (c != ' ' && c != '\t') break;
        remove++;
    }
    if (!remove) return;
    size_t old_cursor = ed->buf.cursor;
    editor_apply(ed, start, start + remove, NULL, 0);
    if (old_cursor >= start + remove) ed->buf.cursor = old_cursor - remove;
    else if (old_cursor > start) ed->buf.cursor = start;
}

static void transpose_chars(Editor *ed) {
    if (ed->buf.len == 0) return;
    size_t cursor = ed->buf.cursor;
    if (cursor == 0) return;
    size_t prev = utf8_prev(ed->buf.text, cursor);
    if (cursor == ed->buf.len) {
        size_t prev2 = utf8_prev(ed->buf.text, prev);
        if (prev2 == prev) return;
        StrBuf seg;
        sb_init(&seg);
        sb_addn(&seg, ed->buf.text + prev, cursor - prev);
        sb_addn(&seg, ed->buf.text + prev2, prev - prev2);
        size_t new_cursor = cursor;
        editor_apply(ed, prev2, cursor, seg.data, seg.len);
        ed->buf.cursor = new_cursor;
        sb_free(&seg);
        return;
    }
    size_t next = utf8_next(ed->buf.text, cursor, ed->buf.len);
    StrBuf seg;
    sb_init(&seg);
    sb_addn(&seg, ed->buf.text + cursor, next - cursor);
    sb_addn(&seg, ed->buf.text + prev, cursor - prev);
    size_t new_cursor = next;
    editor_apply(ed, prev, next, seg.data, seg.len);
    ed->buf.cursor = new_cursor;
    sb_free(&seg);
}

static void open_command(Editor *ed, const char *prefill) {
    ed->help_visible = false;
    ed->command_mode = true;
    snprintf(ed->command, sizeof(ed->command), "%s", prefill ? prefill : "");
    ed->command_len = strlen(ed->command);
}

static void maybe_publish(Editor *ed) {
    double now = now_sec();
    if (now - ed->last_publish >= 0.2) editor_publish(ed, "ok");
}

void editor_handle_key(Editor *ed, int key, uint32_t ch) {
    int mods = key & (KEY_MOD_SHIFT | KEY_MOD_CTRL | KEY_MOD_ALT);
    int base = key & 0xFF;

    if (ed->help_visible) {
        if (base == KEY_F1 || base == KEY_ESC || (base == KEY_CHAR && ch == 'q')) {
            ed->help_visible = false;
            ed->help_scroll = 0;
        } else if (base == KEY_PGUP || base == KEY_UP) {
            if (ed->help_scroll > 0) ed->help_scroll--;
        } else if (base == KEY_PGDN || base == KEY_DOWN) {
            ed->help_scroll++;
        } else if (base == KEY_HOME) {
            ed->help_scroll = 0;
        }
        return;
    }

    if (ed->command_mode) {
        if (base == KEY_ESC) {
            ed->command_mode = false;
            ed->command_len = 0;
            ed->command[0] = '\0';
        } else if (base == KEY_ENTER) {
            ed->command_mode = false;
            char saved[512];
            snprintf(saved, sizeof(saved), "%s", ed->command);
            ed->command_len = strlen(saved);
            memcpy(ed->command, saved, sizeof(saved));
            ed->command_len = strlen(ed->command);
            editor_run_command(ed);
            ed->command_len = 0;
            ed->command[0] = '\0';
        } else if (base == KEY_BACKSPACE) {
            if (ed->command_len > 0) {
                ed->command_len = utf8_prev(ed->command, ed->command_len);
                ed->command[ed->command_len] = '\0';
            }
        } else if (base == KEY_CTRL_U) {
            ed->command_len = 0;
            ed->command[0] = '\0';
        } else if (base == KEY_CHAR && ch >= 0x20 && ch != 0x7F) {
            char tmp[4];
            int n = utf8_encode(ch, tmp);
            if (ed->command_len + (size_t)n < sizeof(ed->command)) {
                memcpy(ed->command + ed->command_len, tmp, (size_t)n);
                ed->command_len += (size_t)n;
                ed->command[ed->command_len] = '\0';
            }
        }
        return;
    }

    switch (base) {
    case KEY_CHAR:
        if (ch >= 0x20 && ch != 0x7F) {
            char tmp[4];
            int n = utf8_encode(ch, tmp);
            editor_insert_text(ed, tmp, (size_t)n);
        }
        return;
    case KEY_ENTER:
        smart_enter(ed);
        return;
    case KEY_BACKSPACE:
        if (buf_has_selection(&ed->buf)) {
            size_t start, end;
            buf_sel_range(&ed->buf, &start, &end);
            editor_apply(ed, start, end, NULL, 0);
        } else if (ed->buf.cursor > 0) {
            size_t prev = utf8_prev(ed->buf.text, ed->buf.cursor);
            editor_apply(ed, prev, ed->buf.cursor, NULL, 0);
        }
        return;
    case KEY_DELETE:
        if (buf_has_selection(&ed->buf)) {
            size_t start, end;
            buf_sel_range(&ed->buf, &start, &end);
            editor_apply(ed, start, end, NULL, 0);
        } else if (ed->buf.cursor < ed->buf.len) {
            size_t next = utf8_next(ed->buf.text, ed->buf.cursor, ed->buf.len);
            editor_apply(ed, ed->buf.cursor, next, NULL, 0);
        }
        return;
    case KEY_TAB:
        if (ed->completion_active) {
            editor_accept_completion(ed, false);
        } else {
            size_t line_start = buf_line_start(&ed->buf, ed->buf.cursor);
            size_t col = display_col(ed->buf.text + line_start,
                                     ed->buf.cursor - line_start,
                                     ed->buf.cursor - line_start, ed->tab_width);
            int spaces = ed->tab_width - (int)(col % (size_t)ed->tab_width);
            if (spaces <= 0) spaces = ed->tab_width;
            char pad[17];
            if (spaces > 16) spaces = 16;
            memset(pad, ' ', (size_t)spaces);
            editor_insert_text(ed, pad, (size_t)spaces);
        }
        return;
    case KEY_BACKTAB:
        dedent_line(ed);
        return;
    case KEY_ESC:
        ed->completion_active = false;
        ed->ghost[0] = '\0';
        ed->buf.anchor = SIZE_MAX;
        return;
    case KEY_UP:
        buf_move_up(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_DOWN:
        buf_move_down(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_LEFT:
        if (mods & (KEY_MOD_CTRL | KEY_MOD_ALT)) buf_move_word_left(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        else buf_move_left(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_RIGHT:
        if (mods & KEY_MOD_CTRL) {
            if (ed->ghost[0]) editor_accept_completion(ed, true);
            else buf_move_word_right(&ed->buf, false);
        } else if (mods & KEY_MOD_ALT) {
            buf_move_word_right(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        } else {
            buf_move_right(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        }
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_HOME:
        if (mods & KEY_MOD_CTRL) buf_move_doc_home(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        else buf_move_home(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_END:
        if (mods & KEY_MOD_CTRL) buf_move_doc_end(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        else buf_move_end(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_DOC_HOME:
        buf_move_doc_home(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_DOC_END:
        buf_move_doc_end(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_PGUP: {
        int step = ed->rows > 4 ? ed->rows - 3 : 10;
        for (int i = 0; i < step; i++) buf_move_up(&ed->buf, false);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    }
    case KEY_PGDN: {
        int step = ed->rows > 4 ? ed->rows - 3 : 10;
        for (int i = 0; i < step; i++) buf_move_down(&ed->buf, false);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    }
    case KEY_WORD_LEFT:
        buf_move_word_left(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_WORD_RIGHT:
        buf_move_word_right(&ed->buf, (mods & KEY_MOD_SHIFT) != 0);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_F1:
        ed->help_visible = true;
        return;
    case KEY_F2:
        editor_save(ed, NULL);
        return;
    case KEY_F3:
        open_command(ed, "w ");
        return;
    case KEY_F4:
        open_command(ed, "e ");
        return;
    case KEY_F5:
        command_autosave(ed, "");
        return;
    case KEY_F6:
        ed->panel_visible = !ed->panel_visible;
        editor_set_message(ed, ed->panel_visible ? "Data panel on" : "Data panel off", false);
        return;
    case KEY_F9:
        open_command(ed, "");
        return;
    case KEY_CTRL_S:
        editor_save(ed, NULL);
        return;
    case KEY_CTRL_O:
        open_command(ed, "e ");
        return;
    case KEY_CTRL_N:
        editor_new(ed, false);
        return;
    case KEY_CTRL_Q:
        request_quit(ed);
        return;
    case KEY_CTRL_P:
        open_command(ed, "");
        return;
    case KEY_CTRL_Z:
        editor_undo(ed);
        return;
    case KEY_CTRL_Y:
        editor_redo(ed);
        return;
    case KEY_CTRL_A:
        buf_select_all(&ed->buf);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_CTRL_AT:
        if (!buf_has_selection(&ed->buf)) ed->buf.anchor = ed->buf.cursor;
        return;
    case KEY_CTRL_C:
        editor_copy(ed, false);
        return;
    case KEY_CTRL_X:
        editor_copy(ed, true);
        return;
    case KEY_CTRL_V:
        editor_paste_latest(ed);
        return;
    case KEY_CTRL_F:
        open_command(ed, "find ");
        return;
    case KEY_CTRL_G:
        open_command(ed, "goto ");
        return;
    case KEY_CTRL_D:
        if (ed->buf.cursor < ed->buf.len) {
            size_t next = utf8_next(ed->buf.text, ed->buf.cursor, ed->buf.len);
            editor_apply(ed, ed->buf.cursor, next, NULL, 0);
        }
        return;
    case KEY_CTRL_K: {
        size_t end = buf_line_end(&ed->buf, ed->buf.cursor);
        if (end == ed->buf.cursor && ed->buf.cursor < ed->buf.len) end++;
        editor_apply(ed, ed->buf.cursor, end, NULL, 0);
        return;
    }
    case KEY_CTRL_U: {
        size_t start = buf_line_start(&ed->buf, ed->buf.cursor);
        editor_apply(ed, start, ed->buf.cursor, NULL, 0);
        return;
    }
    case KEY_CTRL_W:
        if (ed->buf.cursor > 0) {
            size_t i = ed->buf.cursor;
            while (i > 0 && !ident_char((unsigned char)ed->buf.text[i - 1])) i--;
            while (i > 0 && ident_char((unsigned char)ed->buf.text[i - 1])) i--;
            editor_apply(ed, i, ed->buf.cursor, NULL, 0);
        }
        return;
    case KEY_CTRL_T:
        transpose_chars(ed);
        return;
    case KEY_CTRL_E:
        buf_move_end(&ed->buf, false);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_CTRL_L:
        return;
    case KEY_CTRL_H:
        if (ed->buf.cursor > 0) {
            size_t prev = utf8_prev(ed->buf.text, ed->buf.cursor);
            editor_apply(ed, prev, ed->buf.cursor, NULL, 0);
        }
        return;
    case KEY_CTRL_M:
    case KEY_CTRL_J:
        smart_enter(ed);
        return;
    case KEY_CTRL_I:
        editor_handle_key(ed, KEY_TAB, 0);
        return;
    case KEY_ALT_BACKSPACE:
    case KEY_DEL_WORD:
        if (ed->buf.cursor > 0) {
            size_t i = ed->buf.cursor;
            while (i > 0 && !ident_char((unsigned char)ed->buf.text[i - 1])) i--;
            while (i > 0 && ident_char((unsigned char)ed->buf.text[i - 1])) i--;
            editor_apply(ed, i, ed->buf.cursor, NULL, 0);
        }
        return;
    case KEY_ALT_B:
        buf_move_word_left(&ed->buf, false);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_ALT_F:
        buf_move_word_right(&ed->buf, false);
        editor_update_completion(ed);
        maybe_publish(ed);
        return;
    case KEY_ALT_D:
        if (ed->buf.cursor < ed->buf.len) {
            size_t i = ed->buf.cursor;
            while (i < ed->buf.len && !ident_char((unsigned char)ed->buf.text[i])) i++;
            while (i < ed->buf.len && ident_char((unsigned char)ed->buf.text[i])) i++;
            editor_apply(ed, ed->buf.cursor, i, NULL, 0);
        }
        return;
    case KEY_ALT_1: case KEY_ALT_2: case KEY_ALT_3: case KEY_ALT_4:
    case KEY_ALT_5: case KEY_ALT_6: case KEY_ALT_7: case KEY_ALT_8:
    case KEY_ALT_9:
        editor_paste_history(ed, base - KEY_ALT_1);
        return;
    default:
        return;
    }
}

const char *editor_display_path(Editor *ed) {
    static char display[PATH_MAX];
    path_display(ed->has_path ? ed->path : "", display, sizeof(display));
    return display;
}

char *editor_trie_json(Editor *ed, int depth, int limit) {
    StrBuf sb;
    sb_init(&sb);
    trie_to_json(&ed->trie, ed->prefix, depth, limit, 60, 12, &sb);
    if (!sb.data) return xstrdup("{}");
    return sb.data;
}

static void add_string_or_null(StrBuf *sb, const char *value) {
    if (value) sb_json_str(sb, value);
    else sb_adds(sb, "null");
}

void editor_build_state(Editor *ed, const char *status, StrBuf *sb) {
    double now = now_sec();
    int line = 1, col = 1;
    buf_line_col(&ed->buf, ed->buf.cursor, &line, &col);
    int cursor_index = utf8_len_n(ed->buf.text ? ed->buf.text : "", ed->buf.cursor);
    int selection_chars = 0;
    if (buf_has_selection(&ed->buf)) {
        size_t s, e;
        buf_sel_range(&ed->buf, &s, &e);
        selection_chars = utf8_len_n(ed->buf.text + s, e - s);
    }
    char display[PATH_MAX];
    path_display(ed->has_path ? ed->path : "", display, sizeof(display));
    char bom_hex[16] = "";
    for (int i = 0; i < ed->bom_len; i++) {
        char part[4];
        snprintf(part, sizeof(part), "%02x", ed->bom[i]);
        strncat(bom_hex, part, sizeof(bom_hex) - strlen(bom_hex) - 1);
    }
    double seconds_since_save = -1.0;
    if (ed->ever_saved) seconds_since_save = now - ed->last_saved;
    double due_in = -1.0;
    bool autosave_active = ed->autosave_enabled && ed->has_path && ed->dirty;
    if (ed->autosave_enabled && ed->dirty && ed->has_path && ed->ever_saved) {
        due_in = ed->autosave_interval - (now - ed->last_saved);
        if (due_in < 0) due_in = 0;
    }
    char *text_preview = preview_clip(ed->buf.text ? ed->buf.text : "",
                                      ed->buf.len, 240);

    sb_adds(sb, "{\"schema\":");
    sb_json_str(sb, ANVIL_SCHEMA);
    sb_addf(sb, ",\"version\":%d,\"seq\":%ld,\"timestamp\":%.3f,\"status\":",
            ANVIL_SCHEMA_VERSION, ed->state_seq, now);
    sb_json_str(sb, status ? status : "ok");

    sb_adds(sb, ",\"app\":{\"name\":\"dsa-editor\",\"version\":");
    sb_json_str(sb, ANVIL_VERSION);
    sb_addf(sb, ",\"pid\":%ld,\"session\":", (long)getpid());
    sb_json_str(sb, ed->session);
    sb_adds(sb, ",\"port\":");
    if (ed->port > 0) sb_addf(sb, "%d", ed->port);
    else sb_adds(sb, "null");
    sb_adds(sb, ",\"server_url\":");
    if (ed->port > 0) {
        char url[64];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d", ed->port);
        sb_json_str(sb, url);
    } else {
        sb_adds(sb, "null");
    }
    sb_adds(sb, ",\"theme\":");
    sb_json_str(sb, ed->theme);
    sb_addf(sb, ",\"tab_width\":%d,\"state_file\":", ed->tab_width);
    add_string_or_null(sb, ed->state_file_enabled && ed->state_file[0] ? ed->state_file : NULL);
    sb_adds(sb, ",\"state_file_error\":");
    add_string_or_null(sb, ed->state_file_error[0] ? ed->state_file_error : NULL);
    sb_addc(sb, '}');

    sb_adds(sb, ",\"file\":{\"path\":");
    add_string_or_null(sb, ed->has_path ? ed->path : NULL);
    sb_adds(sb, ",\"display_path\":");
    sb_json_str(sb, display);
    sb_adds(sb, ",\"name\":");
    sb_json_str(sb, ed->has_path ? path_basename(ed->path) : "untitled");
    sb_addf(sb, ",\"exists\":%s,\"dirty\":%s,\"encoding\":",
            ed->has_path && file_exists(ed->path) ? "true" : "false",
            ed->dirty ? "true" : "false");
    sb_json_str(sb, ed->encoding);
    sb_adds(sb, ",\"bom\":");
    sb_json_str(sb, bom_hex);
    sb_adds(sb, ",\"newline\":");
    sb_json_str(sb, newline_name(ed->newline));
    sb_adds(sb, ",\"language\":");
    sb_json_str(sb, ed->language);
    sb_adds(sb, ",\"language_label\":");
    sb_json_str(sb, ed->language_label);
    sb_addf(sb, ",\"size_bytes\":%zu,\"last_saved\":", ed->size_bytes);
    if (ed->ever_saved) sb_addf(sb, "%.3f", ed->last_saved);
    else sb_adds(sb, "null");
    sb_adds(sb, ",\"seconds_since_save\":");
    if (ed->ever_saved) sb_addf(sb, "%.1f", seconds_since_save);
    else sb_adds(sb, "null");
    sb_addc(sb, '}');

    sb_addf(sb, ",\"stats\":{\"words\":%d,\"chars\":%d,\"chars_no_whitespace\":%d,"
                "\"lines\":%d,\"cursor_index\":%d,\"cursor_line\":%d,\"cursor_col\":%d,"
                "\"selection_chars\":%d,\"saved_chars\":%zu}",
            ed->stats.words, ed->stats.chars, ed->stats.chars_no_ws, ed->stats.lines,
            cursor_index, line, col, selection_chars, ed->saved_chars);

    sb_adds(sb, ",\"document\":{\"text_preview\":");
    sb_json_str(sb, text_preview);
    sb_addf(sb, ",\"chars\":%d,\"lines\":%d,\"cursor_index\":%d,\"selection_chars\":%d}",
            ed->stats.chars, ed->stats.lines, cursor_index, selection_chars);

    sb_adds(sb, ",\"undo\":");
    undo_to_json(&ed->undo, sb);
    sb_adds(sb, ",\"clipboard\":");
    clip_to_json(&ed->clip, sb);
    sb_adds(sb, ",\"trie\":");
    trie_to_json(&ed->trie, ed->prefix, 3, 800, 60, 12, sb);

    sb_adds(sb, ",\"autosave\":{\"enabled\":");
    sb_adds(sb, ed->autosave_enabled ? "true" : "false");
    sb_addf(sb, ",\"interval_seconds\":%g,\"last_saved\":", ed->autosave_interval);
    if (ed->ever_saved) sb_addf(sb, "%.3f", ed->last_saved);
    else sb_adds(sb, "null");
    sb_adds(sb, ",\"seconds_since_save\":");
    if (ed->ever_saved) sb_addf(sb, "%.1f", seconds_since_save);
    else sb_adds(sb, "null");
    sb_adds(sb, ",\"due_in_seconds\":");
    if (due_in >= 0) sb_addf(sb, "%.1f", due_in);
    else sb_adds(sb, "null");
    sb_addf(sb, ",\"active\":%s}", autosave_active ? "true" : "false");

    sb_addf(sb, ",\"settings\":{\"undo_capacity\":%zu,\"clipboard_capacity\":%d,"
                "\"autosave_interval\":%g,\"tab_width\":%d,\"theme\":",
            ed->undo.capacity, ed->clip.capacity, ed->autosave_interval, ed->tab_width);
    sb_json_str(sb, ed->theme);
    sb_addf(sb, ",\"server_enabled\":%s}", ed->server_enabled ? "true" : "false");

    sb_adds(sb, ",\"ui\":{\"panel_visible\":");
    sb_adds(sb, ed->panel_visible ? "true" : "false");
    sb_adds(sb, ",\"help_visible\":");
    sb_adds(sb, ed->help_visible ? "true" : "false");
    sb_adds(sb, ",\"command_mode\":");
    sb_adds(sb, ed->command_mode ? "true" : "false");
    sb_adds(sb, ",\"message\":");
    sb_json_str(sb, ed->message);
    sb_addf(sb, ",\"message_error\":%s}}", ed->message_error ? "true" : "false");

    free(text_preview);
}

void editor_publish(Editor *ed, const char *status) {
    ed->state_seq++;
    ed->state_timestamp = now_sec();
    StrBuf sb;
    sb_init(&sb);
    editor_build_state(ed, status, &sb);
    if (ed->bridge) {
        bridge_publish(ed->bridge, sb.data ? sb.data : "{}", ed->state_seq);
        ed->port = bridge_port(ed->bridge);
    }
    double now = now_sec();
    if (ed->state_file_enabled && ed->state_file[0]) {
        if (strcmp(status ? status : "ok", "ok") != 0 || now - ed->last_state_file >= 1.0) {
            char err[256] = "";
            bridge_write_state_file(ed->state_file, sb.data ? sb.data : "{}", err, sizeof(err));
            if (err[0]) snprintf(ed->state_file_error, sizeof(ed->state_file_error), "%s", err);
            else ed->state_file_error[0] = '\0';
            ed->last_state_file = now;
        }
    }
    ed->last_publish = now;
    sb_free(&sb);
}

void editor_check_autosave(Editor *ed) {
    if (!ed->autosave_enabled || !ed->dirty || !ed->has_path) return;
    double now = now_sec();
    if (ed->last_saved <= 0 || now - ed->last_saved >= ed->autosave_interval) {
        save_internal(ed, NULL, true);
    }
}

void editor_tick(Editor *ed) {
    double now = now_sec();
    if (now - ed->last_publish >= 1.0) editor_publish(ed, "ok");
    editor_check_autosave(ed);
}
