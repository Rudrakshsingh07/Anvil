"""Document statistics (live word/character/line count)."""

from __future__ import annotations

import re
from dataclasses import dataclass

_WORD_RE = re.compile(r"\S+")


@dataclass(frozen=True)
class TextStats:
    words: int
    chars: int
    chars_no_whitespace: int
    lines: int

    def to_dict(self) -> dict:
        return {
            "words": self.words,
            "chars": self.chars,
            "chars_no_whitespace": self.chars_no_whitespace,
            "lines": self.lines,
        }


def compute_text_stats(text: str) -> TextStats:
    return TextStats(
        words=len(_WORD_RE.findall(text)),
        chars=len(text),
        chars_no_whitespace=sum(1 for char in text if not char.isspace()),
        lines=text.count("\n") + 1,
    )


def cursor_line_col(text: str, index: int) -> tuple[int, int]:
    """Return 1-based ``(line, column)`` for a character index."""

    index = max(0, min(index, len(text)))
    line = text.count("\n", 0, index) + 1
    last_newline = text.rfind("\n", 0, index)
    column = index - last_newline
    return line, column
