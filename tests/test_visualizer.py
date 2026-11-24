"""Tests for the visualizer's pure model, tracker and live source."""

from __future__ import annotations

import json
import socket
import tempfile
import time
import unittest
from pathlib import Path

from dsa_editor.bridge import StateHub, StateServer
from dsa_visualizer.model import (
    ChangeTracker,
    build_trie_lens,
    clipboard_cards,
    format_age,
    format_bytes,
    format_int,
    layout_trie,
    shorten,
    sparkline_points,
    stack_cards,
    suggestion_words,
)
from dsa_visualizer.source import LiveSource


def make_tree(words: list[str]) -> dict:
    root = {"char": "", "terminal": False, "weight": 0, "kind": None, "children": {}}
    for word in words:
        node = root
        for char in word:
            node = node["children"].setdefault(
                char,
                {
                    "char": char,
                    "terminal": False,
                    "weight": 0,
                    "kind": None,
                    "children": {},
                },
            )
        node["terminal"] = True
        node["weight"] = 10
        node["kind"] = "keyword"
    return root


def snapshot(
    seq: int,
    *,
    words: int = 0,
    undo: int = 0,
    redo: int = 0,
    clip_tail: int | None = None,
    clip_size: int | None = None,
    saved: float | None = None,
    prefix: str = "",
    suggestions: list[str] | None = None,
    status: str = "ok",
) -> dict:
    size = clip_size if clip_size is not None else (0 if clip_tail is None else 1)
    entries = (
        [{"id": clip_tail, "preview": f"entry {clip_tail}"}]
        if clip_tail is not None
        else []
    )
    return {
        "seq": seq,
        "status": status,
        "stats": {"words": words, "chars": words * 5, "lines": 1},
        "undo": {
            "undo_stack": {
                "depth": undo,
                "capacity": 150,
                "entries": [{"label": "insert", "kind": "insert"} for _ in range(undo)],
            },
            "redo_stack": {"depth": redo, "capacity": 150, "entries": []},
        },
        "clipboard": {"size": size, "capacity": 50, "tail": clip_tail, "entries": entries},
        "file": {"name": "demo.py", "last_saved": saved},
        "autosave": {"enabled": True},
        "trie": {
            "prefix": prefix,
            "suggestions": [{"word": word} for word in suggestions or []],
        },
        "app": {"state_file": "/tmp/state.json"},
    }


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class TrieLensTests(unittest.TestCase):
    def test_empty_tree(self) -> None:
        lens = build_trie_lens(None, "pr", ["print"])
        self.assertIsNone(lens.root)
        self.assertEqual(lens.prefix, "pr")
        self.assertEqual(lens.suggestions, ["print"])
        self.assertEqual(lens.node_count, 0)

    def test_prefix_path_and_focus(self) -> None:
        tree = make_tree(["print", "private", "printf", "while"])
        lens = build_trie_lens(
            tree, "pri", ["print", "private"], max_nodes=40
        )
        self.assertIsNotNone(lens.root)
        assert lens.root is not None
        self.assertEqual(lens.matched_prefix, "pri")
        path = [node.char for node in lens.walk() if node.on_path]
        self.assertEqual(path, ["", "p", "r", "i"])
        self.assertTrue(any(node.is_prefix for node in lens.walk()))
        suggested = {node.word for node in lens.walk() if node.suggested}
        self.assertIn("print", suggested)

    def test_missing_prefix_matches_partial(self) -> None:
        tree = make_tree(["print", "private"])
        lens = build_trie_lens(tree, "prix")
        self.assertEqual(lens.matched_prefix, "pri")
        self.assertTrue(lens.node_count >= 4)

    def test_node_budget_is_respected(self) -> None:
        words = [f"word{index:03d}" for index in range(120)]
        tree = make_tree(words)
        lens = build_trie_lens(tree, "wor", max_nodes=25, max_children=8)
        self.assertLessEqual(lens.node_count, 25)
        self.assertTrue(lens.truncated)

    def test_suggestion_words_accepts_dicts_and_strings(self) -> None:
        trie = {"suggestions": [{"word": "print"}, "private", {"nope": 1}]}
        self.assertEqual(suggestion_words(trie), ["print", "private"])

    def test_layout_places_nodes_inside_canvas(self) -> None:
        tree = make_tree(["alpha", "beta", "gamma", "delta"])
        lens = build_trie_lens(tree, "", max_nodes=60)
        nodes = layout_trie(lens, 600, 300, margin_x=30, margin_y=20)
        self.assertTrue(nodes)
        for node in nodes:
            self.assertGreaterEqual(node.x, 0)
            self.assertLessEqual(node.x, 600)
            self.assertGreaterEqual(node.y, 0)
            self.assertLessEqual(node.y, 300)

    def test_layout_leaves_are_ordered(self) -> None:
        tree = make_tree(["aa", "bb", "cc"])
        lens = build_trie_lens(tree, "", max_nodes=60)
        nodes = layout_trie(lens, 400, 200)
        leaves = sorted(
            (node for node in nodes if not node.children), key=lambda node: node.x
        )
        self.assertEqual(len(leaves), 3)
        self.assertLess(leaves[0].x, leaves[1].x)
        self.assertLess(leaves[1].x, leaves[2].x)


class CardAndChartTests(unittest.TestCase):
    def test_stack_cards_keeps_top_and_counts_hidden(self) -> None:
        entries = [{"label": str(index)} for index in range(10)]
        visible, hidden = stack_cards(entries, 3)
        self.assertEqual([item["label"] for item in visible], ["0", "1", "2"])
        self.assertEqual(hidden, 7)

    def test_clipboard_cards_keeps_newest(self) -> None:
        entries = [{"id": index} for index in range(10)]
        visible, hidden = clipboard_cards(entries, 3)
        self.assertEqual([item["id"] for item in visible], [7, 8, 9])
        self.assertEqual(hidden, 7)

    def test_sparkline_bounds(self) -> None:
        points = sparkline_points([0, 5, 10, 2], 200, 100, pad=5)
        self.assertEqual(len(points), 4)
        for x, y in points:
            self.assertGreaterEqual(x, 5)
            self.assertLessEqual(x, 195)
            self.assertGreaterEqual(y, 5)
            self.assertLessEqual(y, 95)

    def test_sparkline_constant_series_is_centered(self) -> None:
        points = sparkline_points([4, 4, 4], 100, 50, pad=5)
        self.assertTrue(all(point[1] == 25 for point in points))

    def test_sparkline_empty(self) -> None:
        self.assertEqual(sparkline_points([], 100, 50), [])

    def test_formatters(self) -> None:
        self.assertEqual(format_int(1234567), "1,234,567")
        self.assertEqual(format_bytes(2048), "2.0 KiB")
        self.assertEqual(format_age(0.5), "now")
        self.assertEqual(format_age(12), "12s ago")
        self.assertEqual(format_age(120), "2m ago")
        self.assertEqual(shorten("a\nb\tc", 20), "a b c")


class ChangeTrackerTests(unittest.TestCase):
    def test_first_snapshot_only_records_history(self) -> None:
        tracker = ChangeTracker()
        self.assertEqual(tracker.update(snapshot(1, words=3)), [])
        self.assertEqual(tracker.word_series, [3])
        self.assertEqual(tracker.latest(), [])

    def test_undo_and_redo_events(self) -> None:
        tracker = ChangeTracker()
        tracker.update(snapshot(1))
        events = tracker.update(snapshot(2, undo=1))
        self.assertEqual([event.kind for event in events], ["undo-push"])
        events = tracker.update(snapshot(3, undo=0, redo=1))
        self.assertEqual(
            [event.kind for event in events], ["undo-pop", "redo-push"]
        )

    def test_clipboard_save_and_close_events(self) -> None:
        tracker = ChangeTracker()
        tracker.update(snapshot(1, saved=100.0))
        events = tracker.update(snapshot(2, clip_tail=7, saved=100.0))
        self.assertIn("clipboard", [event.kind for event in events])
        events = tracker.update(snapshot(3, clip_tail=7, saved=110.0))
        self.assertIn("save", [event.kind for event in events])
        events = tracker.update(snapshot(4, clip_tail=7, saved=110.0, status="closed"))
        self.assertIn("closed", [event.kind for event in events])

    def test_prefix_change_emits_trie_event(self) -> None:
        tracker = ChangeTracker()
        tracker.update(snapshot(1, prefix=""))
        events = tracker.update(snapshot(2, prefix="pri", suggestions=["print"]))
        self.assertEqual([event.kind for event in events], ["trie"])

    def test_history_is_bounded(self) -> None:
        tracker = ChangeTracker(history=5, max_events=2)
        for seq in range(1, 12):
            tracker.update(snapshot(seq, words=seq, undo=seq))
        self.assertEqual(len(tracker.word_series), 5)
        self.assertEqual(len(tracker.undo_series), 5)
        self.assertLessEqual(len(tracker.latest()), 2)


class LiveSourceTests(unittest.TestCase):
    def test_reads_http_snapshots(self) -> None:
        hub = StateHub()
        server = StateServer(hub, port=0)
        port = server.start()
        hub.publish(snapshot(1, words=5))
        source = LiveSource(port=port, interval=0.05, deep_trie=False)
        source.start()
        try:
            deadline = time.monotonic() + 4.0
            payload = None
            while time.monotonic() < deadline:
                try:
                    kind, payload, mode = source.queue.get(timeout=0.2)
                except Exception:
                    continue
                if kind == "state":
                    self.assertEqual(mode, "http")
                    break
            self.assertIsNotNone(payload)
            assert payload is not None
            self.assertEqual(payload["seq"], 1)
            self.assertEqual(source.status().mode, "http")
        finally:
            source.stop()
            server.stop()

    def test_falls_back_to_state_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "state.json"
            path.write_text(json.dumps(snapshot(9, words=2)), encoding="utf-8")
            source = LiveSource(
                port=free_port(),
                state_file=path,
                interval=0.05,
                deep_trie=False,
            )
            source.start()
            try:
                deadline = time.monotonic() + 4.0
                payload = None
                mode = None
                while time.monotonic() < deadline:
                    try:
                        kind, payload, mode = source.queue.get(timeout=0.2)
                    except Exception:
                        continue
                    if kind == "state":
                        break
                self.assertIsNotNone(payload)
                self.assertEqual(mode, "file")
                assert payload is not None
                self.assertEqual(payload["seq"], 9)
            finally:
                source.stop()


if __name__ == "__main__":
    unittest.main()
