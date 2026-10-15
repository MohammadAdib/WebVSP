"""More OpenVSP windows: each is a separate engine process.

OpenVSP keeps one global model and one GUI per process, so a second, independent model
needs a second engine. The engine the user started launches them on free ports; each
serves its own browser tab and exits by itself once its tab has been closed for a while
(see `--exit-when-idle` in __main__).
"""

from __future__ import annotations

import asyncio
import json
import os
import socket
import subprocess
import sys
import urllib.request

# How long a new engine may take to load OpenVSP and start serving.
START_TIMEOUT_S = 120
# How long an extra engine keeps running with no tab connected (covers reloads).
IDLE_EXIT_S = 60


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _ready(port: int) -> bool:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/api/ping", timeout=1) as r:
            return json.load(r).get("app") == "vspweb"
    except Exception:  # noqa: BLE001 - not up yet
        return False


class Instances:
    def __init__(self) -> None:
        self._procs: dict[int, subprocess.Popen] = {}

    async def spawn(self) -> int:
        """Start another engine and return its port once it answers."""
        port = _free_port()
        cmd = [sys.executable, "-m", "vspweb", "--port", str(port), "--no-browser",
               "--exit-when-idle", str(IDLE_EXIT_S)]
        # Same console as this engine: closing its window stops them all.
        proc = subprocess.Popen(cmd, cwd=os.getcwd(), stdout=subprocess.DEVNULL)
        self._procs[port] = proc
        loop = asyncio.get_running_loop()
        deadline = loop.time() + START_TIMEOUT_S
        while loop.time() < deadline:
            if proc.poll() is not None:
                self._procs.pop(port, None)
                raise RuntimeError(f"the new engine exited during startup (code {proc.returncode})")
            if await asyncio.to_thread(_ready, port):
                print(f"WebVSP: new window engine at http://localhost:{port}/", flush=True)
                return port
            await asyncio.sleep(0.25)
        proc.kill()
        self._procs.pop(port, None)
        raise RuntimeError("the new engine did not start in time")

    def running(self) -> list[int]:
        for port in [p for p, proc in self._procs.items() if proc.poll() is not None]:
            del self._procs[port]
        return sorted(self._procs)
