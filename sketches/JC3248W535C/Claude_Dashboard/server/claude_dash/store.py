"""Stockage fichier des sessions Claude Code (un JSON par session)."""

from __future__ import annotations

import contextlib
import fcntl
import json
import os
import re
import tempfile
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import Any

Data = dict[str, Any]

_SAFE_ID = re.compile(r"^[A-Za-z0-9_-]{1,100}$")


def default_base() -> Path:
    return Path(os.environ.get("CLAUDE_DASH_DIR", Path.home() / ".claude" / "dashboard"))


def _check_id(session_id: str) -> None:
    if not _SAFE_ID.fullmatch(session_id):
        raise ValueError(f"session id invalide: {session_id!r}")


@contextlib.contextmanager
def _locked(base: Path) -> Iterator[None]:
    base.mkdir(parents=True, exist_ok=True)
    with open(base / ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


def _read(path: Path) -> Data:
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def _write_atomic(path: Path, data: Data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=".tmp-")
    try:
        with os.fdopen(fd, "w") as f:
            json.dump(data, f)
        os.replace(tmp, path)
    except BaseException:
        with contextlib.suppress(OSError):
            os.unlink(tmp)
        raise


def update_session(base: Path, session_id: str, fn: Callable[[Data], None]) -> None:
    """Lit la session, applique `fn` (mutation en place) et réécrit atomiquement."""
    _check_id(session_id)
    path = base / "sessions" / f"{session_id}.json"
    with _locked(base):
        data = _read(path)
        data["id"] = session_id
        fn(data)
        _write_atomic(path, data)


def session_exists(base: Path, session_id: str) -> bool:
    _check_id(session_id)
    return (base / "sessions" / f"{session_id}.json").is_file()


def remove_session(base: Path, session_id: str) -> None:
    _check_id(session_id)
    with _locked(base), contextlib.suppress(FileNotFoundError):
        (base / "sessions" / f"{session_id}.json").unlink()


def load_sessions(base: Path) -> list[Data]:
    """Sessions du store ; le nom de fichier fait foi pour l'id (jamais le contenu)."""
    folder = base / "sessions"
    if not folder.is_dir():
        return []
    return [
        d | {"id": p.stem}
        for p in sorted(folder.glob("*.json"))
        if _SAFE_ID.fullmatch(p.stem) and (d := _read(p))
    ]


def write_limits(base: Path, limits: Data) -> None:
    path = base / "limits.json"
    with _locked(base):
        data = _read(path)
        data.update(limits)
        _write_atomic(path, data)


def load_limits(base: Path) -> Data:
    return _read(base / "limits.json")
