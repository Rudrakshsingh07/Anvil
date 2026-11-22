"""Thread-safe state hub shared by the editor and the HTTP bridge."""

from __future__ import annotations

import threading
import time
from typing import Any

SCHEMA = "dsa-editor/state"
SCHEMA_VERSION = 1


class StateHub:
    """Stores the latest immutable-ish snapshot and wakes waiting readers.

    The UI thread publishes complete snapshots; HTTP threads only read them.
    That keeps the bridge lock-free with respect to editor internals while
    still supporting long-polling (``wait_since``).
    """

    def __init__(self) -> None:
        self._condition = threading.Condition(threading.RLock())
        self._seq = 0
        self._closed = False
        self._started = time.time()
        self._state: dict[str, Any] = {
            "schema": SCHEMA,
            "version": SCHEMA_VERSION,
            "seq": 0,
            "timestamp": time.time(),
            "status": "starting",
        }

    def publish(self, state: dict[str, Any]) -> dict[str, Any]:
        with self._condition:
            self._seq += 1
            snapshot = dict(state)
            snapshot["schema"] = SCHEMA
            snapshot["version"] = SCHEMA_VERSION
            snapshot["seq"] = self._seq
            snapshot["timestamp"] = time.time()
            self._state = snapshot
            self._condition.notify_all()
            return snapshot

    def latest(self, wait_since: int | None = None, timeout: float = 0.0) -> dict[str, Any]:
        """Return the latest state, optionally waiting for a newer sequence."""

        deadline = time.monotonic() + max(0.0, timeout)
        with self._condition:
            if wait_since is not None:
                while not self._closed and self._seq <= wait_since:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        break
                    self._condition.wait(remaining)
            return self._state

    def close(self) -> None:
        with self._condition:
            self._closed = True
            closed_state = dict(self._state)
            closed_state["status"] = "closed"
            closed_state["timestamp"] = time.time()
            self._state = closed_state
            self._condition.notify_all()

    @property
    def seq(self) -> int:
        with self._condition:
            return self._seq

    @property
    def closed(self) -> bool:
        with self._condition:
            return self._closed

    @property
    def started_at(self) -> float:
        return self._started

    def health(self) -> dict[str, Any]:
        with self._condition:
            return {
                "ok": not self._closed,
                "schema": SCHEMA,
                "version": SCHEMA_VERSION,
                "seq": self._seq,
                "closed": self._closed,
                "uptime_seconds": round(time.time() - self._started, 1),
            }
