"""Live snapshot source for the visualizer.

The visualizer never talks to the editor's internals.  It consumes the public
state bridge:

1. HTTP long-polling against ``/state`` (preferred, low latency);
2. fallback to the atomic JSON state file when the server is unavailable.

A background thread reads snapshots and pushes them into a queue, so Tkinter
only ever touches its own widgets from the main thread.
"""

from __future__ import annotations

import json
import queue
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from dsa_editor.bridge import (
    StateClient,
    StateError,
    default_state_file,
    read_state_file,
)


@dataclass
class SourceStatus:
    mode: str = "connecting"
    message: str = "looking for the editor"
    seq: int | None = None
    port: int = 8765

    @property
    def label(self) -> str:
        return {
            "http": "live",
            "file": "state file",
            "closed": "editor closed",
            "waiting": "waiting",
            "connecting": "connecting",
            "paused": "paused",
            "error": "error",
        }.get(self.mode, self.mode)


class LiveSource:
    """Background reader for editor snapshots.

    ``queue`` yields ``(kind, payload, mode)`` tuples where ``kind`` is
    ``"state"`` for a complete snapshot or ``"trie"`` for a deeper on-demand
    trie tree.
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 8765,
        *,
        state_file: str | Path | None = None,
        interval: float = 0.25,
        deep_trie: bool = True,
        deep_trie_depth: int = 6,
        deep_trie_limit: int = 1600,
    ) -> None:
        self.host = host
        self.port = int(port)
        self.state_file = Path(state_file) if state_file else default_state_file()
        self.interval = max(0.05, float(interval))
        self.deep_trie = deep_trie
        self.deep_trie_depth = deep_trie_depth
        self.deep_trie_limit = deep_trie_limit

        self.queue: queue.Queue[tuple[str, Any, str]] = queue.Queue()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()
        self._status = SourceStatus(port=self.port)

    # ------------------------------------------------------------------ life
    def start(self) -> None:
        if self._thread is not None and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(
            target=self._run, name="dsa-visualizer-source", daemon=True
        )
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=1.5)
            self._thread = None

    def restart(self) -> None:
        self.stop()
        with self.queue.mutex:
            self.queue.queue.clear()
        self._set_status("connecting", "reconnecting", None)
        self.start()

    def status(self) -> SourceStatus:
        with self._lock:
            return SourceStatus(
                mode=self._status.mode,
                message=self._status.message,
                seq=self._status.seq,
                port=self._status.port,
            )

    def _set_status(self, mode: str, message: str, seq: int | None) -> None:
        with self._lock:
            self._status = SourceStatus(
                mode=mode, message=message, seq=seq, port=self.port
            )

    # ------------------------------------------------------------------ read
    def _run(self) -> None:
        client = StateClient(self.host, self.port, timeout=max(1.0, self.interval + 0.5))
        last_seq: int | None = None
        last_prefix: str | None = None
        mode = "http"
        next_http_retry = 0.0

        while not self._stop.is_set():
            if mode == "http":
                try:
                    snapshot = client.state(
                        wait=last_seq is not None,
                        since=last_seq,
                        timeout=self.interval,
                    )
                except StateError:
                    mode = "file"
                    next_http_retry = time.monotonic() + 2.0
                    self._set_status(
                        "file",
                        f"no HTTP on :{self.port} — reading {self.state_file.name}",
                        last_seq,
                    )
                    continue

                seq = snapshot.get("seq")
                if seq != last_seq:
                    last_seq = seq
                    closed = snapshot.get("status") == "closed"
                    self._set_status(
                        "closed" if closed else "http",
                        "editor closed — last snapshot" if closed else f"live on :{self.port}",
                        seq,
                    )
                    self.queue.put(("state", snapshot, "http"))

                    prefix = str((snapshot.get("trie") or {}).get("prefix") or "")
                    if self.deep_trie and prefix != last_prefix:
                        last_prefix = prefix
                        try:
                            tree = client.trie(
                                depth=self.deep_trie_depth,
                                limit=self.deep_trie_limit,
                            )
                        except StateError:
                            tree = None
                        if tree is not None and not self._stop.is_set():
                            self.queue.put(("trie", tree, "http"))
                continue

            # ------------------------------------------------------ file mode
            snapshot = self._read_file()
            if snapshot is not None:
                seq = snapshot.get("seq")
                if seq != last_seq:
                    last_seq = seq
                    closed = snapshot.get("status") == "closed"
                    self._set_status(
                        "closed" if closed else "file",
                        "state file is stale or editor is closed"
                        if closed
                        else f"reading {self.state_file}",
                        seq,
                    )
                    self.queue.put(("state", snapshot, "file"))
                    last_prefix = str((snapshot.get("trie") or {}).get("prefix") or "")
            else:
                self._set_status(
                    "waiting",
                    f"waiting for {self.state_file}",
                    last_seq,
                )

            self._stop.wait(self.interval)

            if time.monotonic() >= next_http_retry:
                next_http_retry = time.monotonic() + 2.0
                try:
                    client.health()
                except StateError:
                    continue
                mode = "http"
                last_seq = None  # force a fresh snapshot
                self._set_status("http", f"live on :{self.port}", None)

        self._set_status("closed", "stopped", self._status.seq)

    def _read_file(self) -> dict[str, Any] | None:
        try:
            snapshot = read_state_file(self.state_file)
        except (OSError, ValueError, json.JSONDecodeError):
            return None
        return snapshot if isinstance(snapshot, dict) else None
