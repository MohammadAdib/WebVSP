"""Labels for integer Parms that are really enums or bit flags.

Keyed by (group, name). Values mirror the enums in src/geom_api/APIDefines.h.
"choice" -> mutually exclusive options; "flags" -> bitmask of independent options.
"""

from __future__ import annotations

_ATTACH = ["None", "Comp", "UV", "RST", "LMN", "EtaMN"]

CHOICES: dict[tuple[str, str], dict] = {
    ("XForm", "Abs_Or_Relitive_flag"): {"kind": "choice", "options": [(0, "Absolute"), (1, "Relative")]},
    ("Attach", "Trans_Attach_Flag"): {"kind": "choice", "options": list(enumerate(_ATTACH))},
    ("Attach", "Rots_Attach_Flag"): {"kind": "choice", "options": list(enumerate(_ATTACH))},
    ("Sym", "Sym_Planar_Flag"): {"kind": "flags", "options": [(1, "XY"), (2, "XZ"), (4, "YZ")]},
    ("Sym", "Sym_Axial_Flag"): {
        "kind": "choice",
        "options": [(0, "None"), (8, "X axis"), (16, "Y axis"), (32, "Z axis")],
    },
    ("Sym", "Flip_Flag"): {"kind": "flags", "options": [(1, "XY"), (2, "XZ"), (4, "YZ")]},
}


def lookup(group: str, name: str) -> dict | None:
    entry = CHOICES.get((group, name))
    if entry is None:
        return None
    return {"kind": entry["kind"], "options": [{"value": v, "label": l} for v, l in entry["options"]]}
