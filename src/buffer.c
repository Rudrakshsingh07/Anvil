/* Text buffer and cursor movement. */
#include "anvil.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool is_word_char(unsigned char c) {
    return isalnum(c) || c == '_';
}

void buf_init(Buffer *b) {
    memset(b, 0, sizeof(*b));
    b->anchor = SIZE_MAX;
}

void buf_free(Buffer *b) {
    free(b->text);
    memset(b, 0, sizeof(*b));
    b->anchor = SIZE_MAX;
}

void buf_reserve(Buffer *b, size_t extra) {
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return;
    size_t cap = b->cap ? b->cap : 128;
    while (cap < need) cap *= 2;
    b->text = xrealloc(b->text, cap);
    b->cap = cap;
}

void buf_set(Buffer *b, const char *text, size_t len, size_t cursor) {
    buf_reserve(b, len);
    if (len) memcpy(b->text, text, len);
    b->len = len;
    b->text[len] = '\0';
    if (cursor > len) cursor = len;
    b->cursor = cursor;
    b->anchor = SIZE_MAX;
    b->goal_col = 0;
}

void buf_insert(Buffer *b, size_t pos, const char *text, size_t len) {
    if (len == 0) return;
    if (pos > b->len) pos = b->len;
    buf_reserve(b, len);
    memmove(b->text + pos + len, b->text + pos, b->len - pos + 1);
    memcpy(b->text + pos, text, len);
    b->len += len;
}

void buf_delete(Buffer *b, size_t pos, size_t len) {
    if (len == 0 || pos >= b->len) return;
    if (pos + len > b->len) len = b->len - pos;
    memmove(b->text + pos, b->text + pos + len, b->len - pos - len + 1);
    b->len -= len;
    if (b->cursor > b->len) b->cursor = b->len;
    if (b->anchor != SIZE_MAX && b->anchor > b->len) b->anchor = b->len;
}

char *buf_range_dup(const Buffer *b, size_t start, size_t end) {
    if (start > b->len) start = b->len;
    if (end > b->len) end = b->len;
    if (end < start) end = start;
    return xstrndup(b->text + start, end - start);
}

size_t buf_line_start(const Buffer *b, size_t pos) {
    if (pos > b->len) pos = b->len;
    while (pos > 0 && b->text[pos - 1] != '\n') pos--;
    return pos;
}

size_t buf_line_end(const Buffer *b, size_t pos) {
    if (pos > b->len) pos = b->len;
    while (pos < b->len && b->text[pos] != '\n') pos++;
    return pos;
}

size_t buf_next_line(const Buffer *b, size_t pos) {
    size_t end = buf_line_end(b, pos);
    if (end < b->len) return end + 1;
    return b->len;
}

size_t buf_prev_line(const Buffer *b, size_t pos) {
    size_t start = buf_line_start(b, pos);
    if (start == 0) return 0;
    return buf_line_start(b, start - 1);
}

void buf_line_col(const Buffer *b, size_t pos, int *line, int *col) {
    if (pos > b->len) pos = b->len;
    int l = 1;
    size_t last = 0;
    for (size_t i = 0; i < pos; i++) {
        if (b->text[i] == '\n') { l++; last = i + 1; }
    }
    if (line) *line = l;
    if (col) *col = utf8_len_n(b->text + last, pos - last) + 1;
}

size_t buf_offset_at(const Buffer *b, int line, int col) {
    if (line < 1) line = 1;
    if (col < 1) col = 1;
    size_t pos = 0;
    int current = 1;
    while (current < line && pos < b->len) {
        size_t end = buf_line_end(b, pos);
        if (end >= b->len) { pos = b->len; break; }
        pos = end + 1;
        current++;
    }
    size_t end = buf_line_end(b, pos);
    int want = col - 1;
    int seen = 0;
    size_t i = pos;
    while (i < end && seen < want) {
        i = utf8_next(b->text, i, end);
        seen++;
    }
    return i;
}

size_t buf_display_col(const Buffer *b, size_t pos) {
    size_t start = buf_line_start(b, pos);
    return display_col(b->text + start, pos - start, pos - start, 4);
}

bool buf_has_selection(const Buffer *b) {
    return b->anchor != SIZE_MAX && b->anchor != b->cursor;
}

void buf_sel_range(const Buffer *b, size_t *start, size_t *end) {
    size_t a = b->cursor;
    size_t z = b->anchor == SIZE_MAX ? b->cursor : b->anchor;
    if (a > z) { size_t t = a; a = z; z = t; }
    if (start) *start = a;
    if (end) *end = z;
}

void buf_clear_selection(Buffer *b) {
    b->anchor = SIZE_MAX;
}

static void begin_move(Buffer *b, bool select) {
    if (select) {
        if (b->anchor == SIZE_MAX) b->anchor = b->cursor;
    } else {
        b->anchor = SIZE_MAX;
    }
}

void buf_move_left(Buffer *b, bool select) {
    begin_move(b, select);
    b->cursor = utf8_prev(b->text, b->cursor);
    b->goal_col = 0;
}

void buf_move_right(Buffer *b, bool select) {
    begin_move(b, select);
    b->cursor = utf8_next(b->text, b->cursor, b->len);
    b->goal_col = 0;
}

static void move_vertical(Buffer *b, int direction, bool select) {
    begin_move(b, select);
    size_t start = buf_line_start(b, b->cursor);
    size_t end = buf_line_end(b, b->cursor);
    size_t col = display_col(b->text + start, end - start, b->cursor - start, 4);
    if (b->goal_col == 0 && col > 0) b->goal_col = col;
    size_t goal = b->goal_col;
    size_t target_start;
    if (direction < 0) {
        if (start == 0) { b->cursor = 0; return; }
        target_start = buf_prev_line(b, b->cursor);
    } else {
        if (end >= b->len) { b->cursor = b->len; return; }
        target_start = end + 1;
    }
    size_t target_end = buf_line_end(b, target_start);
    size_t off = byte_at_display_col(b->text + target_start, target_end - target_start,
                                     goal, 4);
    b->cursor = target_start + off;
}

void buf_move_up(Buffer *b, bool select) { move_vertical(b, -1, select); }
void buf_move_down(Buffer *b, bool select) { move_vertical(b, 1, select); }

void buf_move_home(Buffer *b, bool select) {
    begin_move(b, select);
    b->cursor = buf_line_start(b, b->cursor);
    b->goal_col = 0;
}

void buf_move_end(Buffer *b, bool select) {
    begin_move(b, select);
    b->cursor = buf_line_end(b, b->cursor);
    b->goal_col = 0;
}

void buf_move_doc_home(Buffer *b, bool select) {
    begin_move(b, select);
    b->cursor = 0;
    b->goal_col = 0;
}

void buf_move_doc_end(Buffer *b, bool select) {
    begin_move(b, select);
    b->cursor = b->len;
    b->goal_col = 0;
}

void buf_move_word_left(Buffer *b, bool select) {
    begin_move(b, select);
    size_t i = b->cursor;
    while (i > 0 && !is_word_char((unsigned char)b->text[i - 1])) i--;
    while (i > 0 && is_word_char((unsigned char)b->text[i - 1])) i--;
    b->cursor = i;
    b->goal_col = 0;
}

void buf_move_word_right(Buffer *b, bool select) {
    begin_move(b, select);
    size_t i = b->cursor;
    while (i < b->len && !is_word_char((unsigned char)b->text[i])) i++;
    while (i < b->len && is_word_char((unsigned char)b->text[i])) i++;
    b->cursor = i;
    b->goal_col = 0;
}

void buf_select_all(Buffer *b) {
    b->anchor = 0;
    b->cursor = b->len;
}

void buf_delete_word_before(Buffer *b) {
    size_t i = b->cursor;
    while (i > 0 && !is_word_char((unsigned char)b->text[i - 1])) i--;
    while (i > 0 && is_word_char((unsigned char)b->text[i - 1])) i--;
    if (i < b->cursor) {
        buf_delete(b, i, b->cursor - i);
        b->cursor = i;
    }
}

void buf_delete_word_after(Buffer *b) {
    size_t i = b->cursor;
    while (i < b->len && !is_word_char((unsigned char)b->text[i])) i++;
    while (i < b->len && is_word_char((unsigned char)b->text[i])) i++;
    if (i > b->cursor) buf_delete(b, b->cursor, i - b->cursor);
}

void buf_dedent_line(Buffer *b) {
    size_t start = buf_line_start(b, b->cursor);
    size_t remove = 0;
    while (remove < 4 && start + remove < b->len) {
        char c = b->text[start + remove];
        if (c != ' ' && c != '\t') break;
        remove++;
    }
    if (remove) {
        size_t cursor = b->cursor;
        buf_delete(b, start, remove);
        if (cursor >= start + remove) cursor -= remove;
        else if (cursor > start) cursor = start;
        b->cursor = cursor;
    }
}
