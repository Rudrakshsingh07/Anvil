# DSA Editor Manual

**Version 0.1.0**

DSA Editor is a notepad-like text editor for the terminal with hand-written
data structures.  The installed command is **`anvil`**; the Python package is
`dsa_editor`.  This manual uses both names interchangeably.

The editor is built for a Data Structures & Algorithms lecture: the undo/redo
stacks, completion trie, clipboard-history queue and live statistics are
visible in the UI and exposed to a separate Python process for visualization.

## Contents

1. [What this editor is](#1-what-this-editor-is)
2. [Installation](#2-installation)
3. [Starting the editor](#3-starting-the-editor)
4. [The screen](#4-the-screen)
5. [Editing basics](#5-editing-basics)
6. [Files and saving](#6-files-and-saving)
7. [Undo and redo](#7-undo-and-redo)
8. [Completion and syntax highlighting](#8-completion-and-syntax-highlighting)
9. [Clipboard history](#9-clipboard-history)
10. [Find, go to, and word count](#10-find-go-to-and-word-count)
11. [Autosave](#11-autosave)
12. [Command bar reference](#12-command-bar-reference)
13. [Keyboard reference](#13-keyboard-reference)
14. [Live state bridge](#14-live-state-bridge)
15. [Command-line options](#15-command-line-options)
16. [Configuration and defaults](#16-configuration-and-defaults)
17. [Troubleshooting](#17-troubleshooting)
18. [Architecture and extending](#18-architecture-and-extending)
19. [Lecture demo walkthrough](#19-lecture-demo-walkthrough)
20. [Limitations](#20-limitations)
- [Appendix A: default limits](#appendix-a-default-limits)
- [Appendix B: bundled languages](#appendix-b-bundled-languages)
- [Appendix C: glossary](#appendix-c-glossary)
- [Appendix D: related documents](#appendix-d-related-documents)

---

## 1. What this editor is

DSA Editor is a full-screen terminal application.  It is not a GUI and not an
IDE.  It is intended for ordinary text files: source code, notes, Markdown,
JSON, CSV, SQL and anything else that is valid text.

The project deliberately implements its own data structures instead of calling
a library for them:

| Structure | Purpose | Source |
|---|---|---|
| Two bounded stacks | Undo and redo history | `dsa_editor/structures/stack.py` |
| Character trie | Prefix completion and inline suggestions | `dsa_editor/structures/trie.py` |
| FIFO queue | Clipboard history | `dsa_editor/structures/queue.py` |
| Linear statistics | Live word/character/line counts | `dsa_editor/structures/stats.py` |

The TUI library (`prompt_toolkit`) only draws the screen and handles raw
keystrokes.  Pygments only tokenizes text for colouring.

## 2. Installation

### 2.1 Requirements

- Python **3.10 or newer**
- A modern terminal with ANSI colours
- Linux, macOS, or WSL on Windows
- `prompt_toolkit` and `Pygments` (installed automatically)

### 2.2 Quick install

From the project directory:

```bash
cd dsa-editor
./install.sh
```

The installer:

1. creates `.venv` if it does not exist;
2. installs the package in editable mode;
3. links the `anvil` command into `~/.local/bin` (or `$XDG_BIN_HOME`);
4. prints a note if that directory is not on `PATH`.

Then run:

```bash
anvil
anvil demo.py
anvil --help
```

### 2.3 Manual install

If you prefer to do the steps yourself:

```bash
cd dsa-editor
python3 -m venv .venv
.venv/bin/pip install -e .
mkdir -p ~/.local/bin
ln -sf "$PWD/.venv/bin/anvil" ~/.local/bin/anvil
```

Make sure `~/.local/bin` is on `PATH`.  For example, in `~/.zshrc` or
`~/.bashrc`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

The venv also contains a `dsa-editor` alias:

```bash
.venv/bin/dsa-editor demo.py
```

### 2.4 Run without installing

`run.sh` creates the venv on first use and runs the package directly:

```bash
./run.sh
./run.sh notes.md
./run.sh --help
```

This does not create the global `anvil` link.

### 2.5 Verify the installation

```bash
anvil --version
```

The output depends on the invoked name, for example `anvil 0.1.0` or
`dsa-editor 0.1.0`.

### 2.6 Updating and uninstalling

To reinstall after moving the project or changing dependencies:

```bash
./install.sh
```

To remove the global command:

```bash
rm -f ~/.local/bin/anvil
```

The project's `.venv` can be deleted separately.  Your text files are never
stored inside the project.

## 3. Starting the editor

```
anvil [options] [file]
```

Examples:

```bash
anvil                       # empty untitled buffer
anvil notes.md              # open notes.md, or create it on first save
anvil ~/src/app.py          # ~ is expanded
anvil --no-server file.py   # no HTTP bridge
anvil --theme dracula       # different Pygments theme
```

If `file` does not exist, the editor opens an empty buffer associated with
that path.  `Ctrl-S` creates the file.  Relative paths are resolved against the
current working directory, not the project directory.

If `file` exists but is binary or larger than 64 MiB, the editor refuses to
open it and shows an error instead of corrupting it.

### 3.1 What happens on startup

- The file is loaded with encoding and newline detection.
- The syntax lexer is chosen from the extension.
- The trie is seeded with keywords for that language.
- The undo history is reset (opening a file is not an undoable edit).
- The side panel appears automatically when the terminal is at least 100
  columns wide; otherwise it starts hidden.
- The HTTP bridge starts on `127.0.0.1:8765` unless disabled.
- Autosave starts enabled with a 30-second interval.

## 4. The screen

```
┌──────────────────────────────────────────────────────────────┬────────────────────────────┐
│  1  import re                                                │ DATA STRUCTURES            │
│  2  from dataclasses import dataclass                        │ undo stack      7 / 150    │
│  3                                                           │ redo stack      0 / 150    │
│  4  def word_count(text: str) -> int:                        │ trie nodes   1204   words  │
│  5      return len(re.findall(r"\S+", text))                 │ clipboard       3 / 50     │
│  6                                                           │ ────────────────────────── │
│  7  print(word_count("hello trie"))                          │ DOCUMENT                   │
│                                                              │ demo.py  modified          │
│                                                              │ python · utf-8 · LF        │
│                                                              │ 42 words · 218 chars       │
│                                                              │ autosave 30s · 8s ago      │
│                                                              │ bridge :8765               │
│                                                              │ ────────────────────────── │
│                                                              │ TRIE  prefix 'pri'         │
│                                                              │ print      keyword    80   │
│                                                              │ private    common     45   │
└──────────────────────────────────────────────────────────────┴────────────────────────────┘
 demo.py ● │ python │ Ln 7, Col 1 │ 42 words │ auto on │ bridge :8765
 Ctrl-S save · F3 save as · F4 open · Ctrl-Z/Y undo/redo · F6 panel · F1 help
```

### 4.1 Editor area

- Line numbers on the left.
- Syntax highlighting from the active Pygments lexer.
- Soft-wrapped lines are off; long lines scroll horizontally.
- The cursor is the terminal cursor.

### 4.2 Data-structures panel

Toggle with `F6` or `:panel`.  It is 42 columns wide and shows:

- undo/redo depths and capacities;
- trie node and word counts;
- clipboard queue size;
- document path, language, encoding and newline style;
- word/character counts and cursor line/column;
- autosave interval, countdown and last-save age;
- bridge URL or `off`;
- the top undo entry;
- the newest clipboard entries;
- live trie suggestions for the current prefix.

The panel starts visible when the terminal is at least 100 columns wide, and
hidden otherwise.  `--panel` / `--no-panel` override that.

### 4.3 Status bar

The status bar shows the path, a dirty/clean marker (`●` / `✓`), language,
cursor position, word count, autosave state, selection size and bridge port.

### 4.4 Hint bar

The bottom line shows contextual hints: the last message, command-bar help, or
the default shortcut reminder.  Errors are shown in red for a few seconds.

### 4.5 Command bar

Press `F9`, `Ctrl-P`, or a shortcut such as `Ctrl-F`.  A one-line command bar
appears above the status bar, prefixed with `:`.  Type a command and press
`Enter`; `Esc` cancels.

### 4.6 Help screen

`F1` replaces the editor area with the built-in help.  Scroll with the usual
movement keys.  `Esc`, `F1`, or `q` closes it.

### 4.7 Completion popup and ghost text

After two identifier characters the trie popup appears below the cursor.  The
best completion may also appear as grey text after the cursor.  `Tab` accepts
or cycles the popup; `Ctrl-Right` accepts the grey suggestion.

## 5. Editing basics

### 5.1 Typing and moving

Typing inserts text at the cursor.  Arrow keys move by character or line;
`Home`/`End` go to the line boundaries; `Ctrl-Home`/`Ctrl-End` go to the start
or end of the document; `Ctrl-Left`/`Ctrl-Right` move by word.

`PageUp`/`PageDown` scroll by a page.  `Ctrl-Right` moves to the next word,
unless a grey ghost suggestion is visible, in which case it accepts the
suggestion.

### 5.2 Selecting text

- Hold `Shift` while using the arrow keys.
- `Ctrl-@` (often `Ctrl-Space`) starts a selection; move to extend it.
- `Ctrl-A` selects the whole document.
- `Esc` cancels the selection.

There is no mouse selection; mouse support is intentionally off.

### 5.3 Smart newline and indentation

`Enter` inserts a newline and keeps the current line's leading whitespace.  If
the line before the cursor ends with `:`, `{`, `[` or `(`, one extra
indentation level is added.

`Tab` inserts spaces up to the next tab stop when no completion popup is open.
`Shift-Tab` removes up to one tab width of leading whitespace from the current
line.

### 5.4 Deleting text

| Key | Action |
|---|---|
| `Backspace` | Delete the character before the cursor |
| `Delete` | Delete the character at the cursor |
| `Ctrl-D` | Delete the character at the cursor |
| `Ctrl-K` | Kill from the cursor to the end of the line |
| `Ctrl-U` | Kill from the cursor to the start of the line |
| `Ctrl-W` | Delete the word before the cursor |
| `Alt-D` | Delete the word after the cursor |
| `Alt-Backspace` | Delete the word before the cursor |

### 5.5 Case and transposition

- `Ctrl-T` transposes the two characters around the cursor.
- `Alt-C`, `Alt-L`, `Alt-U` capitalize, lowercase or uppercase the next word.
- `Ctrl-L` clears and redraws the screen.

### 5.6 A note on Emacs keys

The editor uses Emacs-style movement and editing keys, but several standard
bindings are repurposed for the application:

| Standard Emacs | Here |
|---|---|
| `Ctrl-A` beginning of line | Select all (`Home` goes to line start) |
| `Ctrl-N` next line | New file |
| `Ctrl-P` previous line | Command bar |
| `Ctrl-S` forward search | Save |
| `Ctrl-Q` quoted insert | Quit |
| `Ctrl-C` interrupt | Copy |
| `Ctrl-X` prefix | Cut |
| `Ctrl-V` quoted insert | Paste clipboard history |
| `Ctrl-Y` yank | Redo |
| `Ctrl-Z` suspend | Undo |
| `Ctrl-G` cancel | Go to line |

Use `Esc` to cancel dialogs, selections and completion popups.

## 6. Files and saving

### 6.1 New, open, save, save as

| Action | Shortcut | Command |
|---|---|---|
| New file | `Ctrl-N` | `:new` |
| Open | `F4` / `Ctrl-O` | `:e path` |
| Save | `Ctrl-S` / `F2` | `:w` |
| Save as | `F3` | `:w path` or `:saveas path` |
| Save and quit | — | `:wq` |
| Quit | `Ctrl-Q` | `:q` |
| Force quit | — | `:q!` |

If the buffer is dirty, `Ctrl-Q` asks for confirmation: press it again within
2.5 seconds to discard changes.  `:q!` discards immediately.

`F3` opens the command bar with `w ` prefilled; `F4`/`Ctrl-O` opens it with
`e `.  A path containing spaces works after the command, for example
`:w my notes.txt`.

### 6.2 Encoding, BOM and newlines

The editor detects and preserves:

- UTF-8 (with or without BOM)
- UTF-16 LE and UTF-16 BE (with BOM)
- cp1252 as a fallback for non-UTF-8 text
- latin-1 as a final fallback
- LF, CRLF and CR newline styles

Newlines are normalized internally to LF and converted back on save.  Ties in
newline detection prefer LF.

### 6.3 Atomic saves

Saving writes a temporary file in the destination directory, flushes it,
`fsync`s it, and then replaces the original with `os.replace`.  Existing file
permissions are preserved.  A failed save leaves the original file untouched.

Parent directories are created if needed.

### 6.4 Binary and large files

- Files containing NUL bytes in the first 8 KiB, without a UTF BOM, are treated
  as binary and refused.
- Files larger than **5 MiB** open with a warning.
- Files larger than **64 MiB** are refused to keep the TUI responsive.

The editor is a text editor, not a hex editor.

### 6.5 Backups

The file I/O API supports `.bak` backups (`save_text_file(..., backup=True)`),
but the editor does not enable them by default.

## 7. Undo and redo

Undo and redo use the editor's own two bounded stacks, not a library feature.

- `Ctrl-Z` undoes; `Ctrl-Y` redoes.
- `Ctrl-_` and `Ctrl-X Ctrl-U` are readline-style undo aliases and use the same
  custom stack.
- Consecutive non-whitespace inserts within **0.9 seconds** are coalesced into
  one undo unit.  Consecutive non-whitespace deletes coalesce the same way.
- Whitespace, newlines, replacements and other edits that are not plain
  non-whitespace typing become their own undo unit.
- Moving the cursor breaks the current group, so typing at a new position
  starts a new unit.
- A new edit clears the redo stack.
- Each stack is bounded by **150 snapshots** by default and by **4,000,000
  stored characters**.  Old snapshots are dropped from the bottom when either
  limit is reached.

Opening or creating a file resets the history.

## 8. Completion and syntax highlighting

### 8.1 The completion trie

The editor keeps one character trie per open document.  It is seeded with:

- keywords for the detected language;
- common programming words;
- identifiers learned from the document.

The trie learns identifiers at most once per second while typing, and again on
save.  Accepting the grey ghost suggestion increases that word's ranking
weight.

### 8.2 Using completion

- Type at least two identifier characters to open the popup.
- `Tab` accepts the selected completion; pressing it again cycles.
- `Tab` with fewer than two characters explicitly requests completions, so it
  can show one-character prefixes.
- `Ctrl-Right` accepts the grey ghost suggestion.
- `Esc` dismisses the popup.

The popup shows the completion kind (`keyword`, `common`, `word`, `snippet`).
Completion is prefix/dictionary based, not semantic; there is no LSP.

### 8.3 Changing the language

```text
:lang python
:lang javascript
:lang auto
:lang
```

`:lang auto` re-detects from the file extension.  With no argument, the current
language is shown.  Unknown languages are rejected with a message.

### 8.4 Themes

```text
:theme monokai
:theme dracula
:theme
```

Any Pygments style name works.  With no argument, the current theme is shown.
The default is `monokai`.

To list installed Pygments styles:

```bash
.venv/bin/python -c "from pygments.styles import get_all_styles; print(' '.join(sorted(get_all_styles())))"
```

## 9. Clipboard history

Every copy or cut is appended to a bounded FIFO queue.

| Key | Action |
|---|---|
| `Ctrl-C` | Copy the selection |
| `Ctrl-X` | Cut the selection |
| `Ctrl-V` | Paste the newest entry |
| `Alt-1` … `Alt-9` | Paste entry 1 (newest) … 9 |
| `Esc` | Cancel selection |

Details:

- The queue holds **50 entries** by default (`--clipboard-capacity`).
- The newest entry is at the tail; the oldest is at the head.
- When the queue is full, the oldest entry is evicted.
- Consecutive identical copies refresh the tail instead of adding a duplicate.
- The queue is in memory only; it is not persisted between runs.
- The editor also sets prompt_toolkit's in-memory clipboard, but the history
  queue is the source for `Ctrl-V` and `Alt-n`.

If your terminal does not send `Alt-1` as a distinct key, use `Ctrl-V` for the
newest entry or configure the terminal's Meta/Alt behavior.

## 10. Find, go to, and word count

### 10.1 Find

- `Ctrl-F` opens the command bar with `find `.
- `:find text` finds the next occurrence after the cursor and wraps around.
- `:find` repeats the last search.
- `Ctrl-F` with no text opens the command bar.

There is no interactive incremental search and no replace command.

### 10.2 Go to line

- `Ctrl-G` opens the command bar with `goto `.
- `:goto 42` jumps to line 42.
- Line numbers are clamped to the valid range.

### 10.3 Word count

The status bar, panel and bridge expose:

- words: whitespace-separated tokens (`\S+`);
- characters: total code points;
- non-whitespace characters;
- lines: newline count + 1;
- cursor line and column (1-based);
- selection size.

The count updates on every text change.  The scan is O(n) per change.

## 11. Autosave

Autosave is enabled by default with a **30-second** interval.

- It only runs when the buffer is dirty and has a path.
- A new untitled buffer must be saved once (`F3` or `:w path`) before autosave
  can write it.
- Each autosave is atomic and preserves encoding, BOM and newline style.
- The panel and bridge show the interval, countdown and last-save age.

Controls:

| Action | How |
|---|---|
| Toggle on/off | `F5` or `:autosave` |
| Set interval | `:autosave 10` |
| Turn off | `:autosave off` |
| Start with it off | `anvil --no-autosave file.py` |
| Change startup interval | `anvil --autosave-interval 10 file.py` |

The minimum interval is 1 second.  Autosave overwrites the open file; if that
is not desired during a demo, turn it off with `F5`.

## 12. Command bar reference

Open with `F9`, `Ctrl-P`, or a shortcut.  Commands are case-insensitive; a
leading `:` is optional.

| Command | Aliases | Action |
|---|---|---|
| `:w [path]` | `:write`, `:save` | Save, optionally to another path |
| `:wq` | — | Save and quit if the save succeeds |
| `:saveas path` | — | Save as another path |
| `:e path` | `:edit`, `:open` | Open a file |
| `:e! path` | — | Open and discard unsaved changes |
| `:new` | — | New untitled buffer |
| `:new!` | — | New buffer and discard unsaved changes |
| `:q` | `:quit` | Quit; asks twice if dirty |
| `:q!` | — | Force quit |
| `:autosave [on\|off\|seconds]` | — | Toggle or configure autosave |
| `:lang [language\|auto]` | `:language`, `:syntax` | Set or detect the language |
| `:find [text]` | — | Find next; no argument repeats the last search |
| `:goto line` | — | Jump to a line |
| `:theme [name]` | — | Set or show the Pygments theme |
| `:tab width` | — | Set the soft-tab width (clamped to 1–16) |
| `:undo` | — | Undo |
| `:redo` | — | Redo |
| `:panel` | `:stats` | Toggle the data-structures panel |
| `:clip` | `:clipboard` | Open the panel and show queue size |
| `:state` | `:bridge` | Show the bridge URL and state-file path |
| `:path` | — | Show the current file path |
| `:languages` | — | List bundled keyword languages |
| `:help` | — | Show or hide help |

Examples:

```text
:w
:w notes/todo.md
:wq
:e src/main.py
:e! src/other.py
:autosave 10
:lang rust
:theme dracula
:goto 120
:find TODO
```

## 13. Keyboard reference

The complete table is in [`keybindings.md`](keybindings.md).  The most useful
keys are repeated here.

### Files and application

| Key | Action |
|---|---|
| `Ctrl-S` / `F2` | Save |
| `F3` | Save as |
| `F4` / `Ctrl-O` | Open |
| `Ctrl-N` | New file |
| `Ctrl-Q` | Quit (twice if dirty) |
| `F5` | Toggle autosave |
| `F6` | Toggle panel |
| `F9` / `Ctrl-P` | Command bar |
| `F1` | Help |
| `Esc` | Close help/command/completion/selection |

### History

| Key | Action |
|---|---|
| `Ctrl-Z` | Undo |
| `Ctrl-Y` | Redo |
| `Ctrl-_` | Undo alias |
| `Ctrl-X` `Ctrl-U` | Undo alias |

### Selection and clipboard

| Key | Action |
|---|---|
| `Shift+arrows` | Select |
| `Ctrl-@` / `Ctrl-Space` | Start selection |
| `Ctrl-A` | Select all |
| `Ctrl-C` / `Ctrl-X` | Copy / cut |
| `Ctrl-V` | Paste newest |
| `Alt-1` … `Alt-9` | Paste history entry |

### Movement and editing

| Key | Action |
|---|---|
| `←` `→` `↑` `↓` | Move |
| `Home` / `End` | Line start / end |
| `Ctrl-Home` / `Ctrl-End` | Document start / end |
| `Ctrl-Left` / `Ctrl-Right` | Word left / right |
| `Alt-B` / `Alt-F` | Word left / right |
| `PageUp` / `PageDown` | Scroll |
| `Enter` | Smart newline |
| `Tab` | Completion / soft tab |
| `Shift-Tab` | Dedent |
| `Backspace` / `Delete` | Delete |
| `Ctrl-D` | Delete character |
| `Ctrl-K` | Kill to line end |
| `Ctrl-U` | Kill to line start |
| `Ctrl-W` | Delete word before |
| `Alt-D` | Delete word after |
| `Alt-Backspace` | Delete word before |
| `Ctrl-T` | Transpose characters |
| `Ctrl-L` | Clear screen |
| `Ctrl-Right` | Accept ghost suggestion |
| `Ctrl-F` / `Ctrl-G` | Find / go to line |

> If `Ctrl-S` or `Ctrl-Q` is captured by terminal flow control, run
> `stty -ixon` in that terminal, or use `F2` and `:q`.

## 14. Live state bridge

The editor can expose every data structure and its live data to a separate
Python application.  This is the contract the future visualizer will use; the
editor does not need a special demo mode.

### 14.1 Two channels

| Channel | Address | Notes |
|---|---|---|
| HTTP | `http://127.0.0.1:8765/state` | Preferred; read-only, CORS enabled |
| JSON file | `~/.local/state/dsa-editor/state.json` | Atomic writes, at most once per second |

The HTTP port defaults to 8765.  If it is busy, the editor tries the requested
port and the next nine, then an ephemeral port.  Read the actual port from the
panel, `/health`, or the `app.port` field.

Disable the HTTP server with `--no-server`; disable the JSON file with
`--no-state-file`.  `--state-file PATH` writes the JSON file elsewhere.

The state directory respects `$XDG_STATE_HOME`:

```text
$XDG_STATE_HOME/dsa-editor/state.json
```

or, when unset:

```text
~/.local/state/dsa-editor/state.json
```

The JSON file is written at most once per second, and once more on exit with
`status: "closed"`.

### 14.2 Python client

```python
from dsa_editor.bridge import StateClient

client = StateClient()                 # 127.0.0.1:8765
for snapshot in client.watch():        # long-polls for changes
    print(snapshot["stats"]["words"])
    print(snapshot["undo"]["undo_stack"]["depth"])
    print(snapshot["clipboard"]["entries"])
    print(snapshot["trie"]["suggestions"])
```

`watch()` yields only when the sequence number changes.  Handle connection
errors if the visualizer should survive editor restarts.

Without HTTP:

```python
from dsa_editor.bridge import default_state_file, read_state_file

snapshot = read_state_file(default_state_file())
```

Run the included consumer:

```bash
.venv/bin/python examples/watch_state.py
.venv/bin/python examples/watch_state.py --raw
.venv/bin/python examples/watch_state.py --file
```

### 14.3 Endpoints

| Endpoint | Result |
|---|---|
| `/` | Small HTML index with current counts |
| `/health` | `{ok, schema, version, seq, closed, uptime_seconds, port, pid}` |
| `/state` | Complete snapshot |
| `/state?wait=1&since=N&timeout=S` | Wait up to S seconds for `seq > N` |
| `/trie?depth=D&limit=L` | On-demand trie tree (`D` ≤ 12, `L` ≤ 5000) |
| `/undo` | `{seq, timestamp, undo: <undo section>}` |
| `/redo` | `{seq, timestamp, redo: <redo stack>}` |
| `/clipboard` | `{seq, timestamp, clipboard: <queue>}` |
| `/stats` | `{seq, timestamp, stats: <counts>}` |
| `/file` | `{seq, timestamp, file: <file info>}` |
| `/autosave` | `{seq, timestamp, autosave: <settings>}` |
| `/ui` | `{seq, timestamp, ui: <visibility>}` |

All JSON endpoints send `Cache-Control: no-store` and CORS `*`.  `/` returns a
small HTML index instead.

### 14.4 Snapshot envelope

Every snapshot includes:

```json
{
  "schema": "dsa-editor/state",
  "version": 1,
  "seq": 42,
  "timestamp": 1760000000.123,
  "status": "ok"
}
```

`seq` increases on every publish.  `status` is `"ok"` while running and
`"closed"` in the final snapshot.  Use `seq` to avoid redrawing unchanged
state.

Top-level sections:

| Key | Contents |
|---|---|
| `app` | Name, version, pid, session, port, theme, tab width, state-file path |
| `file` | Path, name, dirty flag, encoding, BOM, newline, language, size, save times |
| `stats` | Word/character/line counts, cursor, selection, saved size |
| `document` | Text preview, char/line counts, cursor index |
| `undo` | Both stacks with entries and policy |
| `clipboard` | Queue capacity, size, head/tail ids, entries |
| `trie` | Stats, depth-limited tree, top words, prefix, suggestions |
| `autosave` | Enabled, interval, last save, countdown, active |
| `settings` | Capacities, theme, tab width, bridge enabled |
| `ui` | Panel/help/command visibility and current message |

The complete field-by-field schema, ordering rules and sample payloads are in
[`state_api.md`](state_api.md).

### 14.5 Consistency and security

- The UI thread builds complete snapshots and publishes them; HTTP threads only
  read the latest published snapshot.
- `/trie` can build a deeper tree on demand; the trie has its own lock.
- The bridge is read-only.  There is no remote editing API.
- It binds to `127.0.0.1` only.  Do not expose it to a network.
- Clipboard text is included in the snapshot; keep the bridge local.

## 15. Command-line options

```
anvil [-h] [--autosave | --no-autosave]
      [--autosave-interval SECONDS] [--port PORT] [--no-server]
      [--state-file PATH] [--no-state-file] [--theme THEME]
      [--tab-width TAB_WIDTH] [--panel | --no-panel]
      [--undo-capacity UNDO_CAPACITY]
      [--clipboard-capacity CLIPBOARD_CAPACITY] [--version]
      [file]
```

| Option | Default | Meaning |
|---|---|---|
| `file` | untitled | File to open or create |
| `--autosave` / `--no-autosave` | on | Start with autosave enabled/disabled |
| `--autosave-interval SECONDS` | 30 | Autosave interval; minimum 1 |
| `--port PORT` | 8765 | HTTP bridge port; tries it plus the next nine, then an ephemeral port |
| `--no-server` | off | Disable the HTTP bridge |
| `--state-file PATH` | XDG path | JSON snapshot path |
| `--no-state-file` | off | Disable JSON snapshot writes |
| `--theme THEME` | monokai | Pygments style name |
| `--tab-width WIDTH` | 4 | Soft-tab width, clamped to 1–16 |
| `--panel` / `--no-panel` | auto | Force the data panel on/off |
| `--undo-capacity N` | 150 | Maximum snapshots per stack |
| `--clipboard-capacity N` | 50 | Clipboard-history capacity |
| `--version` | — | Print the version and exit |

## 16. Configuration and defaults

There is no configuration file in version 0.1.0.  Settings come from:

1. command-line options at startup;
2. command-bar commands during the session;
3. a few keyboard toggles (`F5` autosave, `F6` panel).

Defaults:

| Setting | Default |
|---|---|
| Autosave | on, 30 s |
| Undo capacity | 150 snapshots per stack |
| Undo character limit | 4,000,000 |
| Undo grouping window | 0.9 s |
| Clipboard capacity | 50 entries |
| Tab width | 4 |
| Theme | monokai |
| HTTP bridge | on, 127.0.0.1:8765 |
| JSON state file | on, XDG state path |
| Panel | auto (on at ≥100 columns) |
| Mouse | off |

Changing the tab width or theme does not persist across runs.

## 17. Troubleshooting

### `anvil: command not found`

- Check that `~/.local/bin` is on `PATH`.
- Re-run `./install.sh`.
- Or run `.venv/bin/anvil` directly.

### `Ctrl-S` or `Ctrl-Q` does nothing

The terminal is capturing flow control.  Run:

```bash
stty -ixon
```

Then restart the editor, or use `F2` to save and `:q` to quit.

### The panel is missing

The panel starts hidden on terminals narrower than 100 columns.  Press `F6` or
run `anvil --panel`.

### `Alt-1` does not paste history

Some terminals do not send Meta/Alt combinations distinctly.  Use `Ctrl-V` for
the newest entry, or adjust the terminal's Alt/Meta setting.  In many terminals
`Alt-1` is sent as `Esc` followed by `1`, which the editor handles.

### Completion does not appear

- Type at least two identifier characters, or press `Tab` to request it.
- The current language may not have bundled keywords; the trie still learns
  identifiers from the document.
- Check that the prefix is an identifier prefix (letters, digits, `_`).

### The bridge port is in use

The editor tries the requested port and the next nine automatically.  Read the
actual port from the panel or `curl http://127.0.0.1:8765/health` (if 8765 is
the one that started).  You can also choose a port with `--port 9000`.

### The state file is not updating

- Check the panel for a state-file error.
- Ensure the state directory is writable.
- Use `--state-file /tmp/state.json` to test.
- The file is written at most once per second; it is not updated on every
  keystroke.

### A binary file will not open

That is intentional.  The editor refuses files with NUL bytes (unless they have
a UTF BOM) to avoid corrupting them.

### A large file is slow

Files over 5 MiB warn; over 64 MiB are refused.  The whole document is kept in
memory and undo snapshots store whole documents, so very large files are not a
good fit.

### Autosave overwrote my file

Autosave writes the open file every 30 seconds when it is dirty.  Turn it off
with `F5` or start with `--no-autosave`.  Use `F3` to save a copy before
experimenting.

### Undo removed more than expected

Consecutive typing is coalesced into one undo unit.  Moving the cursor starts a
new unit; typing a whole word without moving may be one unit.  This is
intentional.  Use `Ctrl-Y` to redo.

### A theme or language is unknown

Themes must be valid Pygments style names.  Languages must be one of the
bundled keyword languages or a language Pygments recognizes.  Use
`:languages` for the bundled list.

### Colours look wrong

Use a 256-colour terminal (`TERM=xterm-256color`) and a font with ANSI colour
support.  The theme only controls syntax colours; the UI colours are fixed.

### The editor looks cramped

The panel takes 42 columns.  Hide it with `F6`, use a wider terminal, or start
with `--no-panel`.

## 18. Architecture and extending

### 18.1 Module map

```
dsa_editor/
├── app.py          TUI layout, keybindings, autosave, state snapshot builder
├── cli.py          argparse entry point for anvil / dsa-editor
├── fileio.py       encoding/newline detection and atomic save
├── highlight.py    Pygments lexer selection
├── keywords.py     keyword seeds loaded from data/keywords.json
├── structures/
│   ├── stack.py    BoundedStack, Snapshot, UndoRedoManager
│   ├── trie.py     Trie, completion, JSON tree view
│   ├── queue.py    ClipboardHistory FIFO queue
│   └── stats.py    word/character/line counting
└── bridge/
    ├── hub.py      latest snapshot + long-poll condition
    ├── server.py   read-only HTTP server
    ├── client.py   StateClient
    └── statefile.py atomic JSON writer
```

### 18.2 Data flow

```
keystroke
   │
   ▼
prompt_toolkit Buffer
   │  on_text_changed
   ▼
EditorApp
   ├── UndoRedoManager.record()      custom undo stack
   ├── Trie.learn_text()             learn identifiers
   ├── compute_text_stats()          live counts
   ├── ClipboardHistory             copy/cut history
   └── build_state() ──► StateHub ──► HTTP server
                                  └─► state.json
```

### 18.3 Adding a command

1. Add a branch in `EditorApp._run_command`.
2. Update the help text (`HELP_TEXT`) and this manual.
3. Add a test in `tests/test_app.py` if the command changes state.

### 18.4 Adding a keybinding

Add a handler in `EditorApp._build_key_bindings`.  Use `filter=editor_focused`
when the key should only act in the editor, and remember that
`@kb.add("c-x", "c-u")` creates a two-key sequence.

### 18.5 Adding a language

Add keyword entries to `dsa_editor/data/keywords.json` and, if needed, an alias
in `dsa_editor/keywords.py`.  Pygments supplies the syntax lexer.

### 18.6 Tests

```bash
.venv/bin/python -m unittest discover -s tests -v
.venv/bin/python scripts/pty_smoke.py
.venv/bin/python scripts/bridge_smoke.py
```

The unit suite covers the structures, file I/O, bridge and app integration.
The PTY scripts launch the real full-screen editor.

## 19. Lecture demo walkthrough

1. **Open a file.**  Run `anvil demo.py` and point out the editor, line
   numbers, syntax colours and status bar.
2. **Live word count.**  Type a sentence and watch the word count and panel
   update immediately.
3. **Trie completion.**  Type `pri`.  Show the popup (`print`, `private`) and
   the grey inline suggestion.  Press `Ctrl-Right` to accept it.  Type a new
   identifier such as `my_helper`, then use its prefix to show that the trie
   learned it.
4. **Undo/redo stacks.**  Type several characters without moving the cursor,
   then `Ctrl-Z`: the whole group disappears and the panel's undo depth drops.
   Press `Ctrl-Y` to redo.  Move the cursor and type again to show a new undo
   unit.
5. **Clipboard queue.**  Select text with `Shift+arrows`, copy with `Ctrl-C`,
   paste with `Ctrl-V`.  Copy another selection and use `Alt-2` to paste the
   older entry.  Show the queue in the panel.
6. **Autosave.**  Press `F5` to toggle it and watch the panel countdown.  Press
   `F5` again to turn it back on.
7. **Live bridge.**  In a second terminal:

   ```bash
   cd dsa-editor
   .venv/bin/python examples/watch_state.py
   ```

   Or open `http://127.0.0.1:8765/state` in a browser.  Point out live word
   count, undo/redo depths, trie nodes, clipboard queue and suggestions.
8. **Explain the contract.**  The visualizer will consume the same JSON; the
   editor needs no special demo mode.

## 20. Limitations

- Text only; binary files are refused.
- No LSP or semantic completion.
- Whole-document snapshots for undo; memory is bounded but not delta-encoded.
- Clipboard history is in memory only.
- No mouse support.
- One buffer per process; no tabs or splits.
- No incremental search or replace; only find-next and go-to-line.
- No persistent settings or configuration file.
- No plugin system.
- Autosave can overwrite the open file; turn it off if that is not desired.
- The HTTP bridge is read-only and local-only; it is not a remote editing API.

## Appendix A: default limits

| Limit | Value |
|---|---|
| Undo snapshots per stack | 150 |
| Undo stored characters | 4,000,000 |
| Undo grouping window | 0.9 s |
| Clipboard entries | 50 |
| Tab width | 4 (1–16) |
| Autosave interval | 30 s (minimum 1 s) |
| File warning size | 5 MiB |
| File refusal size | 64 MiB |
| HTTP port | 8765 (requested + next 9, then ephemeral) |
| State-file write interval | 1 s |
| Snapshot trie depth | 3 |
| Snapshot trie nodes | 800 |

## Appendix B: bundled languages

The trie ships keyword lists for:

`bash`, `c`, `cpp`, `csharp`, `css`, `go`, `html`, `java`, `javascript`,
`json`, `kotlin`, `lua`, `markdown`, `php`, `python`, `ruby`, `rust`, `sql`,
`swift`, `typescript`, `yaml`.

Unknown extensions are treated as plain text.  Pygments may still highlight
other languages if selected with `:lang`, and the trie falls back to generic
programming words plus identifiers learned from the document.

## Appendix C: glossary

- **Snapshot** — the document text and cursor position stored before an edit;
  the unit of undo/redo.
- **Coalescing** — merging consecutive similar edits into one undo unit.
- **Bounded stack** — a stack with a maximum size; the oldest item is evicted.
- **Trie** — a tree keyed by characters; shared prefixes share a path.
- **Terminal node** — a trie node that marks the end of a complete word.
- **FIFO** — first in, first out; the clipboard queue evicts the oldest entry.
- **Dirty buffer** — a buffer with unsaved changes.
- **State bridge** — the read-only HTTP/JSON channel that exposes live data.
- **Sequence number (`seq`)** — increments on every published snapshot.
- **Ghost text** — the grey inline completion shown after the cursor.

## Appendix D: related documents

- [`keybindings.md`](keybindings.md) — complete keyboard reference.
- [`state_api.md`](state_api.md) — visualizer JSON/HTTP contract.
- [`dsa_notes.md`](dsa_notes.md) — lecture notes on each data structure.
- [`../README.md`](../README.md) — project overview and quick start.
- [`../examples/watch_state.py`](../examples/watch_state.py) — live-state
  consumer example.
