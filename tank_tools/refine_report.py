#!/usr/bin/env python3
"""refine_report.py - Reporte en TEXTO PLANO de UNA corrida de un refinador.

Escribe, junto a la carpeta de validacion de refine_arcs.py:

    <validacion>/refine_report.txt

con tres bloques:

    1. PARAMETROS   claves del .d del refinador y, si se puede resolver, el
                    contenido del .hyp (hyp2000) o de la plantilla de control
                    de NLLoc.
    2. EVENTOS      TODOS los ARC del suelo, con el CRUDO al lado del refinado
                    y las diferencias (dz, dd, drms).
    3. RESUMEN      conteos (refinados / descartados por filtro / sin
                    localizacion / error) y estadisticas de las diferencias.

Uso:
    refine_report.py <validacion> [--baseline DIR] [--out FILE] [--selftest]
"""
import argparse
import glob
import json
import os
import sys
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from offline_report import KM_PER_DEG, gc_deg  # noqa: E402
import refine_arcs  # noqa: E402


def fnum(v, nd=1):
    if v is None:
        return "-"
    try:
        return "%.*f" % (nd, float(v))
    except (TypeError, ValueError):
        return str(v)


def median(xs):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return None
    return xs[len(xs) // 2]


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
# Lectura
# --------------------------------------------------------------------------
def load_manifest(val_dir):
    path = os.path.join(val_dir, "manifest.json")
    if not os.path.isfile(path):
        return {}
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, json.JSONDecodeError):
        return {}


def _ctrl_path(d_keys):
    """La plantilla de control que se uso: ControlFile o el ctl de ModelBand."""
    ctl = ""
    for k, v in d_keys:
        if k == "ControlFile":
            ctl = v
        elif k == "ModelBand":
            parts = v.split()
            if len(parts) >= 7:
                ctl = parts[-1]
    return ctl


def read_lines(path, limit=200):
    if not path or not os.path.isfile(path):
        return []
    out = []
    with open(path, "r", encoding="latin-1", errors="replace") as fh:
        for line in fh:
            out.append(line.rstrip("\n"))
            if len(out) >= limit:
                out.append("... (truncado)")
                break
    return out


def collect(val_dir, baseline_dir, minphases=0, maxrms=0.0, only="*"):
    """Filas crudo<->refinado para los ARC del suelo que estuvieron en alcance."""
    import fnmatch

    ref_dir = os.path.join(val_dir, "arcs_ref")
    rows = []
    n_ok = n_drop = n_noloc = 0
    for ap in sorted(glob.glob(os.path.join(baseline_dir, "*.arc"))):
        base = os.path.basename(ap)
        if only != "*" and not fnmatch.fnmatch(refine_arcs.arc_slug(ap), only):
            continue
        with open(ap, "r", encoding="latin-1", errors="replace") as fh:
            raw = refine_arcs.arc_header(fh.read())
        rp = os.path.join(ref_dir, base)
        ref = None
        if os.path.isfile(rp):
            with open(rp, "r", encoding="latin-1", errors="replace") as fh:
                ref = refine_arcs.arc_header(fh.read())
        if ref is not None:
            estado = "ok"
            n_ok += 1
        else:
            if raw is not None and (
                    (minphases > 0 and raw["nphases"] < minphases) or
                    (maxrms > 0.0 and raw["rms_sec"] is not None
                     and raw["rms_sec"] > maxrms)):
                estado = "descartado"
                n_drop += 1
            else:
                estado = "sin_refinar"
                n_noloc += 1
        dz = dd = drms = None
        if ref is not None and raw is not None:
            if ref["depth_km"] is not None and raw["depth_km"] is not None:
                dz = ref["depth_km"] - raw["depth_km"]
            if all(ref.get(k) is not None for k in ("lat", "lon")) and \
               all(raw.get(k) is not None for k in ("lat", "lon")):
                dd = gc_deg(raw["lat"], raw["lon"], ref["lat"], ref["lon"]) * KM_PER_DEG
            if ref["rms_sec"] is not None and raw["rms_sec"] is not None:
                drms = ref["rms_sec"] - raw["rms_sec"]
        rows.append({"arc": base, "slug": refine_arcs.arc_slug(ap),
                     "estado": estado, "raw": raw, "ref": ref,
                     "dz": dz, "dd": dd, "drms": drms})
    return rows, {"n_ok": n_ok, "n_drop": n_drop, "n_noloc": n_noloc}


def build_report(val_dir, baseline_dir=None, out_path=None):
    manifest = load_manifest(val_dir)
    if not baseline_dir:
        baseline_dir = manifest.get("baseline_dir", "")
    filters = manifest.get("filters") or {}
    minph = int(filters.get("MinPhases") or 0)
    maxrms = float(filters.get("MaxRMS") or 0.0)

    rows, counts = collect(val_dir, baseline_dir, minph, maxrms,
                           manifest.get("only", "*"))
    d_keys = [tuple(k) for k in (manifest.get("d_keys") or [])]

    L = []
    L.append("=" * 100)
    L.append("REPORTE DE CORRIDA - refinador")
    L.append("=" * 100)
    L.append("validacion : %s" % val_dir)
    L.append("baseline   : %s" % (baseline_dir or "?"))
    L.append("binario    : %s" % manifest.get("refiner", "?"))
    L.append("sha256     : %s" % (manifest.get("refiner_sha256") or "?"))
    L.append("config     : %s" % manifest.get("config", "?"))
    L.append("config sha : %s" % (manifest.get("config_sha256") or "?"))
    L.append("WorkDir    : %s" % manifest.get("workdir", ""))
    L.append("filtros    : MinPhases %d  MaxRMS %.2f" % (minph, maxrms))
    L.append("only       : %s" % manifest.get("only", "*"))
    L.append("generado   : %s" % datetime.now(timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ"))

    # 1) parametros
    L.append("")
    L.append("-" * 100)
    L.append("1. PARAMETROS DEL REFINADOR")
    L.append("-" * 100)
    if d_keys:
        w = max(len(k) for k, _ in d_keys)
        for k, v in d_keys:
            L.append("  %-*s  %s" % (w, k, v))
    else:
        L.append("  (sin claves en el manifest)")

    cmd = manifest.get("command_file", "")
    wd = manifest.get("workdir", "")
    body = os.path.join(wd, cmd) if (wd and cmd) else ""
    if not body or not os.path.isfile(body):
        body = _ctrl_path(d_keys)
    if body and os.path.isfile(body):
        L.append("")
        L.append("  --- %s ---" % body)
        for line in read_lines(body):
            L.append("  %s" % line)
    else:
        L.append("")
        L.append("  (no encontre el .hyp ni la plantilla de control)")

    # 2) eventos
    L.append("")
    L.append("-" * 100)
    L.append("2. EVENTOS (%d ARC en el suelo)" % len(rows))
    L.append("-" * 100)
    trows = []
    for r in rows:
        ref, raw = r["ref"], r["raw"]
        if ref is None:
            head = ["-", "-", "-", "-", "-"]
        else:
            head = [fnum(ref["lat"], 3), fnum(ref["lon"], 3),
                    fnum(ref["depth_km"], 1), ref["nphases"],
                    fnum(ref["rms_sec"], 2)]
        if raw is None:
            tail = ["-", "-", "-", "-"]
        else:
            tail = [fnum(raw["depth_km"], 1), raw["nphases"],
                    fnum(raw["rms_sec"], 2), fnum(r["dz"], 1)]
        trows.append([r["slug"], r["arc"], r["estado"]] + head + tail +
                     [fnum(r["dd"], 1), fnum(r["drms"], 2)])
    L += table(["test", "arc", "estado", "lat", "lon", "z_km", "nph", "rms",
                "z_crudo", "nph_crudo", "rms_crudo", "dz_km", "dd_km",
                "drms"], trows)

    # 3) resumen
    ok = [r for r in rows if r["ref"] is not None and r["raw"] is not None]
    dzs = [r["dz"] for r in ok if r["dz"] is not None]
    dds = [r["dd"] for r in ok if r["dd"] is not None]
    zraw = [r["raw"]["depth_km"] for r in ok if r["raw"]["depth_km"] is not None]
    zref = [r["ref"]["depth_km"] for r in ok if r["ref"]["depth_km"] is not None]
    rraw = [r["raw"]["rms_sec"] for r in ok if r["raw"]["rms_sec"] is not None]
    rref = [r["ref"]["rms_sec"] for r in ok if r["ref"]["rms_sec"] is not None]

    L.append("")
    L.append("-" * 100)
    L.append("3. RESUMEN")
    L.append("-" * 100)
    L.append("  ARC en el suelo            : %d" % len(rows))
    L.append("  refinados                  : %d" % counts["n_ok"])
    L.append("  descartados por filtro     : %d" % counts["n_drop"])
    L.append("  sin localizacion           : %d" % counts["n_noloc"])
    if dzs:
        adz = sorted(abs(d) for d in dzs)
        L.append("  comparables (crudo y refin) : %d" % len(dzs))
        L.append("  dz = z_refin - z_crudo (km): media %+.2f  mediana %+.2f  "
                 "mediana|dz| %.2f" % (sum(dzs) / len(dzs), median(dzs),
                                       adz[len(adz) // 2]))
        L.append("    |dz| <= 5 km: %d   <= 15 km: %d   > 50 km: %d"
                 % (sum(1 for d in adz if d <= 5),
                    sum(1 for d in adz if d <= 15),
                    sum(1 for d in adz if d > 50)))
        L.append("  z mediana: crudo %s -> refinado %s"
                 % (fnum(median(zraw), 1), fnum(median(zref), 1)))
    if dds:
        add = sorted(dds)
        L.append("  desplazamiento epicentral (km): mediana %.1f  <= 5 km: %d   "
                 "<= 15 km: %d" % (add[len(add) // 2],
                                   sum(1 for d in add if d <= 5),
                                   sum(1 for d in add if d <= 15)))
    if rraw and rref:
        L.append("  rms medio: crudo %.3f -> refinado %.3f"
                 % (sum(rraw) / len(rraw), sum(rref) / len(rref)))
    L.append("")

    txt = "\n".join(L)
    path = out_path or os.path.join(val_dir, "refine_report.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(txt)
    return txt, path


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    ap = argparse.ArgumentParser(add_help=False, description=__doc__.split("\n")[0])
    ap.add_argument("validation", nargs="?")
    ap.add_argument("--baseline")
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
        print("refine_report: ERROR: falta o no existe <validacion>", file=sys.stderr)
        return 1
    txt, path = build_report(a.validation, a.baseline, a.out)
    if not a.quiet:
        print(txt)
    print("escrito: %s" % path, file=sys.stderr)
    return 0


# --------------------------------------------------------------------------
# Selftest
# --------------------------------------------------------------------------
def selftest():
    import shutil
    import tempfile

    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("ok  : " if cond else "FAIL: ") + msg)
        if not cond:
            ok = False

    d = tempfile.mkdtemp(prefix="refine_report_selftest_")
    try:
        base = os.path.join(d, "arcs")
        val = os.path.join(d, "val")
        refd = os.path.join(val, "arcs_ref")
        for p in (base, val, refd):
            os.makedirs(p)

        raw = refine_arcs._mk_arc(2026, 7, 22, 2, 3, 4900, -19.36, -70.3185,
                                  2750, 6, 178, 33, 121,
                                  [("PSGCX", "CX", "HHZ", "P")] * 6)
        ref = refine_arcs._mk_arc(2026, 7, 22, 2, 3, 4900, -19.50, -70.40,
                                  4200, 5, 178, 40, 60,
                                  [("PSGCX", "CX", "HHZ", "P")] * 5)
        for name in ("test1_00.arc", "test1_01.arc"):
            with open(os.path.join(base, name), "w", encoding="latin-1") as fh:
                fh.write(raw)
        # _02 con menos fases que MinPhases=5 -> lo descarta el filtro
        raw_low = refine_arcs._mk_arc(2026, 7, 22, 2, 3, 4900, -19.36, -70.3185,
                                      2750, 4, 178, 33, 121,
                                      [("PSGCX", "CX", "HHZ", "P")] * 4)
        with open(os.path.join(base, "test1_02.arc"), "w",
                  encoding="latin-1") as fh:
            fh.write(raw_low)
        # _00 refinado, _01 sin refinar, _02 descartado por MinPhases
        with open(os.path.join(refd, "test1_00.arc"), "w",
                  encoding="latin-1") as fh:
            fh.write(ref)
        with open(os.path.join(val, "manifest.json"), "w", encoding="utf-8") as fh:
            json.dump({"kind": "refined", "refiner": "/bin/hyp2000_ring",
                       "refiner_sha256": "abc", "config": "/x/hyp2000_ring.d",
                       "config_sha256": "def",
                       "d_keys": [["MyModuleId", "MOD_HYP2000_RING"],
                                  ["WorkDir", d], ["MinPhases", "5"]],
                       "workdir": d, "command_file": "no_existe.hyp",
                       "baseline_dir": base, "only": "*",
                       "filters": {"MinPhases": 5, "MaxRMS": 0.0},
                       "tanks": [{"slug": "test1"}]}, fh)

        txt, path = build_report(val, base, os.path.join(d, "rep.txt"))
        check(os.path.isfile(path), "build_report: escribe el archivo")
        for want in ("1. PARAMETROS", "2. EVENTOS", "3. RESUMEN"):
            check(want in txt, "bloque presente: %s" % want)
        check("MOD_HYP2000_RING" in txt and "MinPhases" in txt,
              "parametros del .d impresos")
        check("(no encontre el .hyp ni la plantilla de control)" in txt,
              "avisa si falta el .hyp")
        check("descartado" in txt and "sin_refinar" in txt,
              "estados por ARC")
        check("  refinados                  : 1" in txt, "conteo de refinados")
        check("  descartados por filtro     : 1" in txt, "conteo de descartados")
        check("  sin localizacion           : 1" in txt, "conteo sin localizacion")
        check("comparables (crudo y refin) : 1" in txt, "comparables")
        check("dz = z_refin - z_crudo (km): media +14.50" in txt,
              "dz calculado (42.00 - 27.50)")
        check("z mediana: crudo 27.5 -> refinado 42.0" in txt, "medianas de z")
        check("rms medio: crudo 1.210 -> refinado 0.600" in txt, "rms medio")
    finally:
        shutil.rmtree(d, ignore_errors=True)

    print()
    print("%s selftest refine_report" % ("OK" if ok else "FALLO"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
