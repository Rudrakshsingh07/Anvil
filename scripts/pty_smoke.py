#!/usr/bin/env python3
"""Manual PTY smoke test for the TUI.

Runs the editor in a pseudo-terminal, sends keystrokes and verifies that the
file on disk changes as expected.  This is intentionally not part of the unit
test suite (it depends on terminal timing), but it is very useful before a demo:

    .venv/bin/python scripts/pty_smoke.py
"""

from __future__ import annotations

import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PYTHON = ROOT / ".venv" / "bin" / "python"


def read_available(fd: int, duration: float = 0.4) -> bytes:
    chunks: list[bytes] = []
    end = time.monotonic() + duration
    while time.monotonic() < end:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        chunks.append(chunk)
    return b"".join(chunks)


def run_editor(path: Path, keys: list[tuple[float, bytes]], *, extra_args: list[str] | None = None) -> tuple[int, bytes]:
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
    command = [
        str(PYTHON),
        "-m",
        "dsa_editor",
        str(path),
        "--no-server",
        "--no-state-file",
        "--no-autosave",
        "--panel",
        *(extra_args or []),
    ]
    process = subprocess.Popen(
        command,
        stdin=slave,
        stdout=slave,
        stderr=slave,
        env=env,
        cwd=str(ROOT),
        close_fds=True,
    )
    os.close(slave)

    output = bytearray(read_available(master, 0.9))
    for delay, data in keys:
        time.sleep(delay)
        os.write(master, data)
        output.extend(read_available(master, 0.25))

    deadline = time.monotonic() + 5.0
    while process.poll() is None and time.monotonic() < deadline:
        output.extend(read_available(master, 0.2))
    if process.poll() is None:
        process.kill()
    output.extend(read_available(master, 0.3))
    os.close(master)
    return process.wait(timeout=3), bytes(output)


def main() -> int:
    failures = 0

    with tempfile.TemporaryDirectory() as tmp:
        # 1. Typing + save + quit.
        path = Path(tmp) / "smoke.py"
        path.write_text("def hello():\n    return 1\n", encoding="utf-8")
        code, output = run_editor(
            path,
            [
                (0.4, b"x"),
                (0.2, b"\x13"),  # Ctrl-S
                (0.3, b"\x11"),  # Ctrl-Q
            ],
        )
        text = path.read_text(encoding="utf-8")
        ok = code == 0 and text.startswith("xdef hello")
        print(f"[{'PASS' if ok else 'FAIL'}] type + save + quit (exit={code}, file={text[:18]!r})")
        failures += not ok
        if not ok:
            print(output.decode("utf-8", "replace")[-2000:])

        # 2. Undo a typed group before saving.
        path.write_text("original\n", encoding="utf-8")
        code, output = run_editor(
            path,
            [
                (0.4, b"abc"),
                (0.2, b"\x1a"),  # Ctrl-Z
                (0.2, b"\x13"),  # Ctrl-S
                (0.3, b"\x11"),  # Ctrl-Q
            ],
        )
        text = path.read_text(encoding="utf-8")
        ok = code == 0 and text == "original\n"
        print(f"[{'PASS' if ok else 'FAIL'}] undo group + save (exit={code}, file={text!r})")
        failures += not ok
        if not ok:
            print(output.decode("utf-8", "replace")[-2000:])

        # 3. Command bar save-as.
        source = Path(tmp) / "source.txt"
        source.write_text("hello\n", encoding="utf-8")
        target = Path(tmp) / "renamed.txt"
        command = f"w {target}".encode()
        code, output = run_editor(
            source,
            [
                (0.4, b"\x10"),  # Ctrl-P (command bar)
                (0.2, command),
                (0.2, b"\r"),
                (0.3, b"\x11"),
            ],
        )
        ok = code == 0 and target.exists() and target.read_text() == "hello\n"
        print(f"[{'PASS' if ok else 'FAIL'}] command bar save-as (exit={code}, exists={target.exists()})")
        failures += not ok
        if not ok:
            print(output.decode("utf-8", "replace")[-2000:])

        # 4. Help opens and closes, then quit.
        code, output = run_editor(
            source,
            [
                (0.4, b"\x1bOP"),  # F1 (xterm)
                (0.3, b"\x1b"),  # Esc
                (0.3, b"\x11"),  # Ctrl-Q
            ],
        )
        ok = code == 0 and b"help" in output.lower()
        print(f"[{'PASS' if ok else 'FAIL'}] help open/close (exit={code})")
        failures += not ok

    print()
    print("smoke test:", "PASS" if failures == 0 else f"{failures} FAILURE(S)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
