import struct

import numpy as np
import pytest
from fastapi.testclient import TestClient

from vspweb.app import create_app
from vspweb.session import Session

BASE = "http://localhost:8765"


@pytest.fixture(scope="module")
def session():
    return Session()


@pytest.fixture()
def client(session):
    with TestClient(create_app(session), base_url=BASE) as c:
        c.post("/api/file/new")
        yield c


def parse_mesh(data: bytes):
    assert data[:4] == b"VSPM"
    (n,) = struct.unpack_from("<I", data, 4)
    off = 8
    surfs = []
    for _ in range(n):
        nu, nw, nidx = struct.unpack_from("<III", data, off)
        off += 12
        nv = nu * nw
        pos = np.frombuffer(data, np.float32, nv * 3, off).reshape(-1, 3)
        off += nv * 12
        nrm = np.frombuffer(data, np.float32, nv * 3, off).reshape(-1, 3)
        off += nv * 12
        idx = np.frombuffer(data, np.uint32, nidx, off).reshape(-1, 3)
        off += nidx * 4
        surfs.append((pos, nrm, idx))
    assert off == len(data)
    return surfs


def add(client, type_):
    r = client.post("/api/geoms", json={"type": type_})
    assert r.status_code == 200, r.text
    return r.json()["id"]


def test_tree_add_delete(client):
    gid = add(client, "POD")
    tree = client.get("/api/model").json()
    assert [g["id"] for g in tree["geoms"]] == [gid]
    assert tree["geoms"][0]["type"] == "Pod"
    client.post("/api/geoms/delete", json={"ids": [gid]})
    assert client.get("/api/model").json()["geoms"] == []


def test_schema_every_type(client):
    parent = add(client, "WING")
    for t in client.get("/api/geom-types").json():
        r = client.post("/api/geoms", json={"type": t})
        if r.status_code != 200:  # e.g. Conformal/Auxiliary need a parent
            r = client.post("/api/geoms", json={"type": t, "parent": parent})
        assert r.status_code == 200, (t, r.text)
        gid = r.json()["id"]
        schema = client.get(f"/api/geoms/{gid}/parms").json()
        assert schema["groups"], t


def test_symmetric_wing_mesh(client):
    gid = add(client, "WING")
    surfs = parse_mesh(client.get(f"/api/geoms/{gid}/mesh").content)
    assert len(surfs) == 2
    ys = [pos[:, 1].mean() for pos, _, _ in surfs]
    assert ys[0] * ys[1] < 0  # mirrored about XZ
    for pos, nrm, idx in surfs:
        p0, p1, p2 = pos[idx[:, 0]], pos[idx[:, 1]], pos[idx[:, 2]]
        fn = np.cross(p1 - p0, p2 - p0)
        vn = nrm[idx].sum(axis=1)
        assert np.einsum("ij,ij->i", fn, vn).sum() > 0


def test_parm_change_updates_mesh_and_undo(client):
    gid = add(client, "POD")
    schema = client.get(f"/api/geoms/{gid}/parms").json()
    length = next(p for g in schema["groups"] for p in g["parms"] if p["name"] == "Length")
    before = parse_mesh(client.get(f"/api/geoms/{gid}/mesh").content)
    # The change event may come from this request or from the background watcher, whichever
    # notices first, so check the result (the mesh) rather than which event carried it.
    client.put(f"/api/parms/{length['id']}", json={"value": length["value"] * 2})
    after = parse_mesh(client.get(f"/api/geoms/{gid}/mesh").content)
    span = lambda s: np.ptp(s[0][0][:, 0])
    assert span(after) == pytest.approx(2 * span(before), rel=1e-3)

    client.post("/api/undo")
    vals = client.get(f"/api/geoms/{gid}/values").json()["values"]
    assert vals[length["id"]] == pytest.approx(length["value"])
    client.post("/api/redo")
    vals = client.get(f"/api/geoms/{gid}/values").json()["values"]
    assert vals[length["id"]] == pytest.approx(length["value"] * 2)


def test_structural_undo(client):
    gid = add(client, "FUSELAGE")
    client.post("/api/geoms/delete", json={"ids": [gid]})
    assert client.get("/api/model").json()["geoms"] == []
    client.post("/api/undo")
    assert [g["id"] for g in client.get("/api/model").json()["geoms"]] == [gid]


def test_save_open_roundtrip(client, tmp_path):
    gid = add(client, "WING")
    client.put(f"/api/geoms/{gid}/name", json={"name": "MainWing"})
    path = str(tmp_path / "m.vsp3")
    client.post("/api/file/save", json={"path": path})
    client.post("/api/file/new")
    client.post("/api/file/open", json={"path": path})
    geoms = client.get("/api/model").json()["geoms"]
    assert [g["name"] for g in geoms] == ["MainWing"]


def test_vsp_error_is_400(client):
    r = client.put("/api/parms/NOT_A_PARM", json={"value": 1})
    assert r.status_code == 400


def test_token_required(session):
    with TestClient(create_app(session, token="secret"), base_url=BASE) as c:
        assert c.get("/api/model").status_code == 401
        assert c.get("/api/model", headers={"x-vsp-token": "secret"}).status_code == 200


def test_foreign_origin_and_host_refused(session):
    with TestClient(create_app(session), base_url=BASE) as c:
        assert c.get("/api/model", headers={"origin": BASE}).status_code == 200
        assert c.post("/api/file/new", headers={"origin": "https://evil.example"}).status_code == 403
    with TestClient(create_app(session), base_url="http://evil.example") as c:
        assert c.get("/api/model").status_code == 403


def test_xform_abs_parms_inactive_when_relative(client):
    import openvsp as vsp
    if not hasattr(vsp, "GetParmActiveFlag"):
        pytest.skip("OpenVSP build without GetParmActiveFlag")
    gid = add(client, "POD")
    schema = client.get(f"/api/geoms/{gid}/parms").json()
    xform = {p["name"]: p for g in schema["groups"] if g["name"] == "XForm" for p in g["parms"]}
    assert xform["Abs_Or_Relitive_flag"]["value"] == 1  # REL by default
    assert xform["X_Rel_Location"]["active"]
    inactive = set(client.get(f"/api/geoms/{gid}/values").json()["inactive"])
    assert xform["X_Rel_Location"]["id"] not in inactive


def test_structural_edit_keeps_file_name(client, tmp_path):
    path = str(tmp_path / "keep.vsp3")
    client.post("/api/file/save", json={"path": path})
    add(client, "POD")  # structural edit writes an undo snapshot to a temp file
    assert client.get("/api/model").json()["fileName"].endswith("keep.vsp3")

