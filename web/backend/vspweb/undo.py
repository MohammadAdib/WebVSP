"""Undo/redo owned by the backend (OpenVSP does not export its internal undo stack).

Entries hold undo/redo callables that run on the VSP worker thread. Cheap edits
(parm values, names, show flags) store deltas; structural edits (add, delete,
paste, xsec changes) store whole-model .vsp3 snapshots.
"""

from __future__ import annotations

import os
import tempfile
import time
from dataclasses import dataclass, field
from typing import Callable

from .runtime import vsp

MAX_ENTRIES = 200
COALESCE_SECONDS = 1.0


@dataclass
class Entry:
    label: str
    undo: Callable[[], None]
    redo: Callable[[], None]
    key: str | None = None  # entries with the same key in quick succession merge
    stamp: float = field(default_factory=time.monotonic)
    files: list[str] = field(default_factory=list)


class Snapshot:
    """A .vsp3 file capturing the whole model at one moment."""

    def __init__(self) -> None:
        fd, self.path = tempfile.mkstemp(prefix="vspweb_undo_", suffix=".vsp3")
        os.close(fd)
        self.file_name = vsp.GetVSPFileName()
        vsp.WriteVSPFile(self.path, vsp.SET_ALL)
        # WriteVSPFile makes the written file the model's file; keep the user's file name.
        vsp.SetVSP3FileName(self.file_name)

    def discard(self) -> None:
        try:
            os.remove(self.path)
        except OSError:
            pass

    def restore(self) -> None:
        vsp.ClearVSPModel()
        vsp.ReadVSPFile(self.path)
        vsp.SetVSP3FileName(self.file_name)
        vsp.Update()


class UndoStack:
    def __init__(self) -> None:
        self._undo: list[Entry] = []
        self._redo: list[Entry] = []

    def push(self, entry: Entry) -> None:
        self._discard(self._redo)
        self._redo.clear()
        top = self._undo[-1] if self._undo else None
        if (
            entry.key is not None
            and top is not None
            and top.key == entry.key
            and entry.stamp - top.stamp < COALESCE_SECONDS
        ):
            # Keep the oldest undo, take the newest redo.
            top.redo = entry.redo
            top.stamp = entry.stamp
            top.files.extend(entry.files)
            return
        self._undo.append(entry)
        if len(self._undo) > MAX_ENTRIES:
            self._discard([self._undo.pop(0)])

    def push_parm(self, pid: str, old: float, new: float) -> None:
        def undo():
            vsp.SetParmValUpdate(pid, old)

        def redo():
            vsp.SetParmValUpdate(pid, new)

        self.push(Entry(f"Set {vsp.GetParmName(pid)}", undo, redo, key=f"parm:{pid}"))

    def push_snapshots(self, label: str, before: Snapshot, after: Snapshot) -> None:
        self.push(Entry(label, before.restore, after.restore, files=[before.path, after.path]))

    def undo(self) -> str | None:
        if not self._undo:
            return None
        e = self._undo.pop()
        e.undo()
        self._redo.append(e)
        return e.label

    def redo(self) -> str | None:
        if not self._redo:
            return None
        e = self._redo.pop()
        e.redo()
        self._undo.append(e)
        return e.label

    def clear(self) -> None:
        self._discard(self._undo)
        self._discard(self._redo)
        self._undo.clear()
        self._redo.clear()

    def state(self) -> dict:
        return {
            "canUndo": bool(self._undo),
            "canRedo": bool(self._redo),
            "undoLabel": self._undo[-1].label if self._undo else None,
            "redoLabel": self._redo[-1].label if self._redo else None,
        }

    @staticmethod
    def _discard(entries: list[Entry]) -> None:
        for e in entries:
            for f in e.files:
                try:
                    os.remove(f)
                except OSError:
                    pass
