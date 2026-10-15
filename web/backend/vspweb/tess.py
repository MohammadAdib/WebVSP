"""Tessellation of Geom surfaces into triangle meshes. Runs on the VSP worker thread.

Uses the same u/w tessellation the FLTK GUI draws (GetUWTess01), evaluated with
CompVecPnt01 / CompVecNorm01, for every surface including symmetric copies.

Binary mesh format (little-endian, all fields 4-byte aligned):
    char[4]  magic "VSPM"
    uint32   number of surfaces
    per surface:
        uint32  nu, nw            grid dimensions (vertex k = i*nw + j); nw = 0 means a plain
                                  triangle list of nu vertices (Mesh Geoms have no u/w grid)
        uint32  nidx              number of triangle indices
        float32 positions[nu*nw*3]
        float32 normals[nu*nw*3]
        uint32  indices[nidx]
"""

from __future__ import annotations

import struct

import numpy as np
from .runtime import vsp


def _vec3_array(vecs) -> np.ndarray:
    return np.array([(v.x(), v.y(), v.z()) for v in vecs], dtype=np.float64).reshape(-1, 3)


def _grid_indices(nu: int, nw: int) -> np.ndarray:
    i, j = np.meshgrid(np.arange(nu - 1), np.arange(nw - 1), indexing="ij")
    a = (i * nw + j).ravel()
    b = a + nw
    c = b + 1
    d = a + 1
    return np.stack([a, b, c, a, c, d], axis=1).reshape(-1, 3)


def surface_mesh(geom_id: str, surf: int):
    u, w = vsp.GetUWTess01(geom_id, surf)
    u = np.asarray(u, dtype=np.float64)
    w = np.asarray(w, dtype=np.float64)
    nu, nw = len(u), len(w)
    if nu < 2 or nw < 2:
        return None
    uu, ww = np.meshgrid(u, w, indexing="ij")
    us, ws = uu.ravel().tolist(), ww.ravel().tolist()
    pts = _vec3_array(vsp.CompVecPnt01(geom_id, surf, us, ws))
    nrm = _vec3_array(vsp.CompVecNorm01(geom_id, surf, us, ws))
    tris = _grid_indices(nu, nw)

    # Orient triangle winding to agree with the surface normals (symmetric copies are mirrored).
    p0, p1, p2 = pts[tris[:, 0]], pts[tris[:, 1]], pts[tris[:, 2]]
    face_n = np.cross(p1 - p0, p2 - p0)
    vert_n = nrm[tris[:, 0]] + nrm[tris[:, 1]] + nrm[tris[:, 2]]
    if np.sum(np.einsum("ij,ij->i", face_n, vert_n)) < 0:
        tris = tris[:, [0, 2, 1]]

    return nu, nw, pts.astype(np.float32), nrm.astype(np.float32), tris.astype(np.uint32).ravel()


def triangle_mesh(geom_id: str):
    """A Mesh Geom's triangles as drawn, with flat (per-triangle) normals."""
    flat = vsp.GetMeshGeomTriPnts(geom_id)
    if len(flat) < 9:
        return None
    pts = np.asarray(flat, dtype=np.float64).reshape(-1, 3)
    p0, p1, p2 = pts[0::3], pts[1::3], pts[2::3]
    n = np.cross(p1 - p0, p2 - p0)
    length = np.linalg.norm(n, axis=1, keepdims=True)
    n = np.divide(n, length, out=np.zeros_like(n), where=length > 0)
    nrm = np.repeat(n, 3, axis=0)
    idx = np.arange(len(pts), dtype=np.uint32)
    return len(pts), 0, pts.astype(np.float32), nrm.astype(np.float32), idx


def geom_mesh_bytes(geom_id: str) -> bytes:
    surfaces = []
    for s in range(vsp.GetTotalNumSurfs(geom_id)):
        m = surface_mesh(geom_id, s)
        if m is not None:
            surfaces.append(m)
    if not surfaces and hasattr(vsp, "GetMeshGeomTriPnts") and vsp.GetGeomTypeName(geom_id) in ("Mesh", "NGonMesh"):
        m = triangle_mesh(geom_id)
        if m is not None:
            surfaces.append(m)
    parts = [b"VSPM", struct.pack("<I", len(surfaces))]
    for nu, nw, pts, nrm, idx in surfaces:
        parts.append(struct.pack("<III", nu, nw, len(idx)))
        parts.append(pts.tobytes())
        parts.append(nrm.tobytes())
        parts.append(idx.tobytes())
    # Then the outline (feature lines), as segments: count of points, then x,y,z floats.
    feat = []
    if surfaces and hasattr(vsp, "MirrorFeatureLines"):
        feat = [c for p in vsp.MirrorFeatureLines(geom_id) for c in (p.x(), p.y(), p.z())]
    parts.append(struct.pack("<I", len(feat) // 3))
    parts.append(np.asarray(feat, dtype="<f4").tobytes())
    return b"".join(parts)
