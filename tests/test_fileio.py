from __future__ import annotations

import codecs
import tempfile
import unittest
from pathlib import Path

from dsa_editor.fileio import (
    detect_newline,
    display_path,
    load_text_file,
    save_text_file,
)


class FileIOTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_utf8_lf_roundtrip(self) -> None:
        path = self.root / "sample.py"
        text = "def hello():\n    return 'héllo'\n"
        written = save_text_file(path, text, encoding="utf-8", newline="\n")
        self.assertEqual(written, len(text.encode("utf-8")))
        result = load_text_file(path)
        self.assertTrue(result.ok)
        self.assertEqual(result.text, text)
        self.assertEqual(result.encoding, "utf-8")
        self.assertEqual(result.newline, "\n")
        self.assertEqual(result.bom, b"")

    def test_crlf_is_preserved(self) -> None:
        path = self.root / "windows.txt"
        path.write_bytes(b"line one\r\nline two\r\n")
        result = load_text_file(path)
        self.assertTrue(result.ok)
        self.assertEqual(result.text, "line one\nline two\n")
        self.assertEqual(result.newline, "\r\n")
        save_text_file(path, result.text, newline=result.newline)
        self.assertEqual(path.read_bytes(), b"line one\r\nline two\r\n")

    def test_utf8_bom_is_preserved(self) -> None:
        path = self.root / "bom.txt"
        path.write_bytes(codecs.BOM_UTF8 + b"hello\n")
        result = load_text_file(path)
        self.assertTrue(result.ok)
        self.assertEqual(result.bom, codecs.BOM_UTF8)
        self.assertEqual(result.text, "hello\n")
        save_text_file(path, result.text, bom=result.bom)
        self.assertTrue(path.read_bytes().startswith(codecs.BOM_UTF8))
        self.assertEqual(path.read_bytes()[len(codecs.BOM_UTF8) :], b"hello\n")

    def test_utf16_le_is_preserved(self) -> None:
        path = self.root / "utf16.txt"
        path.write_bytes(codecs.BOM_UTF16_LE + "héllo\n".encode("utf-16-le"))
        result = load_text_file(path)
        self.assertTrue(result.ok)
        self.assertEqual(result.encoding, "utf-16-le")
        self.assertEqual(result.text, "héllo\n")
        save_text_file(
            path,
            result.text,
            encoding=result.encoding,
            bom=result.bom,
            newline=result.newline,
        )
        raw = path.read_bytes()
        self.assertTrue(raw.startswith(codecs.BOM_UTF16_LE))
        self.assertEqual(raw[len(codecs.BOM_UTF16_LE) :].decode("utf-16-le"), "héllo\n")

    def test_binary_file_is_rejected(self) -> None:
        path = self.root / "image.bin"
        path.write_bytes(b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR")
        result = load_text_file(path)
        self.assertFalse(result.ok)
        self.assertIn("binary", result.error.lower())

    def test_missing_file(self) -> None:
        result = load_text_file(self.root / "nope.txt")
        self.assertFalse(result.ok)
        self.assertTrue(result.error)

    def test_atomic_save_creates_parent_directories(self) -> None:
        path = self.root / "nested" / "dir" / "file.txt"
        save_text_file(path, "nested\n")
        self.assertEqual(path.read_text(encoding="utf-8"), "nested\n")

    def test_backup(self) -> None:
        path = self.root / "backup.txt"
        path.write_text("old", encoding="utf-8")
        save_text_file(path, "new", backup=True)
        self.assertEqual(path.read_text(encoding="utf-8"), "new")
        self.assertEqual(
            (self.root / "backup.txt.bak").read_text(encoding="utf-8"), "old"
        )

    def test_detect_newline(self) -> None:
        self.assertEqual(detect_newline("a\nb\n"), "\n")
        self.assertEqual(detect_newline("a\r\nb\r\n"), "\r\n")
        self.assertEqual(detect_newline("a\rb\r"), "\r")
        self.assertEqual(detect_newline("a\r\nb\nc\n"), "\n")

    def test_display_path(self) -> None:
        self.assertEqual(display_path(None), "untitled")
        self.assertIn("sample", display_path(self.root / "sample.txt"))


if __name__ == "__main__":
    unittest.main()
