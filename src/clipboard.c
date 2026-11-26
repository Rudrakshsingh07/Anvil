/* Bounded FIFO clipboard history. */
#include "anvil.h"

#include <stdlib.h>
#include <string.h>

void clip_init(Clipboard *c, int capacity) {
    memset(c, 0, sizeof(*c));
    c->capacity = capacity > 0 ? capacity : 1;
    c->next_id = 1;
    pthread_mutex_init(&c->lock, NULL);
}

void clip_free(Clipboard *c) {
    for (int i = 0; i < c->len; i++) free(c->items[i].text);
    free(c->items);
    pthread_mutex_destroy(&c->lock);
    memset(c, 0, sizeof(*c));
}

void clip_enqueue(Clipboard *c, const char *text) {
    if (!text || !*text) return;
    pthread_mutex_lock(&c->lock);
    if (c->len > 0 && strcmp(c->items[c->len - 1].text, text) == 0) {
        c->items[c->len - 1].timestamp = now_sec();
        pthread_mutex_unlock(&c->lock);
        return;
    }
    if (c->len >= c->capacity) {
        free(c->items[0].text);
        memmove(c->items, c->items + 1, sizeof(ClipEntry) * (size_t)(c->len - 1));
        c->len--;
    }
    if (c->len == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8;
        c->items = xrealloc(c->items, sizeof(ClipEntry) * (size_t)c->cap);
    }
    c->items[c->len].id = c->next_id++;
    c->items[c->len].text = xstrdup(text);
    c->items[c->len].timestamp = now_sec();
    c->len++;
    pthread_mutex_unlock(&c->lock);
}

const ClipEntry *clip_get(Clipboard *c, int index_from_newest) {
    if (index_from_newest < 0) return NULL;
    pthread_mutex_lock(&c->lock);
    const ClipEntry *entry = NULL;
    if (index_from_newest < c->len) {
        entry = &c->items[c->len - 1 - index_from_newest];
    }
    pthread_mutex_unlock(&c->lock);
    return entry;
}

int clip_size(Clipboard *c) {
    pthread_mutex_lock(&c->lock);
    int n = c->len;
    pthread_mutex_unlock(&c->lock);
    return n;
}

void clip_to_json(Clipboard *c, StrBuf *sb) {
    double now = now_sec();
    pthread_mutex_lock(&c->lock);
    sb_addf(sb, "{\"capacity\":%d,\"size\":%d,\"order\":\"oldest_first\",\"head\":",
            c->capacity, c->len);
    if (c->len) sb_addf(sb, "%d", c->items[0].id);
    else sb_adds(sb, "null");
    sb_adds(sb, ",\"tail\":");
    if (c->len) sb_addf(sb, "%d", c->items[c->len - 1].id);
    else sb_adds(sb, "null");
    sb_adds(sb, ",\"entries\":[");
    for (int i = 0; i < c->len; i++) {
        ClipEntry *e = &c->items[i];
        if (i) sb_addc(sb, ',');
        char *preview = preview_collapse(e->text, strlen(e->text), 60);
        int lines = 1;
        for (const char *p = e->text; *p; p++) if (*p == '\n') lines++;
        sb_addf(sb, "{\"id\":%d,\"text\":", e->id);
        sb_json_str(sb, e->text);
        sb_adds(sb, ",\"preview\":");
        sb_json_str(sb, preview);
        sb_addf(sb, ",\"chars\":%zu,\"lines\":%d,\"timestamp\":%.3f,\"age_seconds\":%.2f}",
                strlen(e->text), lines, e->timestamp,
                now > e->timestamp ? now - e->timestamp : 0.0);
        free(preview);
    }
    sb_adds(sb, "]}");
    pthread_mutex_unlock(&c->lock);
}
