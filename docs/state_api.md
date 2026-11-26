# Live state API (visualizer contract)

The editor and the visualizer are separate processes.  The editor publishes a
complete, read-only JSON snapshot of every data structure; the visualizer polls
it.  Nothing in the editor is specific to the visualizer, so no special demo
build is needed.

The bundled implementation is **`anvil-viz`**, a Tkinter app in
`dsa_visualizer/`.  Its user guide is [`visualizer.md`](visualizer.md); this
document is the data contract it consumes.

## Where the state lives

| Channel | Address | Notes |
|---|---|---|
| HTTP (preferred) | `http://127.0.0.1:8765/state` | Read-only, CORS enabled, long-poll supported |
| HTTP trie | `http://127.0.0.1:8765/trie?depth=6&limit=2000` | Builds a deeper trie view on demand |
| JSON file | `~/.local/state/dsa-editor/state.json` | Atomic write, at most once per second |
| Python client | `dsa_editor.bridge.StateClient` | Read-only client kept for the visualizer |

Start the editor normally (`./run.sh file.py`).  The port is shown in the
data panel and in the status bar.  If 8765 is busy, the editor tries the next
10 ports, then an ephemeral port; read `app.port` from `/health` or the panel.

Disable with `--no-server` / `--no-state-file` if desired.

## Envelope

Every snapshot has:

```json
{
  "schema": "dsa-editor/state",
  "version": 1,
  "seq": 42,
  "timestamp": 1760000000.123,
  "status": "ok"
}
```

* `seq` increases on every publish.  Use it to avoid redrawing unchanged state.
* `status` is `"ok"` while running and `"closed"` in the final snapshot.
* `timestamp` is Unix wall-clock seconds (float).

## Top-level sections

| Key | Meaning |
|---|---|
| `app` | name, version, pid, session id, HTTP port, theme, tab width, state-file path |
| `file` | path, name, dirty flag, encoding, BOM, newline style, language, size, save times |
| `stats` | live word/character/line counts + cursor and selection |
| `document` | short text preview, char/line counts, cursor index |
| `undo` | **both stacks** with entries and policy |
| `clipboard` | FIFO queue capacity, size, head/tail ids, entries |
| `trie` | trie stats, depth-limited tree, top words, current prefix + suggestions |
| `autosave` | enabled, interval, last save, countdown, active |
| `settings` | capacities, theme, tab width, bridge enabled |
| `ui` | panel/help/command visibility and the current message |

## `undo` — the two stacks

```json
{
  "undo_stack": {
    "depth": 3,
    "capacity": 150,
    "total_chars": 2450,
    "top_first": true,
    "entries": [
      {
        "label": "insert",
        "kind": "insert",
        "cursor": 120,
        "chars": 800,
        "text_preview": "def hello(): print('hi')",
        "inserted_preview": "pri",
        "deleted_preview": "",
        "edit_count": 3,
        "timestamp": 1760000000.0,
        "last_timestamp": 1760000000.4,
        "age_seconds": 0.7
      }
    ]
  },
  "redo_stack": {
    "depth": 1,
    "capacity": 150,
    "total_chars": 800,
    "top_first": true,
    "entries": []
  },
  "can_undo": true,
  "can_redo": true,
  "group": {"active": true, "kind": "insert"},
  "policy": {
    "snapshot_based": true,
    "group_window_seconds": 0.9,
    "max_total_chars": 4000000
  }
}
```

* `entries` are ordered **top-first**: index 0 is the next item that undo/redo
  would consume.  Draw them bottom-to-top for a stack.
* `text_preview` is the document text stored in that snapshot (before the edit),
  whitespace-collapsed and truncated.  Full text is intentionally not sent.
* `kind` is `insert`, `delete`, `replace` or `edit`; `label` is the human label
  (`insert`, `newline`, `delete`, `replace`).
* `edit_count > 1` means the snapshot is a coalesced typing/backspace group.
* `inserted_preview` / `deleted_preview` describe the change that the undo step
  will revert (or redo will reapply).
* `group.active` shows whether the next keystroke may still merge into the top
  undo unit.

## `clipboard` — FIFO history queue

```json
{
  "capacity": 50,
  "size": 2,
  "order": "oldest_first",
  "head": 3,
  "tail": 4,
  "entries": [
    {
      "id": 3,
      "text": "first copied text",
      "preview": "first copied text",
      "chars": 17,
      "lines": 1,
      "timestamp": 1760000000.0,
      "age_seconds": 12.3
    },
    {
      "id": 4,
      "text": "second copied text",
      "preview": "second copied text",
      "chars": 18,
      "lines": 1,
      "timestamp": 1760000001.0,
      "age_seconds": 11.3
    }
  ]
}
```

* `entries` are ordered **oldest-first**; `head` is the oldest, `tail` is the
  newest (the normal paste source).
* Full clipboard text is included because it is useful for the demo; keep it
  local.
* `Alt-1` in the editor pastes `tail`, `Alt-2` the previous entry, etc.

## `trie` — prefix completion

```json
{
  "stats": {
    "nodes": 1204,
    "words": 388,
    "max_depth": 22,
    "kind_counts": {"keyword": 105, "common": 96, "word": 187}
  },
  "tree": {
    "char": "",
    "terminal": false,
    "weight": 0,
    "kind": null,
    "children": {
      "p": {
        "char": "p",
        "terminal": false,
        "weight": 0,
        "kind": null,
        "children": {
          "r": {
            "char": "r",
            "terminal": false,
            "weight": 0,
            "kind": null,
            "children": {
              "i": {
                "char": "i",
                "terminal": false,
                "weight": 0,
                "kind": null,
                "children": {}
              }
            }
          }
        }
      }
    }
  },
  "tree_meta": {"max_depth": 3, "max_nodes": 800, "truncated": true},
  "top_words": [
    {"word": "print", "weight": 80, "kind": "keyword"},
    {"word": "private", "weight": 45, "kind": "common"}
  ],
  "prefix": "pri",
  "suggestions": [
    {"word": "print", "weight": 80, "kind": "keyword"},
    {"word": "private", "weight": 45, "kind": "common"}
  ]
}
```

* `tree` is nested, keyed by child character.  The root has `char: ""`.
* `tree` in `/state` is limited to depth 3 / 800 nodes for a small payload.
  `tree_meta.truncated` says whether it was cut.  Use
  `/trie?depth=8&limit=4000` for a full drawing.
* `terminal: true` marks a complete word; `weight` is its ranking; `kind` is
  `keyword`, `common`, `word` or `snippet`.
* `prefix` is the identifier currently before the cursor; `suggestions` are the
  live completions for it (the same data as the popup).
* `stats` always describes the complete trie, even when `tree` is truncated.

## `stats` — live counts

```json
{
  "words": 42,
  "chars": 218,
  "chars_no_whitespace": 180,
  "lines": 9,
  "cursor_index": 120,
  "cursor_line": 7,
  "cursor_col": 3,
  "selection_chars": 12,
  "saved_chars": 200
}
```

* `words` counts whitespace-separated tokens (`\S+`), so code punctuation is
  part of a token; this is the notepad-style word count.
* `lines` counts newline characters + 1.
* `cursor_line` / `cursor_col` are **1-based**.
* `selection_chars` is 0 when nothing is selected.

## `file`

```json
{
  "path": "/home/r/demo.py",
  "display_path": "~/demo.py",
  "name": "demo.py",
  "exists": true,
  "dirty": true,
  "encoding": "utf-8",
  "bom": "",
  "newline": "LF",
  "language": "python",
  "language_label": "Python",
  "size_bytes": 218,
  "last_saved": 1760000000.0,
  "seconds_since_save": 8.4
}
```

`bom` is a hex string (e.g. `efbbbf`), empty for none.  `newline` is `LF`,
`CRLF` or `CR`.  `language` is the built-in language key used by the trie and highlighter.

## `autosave`

```json
{
  "enabled": true,
  "interval_seconds": 30.0,
  "last_saved": 1760000000.0,
  "seconds_since_save": 8.4,
  "due_in_seconds": 21.6,
  "active": true
}
```

`active` is true only when autosave is enabled, a path exists and the document
is dirty.

## HTTP details

### Polling

```python
import json, urllib.request

with urllib.request.urlopen("http://127.0.0.1:8765/state") as response:
    state = json.load(response)
```

Polling at 200–500 ms is plenty for a visualization.

### Long-polling (recommended)

```python
from dsa_editor.bridge import StateClient

client = StateClient()
for state in client.watch(interval=0.25):
    draw(state)
```

`watch()` keeps the last `seq`, asks `/state?wait=1&since=N&timeout=0.25`, and
yields only when a newer snapshot arrives.  It reconnects via normal Python
exceptions if the editor exits; wrap the loop if the visualizer should survive
restarts.

### Endpoints

| Path | Result |
|---|---|
| `/` | Small HTML index with current counts |
| `/health` | `{ok, seq, port, pid, uptime_seconds, closed}` |
| `/state` | Complete snapshot |
| `/state?wait=1&since=N&timeout=S` | Wait up to S seconds for `seq > N` |
| `/trie?depth=D&limit=L` | On-demand trie tree (D ≤ 12, L ≤ 5000) |
| `/undo` | `{seq, timestamp, undo: <undo section>}` |
| `/redo` | `{seq, timestamp, redo: <redo_stack>}` |
| `/clipboard` | `{seq, timestamp, clipboard: <queue>}` |
| `/stats` | `{seq, timestamp, stats: <counts>}` |
| `/file` | `{seq, timestamp, file: <file info>}` |
| `/autosave` | `{seq, timestamp, autosave: <settings>}` |
| `/ui` | `{seq, timestamp, ui: <visibility>}` |

All responses are `application/json; charset=utf-8`, `Cache-Control: no-store`,
and CORS `*` (so a browser-based visualizer can read them from `file://` or a
local dev server).

### JSON file fallback

If HTTP is inconvenient, read the atomically-written file:

```python
from dsa_editor.bridge import default_state_file, read_state_file

state = read_state_file(default_state_file())
```

The path is `$XDG_STATE_HOME/dsa-editor/state.json` or
`~/.local/state/dsa-editor/state.json` by default.  Override with
`--state-file PATH`; disable with `--no-state-file`.  The file is updated at
most once per second and once more with `status: "closed"` on exit.

## Thread-safety and consistency

* The UI thread publishes complete snapshots.  HTTP threads only read the last
  published snapshot, so they never observe a half-mutated dictionary.
* The trie, stacks and queue also have internal locks because `/trie` can be
  built on demand from the HTTP thread.
* A snapshot is internally consistent: it was built in one UI-thread pass.
* The bridge is read-only; there is no remote-editing API.

## Suggested visualizer mapping

| Structure | Drawing idea |
|---|---|
| Undo stack | Vertical stack; newest at top; height = `depth`; highlight `group.active` |
| Redo stack | Second vertical stack beside undo; fade when empty |
| Trie | Force-directed or tidy tree using `tree.children`; node radius by `weight`; terminal nodes as filled circles; highlight the `prefix` path and `suggestions` |
| Clipboard queue | Horizontal queue from `head` to `tail`; arrow showing enqueue at tail and dequeue at head; newest entry highlighted |
| Word count | Big live counter + sparkline of `stats.words` over `seq` |
| Cursor | Show `stats.cursor_line/col` and `selection_chars` |
| Autosave | Countdown ring from `autosave.due_in_seconds` |
| File | Title bar with `file.display_path`, `dirty`, `language` |

The bundled visualizer uses exactly this mapping in `dsa_visualizer/app.py`.
A different consumer can be a completely separate Python app using Tkinter,
Pygame, Rich, a web page, or a graph library.  It only needs `StateClient` (or
the JSON file) and the field names above.
