"""Relay between the C++ GUI mirror (WebMirror) and web clients.

The OpenVSP thread produces diffs (see src/gui_and_draw/WebMirror.cpp). This module keeps
the latest full state so a newly connected client gets a snapshot, forwards diffs and
prompts to every client, and passes client input back to the GUI.
"""

from __future__ import annotations

import asyncio
import json
from typing import Awaitable, Callable

from . import runtime

Broadcast = Callable[[dict], Awaitable[None]]


class Mirror:
    def __init__(self, broadcast: Broadcast, has_clients: Callable[[], bool]) -> None:
        self.enabled = runtime.gui
        self._broadcast = broadcast
        self._has_clients = has_clients
        self.windows: dict[int, dict] = {}
        self.widgets: dict[int, dict] = {}
        self.dialogs: dict[int, dict] = {}
        self._task: asyncio.Task | None = None

    def start(self) -> None:
        if self.enabled and self._task is None:
            runtime.vsp.MirrorRequestFull()
            self._task = asyncio.create_task(self._relay())

    async def _relay(self) -> None:
        while True:
            try:
                # Thread-safe in C++; returns immediately.
                msgs = json.loads(runtime.vsp.MirrorTakeOutput())
            except Exception as exc:  # noqa: BLE001
                print(f"vspweb: mirror output error: {exc!r}", flush=True)
                msgs = []
            if msgs:
                for m in msgs:
                    self._apply(m)
                await self._broadcast({"type": "mirror", "msgs": msgs})
            await asyncio.sleep(0.016)

    def _apply(self, m: dict) -> None:
        t = m.get("t")
        if t == "diff":
            if m.get("full"):
                self.windows.clear()
                self.widgets.clear()
            for wid in m.get("closed", []):
                self.windows.pop(wid, None)
                for k in [k for k, w in self.widgets.items() if w.get("win") == wid]:
                    del self.widgets[k]
            for w in m.get("windows", []):
                self.windows[w["id"]] = w
            for w in m.get("widgets", []):
                self.widgets[w["id"]] = w
            for wid in m.get("removed", []):
                self.widgets.pop(wid, None)
        elif t == "dialog":
            if m.get("kind") in ("choice", "file") and not self._has_clients():
                # Nobody to ask: answer like Cancel so the GUI thread doesn't wait forever.
                runtime.vsp.MirrorPostEvent(json.dumps({"op": "dialog", "seq": m["seq"], "result": 0, "path": ""}))
            elif self._has_clients():
                self.dialogs[m["seq"]] = m

    def snapshot(self) -> dict:
        return {
            "type": "mirror_snapshot",
            "enabled": self.enabled,
            "windows": list(self.windows.values()),
            "widgets": list(self.widgets.values()),
            "dialogs": list(self.dialogs.values()),
        }

    async def post(self, event: dict) -> None:
        if not self.enabled:
            return
        if event.get("op") == "dialog":
            seq = int(event.get("seq", 0))
            dialog = self.dialogs.pop(seq, None)
            if dialog is None:
                return  # already answered (e.g. by another tab)
            await self._broadcast({"type": "dialog_done", "seq": seq})
            if dialog.get("kind") not in ("choice", "file"):
                return  # messages don't block the GUI; nothing to send back
        # Values travel as strings/ints only; the C++ side parses flat JSON.
        flat = {k: v if isinstance(v, (int, float, str)) else str(v) for k, v in event.items()}
        runtime.vsp.MirrorPostEvent(json.dumps(flat))
