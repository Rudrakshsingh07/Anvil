#!/usr/bin/env python3
"""Manual smoke test for the live state bridge.

Launches the TUI in a PTY, talks to the HTTP bridge from this process, types
into the editor, and verifies that word count / undo / clipboard / trie state
updates live.

    .venv/bin/python scripts/bridge_smoke.py
"""

from __future__ import annotations

import fcntl
import json
import os
import pty
import select
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PYTHON = ROOT / ".venv" / "bin" / "python"


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def drain(fd: int, duration: float = 0.3) -> bytes:
    chunks = []
    end = time.monotonic() + duration
    while time.monotonic() < end:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if ready:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            chunks.append(chunk)
    return b"".join(chunks)


def get(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=2) as response:
        return json.loads(response.read().decode("utf-8"))


def main() -> int:
    port = free_port()
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "bridge.py"
        state_file = Path(tmp) / "state.json"
        path.write_text("", encoding="utf-8")

        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 120, 0, 0))
        env = dict(os.environ)
        env.update(
            {
                "TERM": "xterm-256color",
                "PROMPT_TOOLKIT_NO_CPR": "1",
                "COLUMNS": "120",
                "LINES": "40",
                "PYTHONPATH": str(ROOT),
            }
        )
        process = subprocess.Popen(
            [
                str(PYTHON),
                "-m",
                "dsa_editor",
                str(path),
                "--port",
                str(port),
                "--state-file",
                str(state_file),
                "--no-autosave",
                "--panel",
            ],
            stdin=slave,
            stdout=slave,
            stderr=slave,
            env=env,
            cwd=str(ROOT),
            close_fds=True,
        )
        os.close(slave)
        base = f"http://127.0.0.1:{port}"

        try:
            health = None
            for _ in range(40):
                try:
                    health = get(base + "/health")
                    break
                except Exception:
                    time.sleep(0.15)
            if not health or not health.get("ok"):
                print("FAIL: bridge did not start")
                return 1
            print(f"[PASS] bridge started on :{port}, seq={health['seq']}")

            state = get(base + "/state")
            print(
                f"       file={state['file']['name']} words={state['stats']['words']} "
                f"undo={state['undo']['undo_stack']['depth']} "
                f"trie_nodes={state['trie']['stats']['nodes']}"
            )

            drain(master, 0.5)
            os.write(master, b"hello world")
            time.sleep(0.8)
            state = get(base + "/state")
            ok_words = state["stats"]["words"] == 2
            ok_undo = state["undo"]["undo_stack"]["depth"] >= 1
            print(
                f"[{'PASS' if ok_words else 'FAIL'}] live word count = "
                f"{state['stats']['words']} (expected 2)"
            )
            print(
                f"[{'PASS' if ok_undo else 'FAIL'}] live undo depth = "
                f"{state['undo']['undo_stack']['depth']}"
            )

            trie = get(base + "/trie?depth=6&limit=2000")
            ok_trie = trie.get("stats", {}).get("nodes", 0) > 100
            print(
                f"[{'PASS' if ok_trie else 'FAIL'}] /trie nodes = "
                f"{trie.get('stats', {}).get('nodes')}"
            )

            os.write(master, b"\x13")  # Ctrl-S
            time.sleep(0.6)
            state = get(base + "/state")
            ok_saved = not state["file"]["dirty"] and path.read_text() == "hello world"
            print(
                f"[{'PASS' if ok_saved else 'FAIL'}] saved via Ctrl-S "
                f"(dirty={state['file']['dirty']})"
            )

            os.write(master, b"\x11")  # Ctrl-Q
            for _ in range(30):
                if process.poll() is not None:
                    break
                time.sleep(0.1)
            ok_exit = process.poll() == 0
            print(f"[{'PASS' if ok_exit else 'FAIL'}] editor exited ({process.poll()})")

            closed = json.loads(state_file.read_text(encoding="utf-8"))
            ok_file = closed.get("status") == "closed"
            print(
                f"[{'PASS' if ok_file else 'FAIL'}] final state file status="
                f"{closed.get('status')} seq={closed.get('seq')}"
            )

            if not all([ok_words, ok_undo, ok_trie, ok_saved, ok_exit, ok_file]):
                print("bridge smoke test: FAIL")
                return 1
            print("bridge smoke test: PASS")
            return 0
        finally:
            if process.poll() is None:
                process.kill()
            os.close(master)


if __name__ == "__main__":
    sys.exit(main())
