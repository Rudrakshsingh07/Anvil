"""Client helpers for the separate visualizer process.

Two ways to read live state:

1. HTTP (preferred, live)::

       from dsa_editor.bridge import StateClient

       client = StateClient(port=8765)
       for snapshot in client.watch():
           draw(snapshot["undo"], snapshot["trie"], snapshot["clipboard"])

2. JSON file fallback::

       from dsa_editor.bridge import read_state_file, default_state_file

       snapshot = read_state_file(default_state_file())
"""

from __future__ import annotations

import json
import os
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Iterator

DEFAULT_PORT = 8765


def default_state_file() -> Path:
    base = os.environ.get("XDG_STATE_HOME")
    if base:
        root = Path(base)
    else:
        root = Path.home() / ".local" / "state"
    return root / "dsa-editor" / "state.json"


def read_state_file(path: str | Path | None = None) -> dict[str, Any]:
    target = Path(path) if path is not None else default_state_file()
    with target.open("r", encoding="utf-8") as handle:
        return json.load(handle)


class StateError(RuntimeError):
    pass


class StateClient:
    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = DEFAULT_PORT,
        *,
        timeout: float = 3.0,
    ) -> None:
        self.host = host
        self.port = int(port)
        self.timeout = float(timeout)

    @property
    def base_url(self) -> str:
        return f"http://{self.host}:{self.port}"

    def _get(self, path: str, timeout: float | None = None) -> dict[str, Any]:
        url = self.base_url + path
        try:
            with urllib.request.urlopen(url, timeout=timeout or self.timeout) as response:
                data = response.read()
        except (urllib.error.URLError, OSError) as exc:
            raise StateError(f"cannot read {url}: {exc}") from exc
        try:
            payload = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise StateError(f"invalid JSON from {url}: {exc}") from exc
        if not isinstance(payload, dict):
            raise StateError(f"unexpected payload from {url}")
        return payload

    def health(self) -> dict[str, Any]:
        return self._get("/health")

    def state(self, *, wait: bool = False, since: int | None = None, timeout: float = 2.0) -> dict[str, Any]:
        query = []
        if wait:
            query.append("wait=1")
        if since is not None:
            query.append(f"since={int(since)}")
        if wait:
            query.append(f"timeout={float(timeout):.2f}")
        suffix = "?" + "&".join(query) if query else ""
        return self._get("/state" + suffix, timeout=timeout + 2.0 if wait else None)

    def trie(self, *, depth: int = 5, limit: int = 1200) -> dict[str, Any]:
        return self._get(f"/trie?depth={int(depth)}&limit={int(limit)}")

    def undo(self) -> dict[str, Any]:
        return self._get("/undo")

    def clipboard(self) -> dict[str, Any]:
        return self._get("/clipboard")

    def stats(self) -> dict[str, Any]:
        return self._get("/stats")

    def watch(
        self,
        *,
        interval: float = 0.25,
        long_poll: bool = True,
        stop_after: float | None = None,
    ) -> Iterator[dict[str, Any]]:
        """Yield snapshots whenever the editor publishes a new sequence.

        With ``long_poll=True`` the server blocks up to ``interval`` seconds,
        so the visualizer reacts quickly without busy polling.
        """

        started = time.monotonic()
        last_seq: int | None = None
        while True:
            if stop_after is not None and time.monotonic() - started > stop_after:
                return
            snapshot = self.state(
                wait=long_poll and last_seq is not None,
                since=last_seq,
                timeout=interval,
            )
            seq = snapshot.get("seq")
            if seq != last_seq:
                last_seq = seq
                yield snapshot
            elif not long_poll:
                time.sleep(interval)
