"""Single desktop instance: the hub accepts one connection per Device ID, two
instances would keep kicking each other off."""

from __future__ import annotations

import fcntl
import os
from pathlib import Path
from typing import IO


def default_lock_path() -> Path:
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    if runtime:
        return Path(runtime) / "claude-dash-desktop.lock"
    return Path.home() / ".config" / "claude-dash" / "desktop.lock"


def acquire(path: Path) -> IO[str] | None:
    """Locked file holding our PID, to keep open while running; None if held."""
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = path.open("a+")
    try:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        handle.close()
        return None
    handle.seek(0)
    handle.truncate()
    handle.write(f"{os.getpid()}\n")
    handle.flush()
    return handle


def holder_pid(path: Path) -> int | None:
    try:
        return int(path.read_text().strip())
    except (OSError, ValueError):
        return None
