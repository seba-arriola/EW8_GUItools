#!/usr/bin/env python3
"""calibrate_csnloc.py - Barrido de parametros de csnloc contra un catalogo.

Para cada variante del fichero declarativo (overrides de claves de csnloc.d):

    1) materializa un csnloc.d AISLADO en <work>/cfg/<tag>.d  (nunca toca
       run_working_v8/params/csnloc.d),
    2) corre validate_csnloc.sh con ese .d sobre la captura (P+S) ->
       <work>/val/<tag>/<slug>.jsonl,
    3) evalua contra el catalogo con catalog_report.py y agrega metricas.

Por cada variante deja, en <work>/val/<tag>/:

    loc_report.txt      parametros + grillas + TODOS los eventos localizados
    catalog_report.txt  cruce con el catalogo (referencia) + adicionales + resumen

Al final imprime una linea por set de parametros (eventos del archivo encontrados
a <=25/50/100 km, mediana del desvio y adicionales) para discriminar cual le
apunta a mas.

Uso:
    calibrate_csnloc.py --capture DIR --catalog FILE --work DIR [opciones]
    calibrate_csnloc.py --dry-run ...

Opciones:
    --variants FILE   JSON de variantes (default: tank_tools/calib/csnloc_variants.json)
    --tags LIST       solo variantes cuyo tag case con algun glob de LIST (coma)
    --only GLOB       evaluar solo slugs que casen (default *); con un GLOB != *
                      se construye una captura recortada en <work>/capture_subset
    --jobs N          validaciones en paralelo (default 1)
    --time-window T   (default 30.0 s)     --dist-deg D  (default 1.0)
    --bands L         bandas de distancia del resumen (default 25,50,100)
    --no-reports      no escribir loc_report/catalog_report por variante
    --base-config F   csnloc.d base (default: run_working_v8/params/csnloc.d)
    --force           rehacer los .jsonl existentes
    --dry-run         solo lista las variantes; no escribe nada
    --selftest        pruebas internas de materialize/load_variants
    -h, --help        esta ayuda

Codigos: 0 ok | 1 error de uso | 2 alguna validacion fallo.
"""

import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import catalog_report  # noqa: E402
import loc_report  # noqa: E402
from offline_report import print_table  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BASE = os.path.join(REPO, "run_working_v8", "params", "csnloc.d")
DEFAULT_VARIANTS = os.path.join(REPO, "tank_tools", "calib", "csnloc_variants.json")
VALIDATE = os.path.join(REPO, "tank_tools", "validate_csnloc.sh")

# clave + un unico token de valor + comentario opcional (evita tocar prosa).
KEY_RE = re.compile(r"^\s*#?\s*([A-Za-z_][A-Za-z0-9_]*)[ \t]+(\S+)[ \t]*(#.*)?$")


# --------------------------------------------------------------------------
# Variantes
# --------------------------------------------------------------------------
def load_variants(path):
    """JSON -> [{'tag': str, 'overrides': {...}, ...}].

    Se CONSERVAN las demas claves de cada variante (p.ej. `grids_dir`): si se
    reconstruyera el dict con solo tag/overrides, una variante que cambia el
    juego de grillas correria en silencio con las de produccion.
    """
    with open(path, "r", encoding="utf-8") as fh:
        data = json.load(fh)
    items = data.get("variants", []) if isinstance(data, dict) else data
    out = []
    for it in items:
        if isinstance(it, str):
            out.append({"tag": it, "overrides": {}})
            continue
        tag = (it.get("tag") or "").strip()
        if not tag:
            continue
        v = dict(it)
        v["tag"] = tag
        v["overrides"] = dict(it.get("overrides") or {})
        out.append(v)
    return out


def filter_tags(variants, spec):
    if not spec:
        return variants
    pats = [p.strip() for p in spec.split(",") if p.strip()]
    return [v for v in variants
            if any(fnmatch.fnmatch(v["tag"], p) for p in pats)]


GRID_KEYS = ("GlobalGrid", "RegionalGrid", "LocalGrid")
LOCAL_GRID_NAMES = ["tarapaca", "antofagasta", "atacama", "coquimbo", "centro",
                    "biobio", "araucania", "los_lagos", "aysen", "magallanes"]


def grids_block(grids_dir):
    """{clave: [rutas absolutas]} del juego de grillas de una variante."""
    d = os.path.abspath(grids_dir)
    return {
        "GlobalGrid": [os.path.join(d, "global.grid")],
        "RegionalGrid": [os.path.join(d, "chile_regional.grid")],
        "LocalGrid": [os.path.join(d, "%s.grid" % n) for n in LOCAL_GRID_NAMES],
    }


def materialize(base_d, overrides, out_d, grids=None):
    """Copia base_d a out_d sustituyendo las claves de overrides.

    - Solo sustituye lineas con la forma `Clave valor [# comentario]`
      (la linea puede estar comentada: se descomenta).
    - Las claves ausentes se anaden al final.
    - `grids` = {clave: [rutas]} reemplaza el BLOQUE de grillas completo. Hace
      falta porque `LocalGrid` se repite 10 veces y el reemplazo por clave
      dejaria las otras 9 apuntando a las grillas de produccion.
    """
    with open(base_d, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()

    seen = set()
    out = []
    grid_done = False
    for line in lines:
        m = KEY_RE.match(line)
        key = m.group(1) if m else None
        if grids is not None and key in GRID_KEYS:
            if not grid_done:
                for gk in GRID_KEYS:
                    for path in grids.get(gk, []):
                        out.append("%-18s %s\n" % (gk, path))
                grid_done = True
            continue
        if key in overrides and key not in seen:
            out.append("%-18s %s\n" % (key, overrides[key]))
            seen.add(key)
        else:
            out.append(line)
    for key, val in overrides.items():
        if key not in seen:
            out.append("%-18s %s\n" % (key, val))
    if grids is not None and not grid_done:
        for gk in GRID_KEYS:
            for path in grids.get(gk, []):
                out.append("%-18s %s\n" % (gk, path))

    d = os.path.dirname(out_d)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(out_d, "w", encoding="utf-8") as fh:
        fh.writelines(out)
    return out_d


# --------------------------------------------------------------------------
# Validacion y evaluacion
# --------------------------------------------------------------------------
def make_subset(capture, work, only):
    """Copia los <slug>.picks que casan con `only` a <work>/capture_subset."""
    dst = os.path.join(work, "capture_subset")
    os.makedirs(dst, exist_ok=True)
    n = 0
    for name in sorted(os.listdir(capture)):
        if not name.endswith(".picks"):
            continue
        if not fnmatch.fnmatch(name[: -len(".picks")], only):
            continue
        d = os.path.join(dst, name)
        if not os.path.exists(d):
            shutil.copy2(os.path.join(capture, name), d)
        n += 1
    return dst, n


def run_validation(capture, config, out_dir, force=False):
    cmd = ["bash", VALIDATE, "--no-report", "--config", config, "--out", out_dir]
    if force:
        cmd.append("--force")
    cmd.append(capture)
    return subprocess.run(cmd, capture_output=True, text=True)


def evaluate(vdir, catalog, T, D):
    return catalog_report.summarize(vdir, catalog, T, D)


def sweep(variants, capture, base_d, work, catalog_path, T, D, only,
          jobs, force, reports=True):
    catalog = catalog_report.load_catalog(catalog_path)
    if only != "*":
        catalog = {k: v for k, v in catalog.items()
                   if fnmatch.fnmatch(k, only)}
    capture_use = capture
    if only != "*":
        capture_use, n = make_subset(capture, work, only)
        print("calibrate: captura recortada -> %s (%d picks)" % (capture_use, n))

    def one(v):
        tag = v["tag"]
        grids = grids_block(v["grids_dir"]) if v.get("grids_dir") else None
        cfg = materialize(base_d, v["overrides"],
                          os.path.join(work, "cfg", tag + ".d"), grids)
        vdir = os.path.join(work, "val", tag)
        r = run_validation(capture_use, cfg, vdir, force=force)
        res = {"tag": tag, "overrides": v["overrides"], "val_dir": vdir}
        if v.get("grids_dir"):
            res["grids_dir"] = v["grids_dir"]
        if r.returncode not in (0, 2):
            res["error"] = r.returncode
            res["stderr"] = (r.stderr or "").strip().splitlines()[-1:] or [""]
            return res
        # Reportes por set de parametros: A = corrida, B = cruce con la referencia.
        if reports:
            loc_report.build_report(vdir, os.path.join(vdir, "loc_report.txt"))
            catalog_report.write_report(vdir, catalog, T=T, D=D)
        res["summary"] = evaluate(vdir, catalog, T, D)
        return res

    if jobs > 1:
        with ThreadPoolExecutor(max_workers=jobs) as ex:
            return list(ex.map(one, variants))
    return [one(v) for v in variants]


# --------------------------------------------------------------------------
# Comparacion de sets de parametros
# --------------------------------------------------------------------------
def print_comparison(results, T, D, bands):
    good = [r for r in results if r.get("summary")]
    bad = [r for r in results if not r.get("summary")]
    top = max(bands)
    # Ordenado por eventos del archivo encontrados a <= banda mayor (desc);
    # a igualdad, mediana del desvio (asc). Es el criterio del usuario, no un indice.
    good.sort(key=lambda r: (-r["summary"]["bands"].get(top, 0),
                             r["summary"]["med_km"]
                             if r["summary"]["med_km"] is not None else 1e9))

    headers = (["variante", "con_sol"] + ["<=%dkm" % b for b in bands] +
               ["sin_sol", "mediana_km", "adicionales", "soluciones"])
    rows = []
    for r in good:
        s = r["summary"]
        rows.append([r["tag"], s["n_con_sol"]]
                    + [s["bands"].get(b, 0) for b in bands]
                    + [s["n_sin_sol"], catalog_report.fnum(s["med_km"], 1),
                       s["n_extra"], s["n_sol_total"]])
    print()
    print("=== comparacion de sets de parametros ===")
    print("(ordenado por eventos del archivo a <=%d km, descendente)" % top)
    print("ventana: T=%.1f s  D=%.2f deg" % (T, D))
    print_table(headers, rows)
    for r in bad:
        print("ERROR %s: rc=%s %s" % (r["tag"], r.get("error"),
                                      (r.get("stderr") or [""])[0]))
    return 0 if not bad else 2


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def build_parser():
    p = argparse.ArgumentParser(add_help=False,
                                description="Barrido de parametros de csnloc.")
    p.add_argument("--capture")
    p.add_argument("--catalog")
    p.add_argument("--variants", default=DEFAULT_VARIANTS)
    p.add_argument("--tags")
    p.add_argument("--work")
    p.add_argument("--only", default="*")
    p.add_argument("--jobs", type=int, default=1)
    p.add_argument("--time-window", type=float, default=30.0)
    p.add_argument("--dist-deg", type=float, default=1.0)
    p.add_argument("--bands", default="25,50,100")
    p.add_argument("--no-reports", dest="reports", action="store_false",
                   help="no escribir loc_report/catalog_report por variante")
    p.add_argument("--base-config", default=DEFAULT_BASE)
    p.add_argument("--force", action="store_true")
    p.add_argument("--dry-run", dest="dry_run", action="store_true")
    p.add_argument("--selftest", action="store_true")
    p.add_argument("-h", "--help", action="store_true")
    return p


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    args = build_parser().parse_args(argv)

    if args.selftest:
        return selftest()
    if args.help:
        print(__doc__.strip())
        return 0
    if not args.capture or not args.catalog or not args.work:
        print("calibrate_csnloc: ERROR: se requiere --capture, --catalog y --work",
              file=sys.stderr)
        return 1
    if not os.path.isdir(args.capture):
        print("calibrate_csnloc: ERROR: no existe la captura %s" % args.capture,
              file=sys.stderr)
        return 1
    if not os.path.isfile(args.catalog):
        print("calibrate_csnloc: ERROR: no existe el catalogo %s" % args.catalog,
              file=sys.stderr)
        return 1
    if not os.path.isfile(args.base_config):
        print("calibrate_csnloc: ERROR: no existe --base-config %s" % args.base_config,
              file=sys.stderr)
        return 1

    variants = filter_tags(load_variants(args.variants), args.tags)
    if not variants:
        print("calibrate_csnloc: ERROR: no hay variantes que evaluar",
              file=sys.stderr)
        return 1

    print("calibrate: %d variantes  captura=%s  only=%s"
          % (len(variants), args.capture, args.only))
    for v in variants:
        print("  %-14s %s" % (v["tag"], json.dumps(v["overrides"])))
    if args.dry_run:
        print("calibrate: --dry-run (no se escribe nada)")
        return 0

    bands = tuple(float(x) for x in args.bands.split(",") if x.strip())
    os.makedirs(args.work, exist_ok=True)
    results = sweep(variants, args.capture, args.base_config, args.work,
                    args.catalog, args.time_window, args.dist_deg,
                    args.only, max(1, args.jobs), args.force, args.reports)

    out = os.path.join(args.work, "calibration.json")
    with open(out, "w", encoding="utf-8") as fh:
        json.dump({"base_config": args.base_config, "capture": args.capture,
                   "only": args.only, "time_window": args.time_window,
                   "dist_deg": args.dist_deg, "bands": list(bands),
                   "results": results}, fh, indent=2)
    print("escrito: %s" % out)
    return print_comparison(results, args.time_window, args.dist_deg, bands)


# --------------------------------------------------------------------------
# Selftest
# --------------------------------------------------------------------------
def selftest():
    import tempfile
    d = tempfile.mkdtemp(prefix="calibrate_csnloc_selftest_")
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("ok  : " if cond else "FAIL: ") + msg)
        if not cond:
            ok = False

    base = os.path.join(d, "base.d")
    with open(base, "w", encoding="utf-8") as fh:
        fh.write("MyModuleId        MOD_CSNLOC        # ID\n"
                 "AssocWindowSec    120.0            # ventana\n"
                 "# DepthPriorKm     0.0             # a priori\n"
                 "# TauTable acepta \"iasp91\" o \"iasp91.tbl\"; deben existir .tbl y .hed\n"
                 "LocalGrid         grids/uno.grid\n"
                 "LocalGrid         grids/dos.grid\n")

    out = materialize(base, {"AssocWindowSec": 60.0, "DepthPriorKm": 30.0,
                             "NuevaClave": 7}, os.path.join(d, "out.d"))
    txt = open(out, encoding="utf-8").read()
    check(re.search(r"^AssocWindowSec\s+60\.0\s*$", txt, re.M) is not None,
          "override de clave presente")
    check("120.0" not in txt, "valor viejo sustituido")
    check(re.search(r"^DepthPriorKm\s+30\.0\s*$", txt, re.M) is not None
          and "# DepthPriorKm" not in txt,
          "clave comentada -> descomentada")
    check("TauTable acepta" in txt, "prosa con nombre de clave NO se toca")
    check(txt.count("LocalGrid") == 2, "claves repetidas se preservan")
    check("NuevaClave" in txt, "clave ausente se anade")

    vf = os.path.join(d, "v.json")
    with open(vf, "w", encoding="utf-8") as fh:
        json.dump({"variants": [
            {"tag": "base", "overrides": {}},
            {"tag": "a1", "overrides": {"MaxRMS": 1.0}},
            "solo_tag",
        ]}, fh)
    vs = load_variants(vf)
    check([v["tag"] for v in vs] == ["base", "a1", "solo_tag"],
          "load_variants: dict/list y string")
    check(filter_tags(vs, "a*")[0]["tag"] == "a1", "filter_tags glob")

    shutil.rmtree(d, ignore_errors=True)
    print()
    print("%s selftest calibrate_csnloc" % ("OK" if ok else "FALLO"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
