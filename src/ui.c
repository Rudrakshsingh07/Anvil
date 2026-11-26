/* Hand-written ANSI/termios terminal UI. */
#include "anvil.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

volatile sig_atomic_t ui_resized = 0;

static struct termios orig_termios;
static bool raw_enabled = false;

static void on_winch(int sig) {
    (void)sig;
    ui_resized = 1;
}

void ui_terminal_size(int *rows, int *cols) {
    struct winsize ws;
    int r = 24, c = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        r = ws.ws_row;
        c = ws.ws_col;
    } else {
        const char *env = getenv("LINES");
        if (env && atoi(env) > 0) r = atoi(env);
        env = getenv("COLUMNS");
        if (env && atoi(env) > 0) c = atoi(env);
    }
    if (rows) *rows = r;
    if (cols) *cols = c;
}

static void write_all(const char *data, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(STDOUT_FILENO, data + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        done += (size_t)n;
    }
}

int ui_init(Editor *ed) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) return -1;
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(unsigned)(OPOST);
    raw.c_cflag |= (unsigned)CS8;
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
    raw_enabled = true;
    signal(SIGWINCH, on_winch);
    int rows, cols;
    ui_terminal_size(&rows, &cols);
    ed->rows = rows;
    ed->cols = cols;
    const char *enter = "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
    write_all(enter, strlen(enter));
    return 0;
}

void ui_shutdown(Editor *ed) {
    (void)ed;
    const char *leave = "\x1b[?25h\x1b[0m\x1b[?1049l";
    write_all(leave, strlen(leave));
    if (raw_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_enabled = false;
    }
}

static int read_byte(int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) return -1;
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n != 1) return -1;
    return c;
}

static int mods_from_number(int m) {
    switch (m) {
    case 2: return KEY_MOD_SHIFT;
    case 3: return KEY_MOD_ALT;
    case 4: return KEY_MOD_SHIFT | KEY_MOD_ALT;
    case 5: return KEY_MOD_CTRL;
    case 6: return KEY_MOD_SHIFT | KEY_MOD_CTRL;
    case 7: return KEY_MOD_ALT | KEY_MOD_CTRL;
    case 8: return KEY_MOD_SHIFT | KEY_MOD_ALT | KEY_MOD_CTRL;
    default: return 0;
    }
}

static int parse_csi(const char *seq, int n, uint32_t *ch) {
    (void)ch;
    if (n <= 0) return KEY_NONE;
    char final = seq[n - 1];
    int mods = 0;
    const char *semi = memchr(seq, ';', (size_t)n);
    if (semi) mods = mods_from_number(atoi(semi + 1));

    if (final == 'Z') return KEY_BACKTAB;
    if (final == 'A') return KEY_UP | mods;
    if (final == 'B') return KEY_DOWN | mods;
    if (final == 'C') return KEY_RIGHT | mods;
    if (final == 'D') return KEY_LEFT | mods;
    if (final == 'H') return KEY_HOME | mods;
    if (final == 'F') return KEY_END | mods;
    if (final == '~') {
        int num = atoi(seq);
        switch (num) {
        case 1: case 7: return KEY_HOME | mods;
        case 2: return KEY_NONE;
        case 3: return KEY_DELETE | mods;
        case 4: case 8: return KEY_END | mods;
        case 5: return KEY_PGUP | mods;
        case 6: return KEY_PGDN | mods;
        case 11: return KEY_F1;
        case 12: return KEY_F2;
        case 13: return KEY_F3;
        case 14: return KEY_F4;
        case 15: return KEY_F5;
        case 17: return KEY_F6;
        case 18: return KEY_F7;
        case 19: return KEY_F8;
        case 20: return KEY_F9;
        case 21: return KEY_F10;
        case 23: return KEY_F11;
        case 24: return KEY_F12;
        default: return KEY_NONE;
        }
    }
    return KEY_NONE;
}

static int parse_ss3(int b) {
    switch (b) {
    case 'P': return KEY_F1;
    case 'Q': return KEY_F2;
    case 'R': return KEY_F3;
    case 'S': return KEY_F4;
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    default: return KEY_NONE;
    }
}

int ui_read_key(int timeout_ms, uint32_t *ch) {
    if (ch) *ch = 0;
    int b = read_byte(timeout_ms);
    if (b < 0) return KEY_NONE;

    if (b == 0x1b) {
        int b2 = read_byte(35);
        if (b2 < 0) return KEY_ESC;
        if (b2 == '[') {
            char seq[32];
            int n = 0;
            while (n < (int)sizeof(seq) - 1) {
                int c = read_byte(35);
                if (c < 0) break;
                seq[n++] = (char)c;
                if (c >= 0x40 && c <= 0x7E) break;
            }
            seq[n] = '\0';
            return parse_csi(seq, n, ch);
        }
        if (b2 == 'O') {
            int c = read_byte(35);
            if (c < 0) return KEY_ESC;
            return parse_ss3(c);
        }
        if (b2 == 0x7f) return KEY_ALT_BACKSPACE;
        if (b2 >= '1' && b2 <= '9') return KEY_ALT_1 + (b2 - '1');
        if (b2 == 'b' || b2 == 'B') return KEY_ALT_B;
        if (b2 == 'f' || b2 == 'F') return KEY_ALT_F;
        if (b2 == 'd' || b2 == 'D') return KEY_ALT_D;
        if (b2 == 'c' || b2 == 'C') return KEY_ALT_C;
        if (b2 == 'l' || b2 == 'L') return KEY_ALT_L;
        if (b2 == 'u' || b2 == 'U') return KEY_ALT_U;
        return KEY_ESC;
    }

    if (b == 0x00) return KEY_CTRL_AT;
    if (b == 0x08) return KEY_BACKSPACE;
    if (b == 0x09) return KEY_TAB;
    if (b == 0x0a || b == 0x0d) return KEY_ENTER;
    if (b == 0x7f) return KEY_BACKSPACE;
    if (b == 0x1f) return KEY_CTRL_Z;
    if (b >= 1 && b <= 26) return KEY_CTRL_A + (b - 1);

    if (b >= 0x20) {
        unsigned char buf[8];
        buf[0] = (unsigned char)b;
        int need = 0;
        if ((b & 0xE0) == 0xC0) need = 1;
        else if ((b & 0xF0) == 0xE0) need = 2;
        else if ((b & 0xF8) == 0xF0) need = 3;
        int got = 1;
        for (int i = 0; i < need; i++) {
            int c = read_byte(35);
            if (c < 0) break;
            buf[got++] = (unsigned char)c;
        }
        uint32_t cp = 0;
        if (utf8_decode((const char *)buf, (size_t)got, &cp) <= 0) cp = (uint32_t)b;
        if (ch) *ch = cp;
        return KEY_CHAR;
    }
    return KEY_NONE;
}

/* --------------------------------------------------------------- themes */

typedef struct {
    const char *name;
    int bg;            /* editor background */
    int fg;            /* normal text */
    int dim;           /* secondary text */
    int separator;     /* panel rules and gutter on empty lines */
    int accent;        /* headings, bridge status */
    int panel_text;    /* panel body text */
    int suggestion;    /* trie suggestions */
    int gutter;        /* line numbers */
    int gutter_cursor; /* current line number */
    int status_fg;
    int status_bg;
    int hint_fg;
    int hint_bg;
    int hint_error;
    int hint_ok;
    int command_fg;
    int command_bg;
    int keyword;
    int string;
    int comment;
    int number;
    int preproc;
    int func;
    int ghost;         /* inline completion ghost text */
} Theme;

static const Theme THEMES[] = {
    {
        .name = "monokai",
        .bg = 234, .fg = 252, .dim = 242, .separator = 238, .accent = 81,
        .panel_text = 250, .suggestion = 121, .gutter = 242,
        .gutter_cursor = 214, .status_fg = 252, .status_bg = 235,
        .hint_fg = 244, .hint_bg = 233, .hint_error = 203, .hint_ok = 111,
        .command_fg = 111, .command_bg = 235,
        .keyword = 197, .string = 186, .comment = 242, .number = 141,
        .preproc = 81, .func = 148, .ghost = 242,
    },
    {
        .name = "dracula",
        .bg = 236, .fg = 253, .dim = 245, .separator = 240, .accent = 141,
        .panel_text = 253, .suggestion = 159, .gutter = 245,
        .gutter_cursor = 215, .status_fg = 253, .status_bg = 238,
        .hint_fg = 245, .hint_bg = 236, .hint_error = 210, .hint_ok = 84,
        .command_fg = 141, .command_bg = 238,
        .keyword = 212, .string = 228, .comment = 243, .number = 141,
        .preproc = 117, .func = 84, .ghost = 245,
    },
    {
        .name = "solarized",
        .bg = 235, .fg = 250, .dim = 245, .separator = 240, .accent = 37,
        .panel_text = 250, .suggestion = 37, .gutter = 245,
        .gutter_cursor = 136, .status_fg = 250, .status_bg = 238,
        .hint_fg = 245, .hint_bg = 235, .hint_error = 160, .hint_ok = 37,
        .command_fg = 37, .command_bg = 238,
        .keyword = 37, .string = 136, .comment = 240, .number = 141,
        .preproc = 125, .func = 64, .ghost = 245,
    },
    {
        .name = "light",
        .bg = 255, .fg = 236, .dim = 245, .separator = 250, .accent = 25,
        .panel_text = 236, .suggestion = 25, .gutter = 245,
        .gutter_cursor = 166, .status_fg = 255, .status_bg = 25,
        .hint_fg = 236, .hint_bg = 254, .hint_error = 160, .hint_ok = 28,
        .command_fg = 25, .command_bg = 254,
        .keyword = 127, .string = 28, .comment = 244, .number = 94,
        .preproc = 25, .func = 130, .ghost = 245,
    },
};

#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

static const Theme *theme_lookup(const char *name) {
    if (name && *name) {
        for (int i = 0; i < THEME_COUNT; i++) {
            if (strcasecmp(name, THEMES[i].name) == 0) return &THEMES[i];
        }
    }
    return &THEMES[0];
}

const char *ui_theme_name(const char *name) {
    if (!name || !*name) return NULL;
    for (int i = 0; i < THEME_COUNT; i++) {
        if (strcasecmp(name, THEMES[i].name) == 0) return THEMES[i].name;
    }
    return NULL;
}

const char *ui_theme_names(void) {
    return "monokai, dracula, solarized, light";
}

/* --------------------------------------------------------------- rendering */

static void ansi_move(StrBuf *out, int row, int col) {
    if (row < 1) row = 1;
    if (col < 1) col = 1;
    sb_addf(out, "\x1b[%d;%dH", row, col);
}

static void ansi_fg(StrBuf *out, int fg) {
    sb_addf(out, "\x1b[38;5;%dm", fg);
}

static void ansi_bg(StrBuf *out, int bg) {
    sb_addf(out, "\x1b[48;5;%dm", bg);
}

static void ansi_reset(StrBuf *out) {
    sb_adds(out, "\x1b[0m");
}

static void add_spaces(StrBuf *out, int n) {
    for (int i = 0; i < n; i++) sb_addc(out, ' ');
}

static int append_visible(StrBuf *out, const char *s, int max_width) {
    if (!s || max_width <= 0) return 0;
    size_t len = strlen(s);
    size_t i = 0;
    int used = 0;
    while (i < len) {
        uint32_t cp;
        int n = utf8_decode(s + i, len - i, &cp);
        if (n <= 0) n = 1;
        int w = cp_width(cp);
        if (w <= 0) w = 1;
        if (used + w > max_width) break;
        sb_addn(out, s + i, (size_t)n);
        used += w;
        i += (size_t)n;
    }
    return used;
}

static void draw_bar(StrBuf *out, int row, int cols, const char *text,
                     int fg, int bg) {
    ansi_move(out, row + 1, 1);
    ansi_fg(out, fg);
    ansi_bg(out, bg);
    int used = append_visible(out, text, cols);
    if (used < cols) add_spaces(out, cols - used);
    ansi_reset(out);
}

static const char *HELP_TEXT =
    "ANVIL - help\n"
    "============\n"
    "\n"
    "Saving and files\n"
    "  Ctrl-S / F2      save\n"
    "  F3               save as (command bar: w path)\n"
    "  F4 / Ctrl-O      open file (command bar: e path)\n"
    "  Ctrl-N           new file\n"
    "  Ctrl-Q           quit (press twice if unsaved)\n"
    "  F5               toggle autosave (default 30s)\n"
    "  Ctrl-A           select all\n"
    "  Shift+arrows     select text\n"
    "  Ctrl-C / Ctrl-X  copy / cut\n"
    "  Ctrl-V           paste newest clipboard entry\n"
    "  Alt-1 .. Alt-9   paste clipboard history entry\n"
    "\n"
    "History\n"
    "  Ctrl-Z / Ctrl-_  undo\n"
    "  Ctrl-Y           redo\n"
    "  Two hand-written bounded stacks. Consecutive typing is coalesced\n"
    "  into one undo unit; moving the cursor starts a new unit.\n"
    "\n"
    "Movement and editing\n"
    "  Arrows           move\n"
    "  Home / End       line start / end\n"
    "  Ctrl-Left/Right  word left / right\n"
    "  Ctrl-Home/End    buffer start / end\n"
    "  PageUp/PageDown  scroll a page\n"
    "  Ctrl-@ / Esc     start selection / cancel selection\n"
    "  Backspace/Delete delete\n"
    "  Ctrl-D           delete character\n"
    "  Ctrl-K           kill to line end\n"
    "  Ctrl-U           kill to line start\n"
    "  Ctrl-W           delete word before\n"
    "  Alt-D            delete word after\n"
    "  Alt-Backspace    delete word before\n"
    "  Ctrl-T           transpose characters\n"
    "  Ctrl-E           end of line\n"
    "  Ctrl-L           clear screen\n"
    "  Alt-B / Alt-F    word back / forward\n"
    "\n"
    "Completion and highlighting\n"
    "  Just type: the trie popup appears after 2 characters.\n"
    "  Tab              accept the selected completion\n"
    "  Ctrl-Right       accept the grey inline suggestion\n"
    "  Shift-Tab        dedent the current line\n"
    "  Enter            smart newline (keeps indentation)\n"
    "  Syntax highlighting follows the file extension; use :lang.\n"
    "\n"
    "Command bar\n"
    "  F9 / Ctrl-P      open the command bar\n"
    "  :w [path]        save (optionally as another path)\n"
    "  :wq              save and quit\n"
    "  :e path          open a file; use :e! to discard changes\n"
    "  :new             new file; use :new! to discard changes\n"
    "  :q  / :q!        quit / quit without saving\n"
    "  :autosave on|off|<seconds>\n"
    "  :lang python     change the syntax language (try :lang auto)\n"
    "  :find text       find next occurrence (wraps)\n"
    "  :goto 42         go to line 42\n"
    "  :theme monokai   change the colour theme\n"
    "  :tab 4           set the soft-tab width\n"
    "  :undo / :redo    history\n"
    "  :panel           toggle the data-structure panel\n"
    "  :state           show the live bridge URL / state file\n"
    "  :help            show or hide this help\n"
    "  :clip            show clipboard history count\n"
    "  Esc              close help / command bar\n"
    "\n"
    "Data structures panel (F6)\n"
    "  Live undo/redo depths, trie node/word counts, clipboard queue,\n"
    "  suggestions, word count, cursor position, autosave countdown and\n"
    "  bridge port. The same data is exposed to another process over HTTP\n"
    "  and a JSON file. See docs/state_api.md.\n"
    "\n"
    "PgUp/PgDn scroll this help. F1 or Esc closes it.\n";

static int help_line_count(void) {
    int count = 1;
    for (const char *p = HELP_TEXT; *p; p++) if (*p == '\n') count++;
    return count;
}

static bool help_nth_line(int index, const char **out, size_t *len) {
    const char *p = HELP_TEXT;
    int idx = 0;
    while (idx < index && *p) {
        const char *nl = strchr(p, '\n');
        if (!nl) return false;
        p = nl + 1;
        idx++;
    }
    if (idx != index) return false;
    const char *nl = strchr(p, '\n');
    *out = p;
    *len = nl ? (size_t)(nl - p) : strlen(p);
    return true;
}

typedef struct {
    char text[512];
    int color;
} PanelLine;

static void panel_add(PanelLine *lines, int max, int *count, int color,
                      const char *fmt, ...) {
    if (*count >= max) return;
    PanelLine *line = &lines[*count];
    line->color = color;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line->text, sizeof(line->text), fmt, ap);
    va_end(ap);
    (*count)++;
}

typedef struct {
    char kind[16];
    int edit_count;
    size_t chars;
    char detail[160];
} TopInfo;

static bool undo_top(Editor *ed, TopInfo *info) {
    memset(info, 0, sizeof(*info));
    pthread_mutex_lock(&ed->undo.lock);
    bool ok = ed->undo.undo.len > 0;
    if (ok) {
        Snapshot *s = &ed->undo.undo.items[ed->undo.undo.len - 1];
        snprintf(info->kind, sizeof(info->kind), "%s", s->kind);
        info->edit_count = s->edit_count;
        info->chars = s->chars;
        const char *source = NULL;
        if (s->inserted && s->inserted[0]) source = s->inserted;
        else if (s->deleted && s->deleted[0]) source = s->deleted;
        else source = s->text;
        char *preview = preview_collapse(source ? source : "",
                                         source ? strlen(source) : 0, 30);
        snprintf(info->detail, sizeof(info->detail), "%s", preview);
        free(preview);
    }
    pthread_mutex_unlock(&ed->undo.lock);
    return ok;
}

static int build_panel(Editor *ed, const Theme *th, PanelLine *lines, int max) {
    int n = 0;
    panel_add(lines, max, &n, th->accent, " DATA STRUCTURES");
    panel_add(lines, max, &n, th->panel_text, " undo stack   %4d / %zu",
              undo_depth(&ed->undo), ed->undo.capacity);
    panel_add(lines, max, &n, th->panel_text, " redo stack   %4d / %zu",
              redo_depth(&ed->undo), ed->undo.capacity);
    panel_add(lines, max, &n, th->panel_text, " trie nodes   %5d   words %d",
              trie_node_count(&ed->trie), trie_word_count(&ed->trie));
    panel_add(lines, max, &n, th->panel_text, " clipboard    %4d / %d",
              clip_size(&ed->clip), ed->clip.capacity);
    panel_add(lines, max, &n, th->separator, " ----------------------------------------");

    panel_add(lines, max, &n, th->accent, " DOCUMENT");
    char disp[PATH_MAX];
    path_display(ed->has_path ? ed->path : "", disp, sizeof(disp));
    if (strlen(disp) > 38) {
        char tmp[PATH_MAX];
        snprintf(tmp, sizeof(tmp), "...%s", disp + strlen(disp) - 35);
        snprintf(disp, sizeof(disp), "%s", tmp);
    }
    panel_add(lines, max, &n, th->panel_text, " %s%s", disp, ed->dirty ? "  modified" : "  saved");
    panel_add(lines, max, &n, th->dim, " %s . %s . %s", ed->language, ed->encoding,
              newline_name(ed->newline));
    panel_add(lines, max, &n, th->panel_text, " %d words . %d chars . %d lines",
              ed->stats.words, ed->stats.chars, ed->stats.lines);
    int line = 1, col = 1;
    buf_line_col(&ed->buf, ed->buf.cursor, &line, &col);
    panel_add(lines, max, &n, th->dim, " Ln %d, Col %d", line, col);
    if (ed->autosave_enabled) {
        char saved[64];
        if (!ed->ever_saved) snprintf(saved, sizeof(saved), "not saved yet");
        else {
            long secs = (long)(now_sec() - ed->last_saved);
            if (secs < 0) secs = 0;
            snprintf(saved, sizeof(saved), "%lds ago", secs);
        }
        panel_add(lines, max, &n, th->dim, " autosave %gs . %s", ed->autosave_interval, saved);
    } else {
        panel_add(lines, max, &n, th->dim, " autosave off");
    }
    if (ed->port > 0) panel_add(lines, max, &n, th->accent, " bridge http://127.0.0.1:%d", ed->port);
    else panel_add(lines, max, &n, th->dim, " bridge off");

    panel_add(lines, max, &n, th->separator, " ----------------------------------------");
    panel_add(lines, max, &n, th->accent, " UNDO TOP");
    TopInfo top;
    if (undo_top(ed, &top)) {
        panel_add(lines, max, &n, th->panel_text, " %s x %d . %zu chars", top.kind,
                  top.edit_count, top.chars);
        panel_add(lines, max, &n, th->dim, " %s", top.detail);
    } else {
        panel_add(lines, max, &n, th->dim, " (empty)");
    }

    panel_add(lines, max, &n, th->separator, " ----------------------------------------");
    panel_add(lines, max, &n, th->accent, " CLIPBOARD  Alt-1..9");
    int clips = clip_size(&ed->clip);
    if (clips == 0) {
        panel_add(lines, max, &n, th->dim, " (empty)");
    } else {
        for (int i = 0; i < clips && i < 6; i++) {
            const ClipEntry *entry = clip_get(&ed->clip, i);
            if (!entry) break;
            char *preview = preview_collapse(entry->text, strlen(entry->text), 30);
            panel_add(lines, max, &n, th->panel_text, " %d. %s", i + 1, preview);
            free(preview);
        }
    }

    panel_add(lines, max, &n, th->separator, " ----------------------------------------");
    panel_add(lines, max, &n, th->accent, " TRIE  prefix '%s'", ed->prefix);
    if (ed->prefix[0] && ed->completion_count > 0) {
        for (int i = 0; i < ed->completion_count && i < 6; i++) {
            panel_add(lines, max, &n, th->suggestion, " %-26s %-7s %4d",
                      ed->suggestions[i].word, ed->suggestions[i].kind,
                      ed->suggestions[i].weight);
        }
    } else {
        panel_add(lines, max, &n, th->dim, " type 2+ letters to see suggestions");
    }
    return n;
}

static void draw_help_row(StrBuf *out, int row, int cols, int index,
                          const Theme *th) {
    ansi_move(out, row + 1, 1);
    ansi_fg(out, th->fg);
    ansi_bg(out, th->bg);
    const char *line = NULL;
    size_t len = 0;
    if (help_nth_line(index, &line, &len)) {
        char tmp[1024];
        if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
        memcpy(tmp, line, len);
        tmp[len] = '\0';
        int used = append_visible(out, tmp, cols);
        if (used < cols) add_spaces(out, cols - used);
    } else {
        add_spaces(out, cols);
    }
    ansi_reset(out);
}

static void draw_editor_row(StrBuf *out, Editor *ed, int row, int line_idx,
                            size_t line_start, int total_lines, int editor_width,
                            int gutter, int text_cols, int left_col,
                            size_t sel_start, size_t sel_end, bool has_sel,
                            size_t cursor_line_start, bool is_cursor_line,
                            PanelLine *panel_lines, int panel_count,
                            int panel_width, const Theme *th) {
    ansi_move(out, row + 1, 1);
    ansi_bg(out, th->bg);
    int used = 0;
    if (line_idx < total_lines) {
        char num[32];
        snprintf(num, sizeof(num), "%d", line_idx + 1);
        int numw = (int)strlen(num);
        int pad = gutter - 1 - numw;
        if (pad < 0) pad = 0;
        ansi_fg(out, is_cursor_line ? th->gutter_cursor : th->gutter);
        add_spaces(out, pad);
        sb_adds(out, num);
        sb_addc(out, ' ');
        used = pad + numw + 1;
    } else {
        ansi_fg(out, th->separator);
        add_spaces(out, gutter);
        used = gutter;
    }

    if (line_idx >= total_lines) {
        ansi_bg(out, th->bg);
        add_spaces(out, editor_width - used);
        if (panel_width > 0) {
            ansi_fg(out, th->separator);
            sb_addc(out, '|');
            ansi_fg(out, th->dim);
            const char *ptext = row < panel_count ? panel_lines[row].text : "";
            int pused = append_visible(out, ptext, panel_width);
            if (pused < panel_width) add_spaces(out, panel_width - pused);
        }
        ansi_reset(out);
        sb_adds(out, "\x1b[K");
        return;
    }

    size_t line_end = buf_line_end(&ed->buf, line_start);
    size_t line_len = line_end - line_start;
    const char *line_ptr = ed->buf.text + line_start;
    HlToken tokens[256];
    int ntok = highlight_line(ed->language, line_ptr, line_len, tokens, 256);

    ansi_fg(out, th->fg);
    int disp = 0;
    size_t i = byte_at_display_col(line_ptr, line_len, (size_t)left_col, ed->tab_width);
    disp = (int)display_col(line_ptr, line_len, i, ed->tab_width);
    if (disp < left_col) {
        add_spaces(out, left_col - disp);
        used += left_col - disp;
        disp = left_col;
    }

    int cur_type = -1;
    bool reversed = false;
    size_t cursor_off = is_cursor_line ? ed->buf.cursor - cursor_line_start : SIZE_MAX;
    int limit = left_col + text_cols;

    while (i < line_len) {
        uint32_t cp;
        int n = utf8_decode(line_ptr + i, line_len - i, &cp);
        if (n <= 0) n = 1;
        if (cp == '\t') {
            int advance = ed->tab_width - (disp % ed->tab_width);
            if (disp + advance > limit) break;
            add_spaces(out, advance);
            used += advance;
            disp += advance;
            i++;
            continue;
        }
        int w = cp_width(cp);
        if (w < 0) w = 0;
        if (disp + w > limit) break;

        if (is_cursor_line && i == cursor_off && ed->completion_active && ed->ghost[0]) {
            ansi_fg(out, th->ghost);
            int gw = append_visible(out, ed->ghost, limit - disp);
            used += gw;
            disp += gw;
            cur_type = -1;
        }

        HlType type = HL_NORMAL;
        int ti = 0;
        while (ti < ntok && tokens[ti].end <= i) ti++;
        if (ti < ntok && i >= tokens[ti].start && i < tokens[ti].end) type = tokens[ti].type;

        bool selected = has_sel && (line_start + i) >= sel_start &&
                        (line_start + i) < sel_end;
        if (selected != reversed) {
            sb_adds(out, selected ? "\x1b[7m" : "\x1b[27m");
            reversed = selected;
        }
        if ((int)type != cur_type) {
            int color = th->fg;
            switch (type) {
            case HL_KEYWORD: color = th->keyword; break;
            case HL_STRING: color = th->string; break;
            case HL_COMMENT: color = th->comment; break;
            case HL_NUMBER: color = th->number; break;
            case HL_PREPROC: color = th->preproc; break;
            case HL_FUNC: color = th->func; break;
            default: color = th->fg; break;
            }
            ansi_fg(out, color);
            cur_type = (int)type;
        }
        sb_addn(out, line_ptr + i, (size_t)n);
        used += w;
        disp += w;
        i += (size_t)n;
    }

    if (is_cursor_line && cursor_off >= line_len && ed->completion_active && ed->ghost[0]) {
        ansi_fg(out, th->ghost);
        int gw = append_visible(out, ed->ghost, limit - disp);
        used += gw;
        disp += gw;
    }
    if (reversed) sb_adds(out, "\x1b[27m");
    ansi_bg(out, th->bg);
    if (used < editor_width) add_spaces(out, editor_width - used);

    if (panel_width > 0) {
        ansi_fg(out, th->separator);
        sb_addc(out, '|');
        ansi_fg(out, row < panel_count ? panel_lines[row].color : th->dim);
        const char *ptext = row < panel_count ? panel_lines[row].text : "";
        int pused = append_visible(out, ptext, panel_width);
        if (pused < panel_width) add_spaces(out, panel_width - pused);
    }
    ansi_reset(out);
    sb_adds(out, "\x1b[K");
}

void ui_draw(Editor *ed) {
    int rows, cols;
    ui_terminal_size(&rows, &cols);
    if (rows < 4) rows = 4;
    if (cols < 20) cols = 20;
    ed->rows = rows;
    ed->cols = cols;

    const Theme *th = theme_lookup(ed->theme);

    bool cmd = ed->command_mode;
    int body_rows = rows - 2 - (cmd ? 1 : 0);
    if (body_rows < 1) body_rows = 1;
    int command_row = body_rows;
    int status_row = body_rows + (cmd ? 1 : 0);
    int hint_row = rows - 1;

    int panel_width = 0;
    if (ed->panel_visible && !ed->help_visible && cols >= 80) panel_width = 42;
    int sep = panel_width ? 1 : 0;
    int editor_width = cols - panel_width - sep;
    if (editor_width < 20) {
        panel_width = 0;
        sep = 0;
        editor_width = cols;
    }

    int total_lines = ed->stats.lines;
    if (total_lines < 1) total_lines = 1;
    int digits = 1;
    int tmp = total_lines;
    while (tmp >= 10) { digits++; tmp /= 10; }
    int gutter = digits + 1;
    if (gutter < 3) gutter = 3;
    if (gutter > editor_width - 2) gutter = editor_width - 2;
    if (gutter < 1) gutter = 1;
    int text_cols = editor_width - gutter;
    if (text_cols < 1) text_cols = 1;

    int cursor_line = 1, cursor_col = 1;
    buf_line_col(&ed->buf, ed->buf.cursor, &cursor_line, &cursor_col);
    int cursor_line0 = cursor_line - 1;

    if (!ed->help_visible) {
        if (cursor_line0 < ed->top_line) ed->top_line = cursor_line0;
        if (cursor_line0 >= ed->top_line + body_rows) ed->top_line = cursor_line0 - body_rows + 1;
        if (ed->top_line < 0) ed->top_line = 0;
        int max_top = total_lines - body_rows;
        if (max_top < 0) max_top = 0;
        if (ed->top_line > max_top) ed->top_line = max_top;

        size_t cursor_line_start = buf_line_start(&ed->buf, ed->buf.cursor);
        size_t cursor_disp = display_col(ed->buf.text + cursor_line_start,
                                         ed->buf.cursor - cursor_line_start,
                                         ed->buf.cursor - cursor_line_start,
                                         ed->tab_width);
        if ((int)cursor_disp < ed->left_col) ed->left_col = (int)cursor_disp;
        if ((int)cursor_disp >= ed->left_col + text_cols) {
            ed->left_col = (int)cursor_disp - text_cols + 1;
        }
        if (ed->left_col < 0) ed->left_col = 0;
    }

    PanelLine panel_lines[96];
    int panel_count = 0;
    if (panel_width > 0) panel_count = build_panel(ed, th, panel_lines, 96);

    StrBuf out;
    sb_init(&out);
    sb_adds(&out, "\x1b[?25l\x1b[H");

    if (ed->help_visible) {
        int count = help_line_count();
        int max_scroll = count - body_rows;
        if (max_scroll < 0) max_scroll = 0;
        if (ed->help_scroll > max_scroll) ed->help_scroll = max_scroll;
        if (ed->help_scroll < 0) ed->help_scroll = 0;
        for (int r = 0; r < body_rows; r++) {
            draw_help_row(&out, r, cols, ed->help_scroll + r, th);
        }
    } else {
        size_t line_start = buf_offset_at(&ed->buf, ed->top_line + 1, 1);
        size_t sel_start = 0, sel_end = 0;
        bool has_sel = buf_has_selection(&ed->buf);
        if (has_sel) buf_sel_range(&ed->buf, &sel_start, &sel_end);
        size_t cursor_line_start = buf_line_start(&ed->buf, ed->buf.cursor);
        for (int r = 0; r < body_rows; r++) {
            int line_idx = ed->top_line + r;
            bool is_cursor = line_idx == cursor_line0;
            draw_editor_row(&out, ed, r, line_idx, line_start, total_lines,
                            editor_width, gutter, text_cols, ed->left_col,
                            sel_start, sel_end, has_sel, cursor_line_start,
                            is_cursor, panel_lines, panel_count, panel_width,
                            th);
            if (line_idx < total_lines) {
                size_t end = buf_line_end(&ed->buf, line_start);
                line_start = end < ed->buf.len ? end + 1 : ed->buf.len;
            }
        }
    }

    char status[2048];
    char disp[PATH_MAX];
    path_display(ed->has_path ? ed->path : "", disp, sizeof(disp));
    if (strlen(disp) > 46) {
        char tmp2[PATH_MAX];
        snprintf(tmp2, sizeof(tmp2), "...%s", disp + strlen(disp) - 43);
        snprintf(disp, sizeof(disp), "%s", tmp2);
    }
    int sel_chars = 0;
    if (buf_has_selection(&ed->buf)) {
        size_t s, e;
        buf_sel_range(&ed->buf, &s, &e);
        sel_chars = utf8_len_n(ed->buf.text + s, e - s);
    }
    int status_len = snprintf(status, sizeof(status),
                              " %s  %s | %s | Ln %d, Col %d | %d words | auto %s",
                              disp, ed->dirty ? "●" : "✓", ed->language,
                              cursor_line, cursor_col, ed->stats.words,
                              ed->autosave_enabled ? "on" : "off");
    if (sel_chars > 0 && status_len > 0 && (size_t)status_len < sizeof(status)) {
        status_len += snprintf(status + status_len, sizeof(status) - (size_t)status_len,
                               " | sel %d", sel_chars);
    }
    if (ed->port > 0 && status_len > 0 && (size_t)status_len < sizeof(status)) {
        snprintf(status + status_len, sizeof(status) - (size_t)status_len,
                 " | bridge :%d", ed->port);
    }
    draw_bar(&out, status_row, cols, status, th->status_fg, th->status_bg);

    char hint[2048];
    double now = now_sec();
    if (ed->message[0] && now < ed->message_until) {
        snprintf(hint, sizeof(hint), " %s", ed->message);
        draw_bar(&out, hint_row, cols, hint,
                 ed->message_error ? th->hint_error : th->hint_ok, th->hint_bg);
    } else if (ed->help_visible) {
        draw_bar(&out, hint_row, cols,
                 " F1 or Esc to close help · PgUp/PgDn scroll · q also closes",
                 th->hint_fg, th->hint_bg);
    } else if (ed->command_mode) {
        draw_bar(&out, hint_row, cols,
                 " Enter run · Esc cancel · :w save · :q quit · :help help",
                 th->hint_fg, th->hint_bg);
    } else {
        draw_bar(&out, hint_row, cols,
                 " Ctrl-S save · F3 save as · F4 open · Ctrl-Z/Y undo/redo · "
                 "F5 autosave · F6 panel · F1 help",
                 th->hint_fg, th->hint_bg);
    }

    int cursor_row = -1;
    int cursor_screen_col = -1;
    if (cmd) {
        ansi_move(&out, command_row + 1, 1);
        ansi_fg(&out, th->command_fg);
        ansi_bg(&out, th->command_bg);
        int used = 0;
        used += append_visible(&out, " : ", cols);
        ansi_fg(&out, th->fg);
        used += append_visible(&out, ed->command, cols - used);
        if (used < cols) add_spaces(&out, cols - used);
        ansi_reset(&out);
        int cmdw = text_width(ed->command, ed->command_len);
        cursor_row = command_row + 1;
        cursor_screen_col = 4 + cmdw;
        if (cursor_screen_col > cols) cursor_screen_col = cols;
    } else if (!ed->help_visible) {
        int rel = cursor_line0 - ed->top_line;
        if (rel >= 0 && rel < body_rows) {
            size_t cls = buf_line_start(&ed->buf, ed->buf.cursor);
            size_t cdisp = display_col(ed->buf.text + cls, ed->buf.cursor - cls,
                                       ed->buf.cursor - cls, ed->tab_width);
            cursor_row = rel + 1;
            cursor_screen_col = gutter + (int)cdisp - ed->left_col + 1;
            if (cursor_screen_col < gutter + 1) cursor_screen_col = gutter + 1;
            if (cursor_screen_col > editor_width) cursor_screen_col = editor_width;
        }
    }

    if (cursor_row > 0 && cursor_screen_col > 0) {
        ansi_move(&out, cursor_row, cursor_screen_col);
        sb_adds(&out, "\x1b[?25h");
    } else {
        sb_adds(&out, "\x1b[?25l");
    }
    ansi_reset(&out);

    write_all(out.data ? out.data : "", out.len);
    sb_free(&out);
}
