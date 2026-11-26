/* Anvil: a terminal text editor written in C.
 *
 * The editor core is plain C11 + POSIX.  The only external process is the
 * optional Python visualizer, which reads the read-only JSON/HTTP bridge.
 */
#ifndef ANVIL_H
#define ANVIL_H

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define ANVIL_VERSION "0.2.0"
#define ANVIL_SCHEMA "dsa-editor/state"
#define ANVIL_SCHEMA_VERSION 1

/* ------------------------------------------------------------------------- */
/* Dynamic strings and small utilities                                       */
/* ------------------------------------------------------------------------- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_clear(StrBuf *sb);
void sb_reserve(StrBuf *sb, size_t extra);
void sb_addc(StrBuf *sb, char c);
void sb_adds(StrBuf *sb, const char *s);
void sb_addn(StrBuf *sb, const char *s, size_t n);
void sb_addf(StrBuf *sb, const char *fmt, ...);
void sb_json_str(StrBuf *sb, const char *s);
void sb_json_nstr(StrBuf *sb, const char *s, size_t n);

char *preview_collapse(const char *s, size_t len, int limit);
char *preview_clip(const char *s, size_t len, int limit);

double now_sec(void);
int utf8_strlen(const char *s);
int utf8_len_n(const char *s, size_t n);
int utf8_decode(const char *s, size_t len, uint32_t *cp);
int utf8_encode(uint32_t cp, char out[4]);
size_t utf8_prev(const char *s, size_t pos);
size_t utf8_next(const char *s, size_t pos, size_t len);
int cp_width(uint32_t cp);
int text_width(const char *s, size_t len);
size_t display_col(const char *line, size_t len, size_t byte_off, int tab_width);
size_t byte_at_display_col(const char *line, size_t len, size_t col, int tab_width);

const char *path_basename(const char *path);
void path_display(const char *path, char *out, size_t outlen);
bool file_exists(const char *path);
void path_dirname(const char *path, char *out, size_t outlen);
void path_expand(const char *in, char *out, size_t outlen);

/* ------------------------------------------------------------------------- */
/* Text buffer                                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    char *text;
    size_t len;
    size_t cap;
    size_t cursor;      /* byte offset */
    size_t anchor;      /* SIZE_MAX when there is no selection */
    size_t goal_col;
} Buffer;

void buf_init(Buffer *b);
void buf_free(Buffer *b);
void buf_set(Buffer *b, const char *text, size_t len, size_t cursor);
void buf_reserve(Buffer *b, size_t extra);
void buf_insert(Buffer *b, size_t pos, const char *text, size_t len);
void buf_delete(Buffer *b, size_t pos, size_t len);
char *buf_range_dup(const Buffer *b, size_t start, size_t end);
size_t buf_line_start(const Buffer *b, size_t pos);
size_t buf_line_end(const Buffer *b, size_t pos);
size_t buf_next_line(const Buffer *b, size_t pos);
size_t buf_prev_line(const Buffer *b, size_t pos);
void buf_line_col(const Buffer *b, size_t pos, int *line, int *col);
size_t buf_offset_at(const Buffer *b, int line, int col);
size_t buf_display_col(const Buffer *b, size_t pos);
bool buf_has_selection(const Buffer *b);
void buf_sel_range(const Buffer *b, size_t *start, size_t *end);
void buf_clear_selection(Buffer *b);
void buf_move_left(Buffer *b, bool select);
void buf_move_right(Buffer *b, bool select);
void buf_move_up(Buffer *b, bool select);
void buf_move_down(Buffer *b, bool select);
void buf_move_home(Buffer *b, bool select);
void buf_move_end(Buffer *b, bool select);
void buf_move_doc_home(Buffer *b, bool select);
void buf_move_doc_end(Buffer *b, bool select);
void buf_move_word_left(Buffer *b, bool select);
void buf_move_word_right(Buffer *b, bool select);
void buf_select_all(Buffer *b);
void buf_delete_word_before(Buffer *b);
void buf_delete_word_after(Buffer *b);
void buf_dedent_line(Buffer *b);

/* ------------------------------------------------------------------------- */
/* Undo / redo                                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    char *text;
    size_t cursor;
    char label[24];
    char kind[16];
    double timestamp;
    int edit_count;
    size_t chars;
    char *inserted;
    char *deleted;
    double last_timestamp;
} Snapshot;

typedef struct {
    Snapshot *items;
    int len;
    int cap;
    size_t capacity;
    size_t total_chars;
} SnapshotStack;

typedef struct {
    SnapshotStack undo;
    SnapshotStack redo;
    size_t capacity;
    double group_window;
    size_t max_total_chars;
    char group_key[32];
    double group_time;
    pthread_mutex_t lock;
} UndoRedo;

void undo_init(UndoRedo *u, size_t capacity, double group_window, size_t max_total_chars);
void undo_free(UndoRedo *u);
void undo_record(UndoRedo *u, const char *old_text, size_t old_cursor,
                 const char *new_text, size_t new_cursor);
void undo_record_op(UndoRedo *u, const char *old_text, size_t old_cursor,
                    size_t position, const char *deleted, size_t deleted_len,
                    const char *inserted, size_t inserted_len, size_t new_cursor);
char *undo_undo(UndoRedo *u, const char *current_text, size_t current_cursor,
                size_t *out_cursor);
char *undo_redo(UndoRedo *u, const char *current_text, size_t current_cursor,
                size_t *out_cursor);
void undo_break_group(UndoRedo *u);
void undo_reset(UndoRedo *u);
int undo_depth(UndoRedo *u);
int redo_depth(UndoRedo *u);
void undo_to_json(UndoRedo *u, StrBuf *sb);

/* ------------------------------------------------------------------------- */
/* Clipboard history                                                         */
/* ------------------------------------------------------------------------- */

typedef struct {
    int id;
    char *text;
    double timestamp;
} ClipEntry;

typedef struct {
    ClipEntry *items;
    int len;
    int cap;
    int capacity;
    int next_id;
    pthread_mutex_t lock;
} Clipboard;

void clip_init(Clipboard *c, int capacity);
void clip_free(Clipboard *c);
void clip_enqueue(Clipboard *c, const char *text);
const ClipEntry *clip_get(Clipboard *c, int index_from_newest);
int clip_size(Clipboard *c);
void clip_to_json(Clipboard *c, StrBuf *sb);

/* ------------------------------------------------------------------------- */
/* Trie                                                                      */
/* ------------------------------------------------------------------------- */

typedef struct TrieNode TrieNode;
struct TrieNode {
    char ch;
    TrieNode **children;
    int nchild;
    int cap;
    int terminal;
    int weight;
    char kind[16];
};

typedef struct {
    TrieNode *root;
    int node_count;
    int word_count;
    pthread_mutex_t lock;
} Trie;

typedef struct {
    char word[80];
    int weight;
    char kind[16];
} WordEntry;

void trie_init(Trie *t);
void trie_free(Trie *t);
bool trie_insert(Trie *t, const char *word, int weight, const char *kind,
                 bool only_if_new);
bool trie_bump(Trie *t, const char *word, int amount);
int trie_learn_text(Trie *t, const char *text, int weight, const char *kind);
int trie_complete(Trie *t, const char *prefix, int limit, WordEntry *out);
void trie_to_json(Trie *t, const char *prefix, int max_depth, int max_nodes,
                  int top_words, int suggestions, StrBuf *sb);
int trie_node_count(Trie *t);
int trie_word_count(Trie *t);
void keyword_seed_trie(Trie *t, const char *language);
bool keyword_is_keyword(const char *language, const char *word);
void keyword_languages_json(StrBuf *sb);
const char *language_label(const char *key);
const char *language_normalize(const char *language);

/* ------------------------------------------------------------------------- */
/* File I/O                                                                  */
/* ------------------------------------------------------------------------- */

typedef enum { NL_LF = 0, NL_CRLF = 1, NL_CR = 2 } Newline;

typedef struct {
    char *text;             /* normalized to LF, UTF-8 */
    size_t len;
    char encoding[16];
    unsigned char bom[4];
    int bom_len;
    Newline newline;
    char warning[256];
} LoadedFile;

int fileio_load(const char *path, LoadedFile *out, char *err, size_t errlen);
int fileio_save(const char *path, const char *text, size_t len,
                const char *encoding, const unsigned char *bom, int bom_len,
                Newline newline, char *err, size_t errlen);
const char *newline_name(Newline nl);
const char *language_from_path(const char *path);
const char *language_from_name(const char *name);

/* ------------------------------------------------------------------------- */
/* Syntax highlighting                                                       */
/* ------------------------------------------------------------------------- */

typedef enum {
    HL_NORMAL = 0,
    HL_KEYWORD,
    HL_STRING,
    HL_COMMENT,
    HL_NUMBER,
    HL_PREPROC,
    HL_FUNC
} HlType;

typedef struct {
    size_t start;
    size_t end;
    HlType type;
} HlToken;

int highlight_line(const char *language, const char *line, size_t len,
                   HlToken *tokens, int max_tokens);

/* ------------------------------------------------------------------------- */
/* Editor state                                                              */
/* ------------------------------------------------------------------------- */

typedef struct {
    int words;
    int chars;
    int chars_no_ws;
    int lines;
} TextStats;

typedef struct Bridge Bridge;

typedef struct Editor {
    Buffer buf;
    char path[PATH_MAX];
    bool has_path;
    char encoding[16];
    unsigned char bom[4];
    int bom_len;
    Newline newline;
    char language[32];
    char language_label[64];
    char theme[32];
    bool dirty;
    size_t size_bytes;

    UndoRedo undo;
    Trie trie;
    Clipboard clip;

    char *saved_text;

    bool autosave_enabled;
    double autosave_interval;
    double last_saved;
    bool ever_saved;
    size_t saved_chars;

    bool panel_visible;
    bool help_visible;
    int help_scroll;
    bool command_mode;
    char command[512];
    size_t command_len;
    char message[512];
    bool message_error;
    double message_until;

    int tab_width;
    int quit_pressed;
    double quit_pressed_at;
    bool running;

    bool server_enabled;
    int port;
    char state_file[PATH_MAX];
    bool state_file_enabled;
    Bridge *bridge;

    bool completion_active;
    int completion_count;
    int completion_selected;
    WordEntry suggestions[12];
    char prefix[80];
    char ghost[80];

    char last_find[256];

    int rows;
    int cols;
    int top_line;
    int left_col;
    TextStats stats;
    char session[32];
    char state_file_error[256];

    double last_publish;
    double last_state_file;
    double last_learn;
    long state_seq;
    double state_timestamp;
} Editor;

/* editor.c */
void editor_init(Editor *ed);
void editor_free(Editor *ed);
void editor_open(Editor *ed, const char *path, bool force);
void editor_new(Editor *ed, bool force);
int editor_save(Editor *ed, const char *path);
void editor_rebuild_trie(Editor *ed);
void editor_learn(Editor *ed);
void editor_update_completion(Editor *ed);
void editor_accept_completion(Editor *ed, bool ghost_only);
void editor_insert_text(Editor *ed, const char *text, size_t len);
void editor_delete_range(Editor *ed, size_t start, size_t end);
void editor_replace_selection(Editor *ed, const char *text, size_t len);
void editor_copy(Editor *ed, bool cut);
void editor_paste_latest(Editor *ed);
void editor_paste_history(Editor *ed, int index);
void editor_undo(Editor *ed);
void editor_redo(Editor *ed);
void editor_handle_key(Editor *ed, int key, uint32_t ch);
void editor_run_command(Editor *ed);
void editor_find_next(Editor *ed, const char *needle);
void editor_goto_line(Editor *ed, int line);
void editor_publish(Editor *ed, const char *status);
void editor_build_state(Editor *ed, const char *status, StrBuf *sb);
void editor_check_autosave(Editor *ed);
void editor_tick(Editor *ed);
char *editor_trie_json(Editor *ed, int depth, int limit);
void editor_set_message(Editor *ed, const char *text, bool error);
void editor_set_language(Editor *ed, const char *name);
const char *editor_display_path(Editor *ed);
void editor_set_state_file(Editor *ed, const char *path, bool enabled);

/* ui.c */
int ui_init(Editor *ed);
void ui_shutdown(Editor *ed);
void ui_draw(Editor *ed);
int ui_read_key(int timeout_ms, uint32_t *ch);
void ui_terminal_size(int *rows, int *cols);
const char *ui_theme_name(const char *name);
const char *ui_theme_names(void);
extern volatile sig_atomic_t ui_resized;

/* Key codes returned by ui_read_key.  Modifiers live in the high bits. */
#define KEY_MOD_SHIFT 0x100
#define KEY_MOD_CTRL  0x200
#define KEY_MOD_ALT   0x400

enum {
    KEY_NONE = 0,
    KEY_CHAR = 1,
    KEY_ENTER = 2,
    KEY_BACKSPACE = 3,
    KEY_DELETE = 4,
    KEY_TAB = 5,
    KEY_BACKTAB = 6,
    KEY_ESC = 7,
    KEY_UP = 8,
    KEY_DOWN = 9,
    KEY_LEFT = 10,
    KEY_RIGHT = 11,
    KEY_HOME = 12,
    KEY_END = 13,
    KEY_PGUP = 14,
    KEY_PGDN = 15,
    KEY_DOC_HOME = 16,
    KEY_DOC_END = 17,
    KEY_WORD_LEFT = 18,
    KEY_WORD_RIGHT = 19,
    KEY_DEL_WORD = 20,
    KEY_F1 = 21,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
    KEY_CTRL_A = 40,
    KEY_CTRL_B,
    KEY_CTRL_C,
    KEY_CTRL_D,
    KEY_CTRL_E,
    KEY_CTRL_F,
    KEY_CTRL_G,
    KEY_CTRL_H,
    KEY_CTRL_I,
    KEY_CTRL_J,
    KEY_CTRL_K,
    KEY_CTRL_L,
    KEY_CTRL_M,
    KEY_CTRL_N,
    KEY_CTRL_O,
    KEY_CTRL_P,
    KEY_CTRL_Q,
    KEY_CTRL_R,
    KEY_CTRL_S,
    KEY_CTRL_T,
    KEY_CTRL_U,
    KEY_CTRL_V,
    KEY_CTRL_W,
    KEY_CTRL_X,
    KEY_CTRL_Y,
    KEY_CTRL_Z,
    KEY_CTRL_AT = 69,
    KEY_ALT_1 = 70,
    KEY_ALT_2,
    KEY_ALT_3,
    KEY_ALT_4,
    KEY_ALT_5,
    KEY_ALT_6,
    KEY_ALT_7,
    KEY_ALT_8,
    KEY_ALT_9,
    KEY_ALT_BACKSPACE = 80,
    KEY_ALT_B,
    KEY_ALT_C,
    KEY_ALT_D,
    KEY_ALT_F,
    KEY_ALT_L,
    KEY_ALT_U
};

/* bridge.c */
Bridge *bridge_start(Editor *ed, int requested_port, bool enable_server,
                     const char *state_file, bool state_file_enabled);
void bridge_stop(Bridge *b);
int bridge_port(Bridge *b);
void bridge_publish(Bridge *b, const char *json, long seq);
long bridge_seq(Bridge *b);
bool bridge_closed(Bridge *b);
void bridge_write_state_file(const char *path, const char *json, char *err,
                             size_t errlen);

#endif /* ANVIL_H */
