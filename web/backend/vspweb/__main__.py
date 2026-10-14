"""`vsp-web` entry point: start the local backend and open the GUI in a browser.

The GUI lives at a fixed URL (http://localhost:8765 by default). Running `vsp-web` while a
server is already up just opens the browser (and hands it the file, if one was given).

Prints one machine-readable line once listening, for wrappers (e.g. a Tauri shell):
    VSPWEB_READY <port> <token or ->
"""

from __future__ import annotations

import argparse
import json
import os
import secrets
import sys
import threading
import urllib.request
import webbrowser

DEFAULT_PORT = 8765


def _running_server(port: int) -> bool:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/api/ping", timeout=1) as r:
            return json.load(r).get("app") == "vspweb"
    except Exception:
        return False


def _open_in_running(port: int, path: str) -> None:
    origin = f"http://localhost:{port}"
    req = urllib.request.Request(
        f"{origin}/api/file/open",
        method="POST",
        data=json.dumps({"path": path}).encode(),
        headers={"content-type": "application/json", "origin": origin},
    )
    urllib.request.urlopen(req, timeout=60).close()


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="vsp-web", description="TitanVSP web GUI")
    ap.add_argument("file", nargs="?", help=".vsp3 file to open")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"port (default {DEFAULT_PORT})")
    ap.add_argument("--dev", action="store_true", help="dev mode: no browser, allow the Vite dev server origin")
    ap.add_argument("--no-browser", action="store_true", help="don't open a browser")
    ap.add_argument("--token", action="store_true", help="also require a random session token (for desktop shells)")
    args = ap.parse_args(argv)

    port = args.port
    url = f"http://localhost:{port}/"
    path = os.path.abspath(args.file) if args.file else None

    if _running_server(port):
        if path:
            _open_in_running(port, path)
        print(f"TitanVSP already running: {url}", flush=True)
        if not args.no_browser:
            webbrowser.open(url)
        return

    import uvicorn

    from .app import create_app
    from .session import Session

    token = secrets.token_urlsafe(24) if args.token else None
    session = Session()
    if path:
        session.worker.call_sync(_open, path)
        session.worker.call_sync(session._refresh_fingerprints)

    extra_origins = ["http://localhost:5173", "http://127.0.0.1:5173"] if args.dev else []
    app = create_app(session, token, port=port, extra_origins=extra_origins)
    open_url = url + (f"#token={token}" if token else "")

    @app.on_event("startup")
    async def _ready():
        print(f"VSPWEB_READY {port} {token or '-'}", flush=True)
        print(f"TitanVSP: {url}", flush=True)
        if not args.dev and not args.no_browser:
            threading.Timer(0.3, webbrowser.open, args=(open_url,)).start()

    uvicorn.run(app, host="127.0.0.1", port=port, log_level="warning")


def _open(path: str) -> None:
    from .runtime import vsp

    vsp.ReadVSPFile(path)
    vsp.Update()
    from .session import resync_gui

    resync_gui()


if __name__ == "__main__":
    main(sys.argv[1:])
