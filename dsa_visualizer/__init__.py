"""Live data-structure visualizer for DSA Editor.

Run the editor in one terminal and the visualizer in another:

    anvil demo.py
    anvil-viz

The visualizer reads the public state bridge; it never imports editor
internals.  See ``docs/visualizer.md``.
"""

from __future__ import annotations

from dsa_editor import __version__

from .model import (
    ChangeEvent,
    ChangeTracker,
    LensNode,
    TrieLens,
    build_trie_lens,
    layout_trie,
)
from .source import LiveSource, SourceStatus

__all__ = [
    "ChangeEvent",
    "ChangeTracker",
    "LensNode",
    "LiveSource",
    "SourceStatus",
    "TrieLens",
    "__version__",
    "build_trie_lens",
    "layout_trie",
]
