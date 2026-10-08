#!/usr/bin/env python3
"""loc_report.py - Reporte en TEXTO PLANO de UNA corrida de csnloc (offline).

Escribe, junto a la carpeta de validacion de validate_csnloc.sh:

    <validacion>/loc_report.txt

con cuatro bloques:

    1. PARAMETROS   todas las claves de csnloc.d tal como se usaron (mas las
                    que no estan en el .d, con su valor por defecto).
    2. GRILLAS      nivel (global/regional/local), bbox, NodeKm, capas de
                    profundidad y nº de nodos: la densidad, que es ajustable.
    3. EVENTOS      TODOS los eventos localizados, una fila por solucion.
    4. RESUMEN      conteo al pie.

Uso:
    loc_report.py <validacion> [--out FILE] [--selftest]

    <validacion>  carpeta csnlocvalidate_<ts>/ (con <slug>.jsonl y manifest.json)
    --out FILE    ruta del reporte (default: <validacion>/loc_report.txt)
"""

import argparse
import math
import os
import re
import sys
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from offline_report import KM_PER_DEG, load_dir  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Claves que csnloc conoce pero que NO estan en run_working_v8/params/csnloc.d
# (valores por defecto de csnloc.c:80-86; mantener en sync con csnloc/README.md).
DEFAULTS = [
    ("EventDedupSec", "30.0", "dedup de eventos entre grillas (s)"),
    ("EventDedupKm", "100.0", "dedup de eventos entre grillas (km)"),
    ("MaxRMSDegrade", "0.10", "empeoramiento de RMS tolerado (fraccion)"),
    ("MaxGapDegradeDeg", "10.0", "empeoramiento de gap tolerado (deg)"),
    ("PhaseAssocTolSec", "2.0", "residual maximo al asociar una P nueva (s)"),
    ("PhaseAssocTolSecS", "4.0", "idem S (s)"),
    ("PhaseResidualMaxSec", "3.0", "poda por residual P (s)"),
    ("PhaseResidualMaxSecS", "5.0", "poda por residual S (s)"),
    ("RenucleateMinNewPhases", "3", "fases nuevas que disparan re-nucleo"),
]

GRID_LEVEL = {"GlobalGrid": "global", "RegionalGrid": "regional",
              "LocalGrid": "local"}

KEY_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)[ \t]+(\S+)")


# --------------------------------------------------------------------------
# Lectura
# --------------------------------------------------------------------------
def load_config(path):
    """[(clave, valor)] en orden, de las lineas activas del .d."""
    out = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            m = KEY_RE.match(s)
            if m:
                out.append((m.group(1), m.group(2)))
    return out


def load_grid(path):
    """Claves de un .grid + geometria derivada (nx, ny, nz, nodos)."""
    g = {"path": path, "name": os.path.basename(path)}
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            f = s.split()
            if len(f) < 2:
                continue
            g[f[0]] = f[1]

    def num(k, d=0.0):
        try:
            return float(g.get(k, d))
        except (TypeError, ValueError):
            return d

    lat_min, lat_max = num("LatMin"), num("LatMax")
    lon_min, lon_max = num("LonMin"), num("LonMax")
    node = num("NodeKm")
    clat = abs(math.cos(math.radians(0.5 * (lat_min + lat_max))))
    if node > 0 and clat > 1e-9:
        g["ny"] = int((lat_max - lat_min) / (node / KM_PER_DEG)) + 1
        g["nx"] = int((lon_max - lon_min) / (node / (KM_PER_DEG * clat))) + 1
    else:
        g["nx"] = g["ny"] = 0

    if "DepthLayers" in g:
        layers = [x for x in g["DepthLayers"].split(",") if x.strip()]
        g["nz"] = len(layers)
        g["depth_txt"] = "capas %s..%s (%d)" % (layers[0], layers[-1], len(layers))
    else:
        dmin, dmax = num("DepthMin"), num("DepthMax")
        dstep = num("DepthStep", 25.0) or 25.0
        g["nz"] = int((dmax - dmin) / dstep) + 1
        g["depth_txt"] = "%.0f..%.0f paso %.0f (%d)" % (dmin, dmax, dstep, g["nz"])
    g["nodes"] = g["nx"] * g["ny"] * g["nz"]
    return g


def _candidates(params_dir):
    cands = []
    for c in (params_dir, os.environ.get("EW_PARAMS"),
              os.path.join(REPO, "run_working_v8", "params")):
        if c and c not in cands:
            cands.append(c)
    return cands


def load_grids(cfg_pairs, params_dir):
    """[(nivel, Grid)] resolviendo las rutas contra los dirs candidatos."""
    cands = _candidates(params_dir)
    out = []
    for key, val in cfg_pairs:
        level = GRID_LEVEL.get(key)
        if level is None:
            continue
        found = None
        for base in cands:
            p = val if os.path.isabs(val) else os.path.join(base, val)
            if os.path.isfile(p):
                found = p
                break
        if found is None:
            out.append((level, {"path": val, "name": os.path.basename(val),
                                "missing": True, "nodes": 0, "depth_txt": "-",
                                "nx": 0, "ny": 0, "nz": 0}))
            continue
        g = load_grid(found)
        g["rel"] = val
        out.append((level, g))
    return out


def slug_key(slug):
    m = re.match(r"^([A-Za-z_]+)(\d+)$", slug)
    return (m.group(1), int(m.group(2))) if m else (slug, -1)


def fmt_t0(rec):
    if rec.get("t0_utc"):
        return rec["t0_utc"]
    if "t0" in rec:
        return datetime.fromtimestamp(rec["t0"], tz=timezone.utc).strftime(
            "%Y-%m-%dT%H:%M:%S") + "Z"
    return "?"


def fnum(v, nd=1):
    if v is None:
        return "-"
    try:
        return "%.*f" % (nd, float(v))
    except (TypeError, ValueError):
        return str(v)


def table(headers, rows):
    if not rows:
        return ["(sin filas)"]
    w = [len(h) for h in headers]
    for r in rows:
        for i, c in enumerate(r):
            w[i] = max(w[i], len(str(c)))
    out = ["  ".join(h.ljust(w[i]) for i, h in enumerate(headers)),
           "-" * (sum(w) + 2 * (len(w) - 1))]
    for r in rows:
        out.append("  ".join(str(c).ljust(w[i]) for i, c in enumerate(r)))
    return out


# --------------------------------------------------------------------------
# Reporte
# --------------------------------------------------------------------------
def build_report(vdir, out_path):
    manifest, by_slug, expected = load_dir(vdir)
    cfg_copy = os.path.join(vdir, "csnloc.d")
    cfg_pairs = load_config(cfg_copy) if os.path.isfile(cfg_copy) else []
    params_dir = ""
    if manifest and manifest.get("config"):
        params_dir = os.path.dirname(manifest["config"])
    grids = load_grids(cfg_pairs, params_dir)

    L = []
    L.append("=" * 100)
    L.append("REPORTE DE CORRIDA - csnloc (modo offline)")
    L.append("=" * 100)
    L.append("validacion : %s" % vdir)
    if manifest:
        L.append("captura    : %s" % manifest.get("capture_dir", "?"))
        b = manifest.get("csnloc_bin") or {}
        L.append("binario    : %s" % b.get("path", "?"))
        L.append("sha256     : %s" % (b.get("sha256") or "?"))
        L.append("config     : %s" % manifest.get("config", "?"))
    L.append("generado   : %s" % datetime.now(timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ"))

    # 1) parametros
    L.append("")
    L.append("-" * 100)
    L.append("1. PARAMETROS DEL LOCALIZADOR (csnloc.d)")
    L.append("-" * 100)
    if not cfg_pairs:
        L.append("(no encontre csnloc.d en la validacion)")
    else:
        w = max(len(k) for k, _ in cfg_pairs)
        for k, v in cfg_pairs:
            L.append("  %-*s  %s" % (w, k, v))
    L.append("")
    L.append("  No presentes en el .d (valor por defecto del codigo):")
    for k, v, desc in DEFAULTS:
        L.append("  %-24s  %-8s  %s" % (k, v, desc))

    # 2) grillas
    L.append("")
    L.append("-" * 100)
    L.append("2. GRILLAS (densidad ajustable)")
    L.append("-" * 100)
    rows = []
    total = 0
    for level, g in grids:
        if g.get("missing"):
            rows.append([level, g["name"], "NO ENCONTRADA (%s)" % g["path"],
                         "-", "-", "-"])
            continue
        bbox = "%.3f..%.3f / %.3f..%.3f" % (
            float(g.get("LatMin", 0)), float(g.get("LatMax", 0)),
            float(g.get("LonMin", 0)), float(g.get("LonMax", 0)))
        rows.append([level, g["name"], bbox, fnum(g.get("NodeKm"), 1),
                     g["depth_txt"], "%d (%dx%dx%d)" % (g["nodes"], g["nx"],
                                                        g["ny"], g["nz"])])
        total += g["nodes"]
    L += table(["nivel", "nombre", "bbox (lat / lon)", "NodeKm", "profundidad",
                "nodos"], rows)
    L.append("")
    L.append("  nodos totales (suma de todas las grillas): %d" % total)

    # 3) eventos
    L.append("")
    L.append("-" * 100)
    L.append("3. EVENTOS LOCALIZADOS (%d soluciones)"
             % sum(len(v) for v in by_slug.values()))
    L.append("-" * 100)
    rows = []
    for slug in sorted(by_slug, key=slug_key):
        for r in by_slug[slug]:
            n_p = n_s = 0
            for p in r.get("phases") or []:
                if str(p.get("phase", "P")).upper() == "S":
                    n_s += 1
                else:
                    n_p += 1
            rows.append([slug, r.get("event", "-"), r.get("id", "-"),
                         r.get("version", "-"), fmt_t0(r),
                         fnum(r.get("lat"), 3), fnum(r.get("lon"), 3),
                         fnum(r.get("depth_km"), 1), r.get("nphases", "-"),
                         n_p, n_s, fnum(r.get("rms_sec"), 2),
                         fnum(r.get("gap_deg"), 0), fnum(r.get("dmin_km"), 0),
                         fnum(r.get("score"), 1),
                         r.get("grid_level", "-"), r.get("depth_ctrl", "-")])
    L += table(["test", "ev", "id", "ver", "t0 (UTC)", "lat", "lon", "z_km",
                "nph", "nP", "nS", "rms", "gap", "dmin", "score", "grilla",
                "ctrl"], rows)

    # 4) resumen
    con_sol = sorted(s for s in by_slug if by_slug[s])
    L.append("")
    L.append("-" * 100)
    L.append("4. RESUMEN")
    L.append("-" * 100)
    L.append("  tanks procesados      : %d" % len(expected))
    L.append("  tanks con solucion    : %d" % len(con_sol))
    L.append("  soluciones totales    : %d"
             % sum(len(v) for v in by_slug.values()))
    sin = sorted(set(expected) - set(con_sol), key=slug_key)
    if sin:
        L.append("  sin solucion (%d)      : %s" % (len(sin), ", ".join(sin)))
    L.append("")

    txt = "\n".join(L)
    if out_path:
        with open(out_path, "w", encoding="utf-8") as fh:
            fh.write(txt)
    return txt, out_path


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    ap = argparse.ArgumentParser(add_help=False,
                                 description="Reporte de una corrida de csnloc.")
    ap.add_argument("validation", nargs="?")
    ap.add_argument("--out")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("-h", "--help", action="store_true")
    a = ap.parse_args(argv)

    if a.selftest:
        return selftest()
    if a.help:
        print(__doc__.strip())
        return 0
    if not a.validation or not os.path.isdir(a.validation):
        print("loc_report: ERROR: falta o no existe <validacion>", file=sys.stderr)
        return 1

    out = a.out or os.path.join(a.validation, "loc_report.txt")
    txt, path = build_report(a.validation, out)
    if not a.quiet:
        print(txt)
    print("escrito: %s" % path, file=sys.stderr)
    return 0


# --------------------------------------------------------------------------
# Selftest
# --------------------------------------------------------------------------
def selftest():
    import json
    import shutil
    import tempfile

    d = tempfile.mkdtemp(prefix="loc_report_selftest_")
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("ok  : " if cond else "FAIL: ") + msg)
        if not cond:
            ok = False

    vdir = os.path.join(d, "val")
    os.makedirs(vdir)
    with open(os.path.join(vdir, "csnloc.d"), "w", encoding="utf-8") as fh:
        fh.write("MyModuleId        MOD_CSNLOC        # ID\n"
                 "AssocWindowSec    120.0            # ventana\n"
                 "# DepthPriorKm     0.0             # no aplica\n"
                 "GlobalGrid        grids/g.global\n"
                 "LocalGrid         grids/l.local\n")
    grids = os.path.join(d, "grids")
    os.makedirs(grids)
    with open(os.path.join(grids, "g.global"), "w", encoding="utf-8") as fh:
        fh.write("Name Global\nLatMin -90.0\nLatMax 90.0\nLonMin -180.0\n"
                 "LonMax 180.0\nNodeKm 100.0\n"
                 "DepthLayers 10,30,50,100,200,300,400,500,600,750\n")
    with open(os.path.join(grids, "l.local"), "w", encoding="utf-8") as fh:
        fh.write("Name Local\nLatMin -22.0\nLatMax -21.0\nLonMin -70.0\n"
                 "LonMax -69.0\nNodeKm 10.0\nDepthMin 0.0\nDepthMax 100.0\n"
                 "DepthStep 25.0\n")
    with open(os.path.join(vdir, "test2.jsonl"), "w", encoding="utf-8") as fh:
        fh.write(json.dumps({
            "event": 1, "id": 1, "version": 1, "t0": 1000.0, "t0_utc": "",
            "lat": -21.5, "lon": -69.5, "depth_km": 12.0, "nphases": 6,
            "rms_sec": 0.4, "gap_deg": 180.0, "dmin_km": 30.0, "score": 1.0,
            "grid_level": 2, "depth_ctrl": "bien",
            "phases": [{"phase": "P"}, {"phase": "P"}, {"phase": "S"}],
        }) + "\n")
    with open(os.path.join(vdir, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump({"capture_dir": "/x", "config": os.path.join(d, "csnloc.d"),
                   "csnloc_bin": {"path": "/bin/csnloc", "sha256": "abc"},
                   "tanks": [{"slug": "test1"}, {"slug": "test2"}]}, fh)

    cfg = load_config(os.path.join(vdir, "csnloc.d"))
    check(("AssocWindowSec", "120.0") in cfg, "load_config: clave activa")
    check(all(k != "DepthPriorKm" for k, _ in cfg),
          "load_config: ignora claves comentadas")

    gs = load_grids(cfg, d)
    check(len(gs) == 2 and gs[0][0] == "global" and gs[1][0] == "local",
          "load_grids: nivel por clave")
    check(gs[0][1]["nz"] == 10 and gs[0][1]["nodes"] > 0,
          "load_grid: DepthLayers -> nz")
    check(gs[1][1]["nz"] == 5, "load_grid: DepthMin/Max/Step -> nz")
    check(gs[1][1]["nx"] > 0 and gs[1][1]["ny"] > 0, "load_grid: nx/ny")

    txt, path = build_report(vdir, os.path.join(d, "rep.txt"))
    check(os.path.isfile(path), "build_report: escribe el archivo")
    for want in ("1. PARAMETROS", "2. GRILLAS", "3. EVENTOS", "4. RESUMEN"):
        check(want in txt, "bloque presente: %s" % want)
    check("test2" in txt and "PhaseResidualMaxSecS" in txt,
          "eventos y defaults presentes")
    check("tanks con solucion    : 1" in txt, "resumen cuenta")

    shutil.rmtree(d, ignore_errors=True)
    print()
    print("%s selftest loc_report" % ("OK" if ok else "FALLO"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
