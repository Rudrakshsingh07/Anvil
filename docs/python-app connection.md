# How the Python visualizer sees the C editor's live data

The editor (`anvil`, C11 + POSIX) and the visualizer (`anvil-viz`, Python +
Tkinter) are **separate OS processes with separate address spaces**, so one
cannot read the other's memory.

Instead, the editor continuously **serializes a copy of its structures to JSON**
and publishes it. The visualizer reads those snapshots over a local socket or
file. The only shared contract is the JSON schema in
[`state_api.md`](state_api.md).

## The handoff, step by step

```text
EDITOR PROCESS (C)                              VISUALIZER PROCESS (Python)
──────────────────                              ───────────────────────────
Buffer / UndoRedo / Trie / Clipboard
        │
        │  editor_build_state()  (UI thread)
        │  locks each structure, writes JSON
        ▼
   JSON snapshot (StrBuf)
        │
        ├──► bridge_publish() ──► HTTP threads ──► 127.0.0.1:8765/state
        │       mutex + cond broadcast                    │
        │                                                 │ long-poll
        └──► atomic state.json  (~1/sec)                  ▼
                                                    LiveSource thread
                                                           │ queue.Queue
                                                           ▼
                                                    Tk main thread
                                                    (_pump every 55 ms)
```

## 1. The editor builds a snapshot

On every edit, cursor move, save, toggle and at least once per second,
`editor_publish()` in `src/editor.c` calls `editor_build_state()`. That function
asks each module for a JSON view:

| Structure | What is copied |
|---|---|
| Undo/redo (`src/undo.c`) | depth, capacity, entry metadata and **72-character previews** — not the full document snapshots |
| Clipboard (`src/clipboard.c`) | every entry, including its full text |
| Trie (`src/trie.c`) | node/word counts, a depth-3 / 800-node tree, top words and current suggestions |
| Document | 240-character text preview, char/line/cursor/selection counts |
| File / autosave / app / settings / UI | metadata and status fields |

Each structure is read under its own `pthread_mutex_t`, so the snapshot is
consistent even if the HTTP threads are reading the trie concurrently.

## 2. The bridge publishes it in-process

`bridge_publish()` in `src/bridge.c`:

```c
pthread_mutex_lock(&b->lock);
free(b->json);
b->json = xstrdup(json);
b->seq = seq;
pthread_cond_broadcast(&b->cond);
pthread_mutex_unlock(&b->lock);
```

This is the handoff between the **TUI thread** and the **HTTP threads** inside
the editor process. Threads in the same process do share memory, so this part is
a normal mutex/condition-variable handoff.

## 3. The HTTP server exposes it

The bridge runs BSD-socket threads on `127.0.0.1:8765` (or the next free port).
For:

```text
GET /state
```

it returns the latest JSON string. For:

```text
GET /state?wait=1&since=42&timeout=0.25
```

the request thread blocks on the condition variable until `seq > 42` or the
timeout expires. That is the **long-poll**: the visualizer asks "wake me when
something changes" instead of busy-polling.

There is also `/trie?depth=6&limit=1600`, which builds a deeper tree on demand
under the trie's lock.

## 4. The JSON file is a fallback

`bridge_write_state_file()` writes the same snapshot atomically (temporary file
+ `fsync` + `rename`) at most once per second, plus a final `status: "closed"`
snapshot on exit. That lets the visualizer replay the last state even after the
editor is gone.

## 5. The visualizer reads snapshots

`dsa_visualizer/source.py` runs a background thread that either:

- calls `StateClient.state(wait=True, since=last_seq, timeout=0.25)` against
  HTTP, or
- falls back to `read_state_file()` when HTTP is unavailable.

`StateClient` lives in `dsa_visualizer/bridge.py` and uses only the standard
library (`urllib`, `json`). It does not import anything from the editor.

It pushes `(kind, payload, mode)` tuples into a `queue.Queue`. The Tkinter main
thread drains that queue in `_pump()` and redraws the canvases. Tk is
single-threaded, so the network thread never touches widgets directly.

## Why not share the actual memory?

- Separate processes have separate virtual address spaces; pointers are
  process-local.
- Even with `fork()`, the child gets copy-on-write pages and would not see later
  edits.
- Real shared memory (`shm_open` + a fixed binary layout) would require explicit
  locking, alignment and versioning, and would couple the visualizer to the
  editor's internal structs.
- JSON over HTTP was chosen because it is simple, read-only,
  language-independent, works across monitors/environments, and supports a file
  fallback.

## Freshness and safety

- **HTTP path:** an edit triggers a publish immediately, and a waiting long-poll
  wakes at once — typically tens of milliseconds.
- **File path:** at most ~1 second old.
- `seq` increments on every publish, so consumers can detect missed or duplicate
  updates.
- The bridge binds to `127.0.0.1` only and is read-only. Clipboard text is
  included, so do not expose the port to a network.
- Full undo snapshots are deliberately **not** exposed; only metadata and short
  previews are sent.

## Where this lives in the code

| Concern | File |
|---|---|
| Snapshot builder | `src/editor.c` (`editor_build_state`, `editor_publish`) |
| In-process handoff and long-poll condition | `src/bridge.c` |
| HTTP endpoints | `src/bridge.c` |
| Atomic JSON fallback | `src/bridge.c` (`bridge_write_state_file`) |
| Python client | `dsa_visualizer/bridge.py` |
| Visualizer reader thread | `dsa_visualizer/source.py` |
| Tkinter update pump | `dsa_visualizer/app.py` |

See also [`state_api.md`](state_api.md) for the full JSON contract and
[`visualizer.md`](visualizer.md) for the visualizer manual.
