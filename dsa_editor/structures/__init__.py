"""Hand-written data structures used by the editor.

Each structure is independent of the TUI and exposes a ``to_dict`` view for
the external visualizer process.
"""

from .queue import ClipEntry, ClipboardHistory
from .stack import BoundedStack, Snapshot, UndoRedoManager, compute_diff
from .stats import TextStats, compute_text_stats, cursor_line_col
from .trie import Trie, TrieNode, current_prefix

__all__ = [
    "BoundedStack",
    "ClipboardHistory",
    "ClipEntry",
    "Snapshot",
    "TextStats",
    "Trie",
    "TrieNode",
    "UndoRedoManager",
    "compute_diff",
    "compute_text_stats",
    "current_prefix",
    "cursor_line_col",
]
