"""Tk layer of the desktop client: draws a view.Screen and plays view.Reaction.

Kept thin and light over remote X: every widget is created once and then only
reconfigured when its text, color or size actually changes. Destroying and
rebuilding widgets blanks them for a moment on the X server, which flickers on
every snapshot.
"""

from __future__ import annotations

import logging
import queue
import time
import tkinter as tk
from tkinter import font as tkfont

from claude_dash.desktop_config import Corner
from claude_dash.model import (
    COLOR_BG,
    COLOR_CARD,
    COLOR_DIM,
    COLOR_OK,
    COLOR_PERMISSION,
    COLOR_TEXT,
    COLOR_TRACK,
)
from claude_dash.subscriber import Event, RemovedEvent, SnapshotEvent, StatusEvent
from claude_dash.view import (
    Card,
    Controller,
    Quota,
    Reaction,
    Screen,
    clock_text,
    duration_text,
    fit_cards,
    initial_geometry,
)

POLL_MS = 200
TICK_MS = 1000
EXPIRE_MS = 30_000
RESIZE_DEBOUNCE_MS = 100
BELL_GAP_MS = 300
FLASH_STEP_MS = 500
FLASH_S = 4.0
BORDER = 5
PAD = 6
LIST_PADX = 8

log = logging.getLogger("claude-dash-desktop")


def _ellipsize(text: str, font: tkfont.Font, max_px: int) -> str:
    if font.measure(text) <= max_px:
        return text
    while text and font.measure(text + "…") > max_px:
        text = text[:-1]
    return text + "…" if text else ""


def _set(widget: tk.Label | tk.Frame, **options: str) -> None:
    """Configure only the options whose value changes (each change repaints)."""
    changed = {k: v for k, v in options.items() if str(widget.cget(k)) != v}
    if changed:
        widget.configure(**changed)


def _label(parent: tk.Misc, font: tkfont.Font, color: str, bg: str = COLOR_BG) -> tk.Label:
    return tk.Label(parent, font=font, fg=color, bg=bg, anchor="w", padx=0, pady=0)


class _Gauge:
    """Percentage bar: one persistent rectangle, moved instead of redrawn."""

    def __init__(self, parent: tk.Misc, width: int, height: int) -> None:
        self.canvas = tk.Canvas(
            parent, width=width, height=height, bg=COLOR_TRACK, highlightthickness=0
        )
        self.height = height
        self.bar = self.canvas.create_rectangle(0, 0, 0, height, fill=COLOR_TRACK, width=0)
        self.shown: tuple[int, int, str] | None = None

    def show(self, width: int, pct: int, color: str) -> None:
        if self.shown == (width, pct, color):
            return
        self.shown = (width, pct, color)
        if str(self.canvas.cget("width")) != str(width):
            self.canvas.configure(width=width)
        filled = width * min(max(pct, 0), 100) // 100
        self.canvas.coords(self.bar, 0, 0, filled, self.height)
        self.canvas.itemconfigure(self.bar, fill=color)


class _CardSlot:
    """One card position in the list, reused for whatever card lands there."""

    def __init__(self, window: DesktopWindow, parent: tk.Frame) -> None:
        self.outer = tk.Frame(parent, bg=COLOR_CARD)
        self.inner = tk.Frame(self.outer, bg=COLOR_CARD, padx=PAD, pady=PAD // 2)
        self.inner.pack(fill="both", expand=True, padx=(BORDER, 0))
        self.title = self._cell(window.f18, 0)
        self.model = self._cell(window.f14, 1)
        self.state = self._cell(window.f14, 2)
        self.tool = self._cell(window.f14, 3)
        self.duration = self._cell(window.f16, 5)
        self.duration.grid_configure(sticky="e", padx=0)
        self.inner.grid_columnconfigure(4, weight=1)
        self.ctx_row = tk.Frame(self.inner, bg=COLOR_CARD)
        self.ctx_row.grid(row=1, column=0, columnspan=6, sticky="w", pady=(2, 0))
        self.ctx_name = _label(self.ctx_row, window.f14, COLOR_DIM, COLOR_CARD)
        self.ctx_name.configure(text="ctx")
        self.ctx_name.pack(side="left")
        self.gauge = _Gauge(self.ctx_row, 140, 10)
        self.gauge.canvas.pack(side="left", padx=8)
        self.ctx_pct = _label(self.ctx_row, window.f14, COLOR_TEXT, COLOR_CARD)
        self.ctx_pct.pack(side="left")
        self.key: tuple[str, str] = ("", "")
        self.color = COLOR_CARD
        self.since = 0

    def _cell(self, font: tkfont.Font, column: int) -> tk.Label:
        cell = _label(self.inner, font, COLOR_TEXT, COLOR_CARD)
        cell.grid(row=0, column=column, sticky="w", padx=(0, 8))
        return cell

    def show(self, window: DesktopWindow, card: Card, width: int, now: int, dimmed: bool) -> None:
        self.key, self.color, self.since = card.key, card.state_color, card.since
        text = COLOR_DIM if dimmed else COLOR_TEXT
        inner_w = width - 2 * LIST_PADX - BORDER - 2 * PAD
        _set(self.outer, bg=card.state_color)
        _set(self.title, text=_ellipsize(card.title, window.f18, int(inner_w * 0.38)), fg=text)
        _set(self.model, text=_ellipsize(card.model, window.f14, int(inner_w * 0.2)), fg=COLOR_DIM)
        _set(self.state, text=card.state_text, fg=COLOR_DIM if dimmed else card.state_color)
        _set(self.tool, text=_ellipsize(card.tool, window.f14, int(inner_w * 0.14)), fg=COLOR_DIM)
        _set(self.duration, text=duration_text(card.since, now), fg=text)
        self.gauge.show(max(inner_w // 3, 60), card.ctx, card.ctx_color)
        _set(self.ctx_pct, text=card.ctx_text, fg=text)

    def paint(self, bg: str) -> None:
        for widget in (
            self.inner,
            self.title,
            self.model,
            self.state,
            self.tool,
            self.duration,
            self.ctx_row,
            self.ctx_name,
            self.ctx_pct,
        ):
            _set(widget, bg=bg)


class DesktopWindow:
    def __init__(
        self,
        root: tk.Tk,
        controller: Controller,
        events: queue.Queue[Event],
        corner: Corner,
    ) -> None:
        self.root = root
        self.controller = controller
        self.events = events
        family = "DejaVu Sans" if "DejaVu Sans" in tkfont.families(root) else "TkDefaultFont"
        # Negative sizes are pixels: same sizes as the LVGL fonts of the screen.
        self.f18 = tkfont.Font(root, family=family, size=-18)
        self.f16 = tkfont.Font(root, family=family, size=-16)
        self.f14 = tkfont.Font(root, family=family, size=-14)
        self.slots: list[_CardSlot] = []
        self.cards: dict[tuple[str, str], _CardSlot] = {}
        self.flash_until: dict[tuple[str, str], float] = {}
        self.flash_on = False
        self.shown_dimmed = False
        self.resize_job: str | None = None
        self.last_size = (0, 0)

        root.title("Claude Code")
        root.configure(bg=COLOR_BG)
        root.geometry(initial_geometry(corner, root.winfo_screenwidth(), root.winfo_screenheight()))
        root.minsize(320, 200)
        # MobaXterm keeps the window above once asked and never lets it go.
        root.attributes("-topmost", True)
        self._build_static()

    # --- static part: header, quotas, banner, list area ---

    def _build_static(self) -> None:
        header = tk.Frame(self.root, bg=COLOR_BG)
        header.pack(fill="x", padx=10, pady=(6, 2))
        title = _label(header, self.f18, COLOR_TEXT)
        title.configure(text="Claude Code")
        title.pack(side="left")
        self.host = _label(header, self.f16, COLOR_DIM)
        self.host.pack(side="left", padx=(12, 0))
        self.clock = _label(header, self.f18, COLOR_TEXT)
        self.clock.pack(side="right")
        self.dot = tk.Canvas(header, width=12, height=12, bg=COLOR_BG, highlightthickness=0)
        self.dot.pack(side="right", padx=8)
        self.dot_item = self.dot.create_oval(1, 1, 11, 11, fill=COLOR_PERMISSION, width=0)
        self.dot_color = COLOR_PERMISSION

        self.quota_rows = [self._quota_row(), self._quota_row()]
        self.banner = tk.Label(self.root, font=self.f16, fg=COLOR_TEXT, bg=COLOR_PERMISSION)
        self.list = tk.Frame(self.root, bg=COLOR_BG)
        self.list.pack(fill="both", expand=True, padx=LIST_PADX, pady=(4, 6))
        self.message = _label(self.list, self.f16, COLOR_DIM)
        self.message.configure(justify="left")
        self.more = _label(self.list, self.f14, COLOR_DIM)

    def _quota_row(self) -> tuple[tk.Label, _Gauge, tk.Label, tk.Label]:
        row = tk.Frame(self.root, bg=COLOR_BG)
        row.pack(fill="x", padx=10, pady=1)
        name = _label(row, self.f16, COLOR_TEXT)
        name.configure(width=3)
        name.pack(side="left")
        gauge = _Gauge(row, 220, 14)
        gauge.canvas.pack(side="left", padx=(4, 10))
        pct = _label(row, self.f16, COLOR_TEXT)
        pct.configure(width=4)
        pct.pack(side="left")
        reset = _label(row, self.f14, COLOR_DIM)
        reset.pack(side="left", padx=(6, 0))
        return name, gauge, pct, reset

    # --- loops ---

    def start(self) -> None:
        self.root.bind("<Configure>", self._on_configure)
        self._render(self.controller.screen(int(time.time())))
        self.root.after(POLL_MS, self._poll)
        self.root.after(TICK_MS, self._tick)
        self.root.after(EXPIRE_MS, self._expire)
        self.root.after(FLASH_STEP_MS, self._flash_step)

    def _poll(self) -> None:
        # Reschedule first: an exception in a Tk callback would end the loop for good.
        self.root.after(POLL_MS, self._poll)
        try:
            self._drain()
        except Exception:
            log.exception("événements MQTT non traités")

    def _drain(self) -> None:
        now = int(time.time())
        rebuild, bells, flash = False, 0, frozenset[tuple[str, str]]()
        while True:
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                break
            reaction = self._route(event, now)
            rebuild |= reaction.rebuild
            bells = max(bells, reaction.bells)
            flash |= reaction.flash
        if rebuild:
            self._render(self.controller.screen(now))
        self._ring(bells)
        deadline = time.monotonic() + FLASH_S
        for key in flash:
            self.flash_until[key] = deadline

    def _route(self, event: Event, now: int) -> Reaction:
        if isinstance(event, SnapshotEvent):
            return self.controller.on_snapshot(event.snapshot, now)
        if isinstance(event, RemovedEvent):
            return self.controller.on_removed(event.host)
        if isinstance(event, StatusEvent):
            return self.controller.on_status(event.status)
        return Reaction(False, 0, frozenset())

    def _ring(self, count: int) -> None:
        for i in range(count):
            self.root.after(i * BELL_GAP_MS, self.root.bell)

    def _tick(self) -> None:
        self.root.after(TICK_MS, self._tick)
        try:
            self._refresh()
        except Exception:
            log.exception("rafraîchissement de la fenêtre impossible")

    def _refresh(self) -> None:
        now = int(time.time())
        screen = self.controller.screen(now)
        if screen.dimmed != self.shown_dimmed:
            self._render(screen)
        else:
            _set(self.clock, text=clock_text(now))
            self._show_banner(screen.banner)
            for slot in self.slots:
                _set(slot.duration, text=duration_text(slot.since, now))

    def _expire(self) -> None:
        self.root.after(EXPIRE_MS, self._expire)
        try:
            now = int(time.time())
            if self.controller.expire(now).rebuild:
                self._render(self.controller.screen(now))
        except Exception:
            log.exception("oubli des serveurs muets impossible")

    def _flash_step(self) -> None:
        self.flash_on = not self.flash_on
        now = time.monotonic()
        for key, until in list(self.flash_until.items()):
            active = until > now
            if not active:
                del self.flash_until[key]
            slot = self.cards.get(key)
            if slot is not None:
                slot.paint(slot.color if active and self.flash_on else COLOR_CARD)
        self.root.after(FLASH_STEP_MS, self._flash_step)

    def _on_configure(self, event: tk.Event[tk.Misc]) -> None:
        if event.widget is not self.root:
            return
        size = (event.width, event.height)
        if size == self.last_size:
            return
        self.last_size = size
        if self.resize_job is not None:
            self.root.after_cancel(self.resize_job)
        self.resize_job = self.root.after(RESIZE_DEBOUNCE_MS, self._on_resized)

    def _on_resized(self) -> None:
        self.resize_job = None
        self._render(self.controller.screen(int(time.time())))

    # --- rendering ---

    def _show_banner(self, text: str) -> None:
        if not text:
            if self.banner.winfo_manager():
                self.banner.pack_forget()
            return
        _set(self.banner, text=text)
        if not self.banner.winfo_manager():
            self.banner.pack(fill="x", padx=LIST_PADX, pady=(4, 0), before=self.list)

    def _render_quota(
        self, row: tuple[tk.Label, _Gauge, tk.Label, tk.Label], q: Quota, width: int
    ) -> None:
        name, gauge, pct, reset = row
        _set(name, text=q.label)
        gauge.show(width, q.pct, q.color)
        _set(pct, text=q.pct_text)
        _set(reset, text=q.reset_text)

    def _render(self, screen: Screen) -> None:
        now = int(time.time())
        win_w = max(self.root.winfo_width(), 320)
        _set(self.host, text=screen.host_label)
        _set(self.clock, text=clock_text(now))
        dot = COLOR_OK if screen.mqtt_ok else COLOR_PERMISSION
        if dot != self.dot_color:
            self.dot_color = dot
            self.dot.itemconfigure(self.dot_item, fill=dot)
        for row, quota in zip(self.quota_rows, screen.quotas, strict=True):
            self._render_quota(row, quota, max(win_w - 260, 60))
        self._show_banner(screen.banner)
        self.shown_dimmed = screen.dimmed
        self._render_cards(screen, win_w, now)

    def _card_height(self) -> int:
        lines = self.f18.metrics("linespace") + self.f14.metrics("linespace")
        return int(lines) + 2 * PAD + 8

    def _available_height(self) -> int:
        self.root.update_idletasks()
        return self.root.winfo_height() - self.list.winfo_y() - 6

    def _render_cards(self, screen: Screen, win_w: int, now: int) -> None:
        shown = 0
        if not screen.message:
            more_h = int(self.f14.metrics("linespace")) + 4
            shown = fit_cards(
                len(screen.cards), self._available_height(), self._card_height(), more_h
            )
        while len(self.slots) > shown:
            self.slots.pop().outer.destroy()
        while len(self.slots) < shown:
            slot = _CardSlot(self, self.list)
            if self.more.winfo_manager():
                slot.outer.pack(fill="x", pady=(0, 4), before=self.more)
            else:
                slot.outer.pack(fill="x", pady=(0, 4))
            self.slots.append(slot)
        self.cards = {}
        for slot, card in zip(self.slots, screen.cards, strict=False):
            slot.show(self, card, win_w, now, screen.dimmed)
            self.cards[card.key] = slot
            flashing = card.key in self.flash_until and self.flash_on
            slot.paint(card.state_color if flashing else COLOR_CARD)
        self._show_line(self.message, screen.message, wraplength=str(win_w - 30))
        hidden = len(screen.cards) - shown if not screen.message else 0
        self._show_line(self.more, f"+{hidden} autres sessions" if hidden > 0 else "")

    @staticmethod
    def _show_line(label: tk.Label, text: str, **options: str) -> None:
        if not text:
            if label.winfo_manager():
                label.pack_forget()
            return
        _set(label, text=text, **options)
        if not label.winfo_manager():
            label.pack(fill="x", pady=(6, 0))
