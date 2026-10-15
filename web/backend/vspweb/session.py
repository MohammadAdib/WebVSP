"""The single OpenVSP editing session served to the frontend.

Every public coroutine runs its OpenVSP work on the worker thread, records undo,
works out which geoms changed, and broadcasts a change event to listeners.
"""

from __future__ import annotations

import asyncio
import hashlib
import struct
from typing import Awaitable, Callable

import numpy as np

from .runtime import vsp

from . import parms, runtime, tess
from .undo import Snapshot, UndoStack
from .vsp_worker import VspWorker

Listener = Callable[[dict], Awaitable[None]]


def resync_gui() -> None:
    """After the model is replaced behind the GUI's back (API New/Open), reset the Geom
    Browser's cached selection, as the GUI's own File menu would."""
    if hasattr(vsp, "MirrorPickGeom"):
        vsp.MirrorPickGeom("")


class Session:
    def __init__(self, worker: VspWorker | None = None) -> None:
        self.worker = worker or VspWorker()
        self.undo = UndoStack()
        self.rev = 0
        self.dirty_file = False
        self._fingerprints: dict[str, bytes] = {}
        self._mesh_cache: dict[str, tuple[bytes, bytes]] = {}
        self._listeners: set[Listener] = set()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._tree_sig: tuple | None = None
        self.worker.call_sync(self._refresh_fingerprints)

    def attach(self, loop: asyncio.AbstractEventLoop) -> None:
        """Start watching for model changes made outside our API (e.g. in the mirrored GUI)."""
        first = self._loop is None
        self._loop = loop
        if first:
            runtime.every(0.15, self._watch)

    # ------------------------------------------------------------------ events

    def subscribe(self, fn: Listener) -> None:
        self._listeners.add(fn)

    def unsubscribe(self, fn: Listener) -> None:
        self._listeners.discard(fn)

    async def _emit(self, event: dict) -> None:
        for fn in list(self._listeners):
            try:
                await fn(event)
            except Exception:
                self._listeners.discard(fn)

    # ------------------------------------------------------- change detection
    # Runs on the worker thread.

    def _fingerprint(self, gid: str, cache: dict[str, bytes]) -> bytes:
        if gid in cache:
            return cache[gid]
        h = hashlib.blake2b(digest_size=16)
        for pid in parms.all_parm_ids(gid):
            h.update(pid.encode())
            h.update(struct.pack("<d", vsp.GetParmVal(pid)))
        h.update(str(vsp.GetXSecSurfIDVec(gid)).encode())
        parent = vsp.GetGeomParent(gid)
        if parent and parent != "NONE":
            # Children move with their parent, so a parent change invalidates the child mesh.
            h.update(self._fingerprint(parent, cache))
        cache[gid] = h.digest()
        return cache[gid]

    def _refresh_fingerprints(self) -> tuple[list[str], list[str]]:
        """Recompute fingerprints; return (changed_or_new_ids, removed_ids)."""
        cache: dict[str, bytes] = {}
        current = list(vsp.FindGeoms())
        changed = []
        for gid in current:
            fp = self._fingerprint(gid, cache)
            if self._fingerprints.get(gid) != fp:
                changed.append(gid)
        removed = [g for g in self._fingerprints if g not in cache]
        self._fingerprints = {g: cache[g] for g in current}
        for g in removed:
            self._mesh_cache.pop(g, None)
        return changed, removed

    def _signature(self) -> tuple:
        geoms = tuple(
            (g, vsp.GetGeomName(g), vsp.GetGeomParent(g), bool(vsp.GetSetFlag(g, vsp.SET_SHOWN)), vsp.GetGeomDrawType(g))
            for g in vsp.FindGeoms()
        )
        active = tuple(vsp.GetActiveGeoms()) if hasattr(vsp, "GetActiveGeoms") else ()
        return geoms, active, vsp.GetVSPFileName()

    def _watch(self) -> None:
        """Runs on the OpenVSP thread every ~150 ms."""
        updated = vsp.GetAndResetUpdateCount() > 0
        sig = self._signature()
        tree_changed = sig != self._tree_sig
        if not updated and not tree_changed:
            return
        self._tree_sig = sig
        changed, removed = self._refresh_fingerprints()
        if (changed or removed or tree_changed) and self._loop is not None and not self._loop.is_closed():
            asyncio.run_coroutine_threadsafe(self._emit_external(changed, removed, tree_changed), self._loop)

    async def _emit_external(self, changed: list[str], removed: list[str], tree: bool) -> None:
        self.rev += 1
        if changed or removed:
            self.dirty_file = True
        await self._emit(
            {
                "type": "model_changed",
                "rev": self.rev,
                "tree": tree or bool(removed),
                "changed": changed,
                "removed": removed,
                "label": None,
                "undo": self.undo.state(),
            }
        )

    async def _after_change(self, tree: bool, label: str | None = None) -> dict:
        changed, removed = await self.worker.call(self._refresh_fingerprints)
        self.rev += 1
        self.dirty_file = True
        event = {
            "type": "model_changed",
            "rev": self.rev,
            "tree": tree or bool(removed),
            "changed": changed,
            "removed": removed,
            "label": label,
            "undo": self.undo.state(),
        }
        await self._emit(event)
        return event

    # ----------------------------------------------------------------- queries

    def _tree(self) -> dict:
        nodes = []
        for gid in vsp.FindGeoms():
            parent = vsp.GetGeomParent(gid)
            color = vsp.GetGeomWireColor(gid)
            nodes.append(
                {
                    "id": gid,
                    "name": vsp.GetGeomName(gid),
                    "type": vsp.GetGeomTypeName(gid),
                    "parent": parent if parent and parent != "NONE" else None,
                    "children": list(vsp.GetGeomChildren(gid)),
                    "shown": bool(vsp.GetSetFlag(gid, vsp.SET_SHOWN)),
                    "drawType": vsp.GetGeomDrawType(gid),
                    "color": [color.x() / 255.0, color.y() / 255.0, color.z() / 255.0],
                    "numXSecSurfs": vsp.GetNumXSecSurfs(gid),
                }
            )
        return {
            "geoms": nodes,
            "active": list(vsp.GetActiveGeoms()) if hasattr(vsp, "GetActiveGeoms") else [],
            "vehicleId": vsp.GetVehicleID(),
            "fileName": vsp.GetVSPFileName(),
            "version": vsp.GetVSPVersion(),
        }

    async def tree(self) -> dict:
        t = await self.worker.call(self._tree)
        t.update(rev=self.rev, dirty=self.dirty_file, undo=self.undo.state())
        return t

    async def geom_types(self) -> list[str]:
        return list(await self.worker.call(vsp.GetGeomTypes))

    async def geom_schema(self, gid: str) -> dict:
        return await self.worker.call(parms.geom_schema, gid)

    async def container_schema(self, cid: str) -> dict:
        return await self.worker.call(parms.container_schema, cid)

    async def containers(self) -> list[dict]:
        def run():
            geoms = set(vsp.FindGeoms())
            return [
                {"id": c, "name": vsp.GetContainerName(c)}
                for c in vsp.FindContainers()
                if c not in geoms
            ]

        return await self.worker.call(run)

    async def geom_values(self, gid: str) -> dict:
        return await self.worker.call(lambda: parms.values(parms.all_parm_ids(gid)))

    async def parm_values(self, pids: list[str]) -> dict:
        return await self.worker.call(parms.values, pids)

    async def mesh(self, gid: str) -> bytes:
        fp = self._fingerprints.get(gid)
        cached = self._mesh_cache.get(gid)
        if cached and fp is not None and cached[0] == fp:
            return cached[1]
        data = await self.worker.call(tess.geom_mesh_bytes, gid)
        if fp is not None:
            self._mesh_cache[gid] = (fp, data)
        return data

    async def bbox(self) -> dict | None:
        def run():
            lo = [float("inf")] * 3
            hi = [float("-inf")] * 3
            any_ = False
            for gid in vsp.FindGeoms():
                if not vsp.GetSetFlag(gid, vsp.SET_SHOWN):
                    continue
                boxes = []
                for s in range(vsp.GetTotalNumSurfs(gid)):
                    a = vsp.GetGeomBBoxMin(gid, s, True)
                    b = vsp.GetGeomBBoxMax(gid, s, True)
                    boxes.append(((a.x(), a.y(), a.z()), (b.x(), b.y(), b.z())))
                if not boxes and hasattr(vsp, "GetMeshGeomTriPnts") and vsp.GetGeomTypeName(gid) in ("Mesh", "NGonMesh"):
                    # Mesh Geoms have no surfaces: bound the triangles they draw.
                    pts = vsp.GetMeshGeomTriPnts(gid)
                    if pts:
                        p = np.asarray(pts).reshape(-1, 3)
                        boxes.append((p.min(axis=0).tolist(), p.max(axis=0).tolist()))
                for a, b in boxes:
                    for k, (x, y) in enumerate(zip(a, b)):
                        lo[k] = min(lo[k], x)
                        hi[k] = max(hi[k], y)
                    any_ = True
            return {"min": lo, "max": hi} if any_ else None

        return await self.worker.call(run)

    # --------------------------------------------------------------- mutations

    async def set_parm(self, pid: str, value: float) -> dict:
        def run():
            old = vsp.GetParmVal(pid)
            new = vsp.SetParmValUpdate(pid, value)
            if new != old:
                self.undo.push_parm(pid, old, new)
            return new

        await self.worker.call(run)
        return await self._after_change(tree=False)

    async def geom_origin(self, gid: str) -> list[float]:
        def run():
            p = vsp.MirrorGeomOrigin(gid)
            return [p.x(), p.y(), p.z()]

        return await self.worker.call(run)

    async def move_geoms(self, ids: list[str], delta: list[float]) -> None:
        """Move geoms by an offset in the 3D view (the move gizmo). OpenVSP records it as one
        step of Edit > Undo, which is the undo the page uses."""
        await self.worker.call(lambda: vsp.MirrorMoveGeoms(ids, vsp.vec3d(*delta)))
        await self._after_change(tree=False)

    async def _structural(self, label: str, fn: Callable[[], object], tree: bool = True) -> object:
        def run():
            before = Snapshot()
            try:
                result = fn()
            except Exception:
                before.discard()
                raise
            vsp.Update()
            after = Snapshot()
            self.undo.push_snapshots(label, before, after)
            return result

        result = await self.worker.call(run)
        await self._after_change(tree=tree, label=label)
        return result

    async def add_geom(self, type_name: str, parent: str | None = None) -> str:
        return await self._structural(f"Add {type_name}", lambda: vsp.AddGeom(type_name, parent or ""))

    async def delete_geoms(self, ids: list[str]) -> None:
        await self._structural("Delete", lambda: vsp.DeleteGeomVec(ids))

    async def cut_geom(self, gid: str) -> None:
        await self._structural("Cut", lambda: vsp.CutGeomToClipboard(gid))

    async def copy_geom(self, gid: str) -> None:
        await self.worker.call(vsp.CopyGeomToClipboard, gid)

    async def paste_geom(self, parent: str | None = None) -> list[str]:
        return list(await self._structural("Paste", lambda: vsp.PasteGeomClipboard(parent or "")))

    async def rename_geom(self, gid: str, name: str) -> None:
        await self._structural("Rename", lambda: vsp.SetGeomName(gid, name))

    async def set_shown(self, gid: str, shown: bool) -> None:
        def fn():
            vsp.SetSetFlag(gid, vsp.SET_SHOWN, shown)
            vsp.SetSetFlag(gid, vsp.SET_NOT_SHOWN, not shown)

        await self._structural("Show" if shown else "Hide", fn)

    async def insert_xsec(self, gid: str, index: int, shape: int) -> None:
        await self._structural("Insert section", lambda: vsp.InsertXSec(gid, index, shape), tree=False)

    async def cut_xsec(self, gid: str, index: int) -> None:
        await self._structural("Delete section", lambda: vsp.CutXSec(gid, index), tree=False)

    async def change_xsec_shape(self, xsec_surf_id: str, index: int, shape: int) -> None:
        await self._structural(
            "Change section shape", lambda: vsp.ChangeXSecShape(xsec_surf_id, index, shape), tree=False
        )

    async def do_undo(self) -> str | None:
        label = await self.worker.call(self.undo.undo)
        if label:
            await self._after_change(tree=True, label=f"Undo {label}")
        return label

    async def do_redo(self) -> str | None:
        label = await self.worker.call(self.undo.redo)
        if label:
            await self._after_change(tree=True, label=f"Redo {label}")
        return label

    # -------------------------------------------------------------------- files

    async def new_model(self) -> None:
        def run():
            vsp.ClearVSPModel()
            vsp.Update()
            self.undo.clear()
            resync_gui()

        await self.worker.call(run)
        await self._after_change(tree=True, label="New")
        self.dirty_file = False

    async def open_file(self, path: str) -> None:
        def run():
            vsp.ClearVSPModel()
            vsp.ReadVSPFile(path)
            vsp.Update()
            self.undo.clear()
            resync_gui()

        await self.worker.call(run)
        await self._after_change(tree=True, label="Open")
        self.dirty_file = False

    async def insert_file(self, path: str, parent: str | None = None) -> None:
        await self._structural("Insert file", lambda: vsp.InsertVSPFile(path, parent or ""))

    async def save_file(self, path: str | None = None) -> str:
        def run():
            target = path or vsp.GetVSPFileName()
            vsp.SetVSP3FileName(target)
            vsp.WriteVSPFile(target, vsp.SET_ALL)
            return target

        target = await self.worker.call(run)
        self.dirty_file = False
        await self._emit({"type": "saved", "fileName": target})
        return target

    async def export_file(self, path: str, file_type: int, set_index: int = 0) -> str:
        return await self.worker.call(vsp.ExportFile, path, set_index, file_type)

    async def pick(self, gid: str, toggle: bool = False) -> None:
        """Select a geom in the GUI, as a click in the 3D view would (toggle: a shift click)."""
        if hasattr(vsp, "MirrorPickGeom"):
            await self.worker.call(vsp.MirrorPickGeom, gid, toggle)
