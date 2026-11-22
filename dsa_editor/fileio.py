"""Encoding-aware, newline-preserving, atomic file I/O."""

from __future__ import annotations

import codecs
import os
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path

WARN_SIZE = 5 * 1024 * 1024
MAX_SIZE = 64 * 1024 * 1024


@dataclass
class LoadResult:
    ok: bool
    text: str = ""
    encoding: str = "utf-8"
    bom: bytes = b""
    newline: str = "\n"
    size_bytes: int = 0
    error: str = ""
    warning: str = ""

    def to_dict(self) -> dict:
        return {
            "ok": self.ok,
            "encoding": self.encoding,
            "newline": newline_name(self.newline),
            "size_bytes": self.size_bytes,
            "error": self.error,
            "warning": self.warning,
        }


def newline_name(newline: str) -> str:
    return {"\n": "LF", "\r\n": "CRLF", "\r": "CR"}.get(newline, repr(newline))


def detect_newline(text: str) -> str:
    crlf = text.count("\r\n")
    lf = text.count("\n") - crlf
    cr = text.count("\r") - crlf
    counts = {"\r\n": crlf, "\n": lf, "\r": cr}
    # Ties prefer the platform-independent LF.
    return max(counts, key=lambda key: (counts[key], key == "\n"))


def _decode(raw: bytes) -> tuple[str, str, bytes]:
    if raw.startswith(codecs.BOM_UTF8):
        return raw[len(codecs.BOM_UTF8) :].decode("utf-8", errors="replace"), "utf-8", codecs.BOM_UTF8
    if raw.startswith(codecs.BOM_UTF16_LE):
        return (
            raw[len(codecs.BOM_UTF16_LE) :].decode("utf-16-le", errors="replace"),
            "utf-16-le",
            codecs.BOM_UTF16_LE,
        )
    if raw.startswith(codecs.BOM_UTF16_BE):
        return (
            raw[len(codecs.BOM_UTF16_BE) :].decode("utf-16-be", errors="replace"),
            "utf-16-be",
            codecs.BOM_UTF16_BE,
        )

    try:
        return raw.decode("utf-8"), "utf-8", b""
    except UnicodeDecodeError:
        pass
    try:
        return raw.decode("cp1252"), "cp1252", b""
    except UnicodeDecodeError:
        return raw.decode("latin-1"), "latin-1", b""


def load_text_file(path: str | Path) -> LoadResult:
    file_path = Path(path).expanduser()
    try:
        size = file_path.stat().st_size
    except OSError as exc:
        return LoadResult(ok=False, error=str(exc))

    if size > MAX_SIZE:
        return LoadResult(
            ok=False,
            size_bytes=size,
            error=f"File is too large to open safely ({size / 1_048_576:.1f} MiB; limit 64 MiB)",
        )

    try:
        raw = file_path.read_bytes()
    except OSError as exc:
        return LoadResult(ok=False, error=str(exc))

    # UTF-16 files legitimately contain NUL bytes, so only reject binary data
    # when there is no UTF BOM.
    has_utf_bom = raw.startswith((codecs.BOM_UTF8, codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE))
    if not has_utf_bom and b"\x00" in raw[:8192]:
        return LoadResult(
            ok=False,
            size_bytes=size,
            error="This looks like a binary file; the editor only opens text files",
        )

    text, encoding, bom = _decode(raw)
    newline = detect_newline(text)
    text = text.replace("\r\n", "\n").replace("\r", "\n")

    warning = ""
    if size > WARN_SIZE:
        warning = f"Large file ({size / 1_048_576:.1f} MiB); editing may be slow"

    return LoadResult(
        ok=True,
        text=text,
        encoding=encoding,
        bom=bom,
        newline=newline,
        size_bytes=size,
        warning=warning,
    )


def save_text_file(
    path: str | Path,
    text: str,
    *,
    encoding: str = "utf-8",
    bom: bytes = b"",
    newline: str = "\n",
    backup: bool = False,
) -> int:
    """Atomically write text.  Returns the number of bytes written.

    A temporary file in the destination directory is written, fsynced and then
    ``os.replace``d over the target.  Existing permissions are preserved.
    """

    file_path = Path(path).expanduser()
    if file_path.parent and not file_path.parent.exists():
        file_path.parent.mkdir(parents=True, exist_ok=True)

    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    payload = normalized.replace("\n", newline)
    data = bom + payload.encode(encoding, errors="replace")

    if backup and file_path.exists():
        try:
            shutil.copy2(file_path, file_path.with_name(file_path.name + ".bak"))
        except OSError:
            pass

    mode: int | None = None
    try:
        mode = file_path.stat().st_mode & 0o7777
    except OSError:
        pass

    fd, temp_name = tempfile.mkstemp(
        prefix=f".{file_path.name}.", suffix=".tmp", dir=str(file_path.parent)
    )
    temp_path = Path(temp_name)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        if mode is not None:
            try:
                os.chmod(temp_path, mode)
            except OSError:
                pass
        os.replace(temp_path, file_path)
    except BaseException:
        try:
            temp_path.unlink(missing_ok=True)
        except OSError:
            pass
        raise

    return len(data)


def display_path(path: str | Path | None) -> str:
    if path is None:
        return "untitled"
    try:
        return "~/" + str(Path(path).expanduser().resolve().relative_to(Path.home()))
    except (ValueError, OSError):
        return str(path)
