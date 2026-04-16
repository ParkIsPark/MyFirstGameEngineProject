#!/usr/bin/env python3
"""
convert_tilemap.py — Convert legacy single-char .tilemap files to the new
@HEADER / @SYMBOLS / @DEFS / @INSTANCES block format.

Usage:
    python convert_tilemap.py input.tilemap [output.tilemap]

If no output path is given, the result is written to <input>_new.tilemap
alongside the source file.

Single-char symbol names are remapped to safe ASCII identifiers (e.g. '#' ->
'hash', '.' -> 'dot').  The resulting file is valid input for FTilemapParser.
"""

from __future__ import annotations
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Data model (mirrors C++ structs)
# ---------------------------------------------------------------------------

@dataclass
class Geometry:
    half:   Optional[tuple[float, float, float]] = None
    radius: Optional[float] = None


@dataclass
class Material:
    kd:        Optional[tuple[float, float, float]] = None
    ka:        Optional[tuple[float, float, float]] = None
    ks:        Optional[tuple[float, float, float]] = None
    km:        Optional[tuple[float, float, float]] = None
    shininess: Optional[float] = None
    tex:       Optional[str] = None


@dataclass
class Physics:
    has_physics: bool = False
    bStatic:     bool = False
    mass:        Optional[float] = None
    restitution: Optional[float] = None
    friction:    Optional[float] = None
    no_gravity:  bool = False


@dataclass
class SymbolDef:
    sym:      str            # original single-char key
    type_name: str           # CUBE | SPHERE | PLAYER | EMPTY
    geometry: Geometry = field(default_factory=Geometry)
    material: Material = field(default_factory=Material)
    physics:  Physics  = field(default_factory=Physics)


@dataclass
class GridLayer:
    layer_idx: int
    rows: list[str] = field(default_factory=list)  # compact char-row strings


@dataclass
class TilemapDoc:
    scale:  float = 2.0
    origin: tuple[float, float, float] = (0.0, 0.0, 0.0)
    # Insertion-ordered dict (Python 3.7+) preserves declaration order
    defs:   dict[str, SymbolDef] = field(default_factory=dict)
    layers: dict[int, GridLayer]  = field(default_factory=dict)


# ---------------------------------------------------------------------------
# Legacy parser
# ---------------------------------------------------------------------------

def _strip_comment_legacy(line: str) -> str:
    """Remove '//' comments only.
    '#' is NOT stripped here — it is a valid symbol character in legacy format.
    """
    idx = line.find("//")
    if idx != -1:
        line = line[:idx]
    return line.strip()


def _parse_comma_vec3(s: str) -> tuple[float, float, float]:
    parts = [p.strip() for p in s.split(",")]
    if len(parts) != 3:
        raise ValueError(f"Expected 3 comma-separated floats, got: {s!r}")
    return (float(parts[0]), float(parts[1]), float(parts[2]))


def _get_kv(token: str, key: str) -> Optional[str]:
    prefix = key + "="
    if token.startswith(prefix):
        return token[len(prefix):]
    return None


def parse_legacy(text: str) -> TilemapDoc:
    doc = TilemapDoc()
    current_layer: Optional[int] = None
    in_layer = False

    for raw_line in text.splitlines():
        # Preserve the raw line for layer rows; strip comments for everything else
        stripped = _strip_comment_legacy(raw_line)
        if not stripped:
            continue

        tokens = stripped.split()
        if not tokens:
            continue

        cmd = tokens[0].upper()

        if cmd == "SCALE" and len(tokens) >= 2:
            doc.scale = float(tokens[1])
            in_layer = False

        elif cmd == "ORIGIN" and len(tokens) >= 4:
            doc.origin = (float(tokens[1]), float(tokens[2]), float(tokens[3]))
            in_layer = False

        elif cmd == "DEFINE" and len(tokens) >= 3:
            in_layer = False
            # tokens[1] is the symbol char, tokens[2] is the type
            _parse_legacy_def(tokens[1:], doc)

        elif cmd == "LAYER" and len(tokens) >= 2:
            current_layer = int(tokens[1])
            in_layer = True
            if current_layer not in doc.layers:
                doc.layers[current_layer] = GridLayer(layer_idx=current_layer)

        elif in_layer and current_layer is not None:
            # Grid row — use the raw (non-comment-stripped) line so that '#' is
            # preserved as a symbol character.  Only remove trailing CR/LF.
            row = raw_line.rstrip("\r\n")
            if row.strip():
                doc.layers[current_layer].rows.append(row)

    return doc


def _parse_legacy_def(rest_tokens: list[str], doc: TilemapDoc) -> None:
    """Parse: <char_sym> <TYPE> [key=value ...] and store into doc.defs."""
    if len(rest_tokens) < 2 or len(rest_tokens[0]) != 1:
        return

    char_sym  = rest_tokens[0]
    type_name = rest_tokens[1].upper()
    if type_name not in ("CUBE", "SPHERE", "PLAYER", "EMPTY"):
        return

    sd = SymbolDef(sym=char_sym, type_name=type_name)

    for tok in rest_tokens[2:]:
        tok = tok.strip()
        if not tok:
            continue

        if tok == "physics":
            sd.physics.has_physics = True
            continue
        if tok == "nogravity":
            sd.physics.no_gravity  = True
            sd.physics.has_physics = True
            continue

        for key in ("kd", "ka", "ks", "km"):
            v = _get_kv(tok, key)
            if v is not None:
                setattr(sd.material, key, _parse_comma_vec3(v))
                break
        else:
            v = _get_kv(tok, "half")
            if v is not None:
                sd.geometry.half = _parse_comma_vec3(v)
                continue

            v = _get_kv(tok, "r")
            if v is not None:
                sd.geometry.radius = float(v)
                continue

            v = _get_kv(tok, "shine")
            if v is not None:
                sd.material.shininess = float(v)
                continue

            v = _get_kv(tok, "mass")
            if v is not None:
                sd.physics.mass        = float(v)
                sd.physics.has_physics = True
                continue

            v = _get_kv(tok, "restitution")
            if v is not None:
                sd.physics.restitution = float(v)
                sd.physics.has_physics = True
                continue

            v = _get_kv(tok, "friction")
            if v is not None:
                sd.physics.friction    = float(v)
                sd.physics.has_physics = True
                continue

            v = _get_kv(tok, "tex")
            if v is not None:
                sd.material.tex = v
                continue

            v = _get_kv(tok, "static")
            if v is not None:
                sd.physics.bStatic     = (v.lower() == "true")
                sd.physics.has_physics = True
                continue

    doc.defs[char_sym] = sd


# ---------------------------------------------------------------------------
# New-format serializer
# ---------------------------------------------------------------------------

# Map special single chars to safe parser tokens
_SAFE_NAME_MAP: dict[str, str] = {
    "#": "hash",
    ".": "dot",    # Note: '.' is the empty-cell sentinel — avoid using as sym
    "@": "at",
    "!": "bang",
    "*": "star",
    "+": "plus",
    "=": "eq",
    "/": "slash",
    "\\": "bslash",
    " ": "sp",
    "{": "lbrace",
    "}": "rbrace",
}

def _safe_name(sym: str) -> str:
    """Return a parser-safe identifier for a single-char legacy symbol."""
    if len(sym) == 1:
        return _SAFE_NAME_MAP.get(sym, sym)
    return sym


def _fmt_f(v: float) -> str:
    """Format a float: drop trailing zeros."""
    s = f"{v:.6g}"
    return s


def _fmt_vec3(v: tuple[float, float, float]) -> str:
    return f"{_fmt_f(v[0])} {_fmt_f(v[1])} {_fmt_f(v[2])}"


def serialize(doc: TilemapDoc) -> str:
    out: list[str] = []

    # ── @HEADER ──────────────────────────────────────────────────────────
    out += [
        "// Converted from legacy .tilemap by tools/convert_tilemap.py",
        "@HEADER",
        f"scale  {_fmt_f(doc.scale)}",
        f"origin {_fmt_vec3(doc.origin)}",
        "@END",
        "",
    ]

    # ── @SYMBOLS ─────────────────────────────────────────────────────────
    out.append("@SYMBOLS")
    for sym, sd in doc.defs.items():
        name = _safe_name(sym)
        out.append(f"{name}  {sd.type_name}")
    out += ["@END", ""]

    # ── @DEFS ────────────────────────────────────────────────────────────
    out.append("@DEFS")
    for sym, sd in doc.defs.items():
        name = _safe_name(sym)
        out.append(f"define {name} {{")

        # geometry
        geo_parts: list[str] = []
        if sd.geometry.half   is not None: geo_parts.append(f"half {_fmt_vec3(sd.geometry.half)}")
        if sd.geometry.radius is not None: geo_parts.append(f"radius {_fmt_f(sd.geometry.radius)}")
        if geo_parts:
            out.append("    geometry { " + "; ".join(geo_parts) + " }")

        # material
        mat_parts: list[str] = []
        if sd.material.kd        is not None: mat_parts.append(f"kd {_fmt_vec3(sd.material.kd)}")
        if sd.material.ka        is not None: mat_parts.append(f"ka {_fmt_vec3(sd.material.ka)}")
        if sd.material.ks        is not None: mat_parts.append(f"ks {_fmt_vec3(sd.material.ks)}")
        if sd.material.km        is not None: mat_parts.append(f"km {_fmt_vec3(sd.material.km)}")
        if sd.material.shininess is not None: mat_parts.append(f"shine {_fmt_f(sd.material.shininess)}")
        if sd.material.tex       is not None: mat_parts.append(f"tex {sd.material.tex}")
        if mat_parts:
            out.append("    material { " + "; ".join(mat_parts) + " }")

        # physics (only emit block when physics is active)
        phy = sd.physics
        if phy.has_physics:
            phy_parts: list[str] = []
            if phy.bStatic:              phy_parts.append("static true")
            if phy.mass        is not None: phy_parts.append(f"mass {_fmt_f(phy.mass)}")
            if phy.restitution is not None: phy_parts.append(f"restitution {_fmt_f(phy.restitution)}")
            if phy.friction    is not None: phy_parts.append(f"friction {_fmt_f(phy.friction)}")
            if phy.no_gravity:           phy_parts.append("nogravity")
            out.append("    physics  { " + "; ".join(phy_parts) + " }")

        out.append("}")
    out += ["@END", ""]

    # ── @INSTANCES ───────────────────────────────────────────────────────
    out.append("@INSTANCES")

    for layer_idx, layer in sorted(doc.layers.items()):
        out.append(f"grid layer={layer_idx} {{")
        for row_str in layer.rows:
            new_row: list[str] = []
            for ch in row_str:
                if ch in doc.defs:
                    new_row.append(_safe_name(ch))
                else:
                    # Spaces, tabs, or unknown chars → empty cell
                    new_row.append(".")
            # Skip rows that are entirely empty (all dots)
            if any(tok != "." for tok in new_row):
                out.append("    " + "  ".join(new_row))
        out.append("}")

    out += ["@END", ""]

    return "\n".join(out)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def convert(src: Path, dst: Path) -> None:
    text   = src.read_text(encoding="utf-8", errors="replace")
    doc    = parse_legacy(text)
    result = serialize(doc)
    dst.write_text(result, encoding="utf-8")
    print(f"Converted : {src}  →  {dst}")
    print(f"  Symbols : {len(doc.defs)}")
    total_instances = sum(
        sum(1 for ch in row if ch in doc.defs)
        for layer in doc.layers.values()
        for row in layer.rows
    )
    print(f"  Layers  : {len(doc.layers)}")
    print(f"  Instances (approx): {total_instances}")


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    src = Path(sys.argv[1])
    if not src.exists():
        print(f"Error: file not found: {src}", file=sys.stderr)
        sys.exit(1)

    dst = Path(sys.argv[2]) if len(sys.argv) >= 3 else src.with_name(src.stem + "_new" + src.suffix)
    convert(src, dst)


if __name__ == "__main__":
    main()
