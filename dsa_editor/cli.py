"""Command-line entry point."""

from __future__ import annotations

import argparse
import os
import sys

from . import __version__
from .app import EditorApp
from .bridge import DEFAULT_PORT, default_state_file


def build_parser() -> argparse.ArgumentParser:
    invoked = os.path.basename(sys.argv[0])
    prog = invoked if invoked in {"anvil", "dsa-editor"} else "dsa-editor"
    parser = argparse.ArgumentParser(
        prog=prog,
        description="A notepad-like TUI editor with live DSA state for a visualizer.",
    )
    parser.add_argument(
        "file",
        nargs="?",
        help="file to open or create (any text format; extension selects highlighting)",
    )
    parser.add_argument(
        "--autosave",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="enable autosave (default: on)",
    )
    parser.add_argument(
        "--autosave-interval",
        type=float,
        default=30.0,
        metavar="SECONDS",
        help="autosave interval in seconds (default: 30)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=DEFAULT_PORT,
        help=f"state bridge port (default: {DEFAULT_PORT}; tries the next 10 ports)",
    )
    parser.add_argument(
        "--no-server",
        action="store_true",
        help="disable the HTTP state bridge",
    )
    parser.add_argument(
        "--state-file",
        default=None,
        metavar="PATH",
        help="JSON snapshot path (default: XDG state dir/dsa-editor/state.json)",
    )
    parser.add_argument(
        "--no-state-file",
        action="store_true",
        help="disable JSON snapshot writes",
    )
    parser.add_argument(
        "--theme",
        default="monokai",
        help="Pygments style/theme name (default: monokai)",
    )
    parser.add_argument(
        "--tab-width",
        type=int,
        default=4,
        help="soft-tab width (default: 4)",
    )
    parser.add_argument(
        "--panel",
        action=argparse.BooleanOptionalAction,
        default=None,
        help="show the data-structure panel (default: auto)",
    )
    parser.add_argument(
        "--undo-capacity",
        type=int,
        default=150,
        help="maximum undo snapshots (default: 150)",
    )
    parser.add_argument(
        "--clipboard-capacity",
        type=int,
        default=50,
        help="clipboard history queue capacity (default: 50)",
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"%(prog)s {__version__}",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    state_file = None
    if not args.no_state_file:
        state_file = args.state_file or default_state_file()

    app = EditorApp(
        args.file,
        autosave=args.autosave,
        autosave_interval=args.autosave_interval,
        port=args.port,
        enable_server=not args.no_server,
        state_file=state_file,
        theme=args.theme,
        tab_width=args.tab_width,
        panel=args.panel,
        undo_capacity=max(1, args.undo_capacity),
        clipboard_capacity=max(1, args.clipboard_capacity),
    )
    app.run()
    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
