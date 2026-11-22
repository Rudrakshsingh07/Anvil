from __future__ import annotations

import contextlib
import io
import tempfile
import unittest
from pathlib import Path

from prompt_toolkit.filters import to_filter

from dsa_editor.app import EditorApp


class EditorAppTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "demo.py"
        self.path.write_text("def hello():\n    return 1\n", encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            self.app = EditorApp(
                self.path,
                enable_server=False,
                state_file=None,
                autosave=False,
                panel=True,
            )
        # Editing outside a running event loop would schedule prompt_toolkit
        # completion tasks; turn those off for direct buffer tests.
        self.app.buffer.complete_while_typing = to_filter(False)
        self.app.buffer.auto_suggest = None

    def tearDown(self) -> None:
        self.app._shutdown()
        self.temp.cleanup()

    def test_initial_load(self) -> None:
        self.assertEqual(self.app.language_key, "python")
        self.assertEqual(self.app.buffer.text, "def hello():\n    return 1\n")
        self.assertFalse(self.app.dirty)
        self.assertEqual(self.app.stats.words, 4)
        self.assertGreater(self.app.trie.node_count, 100)

    def test_typing_word_count_and_undo(self) -> None:
        self.app.buffer.cursor_position = len(self.app.buffer.text)
        self.app.buffer.insert_text("\n# hello world")
        self.assertEqual(self.app.stats.words, 7)
        self.assertTrue(self.app.dirty)
        self.assertGreaterEqual(self.app.undo.undo_depth, 1)

        self.app.undo_action()
        self.assertIn("return 1", self.app.buffer.text)
        self.assertNotIn("hello world", self.app.buffer.text)
        self.app.redo_action()
        self.assertIn("hello world", self.app.buffer.text)

    def test_trie_learns_and_suggests(self) -> None:
        self.app.buffer.cursor_position = len(self.app.buffer.text)
        self.app.buffer.insert_text("\npri")
        self.assertIn("print", [word for word, _, _ in self.app.trie.complete("pri", 10)])
        self.assertEqual(self.app.suggestion_for("pri"), "nt")

    def test_save_and_reload(self) -> None:
        self.app.buffer.cursor_position = len(self.app.buffer.text)
        self.app.buffer.insert_text("# saved\n")
        self.assertTrue(self.app.save())
        self.assertFalse(self.app.dirty)
        self.assertIn("# saved", self.path.read_text(encoding="utf-8"))

    def test_build_state_contract(self) -> None:
        state = self.app.build_state()
        for key in (
            "file",
            "stats",
            "undo",
            "clipboard",
            "trie",
            "autosave",
            "ui",
        ):
            self.assertIn(key, state)
        published = self.app.hub.publish(state)
        self.assertEqual(published["schema"], "dsa-editor/state")
        self.assertGreaterEqual(published["seq"], 1)
        self.assertEqual(state["file"]["language"], "python")
        self.assertIn("undo_stack", state["undo"])
        self.assertIn("redo_stack", state["undo"])
        self.assertIn("tree", state["trie"])
        self.assertIn("suggestions", state["trie"])
        self.assertIn("entries", state["clipboard"])

    def test_clipboard_queue(self) -> None:
        buffer = self.app.buffer
        buffer.cursor_position = 0
        buffer.start_selection()
        buffer.cursor_position = 3
        self.app.copy_selection()
        self.assertEqual(len(self.app.clipboard_history), 1)
        self.assertEqual(self.app.clipboard_history.latest().text, "def")
        buffer.cursor_position = len(buffer.text)
        self.app.paste_latest()
        self.assertTrue(buffer.text.endswith("def"))

    def test_command_language_and_goto(self) -> None:
        self.app._run_command(":lang javascript")
        self.assertEqual(self.app.language_key, "javascript")
        self.app._run_command(":goto 2")
        self.assertEqual(self.app.buffer.document.cursor_position_row + 1, 2)

    def test_new_file_and_open(self) -> None:
        other = Path(self.temp.name) / "other.txt"
        other.write_text("other file\n", encoding="utf-8")
        self.assertTrue(self.app.new_file(force=True))
        self.assertIsNone(self.app.path)
        self.assertTrue(self.app.open_file(str(other)))
        self.assertEqual(self.app.buffer.text, "other file\n")
        self.assertEqual(self.app.path, other.resolve())


if __name__ == "__main__":
    unittest.main()
