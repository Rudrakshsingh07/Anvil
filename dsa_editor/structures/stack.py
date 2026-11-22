"""Hand-written bounded stack plus the editor's snapshot-based undo/redo engine.

The undo engine deliberately does not use :mod:`prompt_toolkit`'s internal
undo stack.  It owns two explicit stacks so the lecture demo can show them:

    undo stack:  bottom ... [oldest snapshot] ... [newest snapshot] -> top
    redo stack:  bottom ... [oldest snapshot] ... [newest snapshot] -> top

A snapshot stores the complete document text *before* an edit.  That makes
restoring state trivial and keeps the structure independent from the TUI
library.  The number of snapshots and the total number of stored characters are
both bounded, so a long editing session cannot grow without limit.
"""

from __future__ import annotations

import threading
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Generic, TypeVar

T = TypeVar("T")


def compute_diff(old: str, new: str) -> tuple[int, str, str]:
    """Return ``(position, deleted_text, inserted_text)`` for a single edit.

    This is a cheap common-prefix/common-suffix diff.  It is enough to describe
    normal editor operations (typing, backspace, paste, cut, replace) for the
    undo metadata and for the visualizer.
    """

    if old == new:
        return 0, "", ""

    max_prefix = min(len(old), len(new))
    prefix = 0
    while prefix < max_prefix and old[prefix] == new[prefix]:
        prefix += 1

    max_suffix = min(len(old), len(new)) - prefix
    suffix = 0
    while (
        suffix < max_suffix
        and old[len(old) - 1 - suffix] == new[len(new) - 1 - suffix]
    ):
        suffix += 1

    old_end = len(old) - suffix
    new_end = len(new) - suffix
    return prefix, old[prefix:old_end], new[prefix:new_end]


class BoundedStack(Generic[T]):
    """A stack with a maximum size.

    ``push`` returns the item that was evicted from the bottom (or ``None``).
    This lets the undo manager keep an accurate character count without
    scanning every snapshot on each keystroke.
    """

    def __init__(self, capacity: int = 200) -> None:
        if capacity < 1:
            raise ValueError("capacity must be >= 1")
        self.capacity = int(capacity)
        self._items: deque[T] = deque()

    def push(self, item: T) -> T | None:
        dropped: T | None = None
        if len(self._items) >= self.capacity:
            dropped = self._items.popleft()
        self._items.append(item)
        return dropped

    def pop(self) -> T | None:
        if not self._items:
            return None
        return self._items.pop()

    def peek(self) -> T | None:
        return self._items[-1] if self._items else None

    def popleft(self) -> T | None:
        """Remove the oldest item (bottom of the stack)."""
        return self._items.popleft() if self._items else None

    def clear(self) -> None:
        self._items.clear()

    def items(self) -> list[T]:
        """Newest first (stack top at index 0)."""
        return list(reversed(self._items))

    def bottom_first(self) -> list[T]:
        return list(self._items)

    def __len__(self) -> int:
        return len(self._items)

    def __bool__(self) -> bool:
        return bool(self._items)

    def __iter__(self):
        return iter(self._items)

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return f"BoundedStack(capacity={self.capacity}, size={len(self)})"


@dataclass
class Snapshot:
    """A document state captured immediately before an edit."""

    text: str
    cursor: int = 0
    label: str = "edit"
    kind: str = "edit"
    timestamp: float = field(default_factory=time.time)
    position: int = 0
    inserted: str = ""
    deleted: str = ""
    edit_count: int = 1
    last_timestamp: float = field(default_factory=time.time)

    @property
    def char_count(self) -> int:
        return len(self.text)

    def preview(self, limit: int = 72) -> str:
        flat = " ".join(self.text.split())
        if len(flat) <= limit:
            return flat
        return flat[:limit] + "…"

    def to_dict(self, now: float | None = None, preview_chars: int = 72) -> dict:
        now = time.time() if now is None else now
        return {
            "label": self.label,
            "kind": self.kind,
            "cursor": self.cursor,
            "chars": self.char_count,
            "text_preview": self.preview(preview_chars),
            "inserted_preview": _clip(self.inserted, 120),
            "deleted_preview": _clip(self.deleted, 120),
            "edit_count": self.edit_count,
            "timestamp": round(self.timestamp, 3),
            "last_timestamp": round(self.last_timestamp, 3),
            "age_seconds": round(max(0.0, now - self.timestamp), 2),
        }


def _clip(text: str, limit: int) -> str:
    flat = text.replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t")
    return flat if len(flat) <= limit else flat[:limit] + "…"


class UndoRedoManager:
    """Two bounded stacks with edit coalescing.

    Consecutive typing (or consecutive backspacing) is merged into one undo
    unit.  Moving the cursor breaks the group, so the undo granularity feels
    like a normal editor while still being a visible stack operation.
    """

    def __init__(
        self,
        capacity: int = 150,
        *,
        group_window: float = 0.9,
        max_total_chars: int = 4_000_000,
    ) -> None:
        self.capacity = int(capacity)
        self.group_window = float(group_window)
        self.max_total_chars = int(max_total_chars)

        self.undo_stack: BoundedStack[Snapshot] = BoundedStack(capacity)
        self.redo_stack: BoundedStack[Snapshot] = BoundedStack(capacity)

        self._undo_chars = 0
        self._redo_chars = 0
        self._group_key: str | None = None
        self._group_time = 0.0
        self._lock = threading.RLock()

    # ------------------------------------------------------------------ edits
    def record(
        self,
        old_text: str,
        old_cursor: int,
        new_text: str,
        new_cursor: int,
        *,
        label: str | None = None,
        now: float | None = None,
    ) -> bool:
        """Record the pre-edit state.

        Returns ``True`` when a new snapshot was pushed, ``False`` when the
        change was coalesced into the current group or was a no-op.
        """

        if old_text == new_text:
            return False

        mono = time.monotonic() if now is None else now
        wall = time.time()
        position, deleted, inserted = compute_diff(old_text, new_text)

        if inserted and not deleted:
            kind = "insert"
        elif deleted and not inserted:
            kind = "delete"
        else:
            kind = "replace"

        if label is None:
            label = kind

        # Only plain, non-whitespace edits coalesce.  Newlines, pastes and
        # replacements become their own undo step.
        key: str | None = None
        if kind == "insert" and inserted and not inserted.isspace():
            key = "insert"
        elif kind == "delete" and deleted and not deleted.isspace():
            key = "delete"

        with self._lock:
            if (
                key is not None
                and key == self._group_key
                and (mono - self._group_time) <= self.group_window
            ):
                top = self.undo_stack.peek()
                if top is not None:
                    # Keep the stored text (state before the group) but grow
                    # the human-readable metadata for the visualizer.
                    if kind == "insert":
                        top.inserted = (top.inserted + inserted)[:400]
                    else:
                        top.deleted = (deleted + top.deleted)[:400]
                    top.edit_count += 1
                    top.last_timestamp = wall
                    top.label = label
                    top.kind = kind
                    self._group_time = mono
                    return False

            snap = Snapshot(
                text=old_text,
                cursor=max(0, min(old_cursor, len(old_text))),
                label=label,
                kind=kind,
                timestamp=wall,
                position=position,
                inserted=inserted[:400],
                deleted=deleted[:400],
                edit_count=1,
                last_timestamp=wall,
            )
            dropped = self.undo_stack.push(snap)
            self._undo_chars += snap.char_count
            if dropped is not None:
                self._undo_chars -= dropped.char_count
            self._clear_redo()
            self._trim_undo()

            self._group_key = key
            self._group_time = mono
            return True

    def break_group(self) -> None:
        """End the current coalescing group (e.g. after a cursor move)."""

        with self._lock:
            self._group_key = None
            self._group_time = 0.0

    # --------------------------------------------------------------- history
    def undo(self, current_text: str, current_cursor: int) -> Snapshot | None:
        with self._lock:
            snap = self.undo_stack.pop()
            if snap is None:
                return None
            self._undo_chars -= snap.char_count

            redo_snap = Snapshot(
                text=current_text,
                cursor=max(0, min(current_cursor, len(current_text))),
                label=snap.label,
                kind=snap.kind,
                timestamp=time.time(),
                position=snap.position,
                inserted=snap.inserted,
                deleted=snap.deleted,
                edit_count=snap.edit_count,
            )
            dropped = self.redo_stack.push(redo_snap)
            self._redo_chars += redo_snap.char_count
            if dropped is not None:
                self._redo_chars -= dropped.char_count
            self._trim_redo()
            self.break_group()
            return snap

    def redo(self, current_text: str, current_cursor: int) -> Snapshot | None:
        with self._lock:
            snap = self.redo_stack.pop()
            if snap is None:
                return None
            self._redo_chars -= snap.char_count

            undo_snap = Snapshot(
                text=current_text,
                cursor=max(0, min(current_cursor, len(current_text))),
                label=snap.label,
                kind=snap.kind,
                timestamp=time.time(),
                position=snap.position,
                inserted=snap.inserted,
                deleted=snap.deleted,
                edit_count=snap.edit_count,
            )
            dropped = self.undo_stack.push(undo_snap)
            self._undo_chars += undo_snap.char_count
            if dropped is not None:
                self._undo_chars -= dropped.char_count
            self._trim_undo()
            self.break_group()
            return snap

    def reset(self) -> None:
        with self._lock:
            self.undo_stack.clear()
            self.redo_stack.clear()
            self._undo_chars = 0
            self._redo_chars = 0
            self.break_group()

    def _clear_redo(self) -> None:
        self.redo_stack.clear()
        self._redo_chars = 0

    def _trim_undo(self) -> None:
        while self._undo_chars > self.max_total_chars and len(self.undo_stack) > 1:
            old = self.undo_stack.popleft()
            if old is None:
                break
            self._undo_chars -= old.char_count

    def _trim_redo(self) -> None:
        while self._redo_chars > self.max_total_chars and len(self.redo_stack) > 1:
            old = self.redo_stack.popleft()
            if old is None:
                break
            self._redo_chars -= old.char_count

    # ------------------------------------------------------------------ info
    @property
    def can_undo(self) -> bool:
        with self._lock:
            return bool(self.undo_stack)

    @property
    def can_redo(self) -> bool:
        with self._lock:
            return bool(self.redo_stack)

    @property
    def undo_depth(self) -> int:
        with self._lock:
            return len(self.undo_stack)

    @property
    def redo_depth(self) -> int:
        with self._lock:
            return len(self.redo_stack)

    def to_dict(self, preview_chars: int = 72) -> dict:
        """Serializable view for the visualizer.

        ``entries`` are ordered newest-first (index 0 is the stack top).
        Full document snapshots are intentionally not exposed; previews and
        metadata are enough to draw the stacks.
        """

        now = time.time()
        with self._lock:
            undo_entries = [s.to_dict(now, preview_chars) for s in self.undo_stack.items()]
            redo_entries = [s.to_dict(now, preview_chars) for s in self.redo_stack.items()]
            return {
                "undo_stack": {
                    "depth": len(self.undo_stack),
                    "capacity": self.undo_stack.capacity,
                    "total_chars": self._undo_chars,
                    "top_first": True,
                    "entries": undo_entries,
                },
                "redo_stack": {
                    "depth": len(self.redo_stack),
                    "capacity": self.redo_stack.capacity,
                    "total_chars": self._redo_chars,
                    "top_first": True,
                    "entries": redo_entries,
                },
                "can_undo": bool(self.undo_stack),
                "can_redo": bool(self.redo_stack),
                "group": {
                    "active": self._group_key is not None,
                    "kind": self._group_key,
                },
                "policy": {
                    "snapshot_based": True,
                    "group_window_seconds": self.group_window,
                    "max_total_chars": self.max_total_chars,
                },
            }
