"""Pure data and layout helpers for the live visualizer.

This module deliberately has no Tkinter imports so it can be unit-tested on a
headless machine.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Iterable


# --------------------------------------------------------------------------- #
# Trie lens
# --------------------------------------------------------------------------- #


@dataclass
class LensNode:
    """A node in the limited trie view shown by the visualizer."""

    char: str
    word: str
    depth: int
    terminal: bool = False
    weight: int = 0
    kind: str | None = None
    children: list["LensNode"] = field(default_factory=list)
    on_path: bool = False
    suggested: bool = False
    is_prefix: bool = False
    x: float = 0.0
    y: float = 0.0

    def walk(self) -> Iterable["LensNode"]:
        yield self
        for child in self.children:
            yield from child.walk()

    def edges(self) -> Iterable[tuple["LensNode", "LensNode"]]:
        for child in self.children:
            yield self, child
            yield from child.edges()


@dataclass
class TrieLens:
    root: LensNode | None
    prefix: str
    matched_prefix: str
    suggestions: list[str]
    stats: dict[str, Any]
    truncated: bool
    node_count: int
    max_depth: int

    def walk(self) -> Iterable[LensNode]:
        if self.root is None:
            return iter(())
        return self.root.walk()

    def edges(self) -> Iterable[tuple[LensNode, LensNode]]:
        if self.root is None:
            return iter(())
        return self.root.edges()


def build_trie_lens(
    tree: dict[str, Any] | None,
    prefix: str,
    suggestions: Iterable[str] = (),
    *,
    max_nodes: int = 44,
    max_depth: int = 8,
    max_children: int = 6,
) -> TrieLens:
    """Build a small, prefix-focused view of a serialized trie.

    The editor's snapshot contains a depth-limited tree.  This function walks
    down the current prefix, keeps that path, and then shows a bounded number
    of branches below the prefix.  It never builds the full trie in memory.
    """

    suggestion_list = [word for word in suggestions if word]
    suggestion_set = set(suggestion_list)
    stats = (tree or {}).get("stats") if isinstance(tree, dict) else None
    stats = stats if isinstance(stats, dict) else {}

    if not isinstance(tree, dict) or not tree:
        return TrieLens(
            root=None,
            prefix=prefix,
            matched_prefix="",
            suggestions=suggestion_list,
            stats=stats,
            truncated=False,
            node_count=0,
            max_depth=0,
        )

    # Walk down the prefix, remembering each serialized node.
    path_dicts: list[dict[str, Any]] = [tree]
    current = tree
    matched = ""
    for char in prefix:
        children = current.get("children")
        child = children.get(char) if isinstance(children, dict) else None
        if not isinstance(child, dict):
            break
        current = child
        matched += char
        path_dicts.append(child)

    # Build the visible path root -> deepest matched node.
    path_nodes: list[LensNode] = []
    for depth, data in enumerate(path_dicts):
        node = LensNode(
            char="" if depth == 0 else str(data.get("char", matched[depth - 1])),
            word=matched[:depth],
            depth=depth,
            terminal=bool(data.get("terminal")),
            weight=int(data.get("weight") or 0),
            kind=data.get("kind"),
            on_path=True,
            suggested=matched[:depth] in suggestion_set,
            is_prefix=(depth == len(path_dicts) - 1 and matched == prefix),
        )
        path_nodes.append(node)
    for parent, child in zip(path_nodes, path_nodes[1:]):
        parent.children.append(child)

    root = path_nodes[0]
    focus = path_nodes[-1]
    focus_data = path_dicts[-1]
    budget = [max(0, max_nodes - len(path_nodes))]
    truncated = [False]

    def subtree_score(char: str, data: dict[str, Any], word: str) -> tuple[int, int, int, int]:
        candidate = word + char
        leads_to_suggestion = any(
            suggestion.startswith(candidate) for suggestion in suggestion_set
        )
        terminal = bool(data.get("terminal"))
        weight = int(data.get("weight") or 0)
        return (
            1 if leads_to_suggestion else 0,
            1 if terminal else 0,
            weight,
            -ord(char) if char else 0,
        )

    def add_children(
        parent: LensNode,
        data: dict[str, Any],
        *,
        depth: int,
    ) -> None:
        if budget[0] <= 0:
            if data.get("children"):
                truncated[0] = True
            return
        if depth >= max_depth:
            if data.get("children"):
                truncated[0] = True
            return

        children = data.get("children")
        if not isinstance(children, dict) or not children:
            return

        ordered = sorted(
            children.items(),
            key=lambda item: subtree_score(item[0], item[1], parent.word),
            reverse=True,
        )
        for char, child_data in ordered[:max_children]:
            if not isinstance(child_data, dict):
                continue
            if budget[0] <= 0:
                truncated[0] = True
                break
            word = parent.word + char
            child = LensNode(
                char=char,
                word=word,
                depth=depth + 1,
                terminal=bool(child_data.get("terminal")),
                weight=int(child_data.get("weight") or 0),
                kind=child_data.get("kind"),
                suggested=word in suggestion_set,
            )
            parent.children.append(child)
            budget[0] -= 1
            add_children(child, child_data, depth=depth + 1)

    add_children(focus, focus_data, depth=focus.depth)

    nodes = list(root.walk())
    max_seen_depth = max((node.depth for node in nodes), default=0)
    return TrieLens(
        root=root,
        prefix=prefix,
        matched_prefix=matched,
        suggestions=suggestion_list,
        stats=stats,
        truncated=truncated[0],
        node_count=len(nodes),
        max_depth=max_seen_depth,
    )


def layout_trie(
    lens: TrieLens,
    width: float,
    height: float,
    *,
    margin_x: float = 26.0,
    margin_y: float = 26.0,
) -> list[LensNode]:
    """Assign x/y coordinates to the lens nodes using a tidy tree layout."""

    if lens.root is None:
        return []

    nodes = list(lens.walk())
    margin_x = max(8.0, min(margin_x, max(8.0, width / 4.0)))
    margin_y = max(8.0, min(margin_y, max(8.0, height / 4.0)))

    leaf_index = 0

    def assign_leaf_positions(node: LensNode) -> None:
        nonlocal leaf_index
        if not node.children:
            node.x = float(leaf_index)
            leaf_index += 1
            return
        for child in node.children:
            assign_leaf_positions(child)
        node.x = (node.children[0].x + node.children[-1].x) / 2.0

    assign_leaf_positions(lens.root)
    leaves = max(1, leaf_index)
    max_depth = max((node.depth for node in nodes), default=0)

    if leaves == 1:
        for node in nodes:
            node.x = width / 2.0
    else:
        step = (width - 2.0 * margin_x) / (leaves - 1)
        for node in nodes:
            node.x = margin_x + node.x * step

    if max_depth == 0:
        for node in nodes:
            node.y = height / 2.0
    else:
        step = (height - 2.0 * margin_y) / max_depth
        for node in nodes:
            node.y = margin_y + node.depth * step

    return nodes


# --------------------------------------------------------------------------- #
# Stack / queue / chart helpers
# --------------------------------------------------------------------------- #


def stack_cards(entries: list[dict[str, Any]], max_items: int = 7) -> tuple[list[dict[str, Any]], int]:
    """Return the top ``max_items`` undo/redo entries and a hidden count."""

    entries = entries or []
    visible = entries[: max(1, max_items)]
    return visible, max(0, len(entries) - len(visible))


def clipboard_cards(entries: list[dict[str, Any]], max_items: int = 6) -> tuple[list[dict[str, Any]], int]:
    """Return the newest clipboard entries, oldest-first, plus a hidden count."""

    entries = entries or []
    visible = entries[-max(1, max_items) :] if entries else []
    return visible, max(0, len(entries) - len(visible))


def sparkline_points(
    values: Iterable[float],
    width: float,
    height: float,
    *,
    pad: float = 6.0,
) -> list[tuple[float, float]]:
    """Map values to canvas points, always inside the padded rectangle."""

    data = [float(value) for value in values]
    if not data:
        return []
    if width <= 2 * pad or height <= 2 * pad:
        return [(width / 2.0, height / 2.0)]

    low = min(data)
    high = max(data)
    if high == low:
        return [
            (pad + (width - 2 * pad) * (index / max(1, len(data) - 1)), height / 2.0)
            for index in range(len(data))
        ]

    points: list[tuple[float, float]] = []
    for index, value in enumerate(data):
        x = pad + (width - 2 * pad) * (index / max(1, len(data) - 1))
        y = height - pad - (height - 2 * pad) * ((value - low) / (high - low))
        points.append((x, y))
    return points


# --------------------------------------------------------------------------- #
# Formatting
# --------------------------------------------------------------------------- #


def format_age(seconds: float | int | None) -> str:
    if seconds is None:
        return "—"
    seconds = max(0.0, float(seconds))
    if seconds < 1.0:
        return "now"
    if seconds < 10.0:
        return f"{seconds:.1f}s ago"
    if seconds < 60.0:
        return f"{int(seconds)}s ago"
    if seconds < 3600.0:
        return f"{int(seconds // 60)}m ago"
    return f"{int(seconds // 3600)}h ago"


def format_bytes(size: int | float | None) -> str:
    if size is None:
        return "—"
    value = float(size)
    for unit in ("B", "KiB", "MiB", "GiB"):
        if value < 1024.0 or unit == "GiB":
            return f"{value:.0f} {unit}" if unit == "B" else f"{value:.1f} {unit}"
        value /= 1024.0
    return f"{value:.1f} GiB"


def format_int(value: int | float | None) -> str:
    if value is None:
        return "—"
    return f"{int(value):,}"


def shorten(text: str | None, limit: int = 60) -> str:
    if not text:
        return ""
    flat = " ".join(str(text).replace("\r", " ").replace("\n", " ").split())
    if len(flat) <= limit:
        return flat
    return flat[: max(1, limit - 1)] + "…"


def suggestion_words(trie: dict[str, Any] | None) -> list[str]:
    trie = trie or {}
    words: list[str] = []
    for item in trie.get("suggestions") or []:
        if isinstance(item, dict):
            word = item.get("word")
            if isinstance(word, str):
                words.append(word)
        elif isinstance(item, str):
            words.append(item)
    return words


# --------------------------------------------------------------------------- #
# Change tracking
# --------------------------------------------------------------------------- #


@dataclass(frozen=True)
class ChangeEvent:
    """A short, human-readable event derived from two snapshots."""

    seq: int
    kind: str
    text: str


class ChangeTracker:
    """Turn a stream of snapshots into events and chart series.

    The visualizer calls :meth:`update` once per new ``seq``.  The tracker
    compares the new snapshot with the previous one and emits events for the
    things a lecture audience cares about: stack pushes/pops, clipboard
    activity, saves and completion-prefix changes.
    """

    def __init__(self, *, history: int = 120, max_events: int = 8) -> None:
        self.history = max(4, int(history))
        self.max_events = max(1, int(max_events))
        self.events: list[ChangeEvent] = []
        self.word_series: list[int] = []
        self.undo_series: list[int] = []
        self.clipboard_series: list[int] = []
        self._previous: dict[str, Any] | None = None

    def reset(self) -> None:
        self.events.clear()
        self.word_series.clear()
        self.undo_series.clear()
        self.clipboard_series.clear()
        self._previous = None

    # ------------------------------------------------------------------ read
    def update(self, snapshot: dict[str, Any]) -> list[ChangeEvent]:
        previous = self._previous
        self._previous = snapshot
        seq = int(snapshot.get("seq") or 0)

        words = int((snapshot.get("stats") or {}).get("words") or 0)
        undo = snapshot.get("undo") or {}
        undo_depth = int((undo.get("undo_stack") or {}).get("depth") or 0)
        redo_depth = int((undo.get("redo_stack") or {}).get("depth") or 0)
        clipboard = snapshot.get("clipboard") or {}
        clip_size = int(clipboard.get("size") or 0)

        self.word_series.append(words)
        self.undo_series.append(undo_depth)
        self.clipboard_series.append(clip_size)
        for series in (self.word_series, self.undo_series, self.clipboard_series):
            del series[: max(0, len(series) - self.history)]

        if previous is None:
            return []

        produced: list[ChangeEvent] = []

        def emit(kind: str, text: str) -> None:
            produced.append(ChangeEvent(seq=seq, kind=kind, text=text))

        prev_undo = previous.get("undo") or {}
        prev_undo_depth = int((prev_undo.get("undo_stack") or {}).get("depth") or 0)
        prev_redo_depth = int((prev_undo.get("redo_stack") or {}).get("depth") or 0)

        if undo_depth > prev_undo_depth:
            entries = (undo.get("undo_stack") or {}).get("entries") or []
            label = entries[0].get("label", "edit") if entries else "edit"
            emit("undo-push", f"undo push · {label}")
        elif undo_depth < prev_undo_depth:
            emit("undo-pop", "undo pop · state restored")

        if redo_depth > prev_redo_depth:
            emit("redo-push", "redo push")
        elif redo_depth < prev_redo_depth:
            emit("redo-pop", "redo pop · state restored")

        prev_clip = previous.get("clipboard") or {}
        prev_tail = prev_clip.get("tail")
        tail = clipboard.get("tail")
        if tail is not None and tail != prev_tail:
            entries = clipboard.get("entries") or []
            preview = entries[-1].get("preview", "") if entries else ""
            emit("clipboard", f"clipboard enqueue · {shorten(preview, 42) or 'entry'}")
        elif clip_size < int(prev_clip.get("size") or 0):
            emit("clipboard", "clipboard evicted oldest entry")

        prev_saved = (previous.get("file") or {}).get("last_saved")
        saved = (snapshot.get("file") or {}).get("last_saved")
        if saved is not None and saved != prev_saved:
            name = (snapshot.get("file") or {}).get("name", "file")
            emit("save", f"saved {name}")

        prev_autosave = (previous.get("autosave") or {}).get("enabled")
        autosave = (snapshot.get("autosave") or {}).get("enabled")
        if prev_autosave is not None and autosave != prev_autosave:
            emit("autosave", f"autosave {'enabled' if autosave else 'disabled'}")

        prev_prefix = str((previous.get("trie") or {}).get("prefix") or "")
        prefix = str((snapshot.get("trie") or {}).get("prefix") or "")
        if prefix != prev_prefix and prefix:
            suggestions = (snapshot.get("trie") or {}).get("suggestions") or []
            emit("trie", f"trie prefix {prefix!r} → {len(suggestions)} suggestion(s)")

        if snapshot.get("status") == "closed" and previous.get("status") != "closed":
            emit("closed", "editor closed — showing final state")

        if produced:
            self.events = (produced + self.events)[: self.max_events]
        return produced

    def latest(self, limit: int | None = None) -> list[ChangeEvent]:
        return self.events[: (limit or self.max_events)]
