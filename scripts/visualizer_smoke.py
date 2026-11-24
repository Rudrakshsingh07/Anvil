#!/usr/bin/env python3
"""End-to-end smoke test for the live visualizer.

Starts the real editor in a PTY, seeds it with text and clipboard entries,
checks ``anvil-viz --check``, then opens the Tkinter window, waits for it to
appear on the desktop, and saves a screenshot to ``/tmp/opencode``.

    .venv/bin/python scripts/visualizer_smoke.py
"""

from __future__ import annotations

import fcntl
import json
import os
import pty
import select
import shutil
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
SHOT_DIR = Path("/tmp/opencode")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def drain(fd: int, duration: float = 0.25) -> bytes:
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


def wait_for_health(port: int, timeout: float = 8.0) -> dict:
    deadline = time.monotonic() + timeout
    url = f"http://127.0.0.1:{port}/health"
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1) as response:
                return json.loads(response.read().decode("utf-8"))
        except Exception:
            time.sleep(0.15)
    raise RuntimeError("editor bridge did not start")


def find_window(title_parts: tuple[str, ...], timeout: float = 10.0) -> dict | None:
    """Find a mapped Hyprland client whose title contains all parts."""

    if not shutil.which("hyprctl"):
        return None
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = subprocess.run(
            ["hyprctl", "clients", "-j"], capture_output=True, text=True, check=False
        )
        try:
            clients = json.loads(result.stdout or "[]")
        except json.JSONDecodeError:
            clients = []
        for client in clients:
            title = str(client.get("title") or "").lower()
            if client.get("mapped") and all(part.lower() in title for part in title_parts):
                return client
        time.sleep(0.25)
    return None


def arrange_window(client: dict) -> dict:
    """Float, resize and move the visualizer so it is easy to capture."""

    address = client.get("address")
    if not address:
        return client
    commands = (
        ["focuswindow", f"address:{address}"],
        ["setfloating", f"address:{address}"],
        ["resizeactive", "exact", "1480", "940"],
        ["moveactive", "exact", "80", "70"],
        ["focuswindow", f"address:{address}"],
    )
    for command in commands:
        subprocess.run(
            ["hyprctl", "dispatch", *command],
            capture_output=True,
            text=True,
            check=False,
        )
        time.sleep(0.15)
    time.sleep(0.6)
    return find_window(("anvil", "live data structures"), timeout=3) or client


def screenshot_window(client: dict, target: Path) -> bool:
    if not shutil.which("grim") or not shutil.which("magick"):
        return False
    position = client.get("at") or [0, 0]
    size = client.get("size") or [0, 0]
    x, y = int(position[0]), int(position[1])
    width, height = int(size[0]), int(size[1])
    if width < 100 or height < 100:
        return False
    raw = target.with_suffix(".raw.png")
    subprocess.run(["grim", str(raw)], check=False)
    if not raw.exists():
        return False
    subprocess.run(
        [
            "magick",
            str(raw),
            "-crop",
            f"{width}x{height}+{x}+{y}",
            "+repage",
            str(target),
        ],
        check=False,
    )
    raw.unlink(missing_ok=True)
    return target.exists() and target.stat().st_size > 5000


def main() -> int:
    port = free_port()
    SHOT_DIR.mkdir(parents=True, exist_ok=True)
    shot = SHOT_DIR / "anvil-viz.png"

    with tempfile.TemporaryDirectory() as tmp:
        path = ROOT / "examples" / "demo.py"
        if not path.exists():
            print(f"[FAIL] missing demo file: {path}")
            return 1
        state_file = Path(tmp) / "state.json"

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
        editor = subprocess.Popen(
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
        visualizer: subprocess.Popen | None = None

        try:
            health = wait_for_health(port)
            print(f"[PASS] editor bridge on :{port} seq={health['seq']}")

            visualizer = subprocess.Popen(
                [
                    str(PYTHON),
                    "-m",
                    "dsa_visualizer",
                    "--port",
                    str(port),
                    "--state-file",
                    str(state_file),
                    "--geometry",
                    "1480x940",
                ],
                env=env,
                cwd=str(ROOT),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            window = find_window(("anvil", "live data structures"), timeout=12)
            if window is None:
                print("[FAIL] visualizer window did not appear")
                if visualizer.poll() is not None:
                    out, err = visualizer.communicate(timeout=2)
                    print("stdout:", out)
                    print("stderr:", err)
                return 1
            print(f"[PASS] visualizer window: {window.get('title')}")
            window = arrange_window(window)

            # Type with the visualizer already running, so the chart and event
            # log are populated by live updates rather than a single snapshot.
            os.write(master, b"\x1b[1;5F")  # Ctrl-End (xterm) to end of document
            time.sleep(0.3)
            for chunk in (
                b"\nhelper_function(1)\n",
                b"for index in range(3):\n    print(helper_function(index))\n",
                b"prefix_demo = helper_function\n",
            ):
                os.write(master, chunk)
                time.sleep(0.45)
                drain(master, 0.05)

            # Two different clipboard entries for the FIFO queue.  Typing after
            # Ctrl-A appends at the cursor, so no Escape key is needed (a lone
            # Escape can be mistaken for the start of a Meta sequence).
            # Ctrl-C needs a moment to be dispatched, so allow ~2 s before the
            # next keystroke or the second copy is lost.
            os.write(master, b"\x01")  # Ctrl-A select all
            os.write(master, b"\x03")  # Ctrl-C copy
            time.sleep(2.0)
            os.write(master, b"# copied twice\n")
            time.sleep(0.5)
            os.write(master, b"\x01")
            os.write(master, b"\x03")
            time.sleep(2.0)

            check = subprocess.run(
                [
                    str(PYTHON),
                    "-m",
                    "dsa_visualizer",
                    "--check",
                    "--port",
                    str(port),
                    "--state-file",
                    str(state_file),
                ],
                capture_output=True,
                text=True,
                cwd=str(ROOT),
                timeout=15,
            )
            if check.returncode != 0 or "words" not in check.stdout:
                print("[FAIL] anvil-viz --check")
                print(check.stdout)
                print(check.stderr)
                return 1
            print("[PASS] anvil-viz --check")
            for line in check.stdout.strip().splitlines():
                print("       " + line)

            time.sleep(2.2)
            if screenshot_window(window, shot):
                print(f"[PASS] screenshot saved to {shot} ({shot.stat().st_size} bytes)")
            else:
                print("[WARN] could not capture a screenshot")

            visualizer.terminate()
            try:
                out, err = visualizer.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                visualizer.kill()
                out, err = visualizer.communicate(timeout=2)
            if err.strip():
                print("[viz stderr]")
                print(err[-4000:])
            if out.strip():
                print("[viz stdout]")
                print(out[-2000:])
            print("[PASS] visualizer closed")
        finally:
            if visualizer is not None and visualizer.poll() is None:
                visualizer.kill()
            editor.terminate()
            try:
                editor.wait(timeout=5)
            except subprocess.TimeoutExpired:
                editor.kill()
            os.close(master)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
