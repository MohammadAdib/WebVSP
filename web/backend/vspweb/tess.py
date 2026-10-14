"""Tessellation of Geom surfaces into triangle meshes. Runs on the VSP worker thread.

Uses the same u/w tessellation the FLTK GUI draws (GetUWTess01), evaluated with
CompVecPnt01 / CompVecNorm01, for every surface including symmetric copies.

Binary mesh format (little-endian, all fields 4-byte aligned):
    char[4]  magic "VSPM"
    uint32   number of surfaces
    per surface:
        uint32  nu, nw            grid dimensions (vertex k = i*nw + j)
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


def geom_mesh_bytes(geom_id: str) -> bytes:
    surfaces = []
    for s in range(vsp.GetTotalNumSurfs(geom_id)):
        m = surface_mesh(geom_id, s)
        if m is not None:
            surfaces.append(m)
    parts = [b"VSPM", struct.pack("<I", len(surfaces))]
    for nu, nw, pts, nrm, idx in surfaces:
        parts.append(struct.pack("<III", nu, nw, len(idx)))
        parts.append(pts.tobytes())
        parts.append(nrm.tobytes())
        parts.append(idx.tobytes())
    return b"".join(parts)
