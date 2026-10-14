"""The OpenVSP thread.

OpenVSP is a global singleton and its FLTK GUI is bound to the thread that created it, so
one dedicated thread imports `openvsp`, owns the GUI and runs every API call. With the GUI
build it also drives the web mirror: between jobs it pumps the FLTK event loop.

Import `vsp` from here (not `openvsp` directly) so the module is loaded on that thread:

    from .runtime import vsp
"""

from __future__ import annotations

import os
import queue
import threading
import time
from concurrent.futures import Future
from typing import Any, Callable

_jobs: "queue.Queue[tuple[Callable[[], Any], Future]]" = queue.Queue()
_periodic: list[tuple[float, Callable[[], None]]] = []
_ready = threading.Event()
_error: BaseException | None = None

vsp: Any = None
gui = False


def _want_gui() -> bool:
    return os.environ.get("VSPWEB_GUI", "1") not in ("0", "false", "no")


def _load():
    global vsp, gui
    want_gui = _want_gui()
    try:
        import openvsp_config

        openvsp_config.LOAD_GRAPHICS = want_gui
        # Skip openvsp's optional helper modules (degen_geom, utilities): they pull in
        # matplotlib/scipy/pandas, which the engine never uses.
        openvsp_config._IGNORE_IMPORTS = True
    except ImportError:
        want_gui = False
    import openvsp

    vsp = openvsp
    gui = want_gui and hasattr(openvsp, "MirrorEnable")
    if gui:
        openvsp.MirrorEnable()


def _run_job(fn: Callable[[], Any], fut: Future) -> None:
    if not fut.set_running_or_notify_cancel():
        return
    try:
        fut.set_result(fn())
    except BaseException as exc:  # noqa: BLE001 - handed to the caller
        fut.set_exception(exc)


def _main() -> None:
    global _error
    try:
        _load()
    except BaseException as exc:  # noqa: BLE001
        _error = exc
        _ready.set()
        return
    _ready.set()

    last_run: dict[int, float] = {}
    while True:
        if gui:
            # Run whatever is queued, then let FLTK process timers/events and the mirror diff.
            while True:
                try:
                    fn, fut = _jobs.get_nowait()
                except queue.Empty:
                    break
                _run_job(fn, fut)
            vsp.MirrorPump(0.005)
        else:
            try:
                fn, fut = _jobs.get(timeout=0.1)
                _run_job(fn, fut)
            except queue.Empty:
                pass

        now = time.monotonic()
        for i, (period, cb) in enumerate(list(_periodic)):
            if now - last_run.get(i, 0.0) >= period:
                last_run[i] = now
                try:
                    cb()
                except Exception as exc:  # noqa: BLE001 - keep the loop alive
                    print(f"vspweb: periodic task failed: {exc!r}", flush=True)


def submit(fn: Callable[[], Any]) -> Future:
    """Run fn on the OpenVSP thread; returns a concurrent Future."""
    fut: Future = Future()
    if threading.current_thread() is _thread:
        _run_job(fn, fut)  # already on the VSP thread (e.g. from a periodic task)
    else:
        _jobs.put((fn, fut))
    return fut


def every(seconds: float, cb: Callable[[], None]) -> None:
    """Call cb on the OpenVSP thread about every `seconds`."""
    _periodic.append((seconds, cb))


_thread = threading.Thread(target=_main, name="openvsp", daemon=True)
_thread.start()
_ready.wait()
if _error is not None:
    raise _error
