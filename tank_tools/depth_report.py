#!/usr/bin/env python3
"""depth_report.py - Analisis del sesgo/cuantizacion de PROFUNDIDAD de csnloc.

csnloc nuclea el hipocentro en nodos DISCRETOS de profundidad (g->depth[iz]) y
solo el refinamiento local puede mover z, pero en una caja pequena
(dep_span = RefineNodeKm*2, que se reduce a la mitad en cada iteracion). Por eso
la profundidad final suele quedar a pocos km de un nodo de la grilla.

Este script cuantifica ese patron: para cada solucion mide la distancia al nodo
de profundidad mas cercano (de TODAS las grillas configuradas) y reporta:

  - histograma de profundidades (valores exactos, ordenados por frecuencia);
  - estadistica del offset al nodo mas cercano (media/max, % dentro de umbrales);
  - distribucion por clase de control de profundidad (proxy de gap/nph/dmin);
  - eventos "clavados" en el borde de una grilla (posible artefacto).

Entradas aceptadas (autodetecta):
  - un directorio de validacion de validate_csnloc.sh  ->  <dir>/*.jsonl
  - un directorio de logs de la instancia            ->  <dir>/csnloc_*.log

Uso:
    python3 tank_tools/depth_report.py <dir> [--grids DIR]
    python3 tank_tools/depth_report.py picks/<cap>/csnlocvalidate_<ts>
    python3 tank_tools/depth_report.py /home/seba/ew8portable/run_working_v8/log

Opciones:
    --grids DIR    directorio con los .grid (default: run_working_v8/params/grids)
    --edge-tol G   tolerancia (grados) para marcar borde de grilla (default 0.05)
    --top N        nº de valores de profundidad a listar (default 15)
    --format FMT   table (default) | csv | json | all
    --out PREFIJO  prefijo de los csv/json (default: <dir>/depth_report)
"""
import argparse
import glob
import json
import os
import re
import sys
from collections import Counter, defaultdict

# Default: grillas del repo, relativas a la raiz del proyecto.
_HERE = os.path.dirname(os.path.abspath(__file__))
_DEFAULT_GRIDS = os.path.join(os.path.dirname(_HERE), "run_working_v8", "params", "grids")

# Umbrales (km) para reportar cuan cerca del nodo quedo la profundidad.
NODE_TOL_KM = (2.5, 5.0, 10.0)

# Regex de la linea resumen de csnloc en el log de anillo:
#   csnloc: evento <id>[-<ver>] lat=.. lon=.. z=.. km nph=.. rms=.. gap=..
_LOG_RE = re.compile(
    r"evento\s+(?P<id>\S+?)(?:-(?P<ver>\d+))?\s+"
    r"lat=(?P<lat>[-0-9.]+)\s+lon=(?P<lon>[-0-9.]+)\s+"
    r"z=(?P<z>[-0-9.]+)\s+km\s+"
    r"nph=(?P<nph>\d+)\s+rms=(?P<rms>[-0-9.]+)\s+gap=(?P<gap>[-0-9.]+)"
)


# --------------------------------------------------------------------------
# Grillas
# --------------------------------------------------------------------------
def _strip_comment(line):
    return line.split("#", 1)[0].strip()


def parse_grid_file(path):
    """Lee un .grid de csnloc. Devuelve dict o None si no es una grilla."""
    g = {"name": os.path.basename(path), "depths": [],
         "lat_min": None, "lat_max": None, "lon_min": None, "lon_max": None,
         "node_km": None}
    dmin = dmax = dstep = None
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for raw in fh:
                line = _strip_comment(raw)
                if not line:
                    continue
                parts = line.split()
                if len(parts) < 2:
                    continue
                key, val = parts[0], parts[1]
                if key == "Name":
                    g["name"] = val
                elif key == "LatMin":
                    g["lat_min"] = float(val)
                elif key == "LatMax":
                    g["lat_max"] = float(val)
                elif key == "LonMin":
                    g["lon_min"] = float(val)
                elif key == "LonMax":
                    g["lon_max"] = float(val)
                elif key == "NodeKm":
                    g["node_km"] = float(val)
                elif key == "DepthLayers":
                    g["depths"] = [float(x) for x in val.split(",") if x.strip()]
                elif key == "DepthMin":
                    dmin = float(val)
                elif key == "DepthMax":
                    dmax = float(val)
                elif key == "DepthStep":
                    dstep = float(val)
    except (OSError, ValueError):
        return None

    if not g["depths"] and dmin is not None and dmax is not None:
        step = dstep if dstep and dstep > 0 else 25.0
        n = int(round((dmax - dmin) / step))
        g["depths"] = [dmin + i * step for i in range(n + 1)]

    if not g["depths"]:
        return None
    return g


def load_grids(grids_dir):
    grids = []
    for path in sorted(glob.glob(os.path.join(grids_dir, "*.grid"))):
        g = parse_grid_file(path)
        if g:
            grids.append(g)
    return grids


def all_depth_nodes(grids):
    nodes = sorted({round(d, 3) for g in grids for d in g["depths"]})
    return nodes


def nearest_node(z, nodes):
    if not nodes:
        return None, None
    best = min(nodes, key=lambda n: abs(n - z))
    return best, abs(best - z)


def on_grid_edge(lat, lon, grid, tol_deg):
    """True si (lat,lon) cae dentro del bbox pero a <= tol de algun borde."""
    if None in (grid["lat_min"], grid["lat_max"], grid["lon_min"], grid["lon_max"]):
        return False
    inside = (grid["lat_min"] - tol_deg <= lat <= grid["lat_max"] + tol_deg and
              grid["lon_min"] - tol_deg <= lon <= grid["lon_max"] + tol_deg)
    if not inside:
        return False
    near = (abs(lat - grid["lat_min"]) <= tol_deg or
            abs(lat - grid["lat_max"]) <= tol_deg or
            abs(lon - grid["lon_min"]) <= tol_deg or
            abs(lon - grid["lon_max"]) <= tol_deg)
    return near


# --------------------------------------------------------------------------
# Control de profundidad (proxy; usa depth_ctrl del JSON si ya existe)
# --------------------------------------------------------------------------
def depth_control(rec):
    """Clase cualitativa de control de profundidad."""
    if rec.get("depth_ctrl"):
        return rec["depth_ctrl"]
    nph = rec.get("nphases") or 0
    gap = rec.get("gap_deg")
    z = rec.get("depth_km") or 0.0
    dmin = rec.get("dmin_km")

    if nph < 4 or (gap is not None and gap > 300):
        return "sin_control"
    # La profundidad se resuelve bien si hay una estacion a <= ~1.5*z (Koper).
    if dmin is not None and z > 0 and dmin > 1.5 * z:
        return "pobre"
    if (gap is not None and gap > 180) or nph < 6:
        return "pobre"
    if (gap is not None and gap > 120) or nph < 8:
        return "aceptable"
    return "bien"


# --------------------------------------------------------------------------
# Lectura de eventos
# --------------------------------------------------------------------------
def load_validation_dir(d):
    recs = []
    for path in sorted(glob.glob(os.path.join(d, "*.jsonl"))):
        slug = os.path.basename(path)[: -len(".jsonl")]
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for ln, line in enumerate(fh, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    r = json.loads(line)
                except json.JSONDecodeError:
                    continue
                r["_source"] = slug
                r["_line"] = ln
                recs.append(r)
    return recs


def load_log_dir(d):
    recs = []
    for path in sorted(glob.glob(os.path.join(d, "csnloc_*.log"))):
        src = os.path.basename(path)
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for ln, line in enumerate(fh, 1):
                m = _LOG_RE.search(line)
                if not m:
                    continue
                recs.append({
                    "_source": src, "_line": ln,
                    "id": m.group("id"),
                    "version": int(m.group("ver")) if m.group("ver") else None,
                    "lat": float(m.group("lat")),
                    "lon": float(m.group("lon")),
                    "depth_km": float(m.group("z")),
                    "nphases": int(m.group("nph")),
                    "rms_sec": float(m.group("rms")),
                    "gap_deg": float(m.group("gap")),
                })
    return recs


def load_events(d):
    if glob.glob(os.path.join(d, "*.jsonl")):
        return "validation", load_validation_dir(d)
    if glob.glob(os.path.join(d, "csnloc_*.log")):
        return "logs", load_log_dir(d)
    raise SystemExit(f"depth_report: {d} no contiene .jsonl ni csnloc_*.log")


# --------------------------------------------------------------------------
# Reporte
# --------------------------------------------------------------------------
def fnum(v, nd=1):
    if v is None:
        return "-"
    try:
        return f"{float(v):.{nd}f}"
    except (TypeError, ValueError):
        return str(v)


def analyze(recs, grids, nodes, edge_tol):
    depths = [r["depth_km"] for r in recs if r.get("depth_km") is not None]
    offsets = []
    for z in depths:
        _, off = nearest_node(z, nodes)
        if off is not None:
            offsets.append(off)

    hist = Counter(round(z, 3) for z in depths)
    ctrl = Counter(depth_control(r) for r in recs)

    edge = defaultdict(int)
    for r in recs:
        lat, lon = r.get("lat"), r.get("lon")
        if lat is None or lon is None:
            continue
        for g in grids:
            if on_grid_edge(lat, lon, g, edge_tol):
                edge[g["name"]] += 1

    return {
        "n": len(recs),
        "n_depth": len(depths),
        "z_min": min(depths) if depths else None,
        "z_max": max(depths) if depths else None,
        "hist": hist,
        "offsets": offsets,
        "ctrl": ctrl,
        "edge": edge,
    }


def print_report(d, mode, a, grids, nodes, top):
    print(f"depth_report: {d}  (modo {mode})")
    print(f"grillas: {', '.join(g['name'] for g in grids) or '(ninguna)'}")
    print(f"eventos: {a['n']}  (con profundidad: {a['n_depth']})")
    print(f"rango z: {fnum(a['z_min'])} .. {fnum(a['z_max'])} km")
    print()

    print(f"=== profundidades mas frecuentes (top {top}) ===")
    print(f"{'z_km':>10}  {'n':>6}  {'%':>6}   nodo?")
    print("-" * 40)
    for z, n in a["hist"].most_common(top):
        is_node = "nodo" if any(abs(z - nd) < 1e-6 for nd in nodes) else ""
        print(f"{z:>10.1f}  {n:>6}  {100.0 * n / a['n']:>5.1f}%   {is_node}")
    print()

    offs = a["offsets"]
    print("=== offset al nodo de profundidad mas cercano ===")
    if offs:
        mean = sum(offs) / len(offs)
        print(f"media={mean:.2f} km  max={max(offs):.2f} km")
        for tol in NODE_TOL_KM:
            c = sum(1 for o in offs if o <= tol + 1e-6)
            print(f"  <= {tol:>4.1f} km: {c:>6}/{len(offs)}  ({100.0 * c / len(offs):.1f}%)")
    else:
        print("(sin datos)")
    print()

    print("=== control de profundidad (proxy) ===")
    for k in ("bien", "aceptable", "pobre", "sin_control"):
        n = a["ctrl"].get(k, 0)
        print(f"  {k:<12} {n:>6}  ({100.0 * n / a['n']:>5.1f}%)")
    print()

    print("=== eventos en borde de grilla (posible artefacto) ===")
    if a["edge"]:
        for name, n in sorted(a["edge"].items(), key=lambda x: -x[1]):
            print(f"  {name:<24} {n:>6}")
    else:
        print("  (ninguno)")


def write_out(prefix, a, grids, fmt):
    if fmt in ("csv", "all"):
        path = prefix + ".csv"
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("z_km,n,pct,es_nodo\n")
            for z, n in a["hist"].most_common():
                is_node = any(abs(z - nd) < 1e-6 for nd in all_depth_nodes(grids))
                fh.write(f"{z},{n},{100.0 * n / a['n']:.2f},{int(is_node)}\n")
        print(f"escrito: {path}", file=sys.stderr)
    if fmt in ("json", "all"):
        path = prefix + ".json"
        payload = {
            "n": a["n"],
            "n_depth": a["n_depth"],
            "z_min": a["z_min"], "z_max": a["z_max"],
            "hist": [[z, n] for z, n in a["hist"].most_common()],
            "offset_mean": (sum(a["offsets"]) / len(a["offsets"])) if a["offsets"] else None,
            "offset_max": max(a["offsets"]) if a["offsets"] else None,
            "control": dict(a["ctrl"]),
            "edge": dict(a["edge"]),
            "grids": [g["name"] for g in grids],
            "nodes": all_depth_nodes(grids),
        }
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, indent=2)
        print(f"escrito: {path}", file=sys.stderr)


def main(argv=None):
    p = argparse.ArgumentParser(
        description="Analisis de profundidad/cuantizacion de csnloc.")
    p.add_argument("dir", help="directorio de validacion o de logs")
    p.add_argument("--grids", default=_DEFAULT_GRIDS,
                   help="directorio con los .grid (default: run_working_v8/params/grids)")
    p.add_argument("--edge-tol", type=float, default=0.05,
                   help="tolerancia (grados) para borde de grilla (default 0.05)")
    p.add_argument("--top", type=int, default=15,
                   help="nº de profundidades a listar (default 15)")
    p.add_argument("--format", choices=["table", "csv", "json", "all"],
                   default="table")
    p.add_argument("--out", metavar="PREFIJO", help="prefijo de los csv/json")
    args = p.parse_args(argv)

    if not os.path.isdir(args.dir):
        raise SystemExit(f"depth_report: no existe {args.dir}")

    grids = load_grids(args.grids)
    if not grids:
        print(f"depth_report: aviso: sin .grid en {args.grids}", file=sys.stderr)
    nodes = all_depth_nodes(grids)

    mode, recs = load_events(args.dir)
    if not recs:
        raise SystemExit(f"depth_report: sin eventos en {args.dir}")

    a = analyze(recs, grids, nodes, args.edge_tol)
    print_report(args.dir, mode, a, grids, nodes, args.top)

    if args.format != "table":
        prefix = args.out or os.path.join(args.dir, "depth_report")
        write_out(prefix, a, grids, args.format)
    return 0


if __name__ == "__main__":
    sys.exit(main())
