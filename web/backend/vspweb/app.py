"""FastAPI application: REST commands under /api, change events on /api/ws."""

from __future__ import annotations

import asyncio
import os
import secrets
import string
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse, Response
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from .mirror import Mirror
from .session import Session
from .vsp_worker import VspError

FRONTEND_DIST = Path(__file__).resolve().parents[2] / "frontend" / "dist"
NO_CACHE = {"Cache-Control": "no-cache"}


class ParmSet(BaseModel):
    value: float


class ParmQuery(BaseModel):
    ids: list[str]


class AddGeom(BaseModel):
    type: str
    parent: str | None = None


class IdList(BaseModel):
    ids: list[str]


class Rename(BaseModel):
    name: str


class Shown(BaseModel):
    shown: bool


class Paste(BaseModel):
    parent: str | None = None


class XSecInsert(BaseModel):
    index: int
    shape: int


class XSecShape(BaseModel):
    xsecSurfId: str
    index: int
    shape: int


class FilePath(BaseModel):
    path: str | None = None
    parent: str | None = None


class Pick(BaseModel):
    id: str


class Export(BaseModel):
    path: str
    fileType: int
    set: int = 0


LOCAL_HOSTS = {"localhost", "127.0.0.1", "[::1]"}

# Hosted copies of the web UI (e.g. GitHub Pages) allowed to drive this local engine.
# Only list sites you control: an allowed site can open and save files on this machine.
# Extra origins can be added with WEBVSP_WEB_ORIGINS (comma separated).
WEB_ORIGINS = ["https://mohammadadib.github.io"]


def web_origins() -> list[str]:
    extra = [o.strip().rstrip("/") for o in os.environ.get("WEBVSP_WEB_ORIGINS", "").split(",") if o.strip()]
    return WEB_ORIGINS + extra


def _hostname(hostport: str) -> str:
    if hostport.startswith("["):
        return hostport[: hostport.find("]") + 1]
    return hostport.rsplit(":", 1)[0] if ":" in hostport else hostport


def create_app(
    session: Session | None = None,
    token: str | None = None,
    port: int = 8765,
    extra_origins: list[str] | None = None,
    hosted_origins: list[str] | None = None,
) -> FastAPI:
    """Local-only app. Without a token, it still refuses requests whose Host isn't loopback
    (blocks DNS rebinding) or whose Origin isn't this server or a trusted hosted copy of the
    UI (blocks other websites). Hosted origins get CORS + Private Network Access headers."""
    app = FastAPI(title="WebVSP")
    app.state.session = session or Session()
    app.state.token = token
    sockets: set[WebSocket] = set()

    async def broadcast(msg: dict) -> None:
        for sock in list(sockets):
            try:
                await sock.send_json(msg)
            except Exception:
                sockets.discard(sock)

    mirror = Mirror(broadcast, lambda: bool(sockets))
    app.state.mirror = mirror
    app.state.has_clients = lambda: bool(sockets)

    @app.on_event("startup")
    async def _start_watchers() -> None:
        app.state.session.attach(asyncio.get_running_loop())
        mirror.start()
    hosted = set(web_origins() if hosted_origins is None else hosted_origins)
    allowed_origins = {f"http://{h}:{port}" for h in LOCAL_HOSTS} | set(extra_origins or []) | hosted

    def s() -> Session:
        return app.state.session

    def refused(headers, token_value: str | None) -> str | None:
        if _hostname(headers.get("host", "")) not in LOCAL_HOSTS:
            return "host not allowed"
        origin = headers.get("origin")
        if origin is not None and origin not in allowed_origins:
            return "origin not allowed"
        if app.state.token and not secrets.compare_digest(token_value or "", app.state.token):
            return "bad token"
        return None

    def cors_headers(origin: str | None) -> dict[str, str]:
        if origin not in hosted:
            return {}
        return {"Access-Control-Allow-Origin": origin, "Vary": "Origin"}

    @app.middleware("http")
    async def guard(request: Request, call_next):
        origin = request.headers.get("origin")
        if request.url.path.startswith("/api/"):
            if request.method == "OPTIONS":
                # CORS preflight from the hosted UI. Chrome also asks before a public site may
                # reach a local address (Private Network Access).
                if _hostname(request.headers.get("host", "")) not in LOCAL_HOSTS or origin not in hosted:
                    return JSONResponse({"detail": "origin not allowed"}, status_code=403)
                headers = cors_headers(origin)
                headers.update({
                    "Access-Control-Allow-Methods": "GET, POST, PUT, DELETE, OPTIONS",
                    "Access-Control-Allow-Headers": request.headers.get("access-control-request-headers", "*"),
                    "Access-Control-Max-Age": "600",
                })
                if request.headers.get("access-control-request-private-network") == "true":
                    headers["Access-Control-Allow-Private-Network"] = "true"
                return Response(status_code=204, headers=headers)
            why = refused(request.headers, request.headers.get("x-vsp-token"))
            if why:
                return JSONResponse({"detail": why}, status_code=403 if "allowed" in why else 401,
                                    headers=cors_headers(origin))
        elif _hostname(request.headers.get("host", "")) not in LOCAL_HOSTS:
            return JSONResponse({"detail": "host not allowed"}, status_code=403)
        response = await call_next(request)
        response.headers.update(cors_headers(origin))
        return response

    @app.exception_handler(VspError)
    async def vsp_error(_: Request, exc: VspError):
        return JSONResponse({"detail": str(exc), "errors": exc.messages}, status_code=400)

    # ------------------------------------------------------------- model/tree

    @app.get("/api/ping")
    async def ping():
        return {"app": "vspweb", "clients": len(sockets)}

    @app.get("/api/model")
    async def model():
        return await s().tree()

    @app.post("/api/pick")
    async def pick(body: Pick):
        await s().pick(body.id)
        return {"ok": True}

    @app.get("/api/geom-types")
    async def geom_types():
        return await s().geom_types()

    @app.get("/api/bbox")
    async def bbox():
        return await s().bbox()

    @app.get("/api/containers")
    async def containers():
        return await s().containers()

    @app.get("/api/containers/{cid}/parms")
    async def container_parms(cid: str):
        return await s().container_schema(cid)

    # ------------------------------------------------------------------ geoms

    @app.post("/api/geoms")
    async def add_geom(body: AddGeom):
        return {"id": await s().add_geom(body.type, body.parent)}

    @app.post("/api/geoms/delete")
    async def delete_geoms(body: IdList):
        await s().delete_geoms(body.ids)
        return {"ok": True}

    @app.post("/api/geoms/{gid}/cut")
    async def cut(gid: str):
        await s().cut_geom(gid)
        return {"ok": True}

    @app.post("/api/geoms/{gid}/copy")
    async def copy(gid: str):
        await s().copy_geom(gid)
        return {"ok": True}

    @app.post("/api/geoms/paste")
    async def paste(body: Paste):
        return {"ids": await s().paste_geom(body.parent)}

    @app.put("/api/geoms/{gid}/name")
    async def rename(gid: str, body: Rename):
        await s().rename_geom(gid, body.name)
        return {"ok": True}

    @app.put("/api/geoms/{gid}/shown")
    async def shown(gid: str, body: Shown):
        await s().set_shown(gid, body.shown)
        return {"ok": True}

    @app.get("/api/geoms/{gid}/parms")
    async def geom_parms(gid: str):
        return await s().geom_schema(gid)

    @app.get("/api/geoms/{gid}/values")
    async def geom_values(gid: str):
        return await s().geom_values(gid)

    @app.get("/api/geoms/{gid}/mesh")
    async def mesh(gid: str):
        return Response(await s().mesh(gid), media_type="application/octet-stream")

    @app.post("/api/geoms/{gid}/xsecs")
    async def insert_xsec(gid: str, body: XSecInsert):
        await s().insert_xsec(gid, body.index, body.shape)
        return {"ok": True}

    @app.delete("/api/geoms/{gid}/xsecs/{index}")
    async def cut_xsec(gid: str, index: int):
        await s().cut_xsec(gid, index)
        return {"ok": True}

    @app.put("/api/xsec-shape")
    async def xsec_shape(body: XSecShape):
        await s().change_xsec_shape(body.xsecSurfId, body.index, body.shape)
        return {"ok": True}

    # ------------------------------------------------------------------ parms

    @app.put("/api/parms/{pid}")
    async def set_parm(pid: str, body: ParmSet):
        return await s().set_parm(pid, body.value)

    @app.post("/api/parms/values")
    async def parm_values(body: ParmQuery):
        return await s().parm_values(body.ids)

    # ------------------------------------------------------------- undo/redo

    @app.post("/api/undo")
    async def undo():
        return {"label": await s().do_undo()}

    @app.post("/api/redo")
    async def redo():
        return {"label": await s().do_redo()}

    # ------------------------------------------------------------------ files

    @app.post("/api/file/new")
    async def new_file():
        await s().new_model()
        return {"ok": True}

    @app.post("/api/file/open")
    async def open_file(body: FilePath):
        if not body.path or not os.path.isfile(body.path):
            raise HTTPException(404, f"File not found: {body.path}")
        await s().open_file(body.path)
        return {"ok": True}

    @app.post("/api/file/insert")
    async def insert_file(body: FilePath):
        if not body.path or not os.path.isfile(body.path):
            raise HTTPException(404, f"File not found: {body.path}")
        await s().insert_file(body.path, body.parent)
        return {"ok": True}

    @app.post("/api/file/save")
    async def save_file(body: FilePath):
        return {"path": await s().save_file(body.path)}

    @app.post("/api/file/export")
    async def export_file(body: Export):
        return {"path": await s().export_file(body.path, body.fileType, body.set)}

    @app.get("/api/fs")
    async def list_dir(path: str | None = None):
        """Directory listing for the in-app file browser (local, single-user server)."""
        if not path:
            if os.name == "nt":
                drives = [f"{d}:\\" for d in string.ascii_uppercase if os.path.exists(f"{d}:\\")]
                return {"path": "", "parent": None, "home": str(Path.home()), "entries": [
                    {"name": d, "path": d, "dir": True} for d in drives
                ]}
            path = "/"
        p = Path(path).expanduser().resolve()
        if not p.is_dir():
            raise HTTPException(404, f"Not a directory: {path}")
        entries = []
        try:
            for child in p.iterdir():
                try:
                    is_dir = child.is_dir()
                except OSError:
                    continue
                if child.name.startswith("."):
                    continue
                entries.append({"name": child.name, "path": str(child), "dir": is_dir})
        except PermissionError:
            raise HTTPException(403, f"Permission denied: {path}")
        entries.sort(key=lambda e: (not e["dir"], e["name"].lower()))
        parent = str(p.parent) if p.parent != p else ""
        return {"path": str(p), "parent": parent, "home": str(Path.home()), "entries": entries}

    # -------------------------------------------------------------- websocket

    @app.websocket("/api/ws")
    async def ws(socket: WebSocket):
        if refused(socket.headers, socket.query_params.get("token")):
            await socket.close(code=4403)
            return
        await socket.accept()

        async def send(event: dict):
            await socket.send_json(event)

        s().subscribe(send)
        sockets.add(socket)
        try:
            await socket.send_json(mirror.snapshot())
            while True:
                msg = await socket.receive_json()
                if msg.get("type") == "mirror_event" and isinstance(msg.get("event"), dict):
                    await mirror.post(msg["event"])
        except WebSocketDisconnect:
            pass
        finally:
            sockets.discard(socket)
            s().unsubscribe(send)

    # ---------------------------------------------------------------- static

    if FRONTEND_DIST.is_dir():
        app.mount("/assets", StaticFiles(directory=FRONTEND_DIST / "assets"), name="assets")

        @app.get("/{rest:path}")
        async def spa(rest: str):
            f = FRONTEND_DIST / rest
            if rest and f.is_file() and FRONTEND_DIST in f.resolve().parents:
                return FileResponse(f, headers=NO_CACHE)
            # index.html must never be cached, or a rebuilt UI won't show up until a hard refresh;
            # the hashed files under /assets are safe to cache.
            return FileResponse(FRONTEND_DIST / "index.html", headers=NO_CACHE)

    return app
