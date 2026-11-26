"""Tk layer of the desktop client: draws a view.Screen and plays view.Reaction.

Kept thin on purpose (no logic to test here) and light over remote X: widgets are
rebuilt only when the content or the window size changes; the per-second tick
only touches labels whose text changed.
"""

from __future__ import annotations

import queue
import time
import tkinter as tk
from dataclasses import dataclass, field
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


@dataclass
class _CardWidgets:
    color: str
    inner: tk.Frame
    labels: list[tk.Label] = field(default_factory=list)
    duration: tk.Label | None = None
    since: int = 0


def _ellipsize(text: str, font: tkfont.Font, max_px: int) -> str:
    if font.measure(text) <= max_px:
        return text
    while text and font.measure(text + "…") > max_px:
        text = text[:-1]
    return text + "…" if text else ""


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
        self.cards: dict[tuple[str, str], _CardWidgets] = {}
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

    def _label(
        self, parent: tk.Misc, font: tkfont.Font, color: str, bg: str = COLOR_BG
    ) -> tk.Label:
        return tk.Label(parent, font=font, fg=color, bg=bg, anchor="w", padx=0, pady=0)

    def _build_static(self) -> None:
        header = tk.Frame(self.root, bg=COLOR_BG)
        header.pack(fill="x", padx=10, pady=(6, 2))
        title = self._label(header, self.f18, COLOR_TEXT)
        title.configure(text="Claude Code")
        title.pack(side="left")
        self.host = self._label(header, self.f16, COLOR_DIM)
        self.host.pack(side="left", padx=(12, 0))
        self.clock = self._label(header, self.f18, COLOR_TEXT)
        self.clock.pack(side="right")
        self.dot = tk.Canvas(header, width=12, height=12, bg=COLOR_BG, highlightthickness=0)
        self.dot.pack(side="right", padx=8)
        self.dot_item = self.dot.create_oval(1, 1, 11, 11, fill=COLOR_PERMISSION, width=0)

        self.quota_rows = [self._quota_row(), self._quota_row()]
        self.banner = tk.Label(self.root, font=self.f16, fg=COLOR_TEXT, bg=COLOR_PERMISSION)
        self.list = tk.Frame(self.root, bg=COLOR_BG)
        self.list.pack(fill="both", expand=True, padx=8, pady=(4, 6))

    def _quota_row(self) -> tuple[tk.Label, tk.Canvas, tk.Label, tk.Label]:
        row = tk.Frame(self.root, bg=COLOR_BG)
        row.pack(fill="x", padx=10, pady=1)
        name = self._label(row, self.f16, COLOR_TEXT)
        name.configure(width=3)
        name.pack(side="left")
        gauge = tk.Canvas(row, width=220, height=14, bg=COLOR_TRACK, highlightthickness=0)
        gauge.pack(side="left", padx=(4, 10))
        pct = self._label(row, self.f16, COLOR_TEXT)
        pct.configure(width=4)
        pct.pack(side="left")
        reset = self._label(row, self.f14, COLOR_DIM)
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
        self.root.after(POLL_MS, self._poll)

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
        now = int(time.time())
        screen = self.controller.screen(now)
        if screen.dimmed != self.shown_dimmed:
            self._render(screen)
        else:
            self._set(self.clock, clock_text(now))
            self._show_banner(screen.banner)
            for widgets in self.cards.values():
                if widgets.duration is not None:
                    self._set(widgets.duration, duration_text(widgets.since, now))
        self.root.after(TICK_MS, self._tick)

    def _expire(self) -> None:
        now = int(time.time())
        if self.controller.expire(now).rebuild:
            self._render(self.controller.screen(now))
        self.root.after(EXPIRE_MS, self._expire)

    def _flash_step(self) -> None:
        self.flash_on = not self.flash_on
        now = time.monotonic()
        for key, until in list(self.flash_until.items()):
            widgets = self.cards.get(key)
            active = until > now
            if not active:
                del self.flash_until[key]
            if widgets is not None:
                self._paint_card(widgets, widgets.color if active and self.flash_on else COLOR_CARD)
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

    @staticmethod
    def _set(label: tk.Label, text: str) -> None:
        if label.cget("text") != text:
            label.configure(text=text)

    def _show_banner(self, text: str) -> None:
        if not text:
            self.banner.pack_forget()
            return
        self._set(self.banner, text)
        if not self.banner.winfo_ismapped():
            self.banner.pack(fill="x", padx=8, pady=(4, 0), before=self.list)

    def _gauge(self, canvas: tk.Canvas, width: int, pct: int, color: str) -> None:
        canvas.configure(width=width)
        canvas.delete("all")
        if pct > 0:
            height = int(canvas.cget("height"))
            canvas.create_rectangle(0, 0, width * min(pct, 100) // 100, height, fill=color, width=0)

    def _render_quota(
        self, row: tuple[tk.Label, tk.Canvas, tk.Label, tk.Label], q: Quota, width: int
    ) -> None:
        name, gauge, pct, reset = row
        self._set(name, q.label)
        self._gauge(gauge, width, q.pct, q.color)
        self._set(pct, q.pct_text)
        self._set(reset, q.reset_text)

    def _render(self, screen: Screen) -> None:
        now = int(time.time())
        self.root.update_idletasks()
        win_w = max(self.root.winfo_width(), 320)
        self._set(self.host, screen.host_label)
        self._set(self.clock, clock_text(now))
        self.dot.itemconfigure(self.dot_item, fill=COLOR_OK if screen.mqtt_ok else COLOR_PERMISSION)
        quota_w = max(win_w - 260, 60)
        for row, quota in zip(self.quota_rows, screen.quotas, strict=True):
            self._render_quota(row, quota, quota_w)
        self._show_banner(screen.banner)
        self.shown_dimmed = screen.dimmed
        self._render_cards(screen, win_w, now)

    def _card_height(self) -> int:
        lines = self.f18.metrics("linespace") + self.f14.metrics("linespace")
        return int(lines) + 2 * PAD + 8

    def _render_cards(self, screen: Screen, win_w: int, now: int) -> None:
        for child in self.list.winfo_children():
            child.destroy()
        self.cards = {}
        if screen.message:
            msg = self._label(self.list, self.f16, COLOR_DIM)
            msg.configure(text=screen.message, wraplength=win_w - 30, justify="left")
            msg.pack(fill="x", pady=10)
            return
        self.root.update_idletasks()
        available = self.root.winfo_height() - self.list.winfo_y() - 6
        more_h = int(self.f14.metrics("linespace")) + 4
        shown = fit_cards(len(screen.cards), available, self._card_height(), more_h)
        for card in screen.cards[:shown]:
            self._add_card(card, win_w, now, screen.dimmed)
        hidden = len(screen.cards) - shown
        if hidden > 0:
            more = self._label(self.list, self.f14, COLOR_DIM)
            more.configure(text=f"+{hidden} autres sessions")
            more.pack(fill="x")

    def _add_card(self, card: Card, win_w: int, now: int, dimmed: bool) -> None:
        outer = tk.Frame(self.list, bg=card.state_color)
        outer.pack(fill="x", pady=(0, 4))
        inner = tk.Frame(outer, bg=COLOR_CARD, padx=PAD, pady=PAD // 2)
        inner.pack(fill="both", expand=True, padx=(BORDER, 0))
        widgets = _CardWidgets(color=card.state_color, inner=inner, since=card.since)
        text = COLOR_DIM if dimmed else COLOR_TEXT
        state = COLOR_DIM if dimmed else card.state_color

        def label(font: tkfont.Font, color: str, value: str, row: int, column: int) -> tk.Label:
            lbl = self._label(inner, font, color, COLOR_CARD)
            lbl.configure(text=value)
            lbl.grid(row=row, column=column, sticky="w", padx=(0, 8))
            widgets.labels.append(lbl)
            return lbl

        inner_w = win_w - 2 * 8 - BORDER - 2 * PAD
        label(self.f18, text, _ellipsize(card.title, self.f18, int(inner_w * 0.38)), 0, 0)
        label(self.f14, COLOR_DIM, _ellipsize(card.model, self.f14, int(inner_w * 0.2)), 0, 1)
        label(self.f14, state, card.state_text, 0, 2)
        label(self.f14, COLOR_DIM, _ellipsize(card.tool, self.f14, int(inner_w * 0.14)), 0, 3)
        duration = label(self.f16, text, duration_text(card.since, now), 0, 5)
        duration.grid_configure(sticky="e", padx=0)
        widgets.duration = duration
        inner.grid_columnconfigure(4, weight=1)

        ctx = tk.Frame(inner, bg=COLOR_CARD)
        ctx.grid(row=1, column=0, columnspan=6, sticky="w", pady=(2, 0))
        name = self._label(ctx, self.f14, COLOR_DIM, COLOR_CARD)
        name.configure(text="ctx")
        name.pack(side="left")
        gauge = tk.Canvas(ctx, width=140, height=10, bg=COLOR_TRACK, highlightthickness=0)
        gauge.pack(side="left", padx=8)
        self._gauge(gauge, max(inner_w // 3, 60), card.ctx, card.ctx_color)
        pct = self._label(ctx, self.f14, text, COLOR_CARD)
        pct.configure(text=card.ctx_text)
        pct.pack(side="left")
        widgets.labels.extend([name, pct])
        self.cards[card.key] = widgets
        if card.key in self.flash_until:
            self._paint_card(widgets, card.state_color if self.flash_on else COLOR_CARD)

    @staticmethod
    def _paint_card(widgets: _CardWidgets, bg: str) -> None:
        widgets.inner.configure(bg=bg)
        for child in widgets.inner.winfo_children():
            child.configure(bg=bg)  # type: ignore[call-arg]
        for lbl in widgets.labels:
            lbl.configure(bg=bg)
