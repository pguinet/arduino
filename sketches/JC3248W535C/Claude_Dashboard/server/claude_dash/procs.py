"""Recherche du processus Claude Code parent d'un hook."""

from __future__ import annotations

import os
from collections.abc import Callable

# pid -> (ppid, comm, argv) ; None si le processus n'existe pas
ProcInfo = Callable[[int], tuple[int, str, list[str]] | None]

_CLAUDE_CLI_SUFFIX = "/@anthropic-ai/claude-code/cli.js"


def parse_stat(stat: str) -> tuple[int, str]:
    """Extrait (ppid, comm) d'une ligne /proc/<pid>/stat.

    Le comm peut contenir espaces et parenthèses : on s'appuie sur la dernière ")".
    """
    end = stat.rindex(")")
    comm = stat[stat.index("(") + 1 : end]
    ppid = int(stat[end + 2 :].split()[1])
    return ppid, comm


def proc_info(pid: int) -> tuple[int, str, list[str]] | None:
    try:
        with open(f"/proc/{pid}/stat") as f:
            stat = f.read()
        with open(f"/proc/{pid}/cmdline", "rb") as f:
            raw = f.read()
    except OSError:
        return None
    # arguments séparés par \0, terminés par \0 ; on garde les arguments vides
    argv = [a.decode(errors="replace") for a in raw.rstrip(b"\0").split(b"\0")] if raw else []
    ppid, comm = parse_stat(stat)
    return ppid, comm, argv


def is_claude(comm: str, argv: list[str]) -> bool:
    """Vrai pour le binaire natif `claude`, `node .../@anthropic-ai/claude-code/cli.js`
    ou `node .../claude` (lien npm lancé via `#!/usr/bin/env node`)."""
    if comm == "claude":
        return True
    return (
        len(argv) >= 2
        and os.path.basename(argv[0]).startswith("node")
        and (argv[1].endswith(_CLAUDE_CLI_SUFFIX) or os.path.basename(argv[1]) == "claude")
    )


def find_claude_pid(start: int, info: ProcInfo = proc_info, max_depth: int = 10) -> int | None:
    """Remonte les ancêtres de `start` jusqu'au processus Claude Code."""
    pid = start
    for _ in range(max_depth):
        entry = info(pid)
        if entry is None:
            return None
        ppid, comm, argv = entry
        if pid != start and is_claude(comm, argv):
            return pid
        if ppid <= 1:
            return None
        pid = ppid
    return None


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True
