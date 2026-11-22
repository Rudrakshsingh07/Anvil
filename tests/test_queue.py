from __future__ import annotations

import unittest

from dsa_editor.structures.queue import ClipboardHistory


class ClipboardHistoryTests(unittest.TestCase):
    def test_enqueue_and_latest(self) -> None:
        queue = ClipboardHistory(capacity=3)
        first = queue.enqueue("one")
        second = queue.enqueue("two")
        self.assertIsNotNone(first)
        self.assertIsNotNone(second)
        assert first is not None and second is not None
        self.assertEqual(first.id, 1)
        self.assertEqual(second.id, 2)
        self.assertEqual(queue.latest().text, "two")
        self.assertEqual(queue.oldest().text, "one")
        self.assertEqual(len(queue), 2)

    def test_fifo_eviction_and_dequeue(self) -> None:
        queue = ClipboardHistory(capacity=2)
        queue.enqueue("a")
        queue.enqueue("b")
        queue.enqueue("c")
        self.assertEqual(len(queue), 2)
        self.assertEqual([entry.text for entry in queue.oldest_first()], ["b", "c"])
        dequeued = queue.dequeue()
        assert dequeued is not None
        self.assertEqual(dequeued.text, "b")
        self.assertEqual(queue.latest().text, "c")
        self.assertEqual(len(queue), 1)

    def test_deduplicates_tail_only(self) -> None:
        queue = ClipboardHistory(capacity=5)
        queue.enqueue("same")
        queue.enqueue("same")
        self.assertEqual(len(queue), 1)
        queue.enqueue("other")
        queue.enqueue("same")
        self.assertEqual(len(queue), 3)
        self.assertEqual([entry.text for entry in queue.oldest_first()], ["same", "other", "same"])

    def test_get_from_newest(self) -> None:
        queue = ClipboardHistory(capacity=5)
        for text in ("a", "b", "c"):
            queue.enqueue(text)
        self.assertEqual(queue.get(0).text, "c")
        self.assertEqual(queue.get(1).text, "b")
        self.assertEqual(queue.get(2).text, "a")
        self.assertIsNone(queue.get(3))

    def test_empty_and_clear(self) -> None:
        queue = ClipboardHistory()
        self.assertIsNone(queue.enqueue(""))
        self.assertIsNone(queue.latest())
        self.assertIsNone(queue.oldest())
        self.assertIsNone(queue.dequeue())
        queue.enqueue("x")
        queue.clear()
        self.assertEqual(len(queue), 0)

    def test_to_dict(self) -> None:
        queue = ClipboardHistory(capacity=10)
        queue.enqueue("hello\nworld")
        data = queue.to_dict()
        self.assertEqual(data["capacity"], 10)
        self.assertEqual(data["size"], 1)
        self.assertEqual(data["head"], 1)
        self.assertEqual(data["tail"], 1)
        entry = data["entries"][0]
        self.assertEqual(entry["lines"], 2)
        self.assertEqual(entry["chars"], 11)
        self.assertIn("preview", entry)

    def test_capacity_validation(self) -> None:
        with self.assertRaises(ValueError):
            ClipboardHistory(0)


if __name__ == "__main__":
    unittest.main()
