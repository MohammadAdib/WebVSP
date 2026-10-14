import time

import pytest
from fastapi.testclient import TestClient

from vspweb import runtime
from vspweb.app import create_app
from vspweb.session import Session

BASE = "http://localhost:8765"

pytestmark = pytest.mark.skipif(not runtime.gui, reason="OpenVSP built without the GUI")


@pytest.fixture(scope="module")
def session():
    return Session()


class MirrorClient:
    """Applies mirror messages from a test websocket, like the frontend store does."""

    def __init__(self, ws):
        self.ws = ws
        self.windows: dict[int, dict] = {}
        self.widgets: dict[int, dict] = {}
        self.dialogs: list[dict] = []
        snap = self._recv_type("mirror_snapshot")
        for w in snap["windows"]:
            self.windows[w["id"]] = w
        for w in snap["widgets"]:
            self.widgets[w["id"]] = w

    def _recv_type(self, t):
        while True:
            msg = self.ws.receive_json()
            if msg["type"] == t:
                return msg
            self._apply(msg)

    def _apply(self, msg):
        if msg["type"] != "mirror":
            return
        for m in msg["msgs"]:
            if m["t"] == "dialog":
                self.dialogs.append(m)
                continue
            if m.get("full"):
                self.windows.clear()
                self.widgets.clear()
            for wid in m["closed"]:
                self.windows.pop(wid, None)
            for w in m["windows"]:
                self.windows[w["id"]] = w
            for w in m["widgets"]:
                self.widgets[w["id"]] = w
            for wid in m["removed"]:
                self.widgets.pop(wid, None)

    def wait_for(self, pred, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            result = pred()
            if result:
                return result
            self._apply(self.ws.receive_json())
        raise AssertionError("condition not met")

    def send(self, **event):
        self.ws.send_json({"type": "mirror_event", "event": event})


def test_mirror_end_to_end(session):
    with TestClient(create_app(session), base_url=BASE) as client:
        client.post("/api/file/new")
        with client.websocket_connect("/api/ws", headers={"host": "localhost:8765"}) as ws:
            m = MirrorClient(ws)
            roles = {w["role"] for w in m.wait_for(lambda: m.windows and list(m.windows.values()))}
            assert {"main", "browser"} <= roles

            gid = client.post("/api/geoms", json={"type": "POD"}).json()["id"]
            client.post("/api/pick", json={"id": gid})
            geom_win = m.wait_for(lambda: next((w for w in m.windows.values() if w["role"] == "geom"), None))
            assert geom_win["screen"] == "Pod"

            def length_input():
                return next(
                    (w for w in m.widgets.values()
                     if w["win"] == geom_win["id"] and w.get("parm") == "Design:Length" and w["k"] == "input"),
                    None,
                )

            inp = m.wait_for(length_input)
            m.send(op="set", id=inp["id"], v="7.25")
            m.wait_for(lambda: "7.25" in m.widgets[inp["id"]]["v"])
            vals = client.get(f"/api/geoms/{gid}/values").json()["values"]
            assert 7.25 in [round(v, 4) for v in vals.values()]

            # File > New asks to discard; answering "Discard" clears the model.
            bar = next(w for w in m.widgets.values() if w["k"] == "menubar")
            new_item = next(i for i in bar["items"] if i["label"] == "New")
            m.send(op="pick", id=bar["id"], i=new_item["i"])
            dlg = m.wait_for(lambda: next((d for d in m.dialogs if d["kind"] == "choice"), None))
            assert dlg["b1"] == "Discard"
            m.send(op="dialog", seq=dlg["seq"], result=1)
            m.wait_for(lambda: client.get("/api/model").json()["geoms"] == [])
