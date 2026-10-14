"""Parm schema extraction. All functions here run on the VSP worker thread."""

from __future__ import annotations

import math

from .runtime import vsp

from . import choices

PARM_TYPE_NAMES = {
    vsp.PARM_DOUBLE_TYPE: "double",
    vsp.PARM_INT_TYPE: "int",
    vsp.PARM_BOOL_TYPE: "bool",
    vsp.PARM_FRACTION_TYPE: "fraction",
    vsp.PARM_LIMITED_INT_TYPE: "int",
    vsp.PARM_NOTEQ_TYPE: "double",
    vsp.PARM_POWER_INT_TYPE: "int",
}


def is_active(pid: str) -> bool:
    """False for Parms OpenVSP drives itself (the GUI greys these out).

    GetParmActiveFlag is added to the API on this branch; older builds report everything active.
    """
    fn = getattr(vsp, "GetParmActiveFlag", None)
    return True if fn is None else bool(fn(pid))


def _finite(x: float) -> float | None:
    return x if math.isfinite(x) and abs(x) < 1e11 else None


def parm_info(pid: str) -> dict:
    group = vsp.GetParmGroupName(pid)
    name = vsp.GetParmName(pid)
    info = {
        "id": pid,
        "name": name,
        "group": group,
        "displayGroup": vsp.GetParmDisplayGroupName(pid),
        "descript": vsp.GetParmDescript(pid),
        "type": PARM_TYPE_NAMES.get(vsp.GetParmType(pid), "double"),
        "min": _finite(vsp.GetParmLowerLimit(pid)),
        "max": _finite(vsp.GetParmUpperLimit(pid)),
        "value": vsp.GetParmVal(pid),
        "active": is_active(pid),
    }
    choice = choices.lookup(group, name)
    if choice:
        info["choice"] = choice
    return info


def xsec_parm_ids(geom_id: str) -> list[tuple[str, int, int, str]]:
    """(xsec_surf_index, xsec_index, xsec_id, parm_id) for every XSec parm of a geom."""
    out = []
    for si, surf_id in enumerate(vsp.GetXSecSurfIDVec(geom_id)):
        for xi in range(vsp.GetNumXSec(surf_id)):
            xsec_id = vsp.GetXSec(surf_id, xi)
            for pid in vsp.GetXSecParmIDs(xsec_id):
                out.append((si, xi, xsec_id, pid))
    return out


def geom_schema(geom_id: str) -> dict:
    """Geom-level parms grouped by display group; XSec parms grouped per section, then by group."""
    owner = {pid: (si, xi, xsec_id) for si, xi, xsec_id, pid in xsec_parm_ids(geom_id)}
    ids = list(vsp.GetGeomParmIDs(geom_id))
    known = set(ids)
    ids += [pid for pid in owner if pid not in known]

    groups: dict[str, list[dict]] = {}
    xsecs: dict[str, dict] = {}
    for pid in ids:
        info = parm_info(pid)
        if pid in owner:
            si, xi, xsec_id = owner[pid]
            sec = xsecs.get(xsec_id)
            if sec is None:
                sec = xsecs[xsec_id] = {
                    "id": xsec_id,
                    "surf": si,
                    "surfId": vsp.GetXSecSurfIDVec(geom_id)[si],
                    "index": xi,
                    "shape": vsp.GetXSecShape(xsec_id),
                    "groups": {},
                }
            sec["groups"].setdefault(info["group"], []).append(info)
        else:
            groups.setdefault(info["displayGroup"] or info["group"], []).append(info)

    sections = sorted(xsecs.values(), key=lambda x: (x["surf"], x["index"]))
    for sec in sections:
        sec["groups"] = [{"name": k, "parms": v} for k, v in sec["groups"].items()]
    return {
        "geomId": geom_id,
        "groups": [{"name": k, "parms": v} for k, v in groups.items()],
        "xsecs": sections,
    }


def container_schema(container_id: str) -> dict:
    groups: dict[str, list[dict]] = {}
    for pid in vsp.FindContainerParmIDs(container_id):
        info = parm_info(pid)
        groups.setdefault(info["displayGroup"] or info["group"], []).append(info)
    return {
        "containerId": container_id,
        "name": vsp.GetContainerName(container_id),
        "groups": [{"name": k, "parms": v} for k, v in groups.items()],
    }


def all_parm_ids(geom_id: str) -> list[str]:
    ids = list(vsp.GetGeomParmIDs(geom_id))
    seen = set(ids)
    for _, _, _, pid in xsec_parm_ids(geom_id):
        if pid not in seen:
            ids.append(pid)
            seen.add(pid)
    return ids


def values(pids: list[str]) -> dict:
    """Current values, plus which Parms are inactive (activity changes with other Parms)."""
    return {
        "values": {pid: vsp.GetParmVal(pid) for pid in pids},
        "inactive": [pid for pid in pids if not is_active(pid)],
    }
