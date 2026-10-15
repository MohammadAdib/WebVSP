"""Relay between the C++ GUI mirror (WebMirror) and web clients.

The OpenVSP thread produces diffs (see src/gui_and_draw/WebMirror.cpp). This module keeps
the latest full state so a newly connected client gets a snapshot, forwards diffs and
prompts to every client, and passes client input back to the GUI.
"""

from __future__ import annotations

import asyncio
import json
import os
import re
import sys
from pathlib import Path
from typing import Awaitable, Callable

from . import runtime

Broadcast = Callable[[dict], Awaitable[None]]

# File choosers use the desktop's own open/save dialog (the engine runs on the user's
# machine). WEBVSP_NATIVE_DIALOGS=0 uses the web file browser instead.
NATIVE_DIALOGS = sys.platform == "win32" and os.environ.get("WEBVSP_NATIVE_DIALOGS", "1") not in ("0", "false", "no")


def _settings_path() -> Path:
    base = os.environ.get("APPDATA") or str(Path.home())
    return Path(base) / "WebVSP" / "settings.json"


def _load_settings() -> dict:
    try:
        return json.loads(_settings_path().read_text(encoding="utf-8"))
    except Exception:  # noqa: BLE001 - missing or unreadable: defaults
        return {}


def _save_setting(key: str, value) -> None:
    settings = _load_settings()
    settings[key] = value
    try:
        path = _settings_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(settings, indent=2), encoding="utf-8")
    except OSError as exc:
        print(f"vspweb: could not save settings: {exc!r}", flush=True)


def file_types(fltk_filter: str) -> tuple[str, str]:
    """An FLTK filter ("*.vsp3", "*.{af,dat}", "Images (*.{jpg,png})", one per tab/newline)
    as native dialog file types ("Label\tpattern" lines) plus the default extension."""
    lines, first_ext = [], ""
    for part in re.split(r"[\t\n]+", fltk_filter or ""):
        exts: list[str] = []
        for m in re.finditer(r"\*\.(\{[^}]*\}|[\w]+)", part):
            exts += [e.strip() for e in m.group(1).strip("{}").split(",") if e.strip()]
        exts = [e for e in exts if e != "*"]
        if not exts:
            continue
        first_ext = first_ext or exts[0]
        label = re.sub(r"\s*\(.*\)\s*$", "", part).strip()
        if not label or label.startswith("*"):
            label = ", ".join(e.upper() for e in exts) + " files"
        patterns = ";".join(f"*.{e}" for e in exts)
        lines.append(f"{label} ({patterns})\t{patterns}")
    lines.append("All files (*.*)\t*.*")
    return "\n".join(lines), first_ext


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
                if m.get("kind") == "file" and NATIVE_DIALOGS and hasattr(runtime.vsp, "MirrorNativeFileDialog"):
                    m["native"] = True  # clients show a notice instead of their own picker
                    asyncio.create_task(self._native_file(m))
                self.dialogs[m["seq"]] = m

    async def _native_file(self, m: dict) -> None:
        """Answer a file chooser with the desktop's dialog, opened in the last folder used."""
        save = bool(m.get("mode"))
        types, ext = file_types(m.get("filter", ""))
        last = _load_settings().get("last_dir", "")
        folder = last if last and os.path.isdir(last) else m.get("dir", "")
        preset = os.path.basename(m.get("preset", "")) if save else ""
        title = m.get("title") or ("Save" if save else "Open")
        # Off the OpenVSP thread (the SWIG call releases the GIL), so the GUI keeps running.
        path = await asyncio.to_thread(
            runtime.vsp.MirrorNativeFileDialog, title, save, types, folder, preset, ext if save else ""
        )
        if path.startswith("*error"):
            print(f"vspweb: native file dialog unavailable ({path[1:]}); using the web dialog", flush=True)
            # Hand the prompt to the clients' own file browser instead.
            await self._broadcast({"type": "dialog_done", "seq": m["seq"]})
            m = {**m, "native": False}
            self.dialogs[m["seq"]] = m
            await self._broadcast({"type": "mirror", "msgs": [m]})
            return
        if path:
            _save_setting("last_dir", os.path.dirname(path))
        await self.post({"op": "dialog", "seq": m["seq"], "result": 1 if path else 0, "path": path})

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
