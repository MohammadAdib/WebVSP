"""Calls into OpenVSP, always on the OpenVSP thread (see runtime.py), with API errors
turned into exceptions."""

from __future__ import annotations

import asyncio
import functools
from typing import Any, Callable, TypeVar

from . import runtime
from .runtime import vsp

T = TypeVar("T")


class VspError(RuntimeError):
    def __init__(self, messages: list[str]):
        super().__init__("; ".join(messages))
        self.messages = messages


def _error_mgr():
    return vsp.ErrorMgrSingleton.getInstance()


def _drain_errors() -> list[str]:
    mgr = _error_mgr()
    msgs = []
    while mgr.GetNumTotalErrors() > 0:
        msgs.append(mgr.PopLastError().GetErrorString())
    msgs.reverse()
    return msgs


def _call(fn: Callable[..., T], *args: Any, **kwargs: Any) -> T:
    _drain_errors()
    result = fn(*args, **kwargs)
    errors = _drain_errors()
    if errors:
        raise VspError(errors)
    return result


class VspWorker:
    def __init__(self) -> None:
        self.call_sync(self._init)

    @staticmethod
    def _init() -> None:
        _error_mgr().SilenceErrors()
        vsp.VSPCheckSetup()
        _drain_errors()

    def call_sync(self, fn: Callable[..., T], *args: Any, **kwargs: Any) -> T:
        """Run ``fn`` on the VSP thread and block for the result (for tests/CLI)."""
        return runtime.submit(functools.partial(_call, fn, *args, **kwargs)).result()

    async def call(self, fn: Callable[..., T], *args: Any, **kwargs: Any) -> T:
        return await asyncio.wrap_future(runtime.submit(functools.partial(_call, fn, *args, **kwargs)))

    def shutdown(self) -> None:
        pass
