"""A hand-written prefix trie used for inline code completion.

Every node stores a character, child links, a terminal flag, a ranking weight
and a kind tag (``keyword``, ``common``, ``word``, ``snippet``).  The editor
builds one trie for the active document: language keywords plus identifiers
learned from the document itself.

The trie is thread-safe because the HTTP state bridge may read it while the
UI thread is typing.
"""

from __future__ import annotations

import re
import threading
from typing import Iterable

_IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]{1,}")
MAX_WORD_LENGTH = 64


class TrieNode:
    __slots__ = ("children", "weight", "kind", "terminal")

    def __init__(self) -> None:
        self.children: dict[str, TrieNode] = {}
        self.weight: int = 0
        self.kind: str | None = None
        self.terminal: bool = False


class Trie:
    def __init__(self) -> None:
        self._root = TrieNode()
        self._node_count = 1
        self._word_count = 0
        self._lock = threading.RLock()

    # ---------------------------------------------------------------- updates
    def insert(
        self,
        word: str,
        weight: int = 1,
        kind: str = "word",
        *,
        only_if_new: bool = False,
        bump: bool = False,
    ) -> bool:
        """Insert or update a word.  Returns True when a new word was added."""

        word = word.strip()
        if not word or len(word) > MAX_WORD_LENGTH or any(c.isspace() for c in word):
            return False

        with self._lock:
            node = self._root
            for char in word:
                child = node.children.get(char)
                if child is None:
                    child = TrieNode()
                    node.children[char] = child
                    self._node_count += 1
                node = child

            is_new = not node.terminal
            if is_new:
                node.terminal = True
                self._word_count += 1

            if only_if_new and not is_new:
                return False

            if bump:
                node.weight += max(0, weight)
            else:
                node.weight = max(node.weight, weight)
            if kind:
                node.kind = kind
            return is_new

    def bump(self, word: str, amount: int = 10) -> bool:
        """Increase a word's ranking weight (e.g. after it was accepted)."""

        with self._lock:
            node = self._root
            for char in word:
                node = node.children.get(char)
                if node is None:
                    return False
            if not node.terminal:
                return False
            node.weight += max(0, amount)
            return True

    def remove(self, word: str) -> bool:
        with self._lock:
            path: list[tuple[TrieNode, str]] = []
            node = self._root
            for char in word:
                child = node.children.get(char)
                if child is None:
                    return False
                path.append((node, char))
                node = child
            if not node.terminal:
                return False
            node.terminal = False
            node.weight = 0
            node.kind = None
            self._word_count -= 1

            # Prune now-empty branches.
            for parent, char in reversed(path):
                child = parent.children.get(char)
                if child and not child.terminal and not child.children:
                    del parent.children[char]
                    self._node_count -= 1
                else:
                    break
            return True

    def clear(self) -> None:
        with self._lock:
            self._root = TrieNode()
            self._node_count = 1
            self._word_count = 0

    def learn_text(self, text: str, weight: int = 8, kind: str = "word") -> int:
        """Insert identifiers found in ``text`` if they are not present yet."""

        added = 0
        for match in _IDENT_RE.finditer(text):
            if self.insert(match.group(0), weight=weight, kind=kind, only_if_new=True):
                added += 1
        return added

    # ---------------------------------------------------------------- queries
    def _find_node(self, prefix: str) -> TrieNode | None:
        node = self._root
        for char in prefix:
            node = node.children.get(char)
            if node is None:
                return None
        return node

    def contains(self, word: str) -> bool:
        with self._lock:
            node = self._find_node(word)
            return bool(node and node.terminal)

    def starts_with(self, prefix: str) -> bool:
        with self._lock:
            return self._find_node(prefix) is not None

    def weight_of(self, word: str) -> int:
        with self._lock:
            node = self._find_node(word)
            return node.weight if node and node.terminal else 0

    def _collect(self, node: TrieNode, prefix: str, out: list[tuple[str, int, str]]) -> None:
        if node.terminal:
            out.append((prefix, node.weight, node.kind or "word"))
        for char, child in node.children.items():
            self._collect(child, prefix + char, out)

    def complete(self, prefix: str, limit: int = 20) -> list[tuple[str, int, str]]:
        """Return ``(word, weight, kind)`` completions, best first."""

        if not prefix:
            return []
        with self._lock:
            node = self._find_node(prefix)
            if node is None:
                return []
            found: list[tuple[str, int, str]] = []
            self._collect(node, prefix, found)

        # Weight first, then shorter words, then alphabetical for stability.
        found.sort(key=lambda item: (-item[1], len(item[0]), item[0].lower()))
        return found[: max(0, limit)]

    def all_words(self) -> list[tuple[str, int, str]]:
        with self._lock:
            found: list[tuple[str, int, str]] = []
            self._collect(self._root, "", found)
        found.sort(key=lambda item: (-item[1], item[0].lower()))
        return found

    # ------------------------------------------------------------------- info
    @property
    def node_count(self) -> int:
        with self._lock:
            return self._node_count

    @property
    def word_count(self) -> int:
        with self._lock:
            return self._word_count

    def stats(self) -> dict:
        with self._lock:
            max_depth = 0
            stack: list[tuple[TrieNode, int]] = [(self._root, 0)]
            while stack:
                node, depth = stack.pop()
                if depth > max_depth:
                    max_depth = depth
                for child in node.children.values():
                    stack.append((child, depth + 1))
            return {
                "nodes": self._node_count,
                "words": self._word_count,
                "max_depth": max_depth,
                "branching_factor": round(
                    max(0, self._node_count - 1) / max(1, self._node_count - self._word_count),
                    2,
                ),
            }

    def to_dict(
        self,
        *,
        prefix: str = "",
        max_depth: int = 3,
        max_nodes: int = 800,
        top_words: int = 60,
        suggestions: int = 12,
    ) -> dict:
        """Serializable trie view for the visualizer.

        The full trie can be large, so ``tree`` is depth/node limited and
        marked with ``truncated``.  Counts always describe the complete trie.
        """

        with self._lock:
            budget = max(1, max_nodes)
            truncated = False

            def build(node: TrieNode, char: str, depth: int) -> dict:
                nonlocal budget, truncated
                budget -= 1
                children: dict[str, dict] = {}
                if depth < max_depth:
                    ordered = sorted(
                        node.children.items(),
                        key=lambda item: (-item[1].weight, item[0]),
                    )
                    for child_char, child in ordered:
                        if budget <= 0:
                            truncated = True
                            break
                        children[child_char] = build(child, child_char, depth + 1)
                elif node.children:
                    truncated = True

                return {
                    "char": char,
                    "terminal": node.terminal,
                    "weight": node.weight,
                    "kind": node.kind,
                    "children": children,
                }

            tree = build(self._root, "", 0)
            stats = self._stats_unlocked()
            top = self.all_words_unlocked()[:top_words]
            sugs = self._complete_unlocked(prefix, suggestions) if prefix else []

            return {
                "stats": stats,
                "tree": tree,
                "tree_meta": {
                    "max_depth": max_depth,
                    "max_nodes": max_nodes,
                    "truncated": truncated,
                },
                "top_words": [
                    {"word": word, "weight": weight, "kind": kind}
                    for word, weight, kind in top
                ],
                "prefix": prefix,
                "suggestions": [
                    {"word": word, "weight": weight, "kind": kind}
                    for word, weight, kind in sugs
                ],
            }

    def _stats_unlocked(self) -> dict:
        max_depth = 0
        kind_counts: dict[str, int] = {}
        stack: list[tuple[TrieNode, int]] = [(self._root, 0)]
        while stack:
            node, depth = stack.pop()
            if depth > max_depth:
                max_depth = depth
            if node.terminal:
                kind = node.kind or "word"
                kind_counts[kind] = kind_counts.get(kind, 0) + 1
            for child in node.children.values():
                stack.append((child, depth + 1))
        return {
            "nodes": self._node_count,
            "words": self._word_count,
            "max_depth": max_depth,
            "kind_counts": kind_counts,
        }

    def all_words_unlocked(self) -> list[tuple[str, int, str]]:
        found: list[tuple[str, int, str]] = []
        self._collect(self._root, "", found)
        found.sort(key=lambda item: (-item[1], item[0].lower()))
        return found

    def _complete_unlocked(self, prefix: str, limit: int) -> list[tuple[str, int, str]]:
        node = self._find_node(prefix)
        if node is None:
            return []
        found: list[tuple[str, int, str]] = []
        self._collect(node, prefix, found)
        found.sort(key=lambda item: (-item[1], len(item[0]), item[0].lower()))
        return found[:limit]


def current_prefix(text_before_cursor: str) -> str:
    """Extract the identifier being typed at the cursor."""

    match = re.search(r"[A-Za-z_][A-Za-z0-9_]*$", text_before_cursor)
    return match.group(0) if match else ""


def seed_from_words(trie: Trie, words: Iterable[str], weight: int, kind: str) -> None:
    for word in words:
        trie.insert(word, weight=weight, kind=kind)
