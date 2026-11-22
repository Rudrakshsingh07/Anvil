"""Bounded FIFO clipboard-history queue.

The queue is the retention structure for copied/cut text.  The newest entry is
at the tail, the oldest at the head.  Pasting normally uses the tail (most
recent), while ``dequeue`` demonstrates true FIFO behaviour.
"""

from __future__ import annotations

import threading
import time
from collections import deque
from dataclasses import dataclass, field


@dataclass
class ClipEntry:
    id: int
    text: str
    timestamp: float = field(default_factory=time.time)

    @property
    def chars(self) -> int:
        return len(self.text)

    @property
    def lines(self) -> int:
        return self.text.count("\n") + 1

    def preview(self, limit: int = 60) -> str:
        flat = " ".join(self.text.split())
        if len(flat) <= limit:
            return flat
        return flat[:limit] + "…"

    def to_dict(self, now: float | None = None) -> dict:
        now = time.time() if now is None else now
        return {
            "id": self.id,
            "text": self.text,
            "preview": self.preview(),
            "chars": self.chars,
            "lines": self.lines,
            "timestamp": round(self.timestamp, 3),
            "age_seconds": round(max(0.0, now - self.timestamp), 2),
        }


class ClipboardHistory:
    def __init__(self, capacity: int = 50) -> None:
        if capacity < 1:
            raise ValueError("capacity must be >= 1")
        self.capacity = int(capacity)
        self._items: deque[ClipEntry] = deque()
        self._next_id = 1
        self._lock = threading.RLock()

    def enqueue(self, text: str) -> ClipEntry | None:
        """Add text at the tail.  Empty text is ignored.

        If the current tail is identical, its timestamp is refreshed instead
        of adding a duplicate entry.
        """

        if not text:
            return None
        with self._lock:
            if self._items and self._items[-1].text == text:
                entry = self._items[-1]
                entry.timestamp = time.time()
                return entry

            entry = ClipEntry(id=self._next_id, text=text, timestamp=time.time())
            self._next_id += 1
            if len(self._items) >= self.capacity:
                self._items.popleft()
            self._items.append(entry)
            return entry

    def dequeue(self) -> ClipEntry | None:
        """Remove and return the oldest entry (FIFO)."""

        with self._lock:
            return self._items.popleft() if self._items else None

    def latest(self) -> ClipEntry | None:
        with self._lock:
            return self._items[-1] if self._items else None

    def oldest(self) -> ClipEntry | None:
        with self._lock:
            return self._items[0] if self._items else None

    def get(self, index_from_newest: int = 0) -> ClipEntry | None:
        with self._lock:
            if 0 <= index_from_newest < len(self._items):
                return self._items[-1 - index_from_newest]
            return None

    def clear(self) -> None:
        with self._lock:
            self._items.clear()

    def __len__(self) -> int:
        with self._lock:
            return len(self._items)

    def __bool__(self) -> bool:
        with self._lock:
            return bool(self._items)

    def oldest_first(self) -> list[ClipEntry]:
        with self._lock:
            return list(self._items)

    def newest_first(self) -> list[ClipEntry]:
        with self._lock:
            return list(reversed(self._items))

    def to_dict(self) -> dict:
        now = time.time()
        with self._lock:
            entries = [entry.to_dict(now) for entry in self._items]
            return {
                "capacity": self.capacity,
                "size": len(entries),
                "order": "oldest_first",
                "head": entries[0]["id"] if entries else None,
                "tail": entries[-1]["id"] if entries else None,
                "entries": entries,
            }
