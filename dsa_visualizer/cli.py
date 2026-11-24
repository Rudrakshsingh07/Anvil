"""Command-line entry point for the live visualizer."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from dsa_editor import __version__
from dsa_editor.bridge import (
    DEFAULT_PORT,
    StateClient,
    StateError,
    default_state_file,
    read_state_file,
)

from .source import LiveSource


def build_parser(prog: str = "anvil-viz") -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=prog,
        description=(
            "Live data-structure visualizer for DSA Editor.  Reads the editor's "
            "read-only state bridge (HTTP, with a JSON-file fallback)."
        ),
    )
    parser.add_argument(
        "--host", default="127.0.0.1", help="editor bridge host (default: 127.0.0.1)"
    )
    parser.add_argument(
        "--port",
        type=int,
        default=DEFAULT_PORT,
        help=f"editor bridge port (default: {DEFAULT_PORT})",
    )
    parser.add_argument(
        "--state-file",
        type=Path,
        default=None,
        help="JSON state file used when HTTP is unavailable",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=0.25,
        help="poll/long-poll interval in seconds (default: 0.25)",
    )
    parser.add_argument(
        "--geometry",
        default="1500x1000",
        help="initial window size, e.g. 1500x1000 or 1920x1080",
    )
    parser.add_argument(
        "--no-deep-trie",
        action="store_true",
        help="do not request the deeper /trie tree; use the snapshot's depth-3 view",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="print one snapshot summary and exit (no window; useful over SSH)",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="with --check, print the raw snapshot as JSON",
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"{prog} {__version__}",
    )
    return parser


def _fetch_once(args: argparse.Namespace) -> tuple[dict, str] | None:
    client = StateClient(args.host, args.port, timeout=1.5)
    try:
        return client.state(), f"http://{args.host}:{args.port}"
    except StateError:
        pass
    path = args.state_file or default_state_file()
    try:
        return read_state_file(path), str(path)
    except (OSError, ValueError):
        return None


def _summary(snapshot: dict, source: str) -> str:
    file_info = snapshot.get("file") or {}
    stats = snapshot.get("stats") or {}
    undo = snapshot.get("undo") or {}
    clipboard = snapshot.get("clipboard") or {}
    trie = snapshot.get("trie") or {}
    app = snapshot.get("app") or {}
    prefix = trie.get("prefix") or ""
    suggestions = ", ".join(
        str(item.get("word")) for item in trie.get("suggestions") or []
    )
    undo_stack = undo.get("undo_stack") or {}
    redo_stack = undo.get("redo_stack") or {}
    trie_stats = trie.get("stats") or {}
    lines = [
        f"source     {source}  (seq {snapshot.get('seq')}, status {snapshot.get('status')})",
        f"file       {file_info.get('name', 'untitled')}"
        f"{' [dirty]' if file_info.get('dirty') else ' [saved]'}"
        f"  ·  {file_info.get('language', 'text')}"
        f" · {file_info.get('encoding', 'utf-8')}"
        f" · {file_info.get('newline', 'LF')}",
        f"counts     {stats.get('words', 0)} words"
        f" · {stats.get('chars', 0)} chars"
        f" · {stats.get('lines', 0)} lines"
        f" · cursor {stats.get('cursor_line', 1)}:{stats.get('cursor_col', 1)}",
        f"undo       {undo_stack.get('depth', 0)}/{undo_stack.get('capacity', 0)}"
        f"  redo {redo_stack.get('depth', 0)}/{redo_stack.get('capacity', 0)}"
        f"  clipboard {clipboard.get('size', 0)}/{clipboard.get('capacity', 0)}",
        f"trie       {trie_stats.get('nodes', 0)} nodes"
        f" · {trie_stats.get('words', 0)} words"
        f" · prefix {prefix!r} -> {suggestions or '-'}",
    ]
    if app.get("state_file"):
        lines.append(f"state file {app.get('state_file')}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    prog = Path(sys.argv[0]).name or "anvil-viz"
    args = build_parser(prog).parse_args(argv)

    if args.check:
        result = _fetch_once(args)
        if result is None:
            print(
                "no editor state found: start `anvil` first, or pass --state-file",
                file=sys.stderr,
            )
            return 1
        snapshot, source = result
        if args.json:
            print(json.dumps(snapshot, indent=2, ensure_ascii=False))
        else:
            print(_summary(snapshot, source))
        return 0

    try:
        import tkinter  # noqa: F401
    except Exception as exc:  # pragma: no cover - depends on the system
        print(
            f"tkinter is unavailable ({exc}). Install python3-tk, or use "
            "`anvil-viz --check` for a text summary.",
            file=sys.stderr,
        )
        return 1

    source = LiveSource(
        host=args.host,
        port=args.port,
        state_file=args.state_file,
        interval=args.interval,
        deep_trie=not args.no_deep_trie,
    )

    from .app import Visualizer

    try:
        window = Visualizer(source, geometry=args.geometry)
    except Exception as exc:  # pragma: no cover - display dependent
        print(
            f"cannot open the visualizer window: {exc}\n"
            "Check DISPLAY/WAYLAND_DISPLAY, or use `anvil-viz --check`.",
            file=sys.stderr,
        )
        return 1

    try:
        window.mainloop()
    except KeyboardInterrupt:
        window.close()
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
