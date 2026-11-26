"""claude-dash-desktop: the Claude Code dashboard in an always-on-top Tk window."""

from __future__ import annotations

import argparse
import logging
import queue
import signal
import ssl
import sys
from pathlib import Path
from typing import NoReturn

from claude_dash.config import ConfigError
from claude_dash.desktop_config import default_path, load_desktop_config
from claude_dash.instance_lock import acquire, default_lock_path, holder_pid
from claude_dash.subscriber import Event, start_client, stop_client
from claude_dash.view import Controller


def _fail(code: int, message: str) -> NoReturn:
    # print, not logging: the message must reach the terminal whatever the log setup
    print(f"claude-dash-desktop : {message}", file=sys.stderr)
    sys.exit(code)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(prog="claude-dash-desktop", description=__doc__)
    parser.add_argument("--config", type=Path, default=default_path(), help="desktop.toml")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")

    try:
        cfg = load_desktop_config(args.config)
    except ConfigError as exc:
        _fail(2, str(exc))

    lock_path = default_lock_path()
    lock = acquire(lock_path)
    if lock is None:
        _fail(1, f"déjà lancé (PID {holder_pid(lock_path)})")

    # Imported late: Tk is optional for everything else (tests, CI without Tk).
    import tkinter as tk

    from claude_dash.desktop_ui import DesktopWindow

    try:
        root = tk.Tk(className="claude-dash")
    except tk.TclError as exc:
        _fail(2, f"pas d'affichage X ({exc}) : lancer depuis une session SSH avec X11 forwarding")

    events: queue.Queue[Event] = queue.Queue()
    try:
        client = start_client(cfg, events.put)
    except (OSError, ssl.SSLError, ValueError) as exc:
        root.destroy()
        _fail(2, f"client MQTT impossible à initialiser ({cfg.host}:{cfg.port}) : {exc}")

    def close(*_: object) -> None:
        root.after(0, root.destroy)

    signal.signal(signal.SIGTERM, close)
    signal.signal(signal.SIGINT, close)
    try:
        DesktopWindow(root, Controller(f"{cfg.host}:{cfg.port}"), events, cfg.corner).start()
        root.mainloop()
    finally:
        stop_client(client)
        lock.close()
