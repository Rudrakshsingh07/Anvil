"""Language detection and Pygments lexer selection."""

from __future__ import annotations

from functools import lru_cache
from pathlib import Path

from pygments.lexers import get_lexer_by_name, get_lexer_for_filename
from pygments.lexers.special import TextLexer
from pygments.util import ClassNotFound

# Pygments alias -> key used in data/keywords.json
_ALIAS_MAP = {
    "c++": "cpp",
    "c#": "csharp",
    "cs": "csharp",
    "js": "javascript",
    "node": "javascript",
    "ts": "typescript",
    "py": "python",
    "sh": "bash",
    "shell": "bash",
    "zsh": "bash",
    "console": "bash",
    "md": "markdown",
    "yml": "yaml",
    "htm": "html",
    "xhtml": "html",
    "rs": "rust",
    "rb": "ruby",
    "kt": "kotlin",
    "kts": "kotlin",
    "golang": "go",
    "docker": "bash",
    "dockerfile": "bash",
    "plain": "text",
    "text": "text",
}

_TEXT_ALIASES = {"text", "plain", "none", "txt"}


def _alias_of(lexer) -> str:
    aliases = getattr(lexer, "aliases", None) or []
    if not aliases:
        return "text"
    alias = str(aliases[0]).lower()
    return _ALIAS_MAP.get(alias, alias)


def lexer_for_path(path: str | Path) -> tuple[type, str]:
    """Return ``(pygments_lexer_class, language_key)`` for a file name."""

    try:
        lexer = get_lexer_for_filename(str(path), stripnl=False)
    except ClassNotFound:
        return TextLexer, "text"
    return type(lexer), _alias_of(lexer)


def lexer_for_language(name: str) -> tuple[type, str] | None:
    """Look up a lexer by Pygments alias.  Returns None when unknown."""

    key = name.strip().lower()
    if key in _TEXT_ALIASES:
        return TextLexer, "text"
    try:
        lexer = get_lexer_by_name(key, stripnl=False)
    except ClassNotFound:
        return None
    return type(lexer), _alias_of(lexer)


def language_label(lexer_cls: type) -> str:
    try:
        return lexer_cls().name  # type: ignore[call-arg]
    except Exception:  # pragma: no cover - defensive for exotic lexers
        return lexer_cls.__name__.replace("Lexer", "")


@lru_cache(maxsize=32)
def _known_languages() -> tuple[str, ...]:
    try:
        from pygments.lexers import get_all_lexers

        aliases: set[str] = set()
        for _, aliases_list, _, _ in get_all_lexers():
            for alias in aliases_list:
                aliases.add(_ALIAS_MAP.get(alias.lower(), alias.lower()))
        return tuple(sorted(aliases))
    except Exception:  # pragma: no cover
        return ()


def known_languages() -> tuple[str, ...]:
    return _known_languages()
