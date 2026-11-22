"""Tiny read-only HTTP server exposing editor state to the visualizer.

Endpoints (all JSON, all GET, CORS enabled):

    /                    small HTML index
    /health              liveness + sequence number
    /state               complete snapshot
    /state?wait=1&since=N[&timeout=2]   long-poll for a newer snapshot
    /trie[?depth=5&limit=500]           trie tree on demand
    /undo /redo /clipboard /stats /file /autosave /ui
                         subsets of the latest snapshot

The server never mutates editor state.  It is designed for localhost demos.
"""

from __future__ import annotations

import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Callable
from urllib.parse import parse_qs, urlparse

from .hub import StateHub


class _Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class StateServer:
    def __init__(
        self,
        hub: StateHub,
        *,
        host: str = "127.0.0.1",
        port: int = 8765,
        trie_provider: Callable[[int, int], dict] | None = None,
    ) -> None:
        self.hub = hub
        self.host = host
        self.requested_port = int(port)
        self.port: int | None = None
        self.trie_provider = trie_provider
        self._httpd: _Server | None = None
        self._thread: threading.Thread | None = None
        self._started = time.time()

    @property
    def url(self) -> str:
        return f"http://{self.host}:{self.port}" if self.port else ""

    def start(self) -> int:
        handler = self._make_handler()
        last_error: OSError | None = None
        candidates = [self.requested_port] if self.requested_port == 0 else list(
            range(self.requested_port, self.requested_port + 10)
        ) + [0]

        for candidate in candidates:
            try:
                httpd = _Server((self.host, candidate), handler)
            except OSError as exc:
                last_error = exc
                continue
            self._httpd = httpd
            self.port = int(httpd.server_address[1])
            self._thread = threading.Thread(
                target=httpd.serve_forever,
                name="dsa-editor-state-server",
                daemon=True,
            )
            self._thread.start()
            return self.port

        raise OSError(f"could not bind state server: {last_error}")

    def stop(self) -> None:
        if self._httpd is not None:
            self._httpd.shutdown()
            self._httpd.server_close()
            self._httpd = None
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None

    # ------------------------------------------------------------------ HTTP
    def _make_handler(self):
        server = self

        class Handler(BaseHTTPRequestHandler):
            server_version = "DSAEditorState/1.0"

            def do_GET(self) -> None:  # noqa: N802 (stdlib naming)
                server._handle(self)

            def do_OPTIONS(self) -> None:  # noqa: N802
                self.send_response(204)
                self._cors()
                self.end_headers()

            def log_message(self, format: str, *args: Any) -> None:
                # Keep the TUI clean.
                pass

            def _cors(self) -> None:
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
                self.send_header("Access-Control-Allow-Headers", "Content-Type")

        return Handler

    def _handle(self, handler: BaseHTTPRequestHandler) -> None:
        parsed = urlparse(handler.path)
        path = parsed.path.rstrip("/") or "/"
        query = parse_qs(parsed.query)

        try:
            if path == "/":
                self._send_html(handler, self._index_html())
                return
            if path == "/favicon.ico":
                handler.send_response(204)
                handler.end_headers()
                return
            if path == "/health":
                health = self.hub.health()
                health.update({"port": self.port, "pid": os.getpid(), "uptime_seconds": round(time.time() - self._started, 1)})
                self._send_json(handler, health)
                return
            if path in ("/state", "/state.json", "/snapshot"):
                since = _as_int(query.get("since", [None])[0])
                wait = _as_bool(query.get("wait", ["0"])[0])
                timeout = min(5.0, max(0.0, _as_float(query.get("timeout", ["0"])[0])))
                state = self.hub.latest(wait_since=since if wait else None, timeout=timeout if wait else 0.0)
                self._send_json(handler, state)
                return
            if path == "/trie":
                if self.trie_provider is not None:
                    depth = min(12, max(1, _as_int(query.get("depth", ["4"])[0]) or 4))
                    limit = min(5000, max(10, _as_int(query.get("limit", ["1200"])[0]) or 1200))
                    data = self.trie_provider(depth, limit)
                else:
                    data = self.hub.latest().get("trie", {})
                data = dict(data)
                data["seq"] = self.hub.seq
                self._send_json(handler, data)
                return

            state = self.hub.latest()
            key_map = {
                "/undo": "undo",
                "/redo": "undo",
                "/clipboard": "clipboard",
                "/stats": "stats",
                "/file": "file",
                "/autosave": "autosave",
                "/ui": "ui",
                "/document": "document",
            }
            if path in key_map:
                payload = state.get(key_map[path], {})
                if path == "/redo":
                    payload = state.get("undo", {}).get("redo_stack", {})
                self._send_json(handler, {"seq": state.get("seq"), "timestamp": state.get("timestamp"), path.lstrip("/"): payload})
                return

            self._send_json(handler, {"error": "not found", "path": path}, status=404)
        except (BrokenPipeError, ConnectionResetError):
            return
        except Exception as exc:  # pragma: no cover - defensive boundary
            try:
                self._send_json(handler, {"error": str(exc)}, status=500)
            except Exception:
                pass

    def _send_json(self, handler: BaseHTTPRequestHandler, payload: Any, status: int = 200) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        handler.send_response(status)
        handler.send_header("Content-Type", "application/json; charset=utf-8")
        handler.send_header("Content-Length", str(len(body)))
        handler.send_header("Cache-Control", "no-store")
        handler._cors()  # type: ignore[attr-defined]
        handler.end_headers()
        handler.wfile.write(body)

    def _send_html(self, handler: BaseHTTPRequestHandler, html: str) -> None:
        body = html.encode("utf-8")
        handler.send_response(200)
        handler.send_header("Content-Type", "text/html; charset=utf-8")
        handler.send_header("Content-Length", str(len(body)))
        handler._cors()  # type: ignore[attr-defined]
        handler.end_headers()
        handler.wfile.write(body)

    def _index_html(self) -> str:
        state = self.hub.latest()
        file_info = state.get("file", {})
        stats = state.get("stats", {})
        undo = state.get("undo", {})
        name = _escape(str(file_info.get("name", "untitled")))
        return f"""<!doctype html>
<html><head><meta charset="utf-8"><title>DSA Editor state bridge</title>
<style>
body {{ font-family: ui-monospace, monospace; background:#0b1220; color:#e5e7eb; padding:2rem; }}
a {{ color:#38bdf8; }} code {{ background:#111827; padding:.15rem .35rem; border-radius:4px; }}
</style></head><body>
<h1>DSA Editor · state bridge</h1>
<p>Live file: <b>{name}</b> · seq {state.get("seq")} · words {stats.get("words", 0)} ·
undo {undo.get("undo_stack", {}).get("depth", 0)} / redo {undo.get("redo_stack", {}).get("depth", 0)}</p>
<p>This server is read-only. Poll it from the visualizer, or use the Python client.</p>
<ul>
<li><a href="/state">/state</a> — complete snapshot</li>
<li><a href="/state?wait=1&amp;since=0&amp;timeout=2">/state?wait=1&amp;since=0</a> — long-poll</li>
<li><a href="/trie?depth=5">/trie?depth=5</a> — trie tree</li>
<li><a href="/undo">/undo</a> · <a href="/clipboard">/clipboard</a> · <a href="/stats">/stats</a></li>
<li><a href="/health">/health</a></li>
</ul>
<p>Python: <code>from dsa_editor.bridge import StateClient</code> then
<code>StateClient().watch()</code>.</p>
</body></html>"""


def _as_int(value: str | None) -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _as_float(value: str | None) -> float:
    try:
        return float(value or 0)
    except (TypeError, ValueError):
        return 0.0


def _as_bool(value: str | None) -> bool:
    return str(value or "").strip().lower() in {"1", "true", "yes", "on"}


def _escape(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )
