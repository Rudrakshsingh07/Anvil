#!/usr/bin/env python3
"""Minimal live-state consumer for the future visualizer.

Run the editor in one terminal, then:

    .venv/bin/python examples/watch_state.py
    .venv/bin/python examples/watch_state.py --raw
    .venv/bin/python examples/watch_state.py --file   # JSON-file fallback

This is deliberately not a visualizer; it only demonstrates the data contract.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

# Allow running directly from a checkout without installing the package.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dsa_editor.bridge import (  # noqa: E402
    StateClient,
    StateError,
    default_state_file,
    read_state_file,
)


def summarize(state: dict) -> str:
    undo = state.get("undo", {})
    stats = state.get("stats", {})
    file_info = state.get("file", {})
    trie = state.get("trie", {})
    clipboard = state.get("clipboard", {})
    prefix = trie.get("prefix", "")
    suggestions = ", ".join(item["word"] for item in trie.get("suggestions", [])[:4])
    return (
        f"seq {state.get('seq'):>4} | "
        f"{file_info.get('name', 'untitled'):<16} "
        f"{'dirty' if file_info.get('dirty') else 'saved':<5} | "
        f"words {stats.get('words', 0):>4} "
        f"lines {stats.get('lines', 0):>4} | "
        f"undo {undo.get('undo_stack', {}).get('depth', 0):>2} "
        f"redo {undo.get('redo_stack', {}).get('depth', 0):>2} | "
        f"clip {clipboard.get('size', 0):>2} | "
        f"trie {trie.get('stats', {}).get('nodes', 0):>5} nodes "
        f"{trie.get('stats', {}).get('words', 0):>4} words | "
        f"prefix {prefix!r} -> {suggestions or '-'}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--raw", action="store_true", help="print full JSON snapshots")
    parser.add_argument("--once", action="store_true", help="print one snapshot and exit")
    parser.add_argument(
        "--file",
        action="store_true",
        help="read the JSON state file instead of HTTP",
    )
    parser.add_argument("--state-file", default=None, help="state file path")
    args = parser.parse_args()

    if args.file:
        path = Path(args.state_file) if args.state_file else default_state_file()
        try:
            state = read_state_file(path)
        except OSError as exc:
            print(f"cannot read {path}: {exc}", file=sys.stderr)
            return 1
        print(json.dumps(state, indent=2, ensure_ascii=False) if args.raw else summarize(state))
        return 0

    client = StateClient(host=args.host, port=args.port)
    while True:
        try:
            for state in client.watch(interval=0.25):
                if args.raw:
                    print(json.dumps(state, ensure_ascii=False))
                else:
                    print(summarize(state), flush=True)
                if args.once:
                    return 0
        except KeyboardInterrupt:
            return 0
        except StateError as exc:
            print(f"waiting for editor bridge: {exc}", file=sys.stderr)
            if args.once:
                return 1
            time.sleep(1.0)


if __name__ == "__main__":
    raise SystemExit(main())
