# DSA notes for the lecture

This document maps the editor's behaviour to the data structures in `src/`.
Everything is hand-written in C11; the terminal renderer in `src/ui.c` only
draws the screen and decodes keys.

## 1. Undo / redo — two bounded stacks

**File:** `src/undo.c` (types in `src/anvil.h`)

### Representation

```
undo stack (top at right)                 redo stack (top at right)
+---------+---------+---------+           +---------+
| snap 1  | snap 2  | snap 3  |           | snap X  |
+---------+---------+---------+           +---------+
          bottom      top                        top
```

`SnapshotStack` is a dynamic array with a logical top at `len - 1`:

| Operation | Meaning | Complexity |
|---|---|---|
| `push(x)` | Add to the top; evicts the bottom when over capacity | O(1) amortised |
| `pop()` | Remove and return the top | O(1) |
| `peek()` | Read the top | O(1) |
| `trim()` | Drop the bottom (used for memory trimming) | O(1) per snapshot |

`Snapshot` stores the **document text before the edit**, the cursor position,
the change position, inserted/deleted previews, a label, a timestamp and an
`edit_count` (how many keystrokes were merged into this undo unit).

`UndoRedo` invariants:

* The undo stack contains states from the past; the top is the most recent
  pre-edit state.
* The redo stack is cleared whenever a new edit is recorded.
* `undo()` pops the undo stack, pushes the *current* state onto the redo stack,
  and returns the popped snapshot for the UI to restore.
* `redo()` is symmetric.
* Both stacks are bounded by `capacity` (default 150) **and** by
  `max_total_chars` (default 4,000,000) so snapshots cannot exhaust memory.
* Typing/backspacing coalescing: consecutive non-whitespace inserts (or deletes)
  within `group_window` (0.9 s) extend the current undo unit instead of pushing
  a new snapshot.  A cursor move calls `undo_break_group()`.

### Why snapshots instead of deltas?

Snapshot restore is trivially correct: set the buffer text and cursor.  A delta
stack would need careful inverse operations for every edit path (selection
replace, paste, auto-indent, multi-line cut).  For a lecture, the snapshot
version is easier to explain and to visualize; the memory bound keeps it safe.

### Demo talking points

* Type `hello` and press `Ctrl-Z` once: all five characters disappear because
  `edit_count` is 5.
* Move the cursor, type again: the second group becomes a separate stack entry.
* Undo, then type: the redo stack is cleared.
* Watch the panel's two depths move in real time.

## 2. Trie — prefix completion

**File:** `src/trie.c`

### Representation

Each `TrieNode` has:

```c
char ch;                 /* character on this edge */
TrieNode **children;     /* dynamic array of child pointers */
int nchild, cap;
int terminal;            /* is this the end of a word? */
int weight;              /* ranking (keyword 80, common 45, learned 8) */
char kind[16];           /* keyword / common / word / snippet */
```

The root is an empty-character node.  `print` and `private` share the path
`p → r → i`, then branch.

| Operation | Meaning | Complexity |
|---|---|---|
| `trie_insert(word)` | Add/update a word | O(L) |
| `trie_complete(prefix)` | All words under the prefix, ranked | O(L + K log K) |
| `trie_learn_text(text)` | Insert identifiers not already present | O(n) |
| `trie_bump(word)` | Raise a word's ranking after use | O(L) |
| `trie_to_json()` | JSON tree for the visualizer | O(nodes) |

`L` is word length, `K` the number of matches under the prefix, `n` the
document length.

### How the editor uses it

1. On open / `:lang`, the trie is rebuilt with the language's keywords and
   generic programming words (`src/keywords_data.h`).
2. Identifiers in the document are learned (`trie_learn_text`), at most once
   per second while typing and on save.  `only_if_new` prevents repeated text
   from inflating weights.
3. The completion popup asks `trie_complete(current_prefix)`.  The grey inline
   ghost text uses the best result.
4. Accepting a completion calls `trie_bump(word, 15)`, so frequently used words
   rank higher.

### Demo talking points

* Type `pri`: the popup shows `print` (keyword, weight 80) before `private`
   (common, weight 45).
* Type a new identifier such as `my_helper` twice; it becomes a learned word in
   the trie (panel's word count grows).
* Show `/trie` JSON and point out the shared prefix path and terminal flags.

## 3. Clipboard history — FIFO queue

**File:** `src/clipboard.c`

### Representation

`Clipboard` is a bounded dynamic array with a capacity (default 50):

```
head (oldest)                                        tail (newest)
+-------+    +-------+    +-------+    +-------+
| copy1 | -> | copy2 | -> | copy3 | -> | copy4 |
+-------+    +-------+    +-------+    +-------+
   ^ dequeue (FIFO)                       ^ enqueue / latest paste
```

| Operation | Meaning | Complexity |
|---|---|---|
| `clip_enqueue(text)` | Append at the tail; evict head if full | O(1) amortised |
| `clip_get(n)` | Index from the newest (`Alt-n`) | O(1) |
| `clip_size()` | Number of entries | O(1) |
| `clip_to_json()` | Oldest-first JSON list | O(size) |

Consecutive identical copies refresh the tail's timestamp instead of adding a
duplicate.  The queue is in-memory only; it is intentionally not persisted.

### Demo talking points

* Copy several different selections, then paste the newest with `Ctrl-V` and an
   older one with `Alt-2`.
* The panel shows the queue size and the newest entries; `/clipboard` exposes
   the same data with `head`/`tail` ids.

## 4. Live word count — linear scan

**File:** `src/buffer.c` (`compute_stats`)

```c
words = number of maximal non-whitespace runs;
chars = len;
chars_no_ws = chars - whitespace bytes;
lines = number of '\n' + 1;
```

This is an O(n) scan after each text change.  Cursor line/column are derived
from the cursor byte offset with `buf_line_col()`.  The status bar, panel and
bridge all read the cached result.

### Demo talking points

* Type a word and watch the counter change immediately.
* `/stats` shows the same numbers plus cursor and selection size.

## 5. How the structures reach the visualizer

```
Editor process (C)                          Visualizer (separate process)
┌───────────────────────────┐               ┌──────────────────────────┐
│ undo_to_json()            │               │  StateClient().watch()   │
│ trie_to_json()            │  snapshot     │      or read_state_file  │
│ clip_to_json()            │ ──────────►   │  draw stacks / trie /    │
│ editor_build_state()      │  HTTP/JSON    │  queue / counters        │
└───────────────────────────┘               └──────────────────────────┘
```

`Bridge` stores the latest complete JSON snapshot behind a mutex and condition
variable.  The HTTP threads serve it; the UI thread publishes a new snapshot
after edits.  `/trie` can build a deeper tree on demand; the trie's internal
lock makes that safe while the user types.

The full field-by-field contract is in [`state_api.md`](state_api.md).

## 6. Complexity summary

| Structure | Insert / push | Delete / pop | Search | Memory |
|---|---|---|---|---|
| Undo stack | O(1) | O(1) | O(1) top | O(k · n), bounded |
| Redo stack | O(1) | O(1) | O(1) top | O(k · n), bounded |
| Trie | O(L) | O(L) | O(L) / O(L + K log K) complete | O(total chars) |
| Clipboard queue | O(1) enqueue | O(1) evict | O(1) latest/get | O(capacity · size) |
| Word count | — | — | O(n) per change | O(1) |

`n` = document length, `L` = word/prefix length, `K` = completions under a
prefix, `k` = retained snapshots.

## 7. Possible exam questions

* Why does a stack model undo/redo?  Why does a new edit clear redo?
* Why is a queue the right model for clipboard *history*?  Which end is newest?
* Why is a trie better than a linear scan of a word list for prefix completion?
* What is the cost of the snapshot approach versus storing inverse operations?
* How does coalescing reduce the number of undo entries without losing
  correctness?
* How do you bound memory in each structure?
* Why must the HTTP bridge read a published snapshot instead of walking live
  mutable structures?
