"""Load the bundled keyword lists used to seed the completion trie."""

from __future__ import annotations

import json
from functools import lru_cache
from importlib import resources

from .highlight import _ALIAS_MAP

_COMMON_FALLBACK = (
    "value result data index item node list array string number object "
    "function method class self this true false null error file path name "
    "text line word count size key map set queue stack trie tree save open "
    "read write insert delete remove update search sort merge split join "
    "start end next previous head tail push pop peek enqueue dequeue"
).split()


@lru_cache(maxsize=1)
def _load_data() -> dict[str, list[str]]:
    try:
        data_file = resources.files("dsa_editor.data").joinpath("keywords.json")
        with data_file.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
        if not isinstance(data, dict):
            raise ValueError("keywords.json must contain an object")
        return {
            str(key).lower(): [str(word) for word in words]
            for key, words in data.items()
            if isinstance(words, list)
        }
    except (OSError, ValueError, json.JSONDecodeError):
        return {"common": list(_COMMON_FALLBACK)}


def normalize_language(language: str) -> str:
    key = (language or "text").strip().lower()
    return _ALIAS_MAP.get(key, key)


def keyword_entries(language: str) -> list[tuple[str, int, str]]:
    """Return ``(word, weight, kind)`` seeds for the trie.

    Language keywords outrank generic words.  Duplicates are removed with the
    higher-priority entry winning.
    """

    data = _load_data()
    key = normalize_language(language)
    entries: dict[str, tuple[int, str]] = {}

    for word in data.get("common", _COMMON_FALLBACK):
        entries[word] = (45, "common")

    if key in data and key != "common":
        for word in data[key]:
            entries[word] = (80, "keyword")

    return [(word, weight, kind) for word, (weight, kind) in entries.items()]


def common_words() -> list[str]:
    return list(_load_data().get("common", _COMMON_FALLBACK))


def available_languages() -> list[str]:
    return sorted(key for key in _load_data() if key != "common")
