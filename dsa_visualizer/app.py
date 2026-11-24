"""Tkinter front-end for the live data-structure visualizer.

The window never imports editor internals.  It draws whatever the public state
bridge publishes, so it can run in a separate process, on a second monitor, or
after the editor has already exited (using the last JSON snapshot).
"""

from __future__ import annotations

import queue
import sys
import tkinter as tk
import traceback
from tkinter import font as tkfont
from typing import Any, Callable

from .model import (
    ChangeTracker,
    build_trie_lens,
    clipboard_cards,
    format_age,
    format_int,
    layout_trie,
    shorten,
    sparkline_points,
    stack_cards,
    suggestion_words,
)
from .source import LiveSource, SourceStatus
from .theme import (
    ACCENT,
    ACCENT_DARK,
    BG,
    BLUE,
    BORDER,
    DIM,
    GOLD,
    GOLD_DARK,
    GREEN,
    GREEN_DARK,
    MUTED,
    PANEL,
    PANEL_2,
    PANEL_3,
    PINK,
    PURPLE,
    PURPLE_DARK,
    RED,
    STATUS_COLORS,
    TEXT,
    badge,
    event_color,
    fit_text,
    kind_color,
    meter,
    round_rect,
    wrap_lines,
)

MONO_FAMILIES = (
    "JetBrains Mono",
    "Fira Code",
    "Cascadia Code",
    "Source Code Pro",
    "DejaVu Sans Mono",
    "Liberation Mono",
    "Noto Sans Mono",
    "Ubuntu Mono",
    "Courier New",
)

VIEWS = ("trie", "undo", "redo", "clip", "stats", "events")


class Visualizer(tk.Tk):
    def __init__(
        self,
        source: LiveSource,
        *,
        geometry: str = "1500x1000",
        title: str = "anvil · live data structures",
    ) -> None:
        super().__init__()
        self.source = source
        self.latest: dict[str, Any] = {}
        self.trie_data: dict[str, Any] = {}
        self.tracker = ChangeTracker(history=140, max_events=8)
        self.paused = False
        self._last_seq: int | None = None
        self._pending_state: dict[str, Any] | None = None
        self._pending_trie: dict[str, Any] | None = None
        self._dirty: set[str] = set()
        self._jobs: dict[str, str] = {}
        self._pill_text = ""
        self._last_status: SourceStatus | None = None
        self._closed = False

        self.title(title)
        self.geometry(geometry)
        self.minsize(1080, 720)
        self.configure(bg=BG)

        family = self._pick_family()
        self.f_brand = tkfont.Font(family=family, size=20, weight="bold")
        self.f_header = tkfont.Font(family=family, size=13, weight="bold")
        self.f_label = tkfont.Font(family=family, size=8, weight="bold")
        self.f_mono = tkfont.Font(family=family, size=10)
        self.f_mono_bold = tkfont.Font(family=family, size=10, weight="bold")
        self.f_small = tkfont.Font(family=family, size=8)
        self.f_small_bold = tkfont.Font(family=family, size=8, weight="bold")
        self.f_tiny = tkfont.Font(family=family, size=7)
        self.f_big = tkfont.Font(family=family, size=34, weight="bold")

        self._build_ui()
        self.bind("<Escape>", lambda _event: self.close())
        self.bind("q", lambda _event: self.close())
        self.bind("r", lambda _event: self.reconnect())
        self.bind("<space>", lambda _event: self.toggle_pause())
        self.bind("<F11>", lambda _event: self.toggle_fullscreen())
        self.protocol("WM_DELETE_WINDOW", self.close)

        self.source.start()
        self.after(50, self._pump)

    # ------------------------------------------------------------------ setup
    def _pick_family(self) -> str:
        available = set(tkfont.families(self))
        for name in MONO_FAMILIES:
            if name in available:
                return name
        return "TkFixedFont"

    def _card(self, parent: tk.Misc) -> tk.Frame:
        return tk.Frame(
            parent,
            bg=PANEL,
            bd=0,
            highlightthickness=1,
            highlightbackground=BORDER,
            highlightcolor=BORDER,
        )

    def _canvas(self, parent: tk.Misc, **kwargs) -> tk.Canvas:
        canvas = tk.Canvas(
            parent,
            bg=PANEL,
            bd=0,
            highlightthickness=0,
            takefocus=0,
            **kwargs,
        )
        return canvas

    def _build_ui(self) -> None:
        self.grid_rowconfigure(0, weight=0)
        self.grid_rowconfigure(1, weight=1)
        self.grid_rowconfigure(2, weight=0)
        self.grid_rowconfigure(3, weight=0)
        self.grid_rowconfigure(4, weight=0)
        self.grid_columnconfigure(0, weight=1)

        self._build_header()
        self._build_main()
        self._build_clipboard()
        self._build_events()
        self._build_footer()

    def _build_header(self) -> None:
        header = tk.Frame(self, bg=BG, padx=20, pady=8)
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(0, weight=1)

        brand = tk.Frame(header, bg=BG)
        brand.grid(row=0, column=0, sticky="w")
        tk.Label(
            brand, text="ANVIL", bg=BG, fg=ACCENT, font=self.f_brand
        ).pack(side="left")
        tk.Label(
            brand,
            text="LIVE DATA STRUCTURES",
            bg=BG,
            fg=MUTED,
            font=self.f_label,
        ).pack(side="left", padx=(12, 0), pady=(8, 0))

        status = tk.Frame(header, bg=BG)
        status.grid(row=0, column=1, sticky="e")
        self.pill = tk.Label(
            status,
            text="● CONNECTING",
            bg=ACCENT_DARK,
            fg=ACCENT,
            font=self.f_small_bold,
            padx=10,
            pady=4,
        )
        self.pill.pack(side="left")
        self.seq_label = tk.Label(
            status, text="seq 0", bg=BG, fg=DIM, font=self.f_mono
        )
        self.seq_label.pack(side="left", padx=(10, 0))

        info = tk.Frame(header, bg=BG)
        info.grid(row=1, column=0, sticky="w", pady=(6, 0))
        self.file_label = tk.Label(
            info, text="untitled", bg=BG, fg=TEXT, font=self.f_header
        )
        self.file_label.pack(side="left")
        self.meta_label = tk.Label(
            info, text="waiting for the editor", bg=BG, fg=MUTED, font=self.f_mono
        )
        self.meta_label.pack(side="left", padx=(14, 0))

        self.message_label = tk.Label(
            header,
            text="",
            bg=BG,
            fg=MUTED,
            font=self.f_mono,
            anchor="e",
            width=54,
        )
        self.message_label.grid(row=1, column=1, sticky="e", pady=(6, 0))

        self.path_label = tk.Label(
            header,
            text="",
            bg=BG,
            fg=DIM,
            font=self.f_small,
            anchor="w",
        )
        self.path_label.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(2, 0))

    def _build_main(self) -> None:
        main = tk.Frame(self, bg=BG, padx=20)
        main.grid(row=1, column=0, sticky="nsew")
        main.columnconfigure(0, weight=3, minsize=580)
        main.columnconfigure(1, weight=2, minsize=430)
        main.rowconfigure(0, weight=1)

        self.trie_card = self._card(main)
        self.trie_card.grid(row=0, column=0, sticky="nsew", padx=(0, 12))
        self.trie_canvas = self._canvas(self.trie_card)
        self.trie_canvas.pack(fill="both", expand=True)

        right = tk.Frame(main, bg=BG)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        right.rowconfigure(0, weight=0)
        right.rowconfigure(1, weight=4)
        right.rowconfigure(2, weight=2)

        stats_card = self._card(right)
        stats_card.grid(row=0, column=0, sticky="ew")
        self.stats_canvas = self._canvas(stats_card, height=164)
        self.stats_canvas.pack(fill="both", expand=True)

        undo_card = self._card(right)
        undo_card.grid(row=1, column=0, sticky="nsew", pady=(12, 0))
        self.undo_canvas = self._canvas(undo_card, height=1)
        self.undo_canvas.pack(fill="both", expand=True)

        redo_card = self._card(right)
        redo_card.grid(row=2, column=0, sticky="nsew", pady=(12, 0))
        self.redo_canvas = self._canvas(redo_card, height=1)
        self.redo_canvas.pack(fill="both", expand=True)

        for view, canvas in (
            ("trie", self.trie_canvas),
            ("undo", self.undo_canvas),
            ("redo", self.redo_canvas),
            ("stats", self.stats_canvas),
        ):
            canvas.bind(
                "<Configure>", lambda _event, name=view: self._schedule(name)
            )

    def _build_clipboard(self) -> None:
        self.clip_card = self._card(self)
        self.clip_card.grid(
            row=2, column=0, sticky="ew", padx=20, pady=(12, 0)
        )
        self.clip_canvas = self._canvas(self.clip_card, height=128)
        self.clip_canvas.pack(fill="both", expand=True)
        self.clip_canvas.bind("<Configure>", lambda _event: self._schedule("clip"))

    def _build_events(self) -> None:
        self.events_card = self._card(self)
        self.events_card.grid(
            row=3, column=0, sticky="ew", padx=20, pady=(12, 0)
        )
        self.events_canvas = self._canvas(self.events_card, height=92)
        self.events_canvas.pack(fill="both", expand=True)
        self.events_canvas.bind(
            "<Configure>", lambda _event: self._schedule("events")
        )

    def _build_footer(self) -> None:
        footer = tk.Frame(self, bg=BG, padx=20, pady=8)
        footer.grid(row=4, column=0, sticky="ew")
        footer.columnconfigure(0, weight=1)
        tk.Label(
            footer,
            text="q quit   ·   r reconnect   ·   space pause   ·   F11 fullscreen",
            bg=BG,
            fg=DIM,
            font=self.f_small,
        ).grid(row=0, column=0, sticky="w")
        self.source_label = tk.Label(
            footer,
            text="",
            bg=BG,
            fg=DIM,
            font=self.f_small,
            anchor="e",
        )
        self.source_label.grid(row=0, column=1, sticky="e")

    # ------------------------------------------------------------------ pump
    def _pump(self) -> None:
        if self._closed:
            return
        self._drain()
        self._update_status(self.source.status())
        self._flush()
        self.after(55, self._pump)

    def _drain(self) -> None:
        while True:
            try:
                kind, payload, _mode = self.source.queue.get_nowait()
            except queue.Empty:
                break
            if kind == "state":
                if self.paused:
                    self._pending_state = payload
                else:
                    self._apply_state(payload)
            elif kind == "trie":
                if self.paused:
                    self._pending_trie = payload
                else:
                    self.trie_data = payload
                    self._dirty.add("trie")

    def _apply_state(self, snapshot: dict[str, Any]) -> None:
        seq = snapshot.get("seq")
        if seq == self._last_seq:
            return
        self._last_seq = seq
        self.latest = snapshot
        trie = snapshot.get("trie") or {}
        if trie:
            self.trie_data = trie
        self.tracker.update(snapshot)
        self._dirty.update(VIEWS)
        self._update_header(snapshot)

    def _update_header(self, snapshot: dict[str, Any]) -> None:
        file_info = snapshot.get("file") or {}
        stats = snapshot.get("stats") or {}
        app = snapshot.get("app") or {}
        ui = snapshot.get("ui") or {}

        name = file_info.get("name") or "untitled"
        dirty = bool(file_info.get("dirty"))
        marker = "●" if dirty else "✓"
        self.file_label.configure(
            text=f"{name}  {marker}", fg=GOLD if dirty else TEXT
        )

        self.meta_label.configure(
            text=(
                f"{file_info.get('language') or 'text'}"
                f" · {file_info.get('encoding') or 'utf-8'}"
                f" · {file_info.get('newline') or 'LF'}"
                f" · {format_int(stats.get('words'))} words"
                f" · {format_int(stats.get('lines'))} lines"
                f" · {format_int(stats.get('chars'))} chars"
            )
        )

        path = file_info.get("display_path") or file_info.get("path") or "unsaved buffer"
        state_file = app.get("state_file") or "state file off"
        self.path_label.configure(text=f"{path}    ·    {state_file}")

        message = ui.get("message")
        if message:
            color = RED if ui.get("message_error") else ACCENT
            self.message_label.configure(text=shorten(str(message), 54), fg=color)
        else:
            port = app.get("port")
            self.message_label.configure(
                text=f"read-only bridge http://127.0.0.1:{port}/state"
                if port
                else "bridge disabled — using the state file",
                fg=DIM,
            )

    def _update_status(self, status: SourceStatus) -> None:
        mode = "paused" if self.paused else status.mode
        fg, bg = STATUS_COLORS.get(mode, (MUTED, PANEL_3))
        label = "paused" if self.paused else status.label
        text = f"● {label.upper()}"
        if text != self._pill_text:
            self.pill.configure(text=text, fg=fg, bg=bg)
            self._pill_text = text
        self.seq_label.configure(text=f"seq {format_int(status.seq or 0)}")

        if status is not self._last_status:
            self._last_status = status
            detail = shorten(status.message, 84)
            self.source_label.configure(
                text=f"{detail}   ·   {shorten(str(self.source.state_file), 60)}"
            )

    def _flush(self) -> None:
        if not self._dirty:
            return
        dirty = set(self._dirty)
        self._dirty.clear()
        for view in VIEWS:
            if view in dirty:
                self._draw(view)

    def _schedule(self, view: str, delay: int = 70) -> None:
        job = self._jobs.pop(view, None)
        if job is not None:
            try:
                self.after_cancel(job)
            except tk.TclError:
                pass
        self._jobs[view] = self.after(delay, lambda: self._draw(view))

    def _draw(self, view: str) -> None:
        self._jobs.pop(view, None)
        if self._closed:
            return
        drawer: Callable[[], None] | None = {
            "trie": self._draw_trie,
            "undo": lambda: self._draw_stack("undo"),
            "redo": lambda: self._draw_stack("redo"),
            "clip": self._draw_clipboard,
            "stats": self._draw_stats,
            "events": self._draw_events,
        }.get(view)
        if drawer is not None:
            try:
                drawer()
            except Exception as exc:  # keep the rest of the UI alive
                print(f"[anvil-viz] draw error in {view}: {exc}", file=sys.stderr)
                traceback.print_exc()

    # ------------------------------------------------------------------ trie
    def _draw_trie(self) -> None:
        canvas = self.trie_canvas
        canvas.delete("all")
        width = canvas.winfo_width()
        height = canvas.winfo_height()
        if width < 80 or height < 80:
            return

        trie = self.trie_data or {}
        snapshot_trie = self.latest.get("trie") or {}
        prefix = str(snapshot_trie.get("prefix") or "")
        if str(trie.get("prefix") or "") != prefix:
            trie = snapshot_trie
        prefix = str(trie.get("prefix") or "")
        suggestions = trie.get("suggestions") or []
        suggestion_names = suggestion_words(trie)
        stats = trie.get("stats") or {}

        canvas.create_text(
            20,
            16,
            text="PREFIX TRIE",
            anchor="nw",
            fill=MUTED,
            font=self.f_label,
        )
        if prefix:
            canvas.create_text(
                20,
                34,
                text=f"prefix {prefix!r}",
                anchor="nw",
                fill=ACCENT,
                font=self.f_mono_bold,
            )
        else:
            canvas.create_text(
                20,
                34,
                text="prefix — start typing an identifier",
                anchor="nw",
                fill=DIM,
                font=self.f_mono,
            )
        canvas.create_text(
            width - 20,
            16,
            text=(
                f"{format_int(stats.get('nodes'))} nodes · "
                f"{format_int(stats.get('words'))} words · "
                f"depth {format_int(stats.get('max_depth'))}"
            ),
            anchor="ne",
            fill=MUTED,
            font=self.f_small,
        )

        if width > 620:
            self._draw_trie_legend(canvas, width, 34)

        top = 66.0
        bottom = 62.0
        tree_height = max(80.0, height - top - bottom)
        max_nodes = max(18, min(64, int(width * tree_height / 16000)))
        lens = build_trie_lens(
            trie.get("tree"),
            prefix,
            suggestion_names,
            max_nodes=max_nodes,
            max_depth=8,
            max_children=6,
        )

        if lens.root is None:
            canvas.create_text(
                width / 2,
                top + tree_height / 2,
                text="the trie is empty",
                fill=DIM,
                font=self.f_mono,
            )
            canvas.create_text(
                width / 2,
                top + tree_height / 2 + 22,
                text="open a file or type an identifier in the editor",
                fill=DIM,
                font=self.f_small,
            )
        else:
            nodes = layout_trie(
                lens, width, tree_height, margin_x=46, margin_y=28
            )
            for node in nodes:
                node.y += top

            for parent, child in lens.edges():
                on_path = parent.on_path and child.on_path
                canvas.create_line(
                    parent.x,
                    parent.y,
                    child.x,
                    child.y,
                    fill=ACCENT if on_path else BORDER,
                    width=2.2 if on_path else 1.2,
                )

            radius = 11.0 if lens.node_count <= 42 else 9.0
            for node in nodes:
                if node.on_path:
                    fill, outline, outline_width = ACCENT_DARK, ACCENT, 2.2
                elif node.suggested:
                    fill, outline, outline_width = GREEN_DARK, GREEN, 1.8
                elif node.terminal:
                    fill, outline, outline_width = GOLD_DARK, GOLD, 1.5
                else:
                    fill, outline, outline_width = PANEL_2, BORDER, 1.0
                canvas.create_oval(
                    node.x - radius,
                    node.y - radius,
                    node.x + radius,
                    node.y + radius,
                    fill=fill,
                    outline=outline,
                    width=outline_width,
                )
                label = node.char if node.depth else "·"
                canvas.create_text(
                    node.x,
                    node.y,
                    text=label,
                    fill=TEXT if node.on_path or node.terminal or node.suggested else MUTED,
                    font=self.f_mono_bold if node.depth <= 2 else self.f_small_bold,
                )

            if lens.matched_prefix != prefix:
                caption = (
                    f"prefix {prefix!r} not in the trie yet"
                    f" · deepest match {lens.matched_prefix!r}"
                    " · identifiers are learned within a second"
                )
            elif prefix:
                caption = f"showing {lens.node_count} node(s) below {prefix!r}"
            else:
                caption = f"showing {lens.node_count} node(s) from the root"
            if lens.truncated or (trie.get("tree_meta") or {}).get("truncated"):
                caption += " · depth-limited"
            canvas.create_text(
                20,
                height - bottom + 4,
                text=fit_text(self.f_tiny, caption, width - 40),
                anchor="nw",
                fill=DIM,
                font=self.f_tiny,
            )

        if suggestions:
            self._draw_suggestion_chips(
                canvas, suggestions, width, height, title="suggestions"
            )
        elif not prefix:
            top_words = trie.get("top_words") or []
            self._draw_suggestion_chips(
                canvas, top_words, width, height, title="top words"
            )
        else:
            canvas.create_text(
                20,
                height - 22,
                text=f"no completions for {prefix!r} yet",
                anchor="w",
                fill=DIM,
                font=self.f_tiny,
            )

    def _draw_trie_legend(self, canvas: tk.Canvas, width: int, y: float) -> None:
        """Draw the node-colour key in the top-right corner."""

        x = width - 20.0
        for label, color in (
            ("suggestion", GREEN),
            ("word", GOLD),
            ("prefix path", ACCENT),
        ):
            text_width = self.f_tiny.measure(label)
            canvas.create_text(
                x, y, text=label, anchor="ne", fill=MUTED, font=self.f_tiny
            )
            canvas.create_oval(
                x - text_width - 11,
                y - 3,
                x - text_width - 5,
                y + 3,
                fill=color,
                outline="",
            )
            x -= text_width + 26

    def _draw_suggestion_chips(
        self,
        canvas: tk.Canvas,
        suggestions: list[dict[str, Any]],
        width: int,
        height: int,
        *,
        title: str | None = None,
    ) -> None:
        if not suggestions:
            return
        x = 20.0
        if title:
            canvas.create_text(
                20,
                height - 23,
                text=title,
                anchor="w",
                fill=DIM,
                font=self.f_tiny,
            )
            x += self.f_tiny.measure(title) + 12
        y = height - 34.0
        for item in suggestions[:7]:
            word = str(item.get("word") or "")
            kind = str(item.get("kind") or "word")
            weight = int(item.get("weight") or 0)
            label = f"{word}  {kind} {weight}"
            chip_width = self.f_small.measure(label) + 18
            if x + chip_width > width - 20:
                break
            color = {
                "keyword": GOLD,
                "common": ACCENT,
                "word": GREEN,
                "snippet": PURPLE,
            }.get(kind, MUTED)
            round_rect(
                canvas,
                x,
                y,
                x + chip_width,
                y + 22,
                11,
                fill=PANEL_2,
                outline=BORDER,
            )
            canvas.create_text(
                x + 9,
                y + 11,
                text=label,
                anchor="w",
                fill=color,
                font=self.f_small,
            )
            x += chip_width + 8

    # ----------------------------------------------------------------- stacks
    def _draw_stack(self, which: str) -> None:
        canvas = self.undo_canvas if which == "undo" else self.redo_canvas
        canvas.delete("all")
        width = canvas.winfo_width()
        height = canvas.winfo_height()
        if width < 80 or height < 60:
            return

        undo = self.latest.get("undo") or {}
        section = undo.get(f"{which}_stack") or {}
        entries = section.get("entries") or []
        depth = int(section.get("depth") or 0)
        capacity = int(section.get("capacity") or 0)
        title = "UNDO STACK" if which == "undo" else "REDO STACK"
        color = ACCENT if which == "undo" else PURPLE

        canvas.create_text(
            16, 10, text=title, anchor="nw", fill=MUTED, font=self.f_label
        )
        canvas.create_text(
            width - 16,
            10,
            text=f"{format_int(depth)} / {format_int(capacity)}",
            anchor="ne",
            fill=color if depth else DIM,
            font=self.f_mono_bold,
        )

        fraction = depth / capacity if capacity else 0.0
        meter(
            canvas,
            16,
            28,
            width - 16,
            34,
            fraction,
            fill=color,
            background=PANEL_3,
        )

        if which == "undo":
            group = undo.get("group") or {}
            if group.get("active"):
                badge(
                    canvas,
                    16,
                    38,
                    f"grouping {group.get('kind')}",
                    self.f_tiny,
                    fg=GREEN,
                    bg=GREEN_DARK,
                )

        card_top = 46.0
        card_height = 46.0
        gap = 5.0
        available = max(0.0, height - card_top - 16.0)
        max_items = max(1, int((available + gap) // (card_height + gap)))
        visible, hidden = stack_cards(entries, max_items)

        if not visible:
            message = (
                "empty — type to push snapshots"
                if which == "undo"
                else "empty — Ctrl-Z moves entries here"
            )
            canvas.create_text(
                width / 2,
                card_top + available / 2,
                text=message,
                fill=DIM,
                font=self.f_small,
            )
            return

        for index, entry in enumerate(visible):
            y = card_top + index * (card_height + gap)
            self._draw_stack_card(
                canvas, 16, y, width - 16, y + card_height, entry, index, color
            )

        if hidden:
            canvas.create_text(
                width - 16,
                height - 10,
                text=f"+{hidden} more in stack",
                anchor="se",
                fill=DIM,
                font=self.f_tiny,
            )

    def _draw_stack_card(
        self,
        canvas: tk.Canvas,
        x1: float,
        y1: float,
        x2: float,
        y2: float,
        entry: dict[str, Any],
        index: int,
        color: str,
    ) -> None:
        kind = str(entry.get("kind") or "edit")
        accent = kind_color(kind)
        round_rect(
            canvas, x1, y1, x2, y2, 7, fill=PANEL_2, outline=BORDER
        )
        round_rect(
            canvas, x1, y1 + 4, x1 + 3, y2 - 4, 1.5, fill=accent, outline=""
        )

        label = str(entry.get("label") or kind).upper()
        canvas.create_text(
            x1 + 12,
            y1 + 6,
            text=label,
            anchor="nw",
            fill=accent,
            font=self.f_small_bold,
        )
        if index == 0:
            badge(
                canvas,
                x2 - 52,
                y1 + 4,
                "TOP",
                self.f_tiny,
                fg=color,
                bg=PANEL_3,
            )

        meta = (
            f"{format_int(entry.get('chars'))} chars · "
            f"{format_int(entry.get('edit_count'))} edit(s) · "
            f"{format_age(entry.get('age_seconds'))}"
        )
        canvas.create_text(
            x1 + 12,
            y1 + 19,
            text=fit_text(self.f_tiny, meta, x2 - x1 - 24),
            anchor="nw",
            fill=DIM,
            font=self.f_tiny,
        )
        preview = str(entry.get("text_preview") or "")
        if preview:
            canvas.create_text(
                x1 + 12,
                y1 + 31,
                text=fit_text(self.f_small, preview, x2 - x1 - 24),
                anchor="nw",
                fill=MUTED,
                font=self.f_small,
            )

    # -------------------------------------------------------------- clipboard
    def _draw_clipboard(self) -> None:
        canvas = self.clip_canvas
        canvas.delete("all")
        width = canvas.winfo_width()
        height = canvas.winfo_height()
        if width < 100 or height < 60:
            return

        clipboard = self.latest.get("clipboard") or {}
        entries = clipboard.get("entries") or []
        size = int(clipboard.get("size") or 0)
        capacity = int(clipboard.get("capacity") or 0)

        canvas.create_text(
            18, 12, text="CLIPBOARD QUEUE", anchor="nw", fill=MUTED, font=self.f_label
        )
        canvas.create_text(
            width - 18,
            12,
            text=f"FIFO · newest at tail · {format_int(size)} / {format_int(capacity)}",
            anchor="ne",
            fill=MUTED,
            font=self.f_small,
        )

        if not entries:
            canvas.create_text(
                width / 2,
                height / 2,
                text="no clipboard entries yet — copy with Ctrl-C or cut with Ctrl-X",
                fill=DIM,
                font=self.f_mono,
            )
            return

        max_cards = max(1, min(6, int((width - 40) // 190)))
        visible, hidden = clipboard_cards(entries, max_cards)
        card_top = 40.0
        card_bottom = height - 10.0
        card_height = max(60.0, card_bottom - card_top)
        gap = 22.0
        card_width = min(
            236.0,
            (width - 40 - gap * (len(visible) - 1)) / max(1, len(visible)),
        )

        if hidden:
            canvas.create_text(
                18,
                28,
                text=f"+{hidden} older entr{'y' if hidden == 1 else 'ies'} not shown",
                anchor="nw",
                fill=DIM,
                font=self.f_tiny,
            )

        x = 18.0
        for index, entry in enumerate(visible):
            newest = index == len(visible) - 1
            x2 = x + card_width
            round_rect(
                canvas,
                x,
                card_top,
                x2,
                card_bottom,
                8,
                fill=PANEL_2,
                outline=GREEN if newest else BORDER,
                width=1.6 if newest else 1.0,
            )
            header = f"#{entry.get('id')} · {format_age(entry.get('age_seconds'))}"
            canvas.create_text(
                x + 10,
                card_top + 9,
                text=fit_text(self.f_tiny, header, card_width - 20),
                anchor="nw",
                fill=GREEN if newest else MUTED,
                font=self.f_tiny,
            )
            if newest:
                badge(
                    canvas,
                    x2 - 58,
                    card_top + 6,
                    "NEWEST",
                    self.f_tiny,
                    fg=GREEN,
                    bg=GREEN_DARK,
                )

            preview = str(entry.get("preview") or entry.get("text") or "")
            lines = wrap_lines(
                self.f_small,
                preview,
                card_width - 20,
                max_lines=2 if card_height >= 76 else 1,
            )
            y = card_top + 26
            for line in lines:
                canvas.create_text(
                    x + 10,
                    y,
                    text=line,
                    anchor="nw",
                    fill=TEXT,
                    font=self.f_small,
                )
                y += 14

            meta = (
                f"{format_int(entry.get('chars'))} chars"
                f" · {format_int(entry.get('lines'))} line(s)"
            )
            canvas.create_text(
                x + 10,
                card_bottom - 18,
                text=fit_text(self.f_tiny, meta, card_width - 20),
                anchor="nw",
                fill=DIM,
                font=self.f_tiny,
            )

            if index < len(visible) - 1:
                canvas.create_text(
                    x2 + gap / 2,
                    (card_top + card_bottom) / 2,
                    text="→",
                    fill=ACCENT,
                    font=self.f_mono_bold,
                )
            x += card_width + gap

    # ------------------------------------------------------------------ stats
    def _draw_stats(self) -> None:
        canvas = self.stats_canvas
        canvas.delete("all")
        width = canvas.winfo_width()
        height = canvas.winfo_height()
        if width < 80 or height < 60:
            return

        snapshot = self.latest
        stats = snapshot.get("stats") or {}
        file_info = snapshot.get("file") or {}
        autosave = snapshot.get("autosave") or {}

        canvas.create_text(
            18,
            4,
            text=format_int(stats.get("words")),
            anchor="nw",
            fill=ACCENT,
            font=self.f_big,
        )
        canvas.create_text(
            21,
            52,
            text="WORDS",
            anchor="nw",
            fill=MUTED,
            font=self.f_label,
        )

        rows = (
            ("chars", format_int(stats.get("chars"))),
            ("lines", format_int(stats.get("lines"))),
            ("cursor", f"{format_int(stats.get('cursor_line'))}:{format_int(stats.get('cursor_col'))}"),
            ("last I/O", format_age(file_info.get("seconds_since_save"))),
        )
        x_right = width - 18
        y = 8.0
        for label, value in rows:
            canvas.create_text(
                x_right - 104,
                y,
                text=label.upper(),
                anchor="ne",
                fill=MUTED,
                font=self.f_label,
            )
            canvas.create_text(
                x_right,
                y,
                text=value,
                anchor="ne",
                fill=TEXT,
                font=self.f_mono_bold,
            )
            y += 18

        autosave_text = "autosave on" if autosave.get("enabled") else "autosave off"
        if autosave.get("active") and autosave.get("due_in_seconds") is not None:
            autosave_text += f" · due {format_age(autosave.get('due_in_seconds')).replace(' ago', '')}"
        canvas.create_text(
            18,
            66,
            text=f"{autosave_text}   ·   tab {snapshot.get('settings', {}).get('tab_width', 4)}",
            anchor="nw",
            fill=DIM,
            font=self.f_tiny,
        )

        plot_top = 92.0
        plot_bottom = height - 12.0
        plot_left = 18.0
        plot_right = width - 18.0
        if plot_bottom - plot_top < 24:
            return

        canvas.create_text(
            plot_left,
            plot_top - 12,
            text="HISTORY",
            anchor="nw",
            fill=MUTED,
            font=self.f_label,
        )
        legend_x = plot_right
        canvas.create_text(
            legend_x,
            plot_top - 12,
            text="undo depth",
            anchor="ne",
            fill=PURPLE,
            font=self.f_tiny,
        )
        canvas.create_text(
            legend_x - self.f_tiny.measure("undo depth") - 14,
            plot_top - 12,
            text="words",
            anchor="ne",
            fill=ACCENT,
            font=self.f_tiny,
        )

        for fraction in (0.0, 0.5, 1.0):
            y_line = plot_top + (plot_bottom - plot_top) * fraction
            canvas.create_line(
                plot_left,
                y_line,
                plot_right,
                y_line,
                fill=PANEL_3,
                width=1,
            )

        words = self.tracker.word_series
        undo_depth = self.tracker.undo_series
        if len(words) < 2:
            canvas.create_text(
                (plot_left + plot_right) / 2,
                (plot_top + plot_bottom) / 2,
                text="history builds as you type",
                fill=DIM,
                font=self.f_tiny,
            )
            return

        plot_width = plot_right - plot_left
        plot_height = plot_bottom - plot_top

        word_points = sparkline_points(words, plot_width, plot_height, pad=2)
        canvas.create_line(
            *[coordinate for point in word_points for coordinate in (point[0] + plot_left, point[1] + plot_top)],
            fill=ACCENT,
            width=1.8,
            smooth=True,
        )

        undo_points = sparkline_points(undo_depth, plot_width, plot_height, pad=2)
        canvas.create_line(
            *[coordinate for point in undo_points for coordinate in (point[0] + plot_left, point[1] + plot_top)],
            fill=PURPLE,
            width=1.4,
            dash=(3, 3),
            smooth=True,
        )

    # ----------------------------------------------------------------- events
    def _draw_events(self) -> None:
        canvas = self.events_canvas
        canvas.delete("all")
        width = canvas.winfo_width()
        height = canvas.winfo_height()
        if width < 80 or height < 40:
            return

        canvas.create_text(
            18, 10, text="EVENT LOG", anchor="nw", fill=MUTED, font=self.f_label
        )
        canvas.create_text(
            width - 18,
            10,
            text="newest first",
            anchor="ne",
            fill=DIM,
            font=self.f_tiny,
        )

        events = self.tracker.latest(8)
        if not events:
            canvas.create_text(
                18,
                40,
                text="events appear here: undo pushes, clipboard enqueues, saves, prefix changes…",
                anchor="nw",
                fill=DIM,
                font=self.f_small,
            )
            return

        y = 24.0
        row_height = 16.0
        max_rows = max(1, int((height - y - 2) // row_height))
        for event in events[:max_rows]:
            color = event_color(event.kind)
            canvas.create_oval(
                20, y + 4, 26, y + 10, fill=color, outline=""
            )
            canvas.create_text(
                36,
                y,
                text=f"seq {event.seq:>5}   {event.text}",
                anchor="nw",
                fill=TEXT,
                font=self.f_small,
            )
            y += row_height

    # --------------------------------------------------------------- controls
    def toggle_pause(self) -> None:
        self.paused = not self.paused
        if not self.paused:
            pending = self._pending_state
            self._pending_state = None
            if pending is not None:
                self._apply_state(pending)
            if self._pending_trie is not None:
                self.trie_data = self._pending_trie
                self._pending_trie = None
                self._dirty.add("trie")
        self._last_status = None
        self._update_status(self.source.status())
        self._flush()

    def reconnect(self) -> None:
        self.paused = False
        self._pending_state = None
        self._pending_trie = None
        self._last_status = None
        self.source.restart()
        self._dirty.update(VIEWS)

    def toggle_fullscreen(self) -> None:
        self.attributes("-fullscreen", not bool(self.attributes("-fullscreen")))

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self.source.stop()
        self.destroy()


def run(source: LiveSource, *, geometry: str = "1500x1000") -> None:
    app = Visualizer(source, geometry=geometry)
    app.mainloop()
