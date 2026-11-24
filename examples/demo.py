"""Sample file for the DSA Editor + live visualizer demo.

Open it with:

    anvil examples/demo.py

Then start the visualizer in another terminal:

    anvil-viz
"""

from __future__ import annotations

import re


def word_count(text: str) -> int:
    """Count whitespace-separated words, notepad style."""
    return len(re.findall(r"\S+", text))


def main() -> None:
    sample = "hello trie, hello stack, hello queue"
    print(word_count(sample))


if __name__ == "__main__":
    main()
