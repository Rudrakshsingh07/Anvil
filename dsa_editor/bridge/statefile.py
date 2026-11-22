"""Atomic JSON state-file writer (fallback for the visualizer)."""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path
from typing import Any


def write_state_file(path: str | Path, state: dict[str, Any]) -> int:
    target = Path(path).expanduser()
    target.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(state, ensure_ascii=False, separators=(",", ":")).encode("utf-8")

    fd, temp_name = tempfile.mkstemp(
        prefix=f".{target.name}.", suffix=".tmp", dir=str(target.parent)
    )
    temp_path = Path(temp_name)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp_path, target)
    except BaseException:
        try:
            temp_path.unlink(missing_ok=True)
        except OSError:
            pass
        raise
    return len(data)
