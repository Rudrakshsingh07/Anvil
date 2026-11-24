# Keyboard shortcuts

The editor uses Emacs-style editing keys plus its own application shortcuts.
`Ctrl` is written as `Ctrl-`, `Alt` as `Alt-` (sometimes labelled `Meta`).

See [`manual.md`](manual.md) for the full user manual, [`visualizer.md`](visualizer.md)
for the separate live visualizer, and the command reference below.

> If your terminal captures `Ctrl-S` / `Ctrl-Q` for flow control, run
> `stty -ixon` in that terminal, or use `F2` to save and `:q` to quit.

## Files and application

| Key | Action |
|---|---|
| `Ctrl-S` / `F2` | Save |
| `F3` | Save as (opens the command bar with `w `) |
| `F4` / `Ctrl-O` | Open a file (command bar with `e `) |
| `Ctrl-N` | New file |
| `Ctrl-Q` | Quit; press twice if there are unsaved changes |
| `F5` | Toggle autosave |
| `F6` | Toggle the data-structures panel |
| `F9` / `Ctrl-P` | Open the command bar |
| `F1` | Open/close help |
| `q` | Close help (while help is focused) |
| `Esc` | Close help, command bar, completion popup or selection |

## Undo and redo

| Key | Action |
|---|---|
| `Ctrl-Z` | Undo |
| `Ctrl-Y` | Redo |
| `Ctrl-_` | Undo (readline alias, same custom stack) |
| `Ctrl-X` `Ctrl-U` | Undo (readline alias, same custom stack) |

All of these use the editor's own bounded snapshot stacks; prompt_toolkit's
built-in undo is never used.

## Selection and clipboard

| Key | Action |
|---|---|
| `Shift+arrows` | Select text |
| `Ctrl-@` / `Ctrl-Space` | Start a selection at the cursor |
| `Ctrl-A` | Select all |
| `Ctrl-C` | Copy the selection into the clipboard-history queue |
| `Ctrl-X` | Cut the selection into the clipboard-history queue |
| `Ctrl-V` | Paste the newest clipboard-history entry |
| `Alt-1` … `Alt-9` | Paste clipboard-history entry 1 (newest) … 9 |
| `Esc` | Cancel the current selection |

The clipboard is a FIFO history queue: every copy/cut is appended at the tail,
and the oldest entry is evicted when the queue is full.

## Movement

| Key | Action |
|---|---|
| `←` / `→` | Previous / next character |
| `↑` / `↓` | Previous / next line |
| `Home` / `End` | Start / end of line |
| `Ctrl-Home` / `Ctrl-End` | Start / end of the document |
| `Ctrl-Left` / `Ctrl-Right` | Previous / next word |
| `Alt-B` / `Alt-F` | Previous / next word |
| `PageUp` / `PageDown` | Scroll up / down one page |
| `Ctrl-B` / `Ctrl-F` | Previous / next character (Emacs style) |
| `Ctrl-E` | End of line |

`Ctrl-Right` has a second job: if the grey inline trie suggestion is visible,
it accepts that suggestion instead of moving the cursor.

## Editing

| Key | Action |
|---|---|
| `Enter` | Smart newline: keeps indentation and adds one level after `:`, `{`, `[` or `(` |
| `Tab` | Accept/cycle the completion popup; otherwise insert a soft tab |
| `Shift-Tab` | Dedent the current line |
| `Backspace` / `Delete` | Delete the character before / at the cursor |
| `Ctrl-D` | Delete the character at the cursor |
| `Ctrl-K` | Kill from the cursor to the end of the line |
| `Ctrl-U` | Kill from the cursor to the start of the line |
| `Ctrl-W` | Delete the word before the cursor (whitespace-separated) |
| `Alt-D` | Delete the word after the cursor |
| `Alt-Backspace` | Delete the word before the cursor |
| `Ctrl-T` | Transpose the two characters around the cursor |
| `Ctrl-L` | Clear and redraw the screen |
| `Alt-C` / `Alt-L` / `Alt-U` | Capitalize / lowercase / uppercase the next word |

## Completion and highlighting

| Key | Action |
|---|---|
| Type 2+ identifier characters | Open the trie completion popup and grey inline suggestion |
| `Tab` | Accept/cycle the selected completion |
| `Ctrl-Right` | Accept the grey inline suggestion |
| `Esc` | Dismiss the completion popup |
| `:lang python` | Change the syntax language |
| `:lang auto` | Re-detect the language from the file extension |
| `:theme monokai` | Change the Pygments colour theme |

## Find and go to

| Key | Action |
|---|---|
| `Ctrl-F` | Open the command bar with `find ` |
| `Ctrl-G` | Open the command bar with `goto ` |
| `:find text` | Find the next occurrence (wraps around) |
| `:find` | Repeat the last search |
| `:goto 42` | Jump to line 42 |

## Command bar

Open with `F9`, `Ctrl-P`, or a shortcut above, type the command and press
`Enter`. `Esc` cancels.

| Command | Action |
|---|---|
| `:w [path]` | Save, optionally to another path |
| `:wq` | Save and quit |
| `:saveas path` | Save as another path |
| `:e path` | Open a file; `:e!` discards unsaved changes |
| `:new` | New file; `:new!` discards unsaved changes |
| `:q` / `:q!` | Quit / force quit |
| `:autosave on\|off\|<seconds>` | Configure autosave |
| `:lang python` / `:lang auto` | Set or detect the syntax language |
| `:find text` | Find text |
| `:goto 42` | Go to a line |
| `:theme dracula` | Change the colour theme |
| `:tab 4` | Set the soft-tab width |
| `:undo` / `:redo` | Undo / redo |
| `:panel` | Toggle the data-structures panel |
| `:clip` | Show clipboard queue status |
| `:state` | Show the bridge URL and state-file path |
| `:languages` | List bundled keyword languages |
| `:help` | Show or hide help |

## Help screen

| Key | Action |
|---|---|
| `F1` | Open/close help |
| `Esc` | Close help |
| `q` | Close help (while help is focused) |

## Visualizer window

These keys apply to the separate **`anvil-viz`** window, not to the editor.
See [`visualizer.md`](visualizer.md) for the full guide.

| Key | Action |
|---|---|
| `q` / `Esc` | Close the visualizer |
| `r` | Reconnect to the editor bridge |
| `Space` | Pause/resume applying new snapshots |
| `F11` | Toggle fullscreen |

The visualizer is read-only; none of its keys change the document.  While it is
paused, the editor keeps running and snapshots are buffered until you resume.
