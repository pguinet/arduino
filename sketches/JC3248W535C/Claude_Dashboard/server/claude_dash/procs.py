"""Recherche du processus Claude Code parent d'un hook."""

from __future__ import annotations

import os
from collections.abc import Callable

# pid -> (ppid, "comm + cmdline") ; None si le processus n'existe pas
ProcInfo = Callable[[int], tuple[int, str] | None]


def parse_stat(stat: str) -> tuple[int, str]:
    """Extrait (ppid, comm) d'une ligne /proc/<pid>/stat.

    Le comm peut contenir espaces et parenthèses : on s'appuie sur la dernière ")".
    """
    end = stat.rindex(")")
    comm = stat[stat.index("(") + 1 : end]
    ppid = int(stat[end + 2 :].split()[1])
    return ppid, comm


def proc_info(pid: int) -> tuple[int, str] | None:
    try:
        with open(f"/proc/{pid}/stat") as f:
            stat = f.read()
        with open(f"/proc/{pid}/cmdline", "rb") as f:
            cmdline = f.read().replace(b"\0", b" ").decode(errors="replace")
    except OSError:
        return None
    ppid, comm = parse_stat(stat)
    return ppid, f"{comm} {cmdline}"


def find_claude_pid(start: int, info: ProcInfo = proc_info, max_depth: int = 10) -> int | None:
    """Remonte les ancêtres de `start` jusqu'au processus Claude Code."""
    pid = start
    for _ in range(max_depth):
        entry = info(pid)
        if entry is None:
            return None
        ppid, desc = entry
        if pid != start and ("claude" in desc.split(" ", 1)[0] or "claude-code" in desc):
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
