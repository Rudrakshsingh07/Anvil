"""The full-screen TUI editor.

UI framework: prompt_toolkit.  All editor data structures (undo stacks, trie,
clipboard queue, statistics) are implemented locally in ``structures`` and are
published to the state bridge for the external visualizer.
"""

from __future__ import annotations

import asyncio
import os
import shutil
import time
import uuid
from pathlib import Path

from prompt_toolkit.application import Application
from prompt_toolkit.auto_suggest import AutoSuggest, Suggestion
from prompt_toolkit.buffer import Buffer
from prompt_toolkit.clipboard import ClipboardData, InMemoryClipboard
from prompt_toolkit.completion import CompleteEvent, Completer, Completion
from prompt_toolkit.document import Document
from prompt_toolkit.filters import Condition, has_completions, has_focus
from prompt_toolkit.key_binding import KeyBindings
from prompt_toolkit.layout import (
    ConditionalContainer,
    Dimension,
    HSplit,
    Layout,
    VSplit,
    Window,
)
from prompt_toolkit.layout.controls import BufferControl, FormattedTextControl
from prompt_toolkit.layout.margins import NumberedMargin
from prompt_toolkit.lexers import DynamicLexer, PygmentsLexer
from prompt_toolkit.selection import SelectionType
from prompt_toolkit.styles import Style, merge_styles, style_from_pygments_cls
from pygments.lexers.special import TextLexer
from pygments.styles import get_style_by_name
from pygments.util import ClassNotFound

from . import __version__
from .bridge import StateHub, StateServer, write_state_file
from .fileio import display_path, load_text_file, newline_name, save_text_file
from .highlight import language_label, lexer_for_language, lexer_for_path
from .keywords import available_languages, keyword_entries
from .structures import (
    ClipboardHistory,
    Trie,
    UndoRedoManager,
    compute_diff,
    compute_text_stats,
    current_prefix,
    cursor_line_col,
)

HELP_TEXT = """\
DSA EDITOR — help
=================

Saving and files
  Ctrl-S / F2      save
  F3               save as (opens the command bar with "w ")
  F4 / Ctrl-O      open file (command bar with "e ")
  Ctrl-N           new file
  Ctrl-Q           quit (press twice if there are unsaved changes)
  F5               toggle autosave (default 30s, only for files with a path)
  Ctrl-A           select all          Shift+arrows  select text
  Ctrl-C / Ctrl-X  copy / cut          Ctrl-V        paste newest clipboard entry
  Alt-1 .. Alt-9   paste clipboard history entry (1 = newest)

History
  Ctrl-Z           undo               Ctrl-Y        redo
  Ctrl-_ / Ctrl-X Ctrl-U               undo (same custom stack)
  Undo and redo are two hand-written bounded stacks. Consecutive typing is
  coalesced into one undo unit; moving the cursor starts a new unit.

Movement and editing
  Arrows           move               Home / End     line start / end
  Ctrl-Left/Right  word left / right  Ctrl-Home/End  buffer start / end
  PageUp/PageDown  scroll a page      Shift+arrows   select text
  Ctrl-@           start selection    Esc            cancel selection
  Backspace/Delete delete             Ctrl-D         delete character
  Ctrl-K           kill to line end   Ctrl-U         kill to line start
  Ctrl-W           delete word before Alt-D          delete word after
  Alt-Backspace    delete word before Ctrl-T         transpose characters
  Ctrl-E           end of line        Ctrl-L         clear screen
  Alt-B / Alt-F    word back / forward

Completion and highlighting
  Just type: the trie popup appears inline after 2 characters.
  Tab              accept/cycle the selected completion
  Ctrl-Right       accept the grey inline (ghost) suggestion
  Shift-Tab        dedent the current line
  Enter            smart newline (keeps indentation, adds one level after : { [ ( )
  Syntax highlighting follows the file extension and can be changed with :lang.

Command bar
  F9 / Ctrl-P      open the command bar
  :w [path]        save (optionally as another path)
  :wq              save and quit
  :e path          open a file; use :e! to discard unsaved changes
  :new             new file; use :new! to discard unsaved changes
  :q  / :q!        quit / quit without saving
  :autosave on|off|<seconds>
  :lang python     change the syntax language (try :lang auto)
  :find text       find next occurrence (wraps)
  :goto 42         go to line 42
  :theme monokai   change the colour theme
  :tab 4           set the soft-tab width
  :undo / :redo    history
  :panel           toggle the data-structure panel
  :state           show the live bridge URL / state file
  :help            show or hide this help
  :clip            show clipboard history count
  Esc              close help / command bar

Data structures panel (F6)
  Live undo/redo depths, trie node/word counts, clipboard queue, suggestions,
  word count, cursor position, autosave countdown and bridge port.
  The same data is exposed to another Python process over HTTP and a JSON file.
  See docs/state_api.md and examples/watch_state.py.

Mouse support is intentionally off; use keyboard selection (Shift+arrows).
"""


class TrieCompleter(Completer):
    def __init__(self, editor: "EditorApp") -> None:
        self.editor = editor

    def get_completions(self, document: Document, complete_event: CompleteEvent):
        prefix = current_prefix(document.text_before_cursor)
        minimum = 1 if complete_event.completion_requested else 2
        if len(prefix) < minimum:
            return
        for word, _weight, kind in self.editor.trie.complete(prefix, limit=50):
            if word == prefix:
                continue
            yield Completion(
                word,
                start_position=-len(prefix),
                display=word,
                display_meta=kind,
            )


class TrieAutoSuggest(AutoSuggest):
    def __init__(self, editor: "EditorApp") -> None:
        self.editor = editor

    def get_suggestion(self, buffer: Buffer, document: Document) -> Suggestion | None:
        if buffer.complete_state is not None:
            return None
        remainder = self.editor.suggestion_for(document.text_before_cursor)
        return Suggestion(remainder) if remainder else None


class EditorApp:
    def __init__(
        self,
        path: str | Path | None = None,
        *,
        autosave: bool = True,
        autosave_interval: float = 30.0,
        port: int = 8765,
        enable_server: bool = True,
        state_file: str | Path | None = None,
        theme: str = "monokai",
        tab_width: int = 4,
        panel: bool | None = None,
        undo_capacity: int = 150,
        clipboard_capacity: int = 50,
    ) -> None:
        self.session_id = uuid.uuid4().hex[:8]
        self.path: Path | None = Path(path).expanduser().resolve() if path else None
        self.encoding = "utf-8"
        self.bom = b""
        self.newline = "\n"
        self.size_bytes = 0
        self.dirty = False
        self.last_saved: float | None = None
        self._saved_text = ""

        self.autosave_enabled = bool(autosave)
        self.autosave_interval = max(1.0, float(autosave_interval))
        self.tab_width = max(1, min(16, int(tab_width)))
        self.theme = theme
        self.enable_server = bool(enable_server)
        self.requested_port = int(port)
        self.state_file = Path(state_file).expanduser() if state_file else None

        self.undo = UndoRedoManager(capacity=undo_capacity)
        self.clipboard_history = ClipboardHistory(capacity=clipboard_capacity)
        self.trie = Trie()
        self.stats = compute_text_stats("")
        self.language_key = "text"
        self.lexer_cls: type = TextLexer
        self._lexer_cache: dict[type, PygmentsLexer] = {}

        self._suspend_undo = False
        self._last_text = ""
        self._last_cursor = 0
        self._text_changed_since_cursor = False
        self._current_prefix_cache = ""
        self._last_publish = 0.0
        self._last_learn = 0.0
        self._last_state_file = 0.0
        self._state_file_error = ""

        self._message = ""
        self._message_until = 0.0
        self._message_error = False
        self._pending_quit_at = 0.0
        self._last_search = ""

        self.show_panel = (
            bool(panel)
            if panel is not None
            else shutil.get_terminal_size((100, 30)).columns >= 100
        )
        self.help_visible = False
        self.command_mode = False

        self.hub = StateHub()
        self.server: StateServer | None = (
            StateServer(
                self.hub,
                port=self.requested_port,
                trie_provider=self._trie_provider,
            )
            if self.enable_server
            else None
        )
        self.port: int | None = None

        # ---------------------------------------------------------- widgets
        self.buffer = Buffer(
            name="editor",
            multiline=True,
            completer=TrieCompleter(self),
            auto_suggest=TrieAutoSuggest(self),
            complete_while_typing=True,
            max_number_of_completions=50,
        )
        self.buffer.on_text_changed += self._on_text_changed
        self.buffer.on_cursor_position_changed += self._on_cursor_position_changed

        self.help_buffer = Buffer(
            name="help",
            multiline=True,
            read_only=True,
            document=Document(HELP_TEXT),
        )
        self.command_buffer = Buffer(
            name="command",
            multiline=False,
            accept_handler=self._accept_command,
        )

        self.editor_control = BufferControl(
            buffer=self.buffer,
            lexer=DynamicLexer(lambda: self._ptk_lexer()),
            include_default_input_processors=True,
        )
        self.help_control = BufferControl(buffer=self.help_buffer)
        self.command_control = BufferControl(buffer=self.command_buffer)

        self.editor_window = Window(
            content=self.editor_control,
            left_margins=[NumberedMargin()],
            wrap_lines=False,
            style="class:editor",
        )
        self.help_window = Window(
            content=self.help_control,
            wrap_lines=True,
            style="class:help",
        )
        self.panel_window = Window(
            content=FormattedTextControl(self._panel_fragments),
            width=Dimension.exact(42),
            wrap_lines=False,
            style="class:panel",
        )
        self.status_window = Window(
            content=FormattedTextControl(self._status_fragments),
            height=1,
            style="class:status",
        )
        self.hint_window = Window(
            content=FormattedTextControl(self._hint_fragments),
            height=1,
            style="class:hint",
        )
        self.command_window = Window(
            content=self.command_control,
            height=1,
            style="class:command",
        )
        self.command_label = Window(
            content=FormattedTextControl(
                lambda: [("class:command.prompt", " : ")]
            ),
            width=3,
            height=1,
            style="class:command",
        )

        self._help_condition = Condition(lambda: self.help_visible)
        self._command_condition = Condition(lambda: self.command_mode)
        self._panel_condition = Condition(lambda: self.show_panel)

        body = HSplit(
            [
                ConditionalContainer(
                    VSplit(
                        [
                            self.editor_window,
                            ConditionalContainer(
                                HSplit(
                                    [
                                        Window(
                                            width=1,
                                            char="│",
                                            style="class:separator",
                                        ),
                                        self.panel_window,
                                    ]
                                ),
                                filter=self._panel_condition,
                            ),
                        ]
                    ),
                    filter=~self._help_condition,
                ),
                ConditionalContainer(self.help_window, filter=self._help_condition),
            ]
        )
        command_bar = ConditionalContainer(
            HSplit(
                [
                    VSplit(
                        [
                            self.command_label,
                            self.command_window,
                        ]
                    )
                ]
            ),
            filter=self._command_condition,
        )

        self.layout = Layout(
            HSplit([body, command_bar, self.status_window, self.hint_window]),
            focused_element=self.editor_control,
        )
        self.app = Application(
            layout=self.layout,
            key_bindings=self._build_key_bindings(),
            style=self._build_style(),
            clipboard=InMemoryClipboard(),
            full_screen=True,
            mouse_support=False,
            refresh_interval=1.0,
            enable_page_navigation_bindings=True,
            include_default_pygments_style=False,
        )

        self._initial_load()
        self._set_message("F1 help · F9 commands · F3 save as · F4 open")

    # --------------------------------------------------------------- startup
    def _initial_load(self) -> None:
        if self.path is not None and self.path.exists():
            self._load_path(self.path, force=True, initial=True)
        elif self.path is not None:
            self._reset_document("", path=self.path)
            self._set_message(
                f"New file {self.path.name} — Ctrl-S to create it"
            )
        else:
            self._reset_document("", path=None)
            self._set_message("New file — F3 to choose a path")

    def run(self) -> None:
        if self.server is not None:
            try:
                self.port = self.server.start()
            except OSError as exc:
                self.enable_server = False
                self.server = None
                self._set_message(f"State bridge unavailable: {exc}", error=True)
        self.publish_state()
        try:
            self.app.run(pre_run=self._pre_run)
        finally:
            self._shutdown()

    def _pre_run(self) -> None:
        self.app.create_background_task(self._autosave_loop())

    def _shutdown(self) -> None:
        state = self.build_state(status="closed")
        try:
            published = self.hub.publish(state)
        except Exception:
            published = state
        if self.state_file is not None:
            try:
                write_state_file(self.state_file, published)
            except OSError:
                pass
        if self.server is not None:
            self.server.stop()
        self.hub.close()

    # ------------------------------------------------------------ documents
    def _reset_document(
        self,
        text: str,
        *,
        path: Path | None,
        encoding: str = "utf-8",
        bom: bytes = b"",
        newline: str = "\n",
        size_bytes: int = 0,
    ) -> None:
        self.path = path
        self.encoding = encoding
        self.bom = bom
        self.newline = newline
        self.size_bytes = size_bytes
        self._saved_text = text
        self.dirty = False
        self.last_saved = None if path is None else time.time()

        self._suspend_undo = True
        self._text_changed_since_cursor = False
        try:
            self.buffer.set_document(Document(text, 0), bypass_readonly=True)
            self.buffer.complete_state = None
        finally:
            self._suspend_undo = False
        self._last_text = text
        self._last_cursor = 0
        self._current_prefix_cache = ""
        self.stats = compute_text_stats(text)
        self.undo.reset()
        if path is not None:
            cls, key = lexer_for_path(path)
            self.lexer_cls = cls
            self.language_key = key
        else:
            self.lexer_cls = TextLexer
            self.language_key = "text"
        self._rebuild_trie()
        self.publish_state()

    def _load_path(self, path: Path, *, force: bool = False, initial: bool = False) -> bool:
        if self.dirty and not force:
            self._set_message(
                "Unsaved changes — save first, or use :e! to discard", error=True
            )
            return False
        result = load_text_file(path)
        if not result.ok:
            self._set_message(f"Open failed: {result.error}", error=True)
            return False
        self._reset_document(
            result.text,
            path=path,
            encoding=result.encoding,
            bom=result.bom,
            newline=result.newline,
            size_bytes=result.size_bytes,
        )
        label = display_path(path)
        if initial:
            self._set_message(
                f"Opened {label} · {result.encoding} · {newline_name(result.newline)}"
            )
        else:
            self._set_message(
                f"Opened {label} · {len(result.text)} chars · {result.encoding}"
            )
        if result.warning:
            self._set_message(result.warning, error=True, duration=6.0)
        return True

    def open_file(self, raw_path: str, *, force: bool = False) -> bool:
        target = Path(raw_path).expanduser()
        if not target.is_absolute():
            target = (Path.cwd() / target).resolve()
        if target.exists():
            return self._load_path(target, force=force)
        if self.dirty and not force:
            self._set_message(
                "Unsaved changes — save first, or use :e! to discard", error=True
            )
            return False
        self._reset_document("", path=target)
        self._set_message(f"New file {display_path(target)} — Ctrl-S to create it")
        return True

    def new_file(self, *, force: bool = False) -> bool:
        if self.dirty and not force:
            self._set_message(
                "Unsaved changes — save first, or use :new! to discard", error=True
            )
            return False
        self._reset_document("", path=None)
        self._set_message("New file — F3 to choose a path")
        return True

    def save(self, raw_path: str | None = None, *, force_as: bool = False) -> bool:
        target: Path | None
        if raw_path:
            target = Path(raw_path).expanduser()
            if not target.is_absolute():
                target = (Path.cwd() / target).resolve()
        else:
            target = self.path

        if target is None:
            self._set_message("No filename yet — F3 or :w <path>", error=True)
            return False

        path_changed = self.path != target
        try:
            written = save_text_file(
                target,
                self.buffer.text,
                encoding=self.encoding,
                bom=self.bom,
                newline=self.newline,
            )
        except OSError as exc:
            self._set_message(f"Save failed: {exc}", error=True)
            return False

        self.path = target
        if path_changed or force_as:
            cls, key = lexer_for_path(target)
            if cls is not self.lexer_cls or key != self.language_key:
                self.lexer_cls = cls
                self.language_key = key
                self._rebuild_trie()

        self._saved_text = self.buffer.text
        self.dirty = False
        self.last_saved = time.time()
        self.size_bytes = written
        self.trie.learn_text(self.buffer.text)
        self._set_message(f"Saved {display_path(target)} · {written} bytes")
        self.publish_state()
        return True

    # --------------------------------------------------------------- history
    def undo_action(self) -> None:
        snapshot = self.undo.undo(self.buffer.text, self.buffer.cursor_position)
        if snapshot is None:
            self._set_message("Nothing to undo", error=True, duration=2.0)
            return
        self._restore_snapshot(snapshot)
        self._set_message(
            f"Undo · {snapshot.label} · {self.undo.undo_depth} left", duration=2.5
        )

    def redo_action(self) -> None:
        snapshot = self.undo.redo(self.buffer.text, self.buffer.cursor_position)
        if snapshot is None:
            self._set_message("Nothing to redo", error=True, duration=2.0)
            return
        self._restore_snapshot(snapshot)
        self._set_message(
            f"Redo · {snapshot.label} · {self.undo.redo_depth} left", duration=2.5
        )

    def _restore_snapshot(self, snapshot) -> None:
        text = snapshot.text
        cursor = max(0, min(snapshot.cursor, len(text)))
        self._suspend_undo = True
        self._text_changed_since_cursor = False
        try:
            self.buffer.set_document(Document(text, cursor), bypass_readonly=True)
            self.buffer.complete_state = None
        finally:
            self._suspend_undo = False
        self._last_text = text
        self._last_cursor = cursor
        self._text_changed_since_cursor = False
        self.undo.break_group()
        self.stats = compute_text_stats(text)
        self.dirty = text != self._saved_text
        self._current_prefix_cache = current_prefix(
            self.buffer.document.text_before_cursor
        )
        self.publish_state()

    # --------------------------------------------------------------- editing
    def _on_text_changed(self, _sender=None) -> None:
        text = self.buffer.text
        if text == self._last_text:
            return
        old_text = self._last_text
        old_cursor = self._last_cursor
        if not self._suspend_undo:
            self.undo.record(
                old_text,
                old_cursor,
                text,
                self.buffer.cursor_position,
                label=self._describe_change(old_text, text),
            )
            self.dirty = True

        self._last_text = text
        self._last_cursor = self.buffer.cursor_position
        self._text_changed_since_cursor = True
        self._current_prefix_cache = current_prefix(
            self.buffer.document.text_before_cursor
        )
        self.stats = compute_text_stats(text)

        now = time.monotonic()
        if now - self._last_learn >= 1.0:
            self.trie.learn_text(text)
            self._last_learn = now
        # Publish immediately on text changes so the visualizer sees word
        # counts / undo depth without waiting for the 1s heartbeat.  The JSON
        # file writer inside publish_state() is still throttled.
        self.publish_state()

    def _on_cursor_position_changed(self, _sender=None) -> None:
        if self._text_changed_since_cursor:
            self._text_changed_since_cursor = False
        else:
            self.undo.break_group()
        self._last_cursor = self.buffer.cursor_position
        self._current_prefix_cache = current_prefix(
            self.buffer.document.text_before_cursor
        )
        self._maybe_publish(time.monotonic())

    @staticmethod
    def _describe_change(old_text: str, new_text: str) -> str:
        _position, deleted, inserted = compute_diff(old_text, new_text)
        if inserted and not deleted:
            if "\n" in inserted:
                return "newline"
            return "insert"
        if deleted and not inserted:
            return "delete"
        return "replace"

    # ------------------------------------------------------------ completion
    def suggestion_for(self, text_before_cursor: str) -> str:
        prefix = current_prefix(text_before_cursor)
        if len(prefix) < 2:
            return ""
        for word, _weight, _kind in self.trie.complete(prefix, limit=8):
            if word != prefix and word.startswith(prefix):
                return word[len(prefix) :]
        return ""

    def _accept_suggestion(self) -> bool:
        remainder = self.suggestion_for(self.buffer.document.text_before_cursor)
        if not remainder:
            return False
        prefix = current_prefix(self.buffer.document.text_before_cursor)
        self.buffer.insert_text(remainder)
        self.trie.bump(prefix + remainder, 15)
        self._set_message(f"Accepted {prefix + remainder}", duration=1.5)
        return True

    # ------------------------------------------------------------- clipboard
    def copy_selection(self) -> None:
        data = self.buffer.copy_selection()
        if not data.text:
            self._set_message("Nothing selected — use Shift+arrows", error=True)
            return
        self.clipboard_history.enqueue(data.text)
        self.app.clipboard.set_data(data)
        self._set_message(f"Copied {len(data.text)} chars to clipboard history")
        self.publish_state()

    def cut_selection(self) -> None:
        data = self.buffer.cut_selection()
        if not data.text:
            self._set_message("Nothing selected — use Shift+arrows", error=True)
            return
        self.clipboard_history.enqueue(data.text)
        self.app.clipboard.set_data(data)
        self._set_message(f"Cut {len(data.text)} chars to clipboard history")
        self.publish_state()

    def paste_latest(self) -> None:
        entry = self.clipboard_history.latest()
        text = entry.text if entry is not None else self.app.clipboard.get_data().text
        if not text:
            self._set_message("Clipboard history is empty", error=True)
            return
        self.buffer.paste_clipboard_data(ClipboardData(text))
        self._set_message(
            f"Pasted {len(text)} chars (history {len(self.clipboard_history)})"
        )
        self.publish_state()

    def paste_history(self, index_from_newest: int) -> None:
        entry = self.clipboard_history.get(index_from_newest)
        if entry is None:
            self._set_message(
                f"No clipboard entry #{index_from_newest + 1}", error=True
            )
            return
        self.buffer.paste_clipboard_data(ClipboardData(entry.text))
        self._set_message(f"Pasted clipboard entry #{index_from_newest + 1}")
        self.publish_state()

    # --------------------------------------------------------------- finding
    def find_next(self, needle: str | None = None) -> None:
        query = (needle or "").strip() or self._last_search
        if not query:
            self._open_command("find ")
            return
        self._last_search = query
        text = self.buffer.text
        start = self.buffer.cursor_position
        position = text.find(query, start + 1)
        wrapped = False
        if position < 0:
            position = text.find(query, 0, start + 1)
            wrapped = True
        if position < 0:
            self._set_message(f"Not found: {query}", error=True)
            return
        self.buffer.cursor_position = position
        line, column = cursor_line_col(text, position)
        suffix = " (wrapped)" if wrapped else ""
        self._set_message(f"Found at line {line}, col {column}{suffix}")

    def goto_line(self, line_number: int) -> None:
        total = self.buffer.text.count("\n") + 1
        line_number = max(1, min(int(line_number), total))
        self.buffer.cursor_position = self.buffer.document.translate_row_col_to_index(
            line_number - 1, 0
        )
        self._set_message(f"Line {line_number} of {total}")

    # ------------------------------------------------------------------ help
    def toggle_help(self) -> None:
        if self.command_mode:
            self._close_command()
        self.help_visible = not self.help_visible
        if self.help_visible:
            self.layout.focus(self.help_control)
        else:
            self.layout.focus(self.editor_control)
        self.app.invalidate()

    def _close_help(self) -> None:
        if self.help_visible:
            self.help_visible = False
            self.layout.focus(self.editor_control)
            self.app.invalidate()

    # --------------------------------------------------------------- command
    def _open_command(self, prefill: str = "") -> None:
        if self.help_visible:
            self._close_help()
        self.command_mode = True
        self.command_buffer.reset()
        if prefill:
            self.command_buffer.insert_text(prefill)
            self.command_buffer.cursor_position = len(prefill)
        self.layout.focus(self.command_control)
        self.app.invalidate()

    def _close_command(self) -> None:
        self.command_mode = False
        self.command_buffer.reset()
        self.layout.focus(self.editor_control)
        self.app.invalidate()

    def _accept_command(self, buffer: Buffer) -> bool:
        text = buffer.text.strip()
        self.command_mode = False
        buffer.reset()
        self.layout.focus(self.editor_control)
        self.app.invalidate()
        if text:
            self._run_command(text)
        return False

    def _run_command(self, raw: str) -> None:
        raw = raw.strip()
        if raw.startswith(":"):
            raw = raw[1:].strip()
        if not raw:
            return

        parts = raw.split(None, 1)
        command = parts[0].lower()
        argument = parts[1].strip() if len(parts) > 1 else ""
        force = command.endswith("!")
        if force:
            command = command[:-1]

        if command in {"w", "write", "save"}:
            self.save(argument or None)
        elif command == "wq":
            if self.save(argument or None):
                self._exit()
        elif command in {"e", "edit", "open"}:
            if not argument:
                self._set_message("Usage: :e <path>", error=True)
            else:
                self.open_file(argument, force=force)
        elif command == "saveas":
            if not argument:
                self._set_message("Usage: :saveas <path>", error=True)
            else:
                self.save(argument, force_as=True)
        elif command == "new":
            self.new_file(force=force)
        elif command in {"q", "quit"}:
            if force:
                self._exit()
            else:
                self.request_quit()
        elif command == "help":
            self.toggle_help()
        elif command == "autosave":
            self._command_autosave(argument)
        elif command in {"lang", "language", "syntax"}:
            self._command_language(argument)
        elif command == "find":
            self.find_next(argument)
        elif command == "goto":
            try:
                self.goto_line(int(argument))
            except (TypeError, ValueError):
                self._set_message("Usage: :goto <line>", error=True)
        elif command in {"clip", "clipboard"}:
            self.show_panel = True
            self._set_message(
                f"Clipboard queue: {len(self.clipboard_history)}/"
                f"{self.clipboard_history.capacity} entries"
            )
        elif command in {"stats", "panel"}:
            self.show_panel = not self.show_panel
            self._set_message(f"Data panel {'on' if self.show_panel else 'off'}")
        elif command == "theme":
            self.set_theme(argument)
        elif command == "tab":
            try:
                width = int(argument)
            except (TypeError, ValueError):
                self._set_message("Usage: :tab <1-16>", error=True)
                return
            self.tab_width = max(1, min(16, width))
            self._set_message(f"Tab width: {self.tab_width}")
        elif command == "undo":
            self.undo_action()
        elif command == "redo":
            self.redo_action()
        elif command in {"state", "bridge"}:
            self._set_message(self._bridge_summary(), duration=8.0)
        elif command == "path":
            self._set_message(display_path(self.path), duration=6.0)
        elif command == "languages":
            self._set_message(", ".join(available_languages()), duration=8.0)
        else:
            self._set_message(f"Unknown command: {command}", error=True)

    def _command_autosave(self, argument: str) -> None:
        argument = argument.strip().lower()
        if argument in {"on", "yes", "1"}:
            self.autosave_enabled = True
        elif argument in {"off", "no", "0"}:
            self.autosave_enabled = False
        elif argument:
            try:
                self.autosave_interval = max(1.0, float(argument))
                self.autosave_enabled = True
            except ValueError:
                self._set_message("Usage: :autosave on|off|<seconds>", error=True)
                return
        else:
            self.autosave_enabled = not self.autosave_enabled
        state = "on" if self.autosave_enabled else "off"
        self._set_message(f"Autosave {state} ({self.autosave_interval:g}s)")
        self.publish_state()

    def _command_language(self, argument: str) -> None:
        if not argument:
            self._set_message(
                f"Language: {self.language_key} · try :lang python / :lang auto",
                duration=6.0,
            )
            return
        if argument.lower() == "auto":
            if self.path is not None:
                cls, key = lexer_for_path(self.path)
            else:
                cls, key = TextLexer, "text"
        else:
            result = lexer_for_language(argument)
            if result is None:
                self._set_message(f"Unknown language: {argument}", error=True)
                return
            cls, key = result
        self.lexer_cls = cls
        self.language_key = key
        self._rebuild_trie()
        self._set_message(f"Language: {language_label(cls)} ({key})")
        self.publish_state()

    def set_theme(self, name: str) -> None:
        name = (name or "").strip()
        if not name:
            self._set_message(f"Theme: {self.theme}", duration=4.0)
            return
        try:
            new_style = self._build_style(name)
        except ClassNotFound:
            self._set_message(f"Unknown Pygments theme: {name}", error=True)
            return
        self.theme = name
        self.app.style = new_style
        self.app.invalidate()
        self._set_message(f"Theme: {name}")

    # ---------------------------------------------------------------- autosave
    async def _autosave_loop(self) -> None:
        while True:
            await asyncio.sleep(0.5)
            try:
                now = time.monotonic()
                if now - self._last_publish >= 1.0:
                    self.publish_state()
                if (
                    self.autosave_enabled
                    and self.dirty
                    and self.path is not None
                    and (
                        self.last_saved is None
                        or time.time() - self.last_saved >= self.autosave_interval
                    )
                ):
                    await self._autosave_once()
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # keep the editor alive
                self._set_message(f"Autosave error: {exc}", error=True)
                await asyncio.sleep(5)

    async def _autosave_once(self) -> None:
        path = self.path
        if path is None:
            return
        text = self.buffer.text
        try:
            written = await asyncio.to_thread(
                save_text_file,
                path,
                text,
                encoding=self.encoding,
                bom=self.bom,
                newline=self.newline,
            )
        except OSError as exc:
            self._set_message(f"Autosave failed: {exc}", error=True)
            return

        self.last_saved = time.time()
        self.size_bytes = written
        self._saved_text = text
        self.dirty = self.buffer.text != text
        self._set_message(f"Autosaved {path.name} · {written} bytes", duration=2.5)
        self.publish_state()

    # --------------------------------------------------------------- shutdown
    def request_quit(self) -> None:
        if self.dirty:
            now = time.monotonic()
            if now - self._pending_quit_at > 2.5:
                self._pending_quit_at = now
                self._set_message(
                    "Unsaved changes — press Ctrl-Q again to discard, or Ctrl-S to save",
                    error=True,
                    duration=4.0,
                )
                return
        self._exit()

    def _exit(self) -> None:
        self.publish_state()
        self.app.exit()

    # ---------------------------------------------------------------- state
    def _trie_provider(self, depth: int, limit: int) -> dict:
        trie = self.trie
        return trie.to_dict(
            prefix=self._current_prefix_cache,
            max_depth=depth,
            max_nodes=limit,
        )

    def _rebuild_trie(self) -> None:
        trie = Trie()
        for word, weight, kind in keyword_entries(self.language_key):
            trie.insert(word, weight=weight, kind=kind)
        trie.learn_text(self.buffer.text, weight=8, kind="word")
        self.trie = trie
        self._current_prefix_cache = current_prefix(
            self.buffer.document.text_before_cursor
        )

    def _maybe_publish(self, now: float) -> None:
        if now - self._last_publish >= 0.2:
            self.publish_state()

    def publish_state(self, *, status: str = "ok") -> dict:
        state = self.build_state(status=status)
        published = self.hub.publish(state)
        self._last_publish = time.monotonic()
        if self.state_file is not None:
            now = time.monotonic()
            if now - self._last_state_file >= 1.0 or status != "ok":
                try:
                    write_state_file(self.state_file, published)
                    self._state_file_error = ""
                except OSError as exc:
                    self._state_file_error = str(exc)
                self._last_state_file = now
        return published

    def build_state(self, *, status: str = "ok") -> dict:
        text = self.buffer.text
        document = self.buffer.document
        line, column = cursor_line_col(text, self.buffer.cursor_position)
        prefix = current_prefix(document.text_before_cursor)
        selection_chars = 0
        if self.buffer.selection_state is not None:
            selection_chars = abs(
                self.buffer.cursor_position
                - self.buffer.selection_state.original_cursor_position
            )

        now = time.time()
        seconds_since_save = (
            round(now - self.last_saved, 1) if self.last_saved is not None else None
        )
        due_in = None
        if (
            self.autosave_enabled
            and self.dirty
            and self.path is not None
            and self.last_saved is not None
        ):
            due_in = round(
                max(0.0, self.autosave_interval - (now - self.last_saved)), 1
            )

        file_info = {
            "path": str(self.path) if self.path is not None else None,
            "display_path": display_path(self.path),
            "name": self.path.name if self.path is not None else "untitled",
            "exists": bool(self.path and self.path.exists()),
            "dirty": self.dirty,
            "encoding": self.encoding,
            "bom": self.bom.hex(),
            "newline": newline_name(self.newline),
            "language": self.language_key,
            "language_label": language_label(self.lexer_cls),
            "size_bytes": self.size_bytes,
            "last_saved": round(self.last_saved, 3) if self.last_saved else None,
            "seconds_since_save": seconds_since_save,
        }

        stats = self.stats.to_dict()
        stats.update(
            {
                "cursor_index": self.buffer.cursor_position,
                "cursor_line": line,
                "cursor_col": column,
                "selection_chars": selection_chars,
                "saved_chars": len(self._saved_text),
            }
        )

        return {
            "status": status,
            "app": {
                "name": "dsa-editor",
                "version": __version__,
                "pid": os.getpid(),
                "session": self.session_id,
                "port": self.port,
                "server_url": f"http://127.0.0.1:{self.port}" if self.port else None,
                "theme": self.theme,
                "tab_width": self.tab_width,
                "state_file": str(self.state_file) if self.state_file else None,
                "state_file_error": self._state_file_error or None,
            },
            "file": file_info,
            "stats": stats,
            "document": {
                "text_preview": _clip_text(text, 240),
                "chars": len(text),
                "lines": text.count("\n") + 1,
                "cursor_index": self.buffer.cursor_position,
                "selection_chars": selection_chars,
            },
            "undo": self.undo.to_dict(),
            "clipboard": self.clipboard_history.to_dict(),
            "trie": self.trie.to_dict(
                prefix=prefix,
                max_depth=3,
                max_nodes=800,
                top_words=60,
                suggestions=12,
            ),
            "autosave": {
                "enabled": self.autosave_enabled,
                "interval_seconds": self.autosave_interval,
                "last_saved": round(self.last_saved, 3)
                if self.last_saved is not None
                else None,
                "seconds_since_save": seconds_since_save,
                "due_in_seconds": due_in,
                "active": bool(
                    self.autosave_enabled
                    and self.path is not None
                    and self.dirty
                ),
            },
            "settings": {
                "undo_capacity": self.undo.capacity,
                "clipboard_capacity": self.clipboard_history.capacity,
                "autosave_interval": self.autosave_interval,
                "tab_width": self.tab_width,
                "theme": self.theme,
                "server_enabled": self.enable_server,
            },
            "ui": {
                "panel_visible": self.show_panel,
                "help_visible": self.help_visible,
                "command_mode": self.command_mode,
                "message": self._message,
                "message_error": self._message_error,
            },
        }

    def _bridge_summary(self) -> str:
        if self.port:
            return f"Bridge http://127.0.0.1:{self.port}/state · file {self.state_file or 'off'}"
        if self.state_file:
            return f"Bridge off · state file {self.state_file}"
        return "Bridge off"

    # ---------------------------------------------------------------- styles
    def _build_style(self, theme: str | None = None) -> Style:
        theme_name = theme or self.theme
        try:
            pygments_style = style_from_pygments_cls(get_style_by_name(theme_name))
        except ClassNotFound:
            pygments_style = style_from_pygments_cls(get_style_by_name("monokai"))

        custom = Style.from_dict(
            {
                "editor": "bg:#1e1e1e #d4d4d4",
                "line-number": "#6b7280 bg:#1e1e1e",
                "line-number.current": "#fbbf24 bold bg:#1e1e1e",
                "status": "bg:#111827 #e5e7eb",
                "status.path": "bg:#111827 #93c5fd bold",
                "status.dirty": "bg:#111827 #fbbf24 bold",
                "status.clean": "bg:#111827 #34d399",
                "status.sep": "bg:#111827 #4b5563",
                "status.value": "bg:#111827 #e5e7eb",
                "hint": "bg:#0b1220 #9ca3af",
                "panel": "bg:#0f172a #cbd5e1",
                "panel.title": "bg:#0f172a #38bdf8 bold",
                "panel.label": "bg:#0f172a #94a3b8",
                "panel.value": "bg:#0f172a #f8fafc bold",
                "panel.dim": "bg:#0f172a #64748b",
                "panel.good": "bg:#0f172a #34d399",
                "panel.suggest": "bg:#0f172a #a7f3d0",
                "separator": "bg:#0f172a #334155",
                "command": "bg:#111827 #f8fafc",
                "command.prompt": "bg:#111827 #38bdf8 bold",
                "help": "bg:#0b1220 #e5e7eb",
                "message": "bg:#0b1220 #93c5fd",
                "message.error": "bg:#0b1220 #f87171 bold",
                "auto-suggestion": "bg:#1e1e1e #6b7280 italic",
                "completion-menu": "bg:#1f2937 #e5e7eb",
                "completion-menu.completion": "bg:#1f2937 #e5e7eb",
                "completion-menu.completion.current": "bg:#2563eb #ffffff",
                "completion-menu.meta.completion": "bg:#1f2937 #9ca3af",
                "completion-menu.meta.completion.current": "bg:#2563eb #dbeafe",
                "scrollbar.background": "bg:#1e1e1e",
                "scrollbar.button": "bg:#475569",
            }
        )
        return merge_styles([pygments_style, custom])

    def _ptk_lexer(self):
        lexer_cls = self.lexer_cls or TextLexer
        cached = self._lexer_cache.get(lexer_cls)
        if cached is None:
            cached = PygmentsLexer(lexer_cls, sync_from_start=False)
            self._lexer_cache[lexer_cls] = cached
        return cached

    # --------------------------------------------------------------- keymap
    def _build_key_bindings(self) -> KeyBindings:
        kb = KeyBindings()
        editor_focused = has_focus(self.editor_control)
        help_focused = has_focus(self.help_control)
        editor_edit = editor_focused & ~has_completions

        @kb.add("f1")
        def _help(event) -> None:
            self.toggle_help()

        @kb.add("escape")
        def _escape(event) -> None:
            if self.help_visible:
                self._close_help()
            elif self.command_mode:
                self._close_command()
            elif self.buffer.complete_state is not None:
                self.buffer.complete_state = None
            elif self.buffer.selection_state is not None:
                self.buffer.exit_selection()

        @kb.add("q", filter=help_focused)
        def _help_close_q(event) -> None:
            self._close_help()

        @kb.add("c-s")
        @kb.add("f2")
        def _save(event) -> None:
            self.save()

        @kb.add("f3")
        def _save_as(event) -> None:
            self._open_command("w ")

        @kb.add("f4")
        @kb.add("c-o")
        def _open(event) -> None:
            self._open_command("e ")

        @kb.add("c-n")
        def _new(event) -> None:
            self.new_file()

        @kb.add("c-q")
        def _quit(event) -> None:
            self.request_quit()

        @kb.add("f5")
        def _autosave(event) -> None:
            self._command_autosave("")

        @kb.add("f6")
        def _panel(event) -> None:
            self.show_panel = not self.show_panel
            self._set_message(f"Data panel {'on' if self.show_panel else 'off'}")
            self.app.invalidate()

        @kb.add("f9")
        @kb.add("c-p")
        def _command(event) -> None:
            self._open_command("")

        @kb.add("c-z", filter=editor_focused)
        @kb.add("c-_", filter=editor_focused)
        @kb.add("c-x", "c-u", filter=editor_focused)
        def _undo(event) -> None:
            # The readline-style aliases are routed to our own stack as well,
            # so prompt_toolkit's built-in undo is never used.
            self.undo_action()

        @kb.add("c-y", filter=editor_focused)
        def _redo(event) -> None:
            self.redo_action()

        @kb.add("c-a", filter=editor_focused)
        def _select_all(event) -> None:
            buffer = event.current_buffer
            buffer.cursor_position = 0
            buffer.start_selection(selection_type=SelectionType.CHARACTERS)
            buffer.cursor_position = len(buffer.text)

        @kb.add("c-c", filter=editor_focused)
        def _copy(event) -> None:
            self.copy_selection()

        @kb.add("c-x", filter=editor_focused)
        def _cut(event) -> None:
            self.cut_selection()

        @kb.add("c-v", filter=editor_focused)
        def _paste(event) -> None:
            self.paste_latest()

        for index in range(1, 10):
            @kb.add("escape", str(index), filter=editor_focused)
            def _paste_n(event, _index=index) -> None:
                self.paste_history(_index - 1)

        @kb.add("c-f", filter=editor_focused)
        def _find(event) -> None:
            self._open_command("find ")

        @kb.add("c-g", filter=editor_focused)
        def _goto(event) -> None:
            self._open_command("goto ")

        @kb.add("c-right", filter=editor_focused)
        def _accept_suggestion(event) -> None:
            if not self._accept_suggestion():
                buffer = event.current_buffer
                buffer.cursor_position += (
                    buffer.document.find_next_word_beginning() or 0
                )

        @kb.add("tab", filter=editor_edit)
        def _tab(event) -> None:
            buffer = event.current_buffer
            column = buffer.document.cursor_position_col
            spaces = self.tab_width - (column % self.tab_width)
            buffer.insert_text(" " * spaces)

        @kb.add("s-tab", filter=editor_edit)
        def _dedent(event) -> None:
            buffer = event.current_buffer
            document = buffer.document
            line_start = buffer.cursor_position - document.cursor_position_col
            leading = document.leading_whitespace_in_current_line
            if not leading:
                return
            remove = min(len(leading), self.tab_width)
            buffer.cursor_position = line_start
            buffer.delete(remove)

        @kb.add("enter", filter=editor_edit)
        def _smart_enter(event) -> None:
            buffer = event.current_buffer
            document = buffer.document
            line = document.current_line_before_cursor
            indent = document.leading_whitespace_in_current_line
            extra = ""
            if line.rstrip().endswith((":", "{", "[", "(")):
                extra = " " * self.tab_width
            buffer.insert_text("\n" + indent + extra)

        return kb

    # ---------------------------------------------------------------- views
    def _set_message(
        self, text: str, *, error: bool = False, duration: float = 4.0
    ) -> None:
        self._message = text
        self._message_error = error
        self._message_until = time.monotonic() + duration
        if getattr(self, "app", None) is not None:
            self.app.invalidate()

    def _status_fragments(self):
        path = display_path(self.path)
        if len(path) > 46:
            path = "…" + path[-45:]
        line = self.buffer.document.cursor_position_row + 1
        column = self.buffer.document.cursor_position_col + 1
        selection = 0
        if self.buffer.selection_state is not None:
            selection = abs(
                self.buffer.cursor_position
                - self.buffer.selection_state.original_cursor_position
            )

        fragments: list[tuple[str, str]] = [
            ("class:status.path", f" {path} "),
            ("class:status.dirty" if self.dirty else "class:status.clean",
             " ● " if self.dirty else " ✓ "),
            ("class:status.sep", "│"),
            ("class:status.value", f" {self.language_key} "),
            ("class:status.sep", "│"),
            ("class:status.value", f" Ln {line}, Col {column} "),
            ("class:status.sep", "│"),
            ("class:status.value", f" {self.stats.words} words "),
            ("class:status.sep", "│"),
            (
                "class:status.value",
                f" auto {'on' if self.autosave_enabled else 'off'} ",
            ),
        ]
        if selection:
            fragments.extend(
                [
                    ("class:status.sep", "│"),
                    ("class:status.value", f" sel {selection} "),
                ]
            )
        if self.port:
            fragments.extend(
                [
                    ("class:status.sep", "│"),
                    ("class:status.value", f" bridge :{self.port} "),
                ]
            )
        return fragments

    def _hint_fragments(self):
        now = time.monotonic()
        if self._message and now < self._message_until:
            style = "class:message.error" if self._message_error else "class:message"
            return [(style, " " + self._message)]
        if self.help_visible:
            return [("class:hint", " F1 or Esc to close help · q also closes ")]
        if self.command_mode:
            return [
                (
                    "class:hint",
                    " Enter run · Esc cancel · :w save · :q quit · :help help ",
                )
            ]
        return [
            (
                "class:hint",
                " Ctrl-S save · F3 save as · F4 open · Ctrl-Z/Y undo/redo · "
                "F5 autosave · F6 panel · F1 help ",
            )
        ]

    def _panel_fragments(self):
        lines: list[tuple[str, str]] = []

        def add(style: str, text: str) -> None:
            if lines:
                lines.append(("", "\n"))
            lines.append((style, text))

        trie_stats = self.trie.stats()
        add("class:panel.title", " DATA STRUCTURES")
        add(
            "class:panel.label",
            f" undo stack   {self.undo.undo_depth:>4} / {self.undo.capacity}",
        )
        add(
            "class:panel.label",
            f" redo stack   {self.undo.redo_depth:>4} / {self.undo.capacity}",
        )
        add(
            "class:panel.label",
            f" trie nodes   {trie_stats['nodes']:>5}   words {trie_stats['words']}",
        )
        add(
            "class:panel.label",
            f" clipboard    {len(self.clipboard_history):>4} / "
            f"{self.clipboard_history.capacity}",
        )
        add("", " " + "─" * 40)
        add("class:panel.title", " DOCUMENT")
        add(
            "class:panel.label",
            f" {_short(display_path(self.path), 38)}"
            + ("  modified" if self.dirty else "  saved"),
        )
        add(
            "class:panel.dim",
            f" {self.language_key} · {self.encoding} · {newline_name(self.newline)}",
        )
        add(
            "class:panel.label",
            f" {self.stats.words} words · {self.stats.chars} chars · "
            f"{self.stats.lines} lines",
        )
        line = self.buffer.document.cursor_position_row + 1
        column = self.buffer.document.cursor_position_col + 1
        add("class:panel.dim", f" Ln {line}, Col {column}")
        if self.autosave_enabled:
            if self.last_saved is None:
                saved = "not saved yet"
            else:
                saved = f"{int(time.time() - self.last_saved)}s ago"
            add("class:panel.dim", f" autosave {self.autosave_interval:g}s · {saved}")
        else:
            add("class:panel.dim", " autosave off")
        if self.port:
            add("class:panel.good", f" bridge http://127.0.0.1:{self.port}")
        else:
            add("class:panel.dim", " bridge off")

        add("", " " + "─" * 40)
        add("class:panel.title", " UNDO TOP")
        top = self.undo.undo_stack.peek()
        if top is not None:
            add(
                "class:panel.label",
                f" {top.kind} × {top.edit_count} · {top.char_count} chars",
            )
            detail = top.inserted or top.deleted or top.preview(30)
            add("class:panel.dim", f" {_short(detail.replace(chr(10), ' '), 38)}")
        else:
            add("class:panel.dim", " (empty)")

        add("", " " + "─" * 40)
        add("class:panel.title", " CLIPBOARD  Alt-1..9")
        entries = self.clipboard_history.newest_first()[:6]
        if entries:
            for index, entry in enumerate(entries, start=1):
                add(
                    "class:panel.label",
                    f" {index}. {_short(entry.preview(30), 34)}",
                )
        else:
            add("class:panel.dim", " (empty)")

        add("", " " + "─" * 40)
        prefix = self._current_prefix_cache
        add("class:panel.title", f" TRIE  prefix {prefix!r}")
        suggestions = self.trie.complete(prefix, limit=6) if prefix else []
        if suggestions:
            for word, weight, kind in suggestions:
                add(
                    "class:panel.suggest",
                    f" {_short(word, 26):<26} {kind:<7} {weight:>4}",
                )
        else:
            add("class:panel.dim", " type 2+ letters to see suggestions")
        return lines


def _short(text: str, limit: int) -> str:
    text = " ".join(str(text).split())
    return text if len(text) <= limit else text[: max(0, limit - 1)] + "…"


def _clip_text(text: str, limit: int) -> str:
    flat = text.replace("\r", "\\r").replace("\n", "\\n")
    return flat if len(flat) <= limit else flat[:limit] + "…"
