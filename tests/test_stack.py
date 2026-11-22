from __future__ import annotations

import unittest

from dsa_editor.structures.stack import (
    BoundedStack,
    UndoRedoManager,
    compute_diff,
)


class BoundedStackTests(unittest.TestCase):
    def test_push_pop_and_eviction(self) -> None:
        stack: BoundedStack[int] = BoundedStack(capacity=3)
        self.assertIsNone(stack.push(1))
        self.assertIsNone(stack.push(2))
        self.assertIsNone(stack.push(3))
        self.assertEqual(stack.push(4), 1)
        self.assertEqual(len(stack), 3)
        self.assertEqual(stack.peek(), 4)
        self.assertEqual(stack.items(), [4, 3, 2])
        self.assertEqual(stack.pop(), 4)
        self.assertEqual(stack.pop(), 3)
        self.assertEqual(stack.popleft(), 2)
        self.assertIsNone(stack.pop())
        self.assertFalse(stack)

    def test_capacity_validation(self) -> None:
        with self.assertRaises(ValueError):
            BoundedStack(0)


class DiffTests(unittest.TestCase):
    def test_insert(self) -> None:
        self.assertEqual(compute_diff("abc", "abXc"), (2, "", "X"))

    def test_delete(self) -> None:
        self.assertEqual(compute_diff("abc", "ac"), (1, "b", ""))

    def test_replace(self) -> None:
        self.assertEqual(compute_diff("abc", "aXc"), (1, "b", "X"))

    def test_no_change(self) -> None:
        self.assertEqual(compute_diff("same", "same"), (0, "", ""))


class UndoRedoTests(unittest.TestCase):
    def test_roundtrip(self) -> None:
        manager = UndoRedoManager(capacity=10)
        manager.record("", 0, "abc", 3, now=1.0)
        self.assertEqual(manager.undo_depth, 1)
        snapshot = manager.undo("abc", 3)
        self.assertIsNotNone(snapshot)
        assert snapshot is not None
        self.assertEqual(snapshot.text, "")
        self.assertEqual(snapshot.cursor, 0)
        self.assertEqual(manager.undo_depth, 0)
        self.assertEqual(manager.redo_depth, 1)

        redone = manager.redo("", 0)
        self.assertIsNotNone(redone)
        assert redone is not None
        self.assertEqual(redone.text, "abc")
        self.assertEqual(manager.undo_depth, 1)
        self.assertEqual(manager.redo_depth, 0)

    def test_coalesces_typing(self) -> None:
        manager = UndoRedoManager(capacity=10)
        self.assertTrue(manager.record("", 0, "a", 1, now=10.0))
        self.assertFalse(manager.record("a", 1, "ab", 2, now=10.1))
        self.assertFalse(manager.record("ab", 2, "abc", 3, now=10.2))
        self.assertEqual(manager.undo_depth, 1)
        snapshot = manager.undo("abc", 3)
        assert snapshot is not None
        self.assertEqual(snapshot.text, "")
        self.assertEqual(snapshot.edit_count, 3)
        self.assertEqual(snapshot.inserted, "abc")

    def test_break_group(self) -> None:
        manager = UndoRedoManager(capacity=10)
        manager.record("", 0, "a", 1, now=10.0)
        manager.break_group()
        manager.record("a", 1, "ab", 2, now=10.1)
        self.assertEqual(manager.undo_depth, 2)

    def test_time_window_breaks_group(self) -> None:
        manager = UndoRedoManager(capacity=10, group_window=0.5)
        manager.record("", 0, "a", 1, now=10.0)
        manager.record("a", 1, "ab", 2, now=10.9)
        self.assertEqual(manager.undo_depth, 2)

    def test_new_edit_clears_redo(self) -> None:
        manager = UndoRedoManager(capacity=10)
        manager.record("", 0, "abc", 3, now=1.0)
        manager.undo("abc", 3)
        self.assertTrue(manager.can_redo)
        manager.record("", 0, "x", 1, now=5.0)
        self.assertFalse(manager.can_redo)

    def test_whitespace_is_not_coalesced(self) -> None:
        manager = UndoRedoManager(capacity=10)
        manager.record("", 0, "a", 1, now=1.0)
        manager.record("a", 1, "a ", 2, now=1.1)
        self.assertEqual(manager.undo_depth, 2)

    def test_capacity_and_memory_limits(self) -> None:
        manager = UndoRedoManager(capacity=2, max_total_chars=10_000)
        manager.record("", 0, "a", 1, now=1.0)
        manager.break_group()
        manager.record("a", 1, "ab", 2, now=2.0)
        manager.break_group()
        manager.record("ab", 2, "abc", 3, now=3.0)
        self.assertEqual(manager.undo_depth, 2)

    def test_to_dict_shape(self) -> None:
        manager = UndoRedoManager(capacity=5)
        manager.record("", 0, "hello", 5, now=1.0)
        data = manager.to_dict()
        self.assertIn("undo_stack", data)
        self.assertIn("redo_stack", data)
        self.assertEqual(data["undo_stack"]["capacity"], 5)
        self.assertEqual(data["undo_stack"]["depth"], 1)
        self.assertTrue(data["undo_stack"]["top_first"])
        entry = data["undo_stack"]["entries"][0]
        for key in ("label", "kind", "cursor", "chars", "text_preview", "edit_count"):
            self.assertIn(key, entry)

    def test_empty_undo_redo(self) -> None:
        manager = UndoRedoManager()
        self.assertIsNone(manager.undo("", 0))
        self.assertIsNone(manager.redo("", 0))


if __name__ == "__main__":
    unittest.main()
