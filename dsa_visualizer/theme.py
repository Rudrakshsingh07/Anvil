"""Colour palette and small drawing helpers for the visualizer."""

from __future__ import annotations

from tkinter import Canvas

# --------------------------------------------------------------------------- #
# Palette
# --------------------------------------------------------------------------- #

BG = "#070c17"
PANEL = "#0d1526"
PANEL_2 = "#121d33"
PANEL_3 = "#172440"
BORDER = "#22314d"
TEXT = "#e7eefb"
MUTED = "#8fa1bd"
DIM = "#5a6b88"
ACCENT = "#4cc9f0"
ACCENT_DARK = "#0b3a53"
BLUE = "#3b82f6"
GREEN = "#34d399"
GREEN_DARK = "#063a2c"
GOLD = "#fbbf24"
GOLD_DARK = "#3a2a05"
RED = "#f87171"
RED_DARK = "#3a0d12"
PURPLE = "#a78bfa"
PURPLE_DARK = "#241b3f"
PINK = "#f472b6"

KIND_COLORS = {
    "insert": GREEN,
    "delete": RED,
    "replace": PURPLE,
    "newline": GOLD,
    "edit": ACCENT,
    "paste": BLUE,
    "cut": PINK,
}

EVENT_COLORS = {
    "undo-push": GREEN,
    "undo-pop": GOLD,
    "redo-push": PURPLE,
    "redo-pop": GOLD,
    "clipboard": PINK,
    "save": ACCENT,
    "autosave": BLUE,
    "trie": GREEN,
    "closed": MUTED,
}

STATUS_COLORS = {
    "http": (GREEN, "#052e1f"),
    "file": (GOLD, GOLD_DARK),
    "closed": (MUTED, "#161f30"),
    "waiting": (RED, RED_DARK),
    "connecting": (ACCENT, "#062c3a"),
    "paused": (PURPLE, PURPLE_DARK),
    "error": (RED, RED_DARK),
}


def kind_color(kind: str | None) -> str:
    return KIND_COLORS.get(str(kind or "edit"), ACCENT)


def event_color(kind: str) -> str:
    return EVENT_COLORS.get(kind, ACCENT)


# --------------------------------------------------------------------------- #
# Canvas primitives
# --------------------------------------------------------------------------- #


def round_rect(
    canvas: Canvas,
    x1: float,
    y1: float,
    x2: float,
    y2: float,
    radius: float = 8.0,
    **kwargs,
) -> int:
    """Draw a rounded rectangle as a smoothed polygon."""

    radius = max(0.0, min(radius, abs(x2 - x1) / 2.0, abs(y2 - y1) / 2.0))
    points = [
        x1 + radius,
        y1,
        x2 - radius,
        y1,
        x2,
        y1,
        x2,
        y1 + radius,
        x2,
        y2 - radius,
        x2,
        y2,
        x2 - radius,
        y2,
        x1 + radius,
        y2,
        x1,
        y2,
        x1,
        y2 - radius,
        x1,
        y1 + radius,
        x1,
        y1,
    ]
    return canvas.create_polygon(points, smooth=True, splinesteps=12, **kwargs)


def fit_text(font, text: str, max_width: float) -> str:
    """Trim ``text`` with an ellipsis until it fits ``max_width`` pixels."""

    if not text:
        return ""
    if max_width <= 0:
        return ""
    if font.measure(text) <= max_width:
        return text
    ellipsis = "…"
    low, high = 0, len(text)
    while low < high:
        middle = (low + high + 1) // 2
        if font.measure(text[:middle] + ellipsis) <= max_width:
            low = middle
        else:
            high = middle - 1
    return text[:low] + ellipsis if low else ellipsis


def wrap_lines(font, text: str, max_width: float, max_lines: int = 2) -> list[str]:
    """Wrap text by pixel width into at most ``max_lines`` lines."""

    words = str(text or "").split()
    if not words:
        return []
    lines: list[str] = []
    current = ""
    for word in words:
        candidate = f"{current} {word}".strip()
        if font.measure(candidate) <= max_width or not current:
            current = candidate
        else:
            lines.append(current)
            current = word
            if len(lines) == max_lines:
                break
    if current and len(lines) < max_lines:
        lines.append(current)
    if len(lines) == max_lines:
        joined = " ".join(words)
        if font.measure(" ".join(lines)) < font.measure(joined):
            lines[-1] = fit_text(font, lines[-1] + " …", max_width)
    return lines


def meter(
    canvas: Canvas,
    x1: float,
    y1: float,
    x2: float,
    y2: float,
    fraction: float,
    *,
    fill: str = ACCENT,
    background: str = BORDER,
) -> None:
    """Draw a thin capacity meter."""

    fraction = max(0.0, min(1.0, float(fraction)))
    round_rect(canvas, x1, y1, x2, y2, (y2 - y1) / 2, fill=background, outline="")
    if fraction > 0:
        width = (x2 - x1) * fraction
        if width < (y2 - y1):
            width = min(x2 - x1, y2 - y1)
        round_rect(
            canvas, x1, y1, x1 + width, y2, (y2 - y1) / 2, fill=fill, outline=""
        )


def badge(
    canvas: Canvas,
    x: float,
    y: float,
    text: str,
    font,
    *,
    fg: str = TEXT,
    bg: str = PANEL_3,
    padx: float = 7.0,
    pady: float = 2.0,
) -> float:
    """Draw a small pill and return its width."""

    width = font.measure(text) + 2 * padx
    height = font.metrics("linespace") + 2 * pady
    round_rect(canvas, x, y, x + width, y + height, height / 2, fill=bg, outline="")
    canvas.create_text(
        x + width / 2,
        y + height / 2,
        text=text,
        fill=fg,
        font=font,
        anchor="center",
    )
    return width
