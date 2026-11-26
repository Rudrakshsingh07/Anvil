/* Snapshot-based undo/redo with two bounded stacks. */
#include "anvil.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void snapshot_free(Snapshot *s) {
    free(s->text);
    free(s->inserted);
    free(s->deleted);
    memset(s, 0, sizeof(*s));
}

static void stack_init(SnapshotStack *st, size_t capacity) {
    memset(st, 0, sizeof(*st));
    st->capacity = capacity ? capacity : 1;
}

static void stack_clear(SnapshotStack *st) {
    for (int i = 0; i < st->len; i++) snapshot_free(&st->items[i]);
    st->len = 0;
    st->total_chars = 0;
}

static void stack_push(SnapshotStack *st, Snapshot snap) {
    if (st->len > 0 && (size_t)st->len >= st->capacity) {
        snapshot_free(&st->items[0]);
        memmove(st->items, st->items + 1, sizeof(Snapshot) * (size_t)(st->len - 1));
        st->len--;
    }
    if (st->len == st->cap) {
        st->cap = st->cap ? st->cap * 2 : 16;
        st->items = xrealloc(st->items, sizeof(Snapshot) * (size_t)st->cap);
    }
    st->items[st->len++] = snap;
    st->total_chars += snap.chars;
}

static Snapshot stack_pop(SnapshotStack *st) {
    Snapshot empty;
    memset(&empty, 0, sizeof(empty));
    if (st->len <= 0) return empty;
    Snapshot snap = st->items[--st->len];
    if (snap.chars <= st->total_chars) st->total_chars -= snap.chars;
    else st->total_chars = 0;
    return snap;
}

static void stack_drop_oldest(SnapshotStack *st) {
    if (st->len <= 0) return;
    snapshot_free(&st->items[0]);
    memmove(st->items, st->items + 1, sizeof(Snapshot) * (size_t)(st->len - 1));
    st->len--;
}

static bool all_space(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!isspace((unsigned char)s[i])) return false;
    }
    return true;
}

static char *clip_dup(const char *s, size_t len, size_t limit) {
    if (!s) return xstrdup("");
    if (len > limit) len = limit;
    return xstrndup(s, len);
}

void undo_init(UndoRedo *u, size_t capacity, double group_window, size_t max_total_chars) {
    memset(u, 0, sizeof(*u));
    u->capacity = capacity ? capacity : 1;
    u->group_window = group_window;
    u->max_total_chars = max_total_chars;
    stack_init(&u->undo, u->capacity);
    stack_init(&u->redo, u->capacity);
    pthread_mutex_init(&u->lock, NULL);
}

void undo_free(UndoRedo *u) {
    stack_clear(&u->undo);
    stack_clear(&u->redo);
    free(u->undo.items);
    free(u->redo.items);
    pthread_mutex_destroy(&u->lock);
    memset(u, 0, sizeof(*u));
}

void undo_break_group(UndoRedo *u) {
    pthread_mutex_lock(&u->lock);
    u->group_key[0] = '\0';
    u->group_time = 0.0;
    pthread_mutex_unlock(&u->lock);
}

void undo_reset(UndoRedo *u) {
    pthread_mutex_lock(&u->lock);
    stack_clear(&u->undo);
    stack_clear(&u->redo);
    u->group_key[0] = '\0';
    u->group_time = 0.0;
    pthread_mutex_unlock(&u->lock);
}

static void trim_stack(UndoRedo *u, SnapshotStack *st) {
    while (st->total_chars > u->max_total_chars && st->len > 1) {
        stack_drop_oldest(st);
    }
}

void undo_record_op(UndoRedo *u, const char *old_text, size_t old_cursor,
                    size_t position, const char *deleted, size_t deleted_len,
                    const char *inserted, size_t inserted_len, size_t new_cursor) {
    (void)position;
    (void)new_cursor;
    if (deleted_len == 0 && inserted_len == 0) return;

    double mono = now_sec();
    double wall = mono;

    const char *kind;
    if (inserted_len && !deleted_len) kind = "insert";
    else if (deleted_len && !inserted_len) kind = "delete";
    else kind = "replace";

    const char *label = kind;
    if (strcmp(kind, "insert") == 0 && inserted) {
        for (size_t i = 0; i < inserted_len; i++) {
            if (inserted[i] == '\n') { label = "newline"; break; }
        }
    }

    char key[16] = "";
    if (strcmp(kind, "insert") == 0 && inserted_len && !all_space(inserted, inserted_len)) {
        snprintf(key, sizeof(key), "insert");
    } else if (strcmp(kind, "delete") == 0 && deleted_len && !all_space(deleted, deleted_len)) {
        snprintf(key, sizeof(key), "delete");
    }

    pthread_mutex_lock(&u->lock);

    if (key[0] && strcmp(key, u->group_key) == 0 &&
        (mono - u->group_time) <= u->group_window && u->undo.len > 0) {
        Snapshot *top = &u->undo.items[u->undo.len - 1];
        if (strcmp(kind, "insert") == 0) {
            size_t old = top->inserted ? strlen(top->inserted) : 0;
            size_t take = inserted_len;
            if (old + take > 400) take = old < 400 ? 400 - old : 0;
            char *merged = xmalloc(old + take + 1);
            if (old) memcpy(merged, top->inserted, old);
            if (take) memcpy(merged + old, inserted, take);
            merged[old + take] = '\0';
            free(top->inserted);
            top->inserted = merged;
        } else {
            size_t old = top->deleted ? strlen(top->deleted) : 0;
            size_t take = deleted_len;
            if (old + take > 400) take = old < 400 ? 400 - old : 0;
            char *merged = xmalloc(old + take + 1);
            if (take) memcpy(merged, deleted, take);
            if (old) memcpy(merged + take, top->deleted, old);
            merged[old + take] = '\0';
            free(top->deleted);
            top->deleted = merged;
        }
        top->edit_count++;
        top->last_timestamp = wall;
        snprintf(top->label, sizeof(top->label), "%s", label);
        snprintf(top->kind, sizeof(top->kind), "%s", kind);
        u->group_time = mono;
        pthread_mutex_unlock(&u->lock);
        return;
    }

    Snapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.text = xstrdup(old_text ? old_text : "");
    snap.cursor = old_cursor;
    snprintf(snap.label, sizeof(snap.label), "%s", label);
    snprintf(snap.kind, sizeof(snap.kind), "%s", kind);
    snap.timestamp = wall;
    snap.last_timestamp = wall;
    snap.edit_count = 1;
    snap.chars = strlen(snap.text);
    snap.inserted = clip_dup(inserted, inserted_len, 400);
    snap.deleted = clip_dup(deleted, deleted_len, 400);

    stack_push(&u->undo, snap);
    stack_clear(&u->redo);
    trim_stack(u, &u->undo);

    snprintf(u->group_key, sizeof(u->group_key), "%s", key);
    u->group_time = mono;
    pthread_mutex_unlock(&u->lock);
}

static void compute_diff(const char *old, const char *newer,
                         size_t *pos, size_t *del_start, size_t *del_len,
                         size_t *ins_start, size_t *ins_len) {
    size_t old_len = strlen(old);
    size_t new_len = strlen(newer);
    size_t prefix = 0;
    size_t max_prefix = old_len < new_len ? old_len : new_len;
    while (prefix < max_prefix && old[prefix] == newer[prefix]) prefix++;

    size_t suffix = 0;
    size_t max_suffix = (old_len < new_len ? old_len : new_len) - prefix;
    while (suffix < max_suffix &&
           old[old_len - 1 - suffix] == newer[new_len - 1 - suffix]) {
        suffix++;
    }
    *pos = prefix;
    *del_start = prefix;
    *del_len = old_len - suffix - prefix;
    *ins_start = prefix;
    *ins_len = new_len - suffix - prefix;
}

void undo_record(UndoRedo *u, const char *old_text, size_t old_cursor,
                 const char *new_text, size_t new_cursor) {
    if (!old_text || !new_text || strcmp(old_text, new_text) == 0) return;
    size_t pos, ds, dl, is, il;
    compute_diff(old_text, new_text, &pos, &ds, &dl, &is, &il);
    undo_record_op(u, old_text, old_cursor, pos,
                   old_text + ds, dl, new_text + is, il, new_cursor);
}

char *undo_undo(UndoRedo *u, const char *current_text, size_t current_cursor,
                size_t *out_cursor) {
    pthread_mutex_lock(&u->lock);
    if (u->undo.len == 0) {
        pthread_mutex_unlock(&u->lock);
        return NULL;
    }
    Snapshot snap = stack_pop(&u->undo);

    Snapshot redo;
    memset(&redo, 0, sizeof(redo));
    redo.text = xstrdup(current_text ? current_text : "");
    redo.cursor = current_cursor;
    snprintf(redo.label, sizeof(redo.label), "%s", snap.label);
    snprintf(redo.kind, sizeof(redo.kind), "%s", snap.kind);
    redo.timestamp = now_sec();
    redo.last_timestamp = redo.timestamp;
    redo.edit_count = snap.edit_count;
    redo.chars = strlen(redo.text);
    redo.inserted = xstrdup(snap.inserted ? snap.inserted : "");
    redo.deleted = xstrdup(snap.deleted ? snap.deleted : "");
    stack_push(&u->redo, redo);
    trim_stack(u, &u->redo);
    u->group_key[0] = '\0';
    u->group_time = 0.0;
    pthread_mutex_unlock(&u->lock);

    free(snap.inserted);
    free(snap.deleted);
    if (out_cursor) *out_cursor = snap.cursor;
    return snap.text;
}

char *undo_redo(UndoRedo *u, const char *current_text, size_t current_cursor,
                size_t *out_cursor) {
    pthread_mutex_lock(&u->lock);
    if (u->redo.len == 0) {
        pthread_mutex_unlock(&u->lock);
        return NULL;
    }
    Snapshot snap = stack_pop(&u->redo);

    Snapshot undo;
    memset(&undo, 0, sizeof(undo));
    undo.text = xstrdup(current_text ? current_text : "");
    undo.cursor = current_cursor;
    snprintf(undo.label, sizeof(undo.label), "%s", snap.label);
    snprintf(undo.kind, sizeof(undo.kind), "%s", snap.kind);
    undo.timestamp = now_sec();
    undo.last_timestamp = undo.timestamp;
    undo.edit_count = snap.edit_count;
    undo.chars = strlen(undo.text);
    undo.inserted = xstrdup(snap.inserted ? snap.inserted : "");
    undo.deleted = xstrdup(snap.deleted ? snap.deleted : "");
    stack_push(&u->undo, undo);
    trim_stack(u, &u->undo);
    u->group_key[0] = '\0';
    u->group_time = 0.0;
    pthread_mutex_unlock(&u->lock);

    free(snap.inserted);
    free(snap.deleted);
    if (out_cursor) *out_cursor = snap.cursor;
    return snap.text;
}

int undo_depth(UndoRedo *u) {
    pthread_mutex_lock(&u->lock);
    int n = u->undo.len;
    pthread_mutex_unlock(&u->lock);
    return n;
}

int redo_depth(UndoRedo *u) {
    pthread_mutex_lock(&u->lock);
    int n = u->redo.len;
    pthread_mutex_unlock(&u->lock);
    return n;
}

static void stack_to_json(SnapshotStack *st, StrBuf *sb, double now) {
    sb_addf(sb, "{\"depth\":%d,\"capacity\":%zu,\"total_chars\":%zu,\"top_first\":true,\"entries\":[",
            st->len, st->capacity, st->total_chars);
    bool first = true;
    for (int i = st->len - 1; i >= 0; i--) {
        Snapshot *s = &st->items[i];
        if (!first) sb_addc(sb, ',');
        first = false;
        char *tp = preview_collapse(s->text ? s->text : "", s->text ? strlen(s->text) : 0, 72);
        char *ip = preview_clip(s->inserted ? s->inserted : "",
                                s->inserted ? strlen(s->inserted) : 0, 120);
        char *dp = preview_clip(s->deleted ? s->deleted : "",
                                s->deleted ? strlen(s->deleted) : 0, 120);
        sb_adds(sb, "{\"label\":");
        sb_json_str(sb, s->label);
        sb_adds(sb, ",\"kind\":");
        sb_json_str(sb, s->kind);
        sb_addf(sb, ",\"cursor\":%zu,\"chars\":%zu,\"text_preview\":", s->cursor, s->chars);
        sb_json_str(sb, tp);
        sb_adds(sb, ",\"inserted_preview\":");
        sb_json_str(sb, ip);
        sb_adds(sb, ",\"deleted_preview\":");
        sb_json_str(sb, dp);
        sb_addf(sb, ",\"edit_count\":%d,\"timestamp\":%.3f,\"last_timestamp\":%.3f,\"age_seconds\":%.2f}",
                s->edit_count, s->timestamp, s->last_timestamp,
                now > s->timestamp ? now - s->timestamp : 0.0);
        free(tp);
        free(ip);
        free(dp);
    }
    sb_adds(sb, "]}");
}

void undo_to_json(UndoRedo *u, StrBuf *sb) {
    double now = now_sec();
    pthread_mutex_lock(&u->lock);
    sb_adds(sb, "{\"undo_stack\":");
    stack_to_json(&u->undo, sb, now);
    sb_adds(sb, ",\"redo_stack\":");
    stack_to_json(&u->redo, sb, now);
    sb_addf(sb, ",\"can_undo\":%s,\"can_redo\":%s",
            u->undo.len ? "true" : "false", u->redo.len ? "true" : "false");
    sb_adds(sb, ",\"group\":{\"active\":");
    sb_adds(sb, u->group_key[0] ? "true" : "false");
    sb_adds(sb, ",\"kind\":");
    if (u->group_key[0]) sb_json_str(sb, u->group_key);
    else sb_adds(sb, "null");
    sb_addf(sb, "},\"policy\":{\"snapshot_based\":true,\"group_window_seconds\":%.3f,\"max_total_chars\":%zu}}",
            u->group_window, u->max_total_chars);
    pthread_mutex_unlock(&u->lock);
}
