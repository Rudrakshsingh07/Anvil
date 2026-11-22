from __future__ import annotations

import unittest

from dsa_editor.highlight import (
    language_label,
    lexer_for_language,
    lexer_for_path,
)
from dsa_editor.keywords import available_languages, keyword_entries, normalize_language
from dsa_editor.structures.stats import compute_text_stats, cursor_line_col


class StatsTests(unittest.TestCase):
    def test_empty(self) -> None:
        stats = compute_text_stats("")
        self.assertEqual(stats.words, 0)
        self.assertEqual(stats.chars, 0)
        self.assertEqual(stats.lines, 1)

    def test_counts(self) -> None:
        text = "hello world\nsecond line\n"
        stats = compute_text_stats(text)
        self.assertEqual(stats.words, 4)
        self.assertEqual(stats.chars, len(text))
        self.assertEqual(stats.chars_no_whitespace, len("helloworldsecondline"))
        self.assertEqual(stats.lines, 3)

    def test_cursor_line_col(self) -> None:
        text = "abc\ndefg\nhi"
        self.assertEqual(cursor_line_col(text, 0), (1, 1))
        self.assertEqual(cursor_line_col(text, 2), (1, 3))
        self.assertEqual(cursor_line_col(text, 4), (2, 1))
        self.assertEqual(cursor_line_col(text, 6), (2, 3))
        self.assertEqual(cursor_line_col(text, len(text)), (3, 3))


class KeywordTests(unittest.TestCase):
    def test_python_keywords(self) -> None:
        entries = dict((word, (weight, kind)) for word, weight, kind in keyword_entries("python"))
        self.assertIn("def", entries)
        self.assertIn("return", entries)
        self.assertEqual(entries["def"][1], "keyword")
        self.assertIn("print", entries)

    def test_aliases_and_fallback(self) -> None:
        self.assertEqual(normalize_language("py"), "python")
        self.assertEqual(normalize_language("C++"), "cpp")
        cpp = {word for word, _, _ in keyword_entries("c++")}
        self.assertIn("namespace", cpp)
        unknown = {word for word, _, _ in keyword_entries("does-not-exist")}
        self.assertIn("value", unknown)

    def test_available_languages(self) -> None:
        languages = available_languages()
        self.assertIn("python", languages)
        self.assertIn("javascript", languages)


class HighlightTests(unittest.TestCase):
    def test_path_detection(self) -> None:
        cls, key = lexer_for_path("demo.py")
        self.assertEqual(key, "python")
        self.assertEqual(cls.__name__, "PythonLexer")

        cls, key = lexer_for_path("page.html")
        self.assertEqual(key, "html")

        cls, key = lexer_for_path("mystery.zzz")
        self.assertEqual(key, "text")
        self.assertEqual(cls.__name__, "TextLexer")

    def test_language_lookup(self) -> None:
        result = lexer_for_language("python")
        self.assertIsNotNone(result)
        assert result is not None
        self.assertEqual(result[1], "python")

        result = lexer_for_language("js")
        self.assertIsNotNone(result)
        assert result is not None
        self.assertEqual(result[1], "javascript")

        self.assertIsNone(lexer_for_language("not-a-language"))
        self.assertEqual(lexer_for_language("text")[1], "text")  # type: ignore[index]

    def test_label(self) -> None:
        cls, _ = lexer_for_path("demo.py")
        self.assertIn("Python", language_label(cls))


if __name__ == "__main__":
    unittest.main()
