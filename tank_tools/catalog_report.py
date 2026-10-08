#!/usr/bin/env python3
"""catalog_report.py - Cruce de UNA corrida de csnloc contra el catalogo de referencia.

Escribe, junto a la carpeta de validacion:

    <validacion>/catalog_report.txt

con tres bloques:

    1. EVENTOS DEL ARCHIVO   una fila por evento de tests_soluciones_publicadas.dat,
                             con la solucion localizada mas parecida al lado y las
                             diferencias (km, s, km). Si no hubo solucion, "(sin solucion)".
    2. ADICIONALES           las otras soluciones de cada test (las que NO son el evento
                             del archivo). Son un "plus" a revisar: no se penalizan.
    3. RESUMEN               cuantos eventos del archivo aparecen, cuantos a <=25/50/100 km,
                             la mediana del desvio y cuantos adicionales.

El catalogo es una REFERENCIA para mirar a ojo si los eventos aparecen y que tan
parecidos son; NO es una metrica absoluta.

Uso:
    catalog_report.py <validacion> --catalog FILE [--out FILE] [--selftest]

    --time-window T   ventana para elegir "la mas parecida" en tiempo (default 30 s)
    --dist-deg D      idem en espacio (default 1.0 deg; ~111 km)
    --bands L         bandas de distancia del resumen (default 25,50,100)
"""

import argparse
import math
import os
import re
import sys
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from offline_report import KM_PER_DEG, gc_deg, has_geo, load_dir  # noqa: E402

BANDS = (25.0, 50.0, 100.0)


# --------------------------------------------------------------------------
# Catalogo
# --------------------------------------------------------------------------
def parse_iso_utc(s):
    s = (s or "").strip()
    if not s:
        return None
    try:
        dt = datetime.fromisoformat(s.replace("Z", "+00:00"))
    except ValueError:
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return dt.timestamp()


def load_catalog(path):
    """TSV -> {slug: Ref}. La clave es 'test<N_test>'."""
    out = {}
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            parts = [p.strip() for p in s.split("\t")]
            if len(parts) < 5:
                parts = s.split()
            if len(parts) < 5 or not parts[0].isdigit():
                continue
            try:
                lat, lon, dep = float(parts[2]), float(parts[3]), float(parts[4])
            except ValueError:
                continue
            n = int(parts[0])
            out["test%d" % n] = {
                "n": n, "t0": parse_iso_utc(parts[1]), "t0_utc": parts[1],
                "lat": lat, "lon": lon, "depth_km": dep,
                "mag": parts[5] if len(parts) > 5 else None,
                "mag_type": parts[6] if len(parts) > 6 else None,
            }
    return out


# --------------------------------------------------------------------------
# Cruce
# --------------------------------------------------------------------------
def slug_key(slug):
    m = re.match(r"^([A-Za-z_]+)(\d+)$", slug)
    return (m.group(1), int(m.group(2))) if m else (slug, -1)


def fnum(v, nd=1):
    if v is None:
        return "-"
    try:
        return "%.*f" % (nd, float(v))
    except (TypeError, ValueError):
        return str(v)


def _median(xs):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return None
    return xs[len(xs) // 2]


def fmt_t0(rec):
    if rec.get("t0_utc"):
        return rec["t0_utc"]
    if "t0" in rec:
        return datetime.fromtimestamp(rec["t0"], tz=timezone.utc).strftime(
            "%Y-%m-%dT%H:%M:%S") + "Z"
    return "?"


def _deltas(rec, ref):
    """(dt_s, d_km) entre una solucion y la referencia; (None, None) si falta algo."""
    # OJO: hay que validar LOS DOS lados. El catalogo puede traer una fila sin
    # lat/lon y un ARC refinado puede salir con el header malformado.
    if not ref or not has_geo(rec) or not has_geo(ref):
        return None, None
    dt = abs(rec["t0"] - ref["t0"])
    dd = gc_deg(rec["lat"], rec["lon"], ref["lat"], ref["lon"]) * KM_PER_DEG
    return dt, dd


def _best(recs, ref, T, D):
    """La solucion mas parecida a la referencia (tiempo+espacio normalizado).

    Sin referencia, la ultima generada. Devuelve (rec, dt_s, d_km, dz_km).
    """
    if not recs:
        return None, None, None, None

    def score(r):
        dt, d = _deltas(r, ref)
        if dt is None:
            return 1e18
        return dt / T + d / D

    best = min(recs, key=score) if ref else recs[-1]
    dt, d = _deltas(best, ref)
    dz = None
    if ref and best.get("depth_km") is not None and ref.get("depth_km") is not None:
        dz = float(best["depth_km"]) - float(ref["depth_km"])
    return best, dt, d, dz


def cross_dir(vdir, catalog, T=30.0, D=1.0, bands=BANDS, baseline_dir=None):
    """Cruza la corrida con el catalogo. Devuelve dict con rows/extra/summary.

    Con `baseline_dir` se cruza tambien el CRUDO (p. ej. los ARC de csnloc antes
    de refinar): cada fila lleva al lado la solucion del crudo y su distancia.
    """
    _, by_slug, _ = load_dir(vdir)
    base_by_slug = {}
    if baseline_dir:
        _, base_by_slug, _ = load_dir(baseline_dir)
    # Solo los slugs del catalogo (referencia) mas los que tienen solucion: asi
    # un --only sobre un subconjunto no arrastra el resto del baseline.
    slugs = sorted(set(catalog) | set(by_slug), key=slug_key)

    rows, extra = [], []
    n_sol_total = 0
    for slug in slugs:
        ref = catalog.get(slug)
        recs = by_slug.get(slug, [])
        n_sol_total += len(recs)

        best, dt, d, dz = _best(recs, ref, T, D)
        row = {"slug": slug, "n": len(recs), "ref": ref, "loc": best,
               "dt": dt, "d_km": d, "dz": dz,
               "base": None, "dt_base": None, "d_km_base": None, "dz_base": None}
        if baseline_dir:
            b, bdt, bd, bdz = _best(base_by_slug.get(slug, []), ref, T, D)
            row.update({"base": b, "dt_base": bdt, "d_km_base": bd, "dz_base": bdz})
        rows.append(row)

        for r in recs:
            if r is best:
                continue
            edt, ed = _deltas(r, ref)
            extra.append({"slug": slug, "rec": r, "dt": edt, "d_km": ed})

    con_sol = [r for r in rows if r["loc"] is not None]
    dist = [r["d_km"] for r in con_sol if r["d_km"] is not None]
    dzs = [abs(r["dz"]) for r in rows if r.get("dz") is not None]
    summary = {
        "n_ref": len(catalog),
        "n_tanks": len(slugs),
        "n_con_sol": len(con_sol),
        "n_sin_sol": len(rows) - len(con_sol),
        "n_sol_total": n_sol_total,
        "n_extra": len(extra),
        "med_km": (sorted(dist)[len(dist) // 2] if dist else None),
        "bands": {b: sum(1 for d in dist if d <= b) for b in bands},
        "n_dz": len(dzs),
        "med_abs_dz": _median(dzs),
        "bands_dz": {b: sum(1 for d in dzs if d <= b) for b in bands},
    }
    if baseline_dir:
        bcon = [r for r in rows if r["base"] is not None]
        bdist = [r["d_km_base"] for r in bcon if r["d_km_base"] is not None]
        bdzs = [abs(r["dz_base"]) for r in rows if r.get("dz_base") is not None]
        summary.update({
            "n_con_sol_base": len(bcon),
            "med_km_base": (sorted(bdist)[len(bdist) // 2] if bdist else None),
            "bands_base": {b: sum(1 for d in bdist if d <= b) for b in bands},
            "n_dz_base": len(bdzs),
            "med_abs_dz_base": _median(bdzs),
            "bands_dz_base": {b: sum(1 for d in bdzs if d <= b) for b in bands},
        })
    return {"rows": rows, "extra": extra, "summary": summary}


def summarize(vdir, catalog, T=30.0, D=1.0, baseline_dir=None):
    """Solo el resumen (para el barrido). `catalog` puede ser un dict o una ruta."""
    if not isinstance(catalog, dict):
        catalog = load_catalog(catalog)
    return cross_dir(vdir, catalog, T, D, baseline_dir=baseline_dir)["summary"]


# --------------------------------------------------------------------------
# Texto
# --------------------------------------------------------------------------
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


def build_text(vdir, catalog, T=30.0, D=1.0, bands=BANDS, baseline_dir=None):
    res = cross_dir(vdir, catalog, T, D, bands, baseline_dir)
    rows, extra, s = res["rows"], res["extra"], res["summary"]

    L = []
    L.append("=" * 100)
    L.append("CRUCE CON EL CATALOGO DE REFERENCIA")
    L.append("=" * 100)
    L.append("validacion : %s" % vdir)
    if baseline_dir:
        L.append("baseline   : %s  (el crudo, antes de refinar)" % baseline_dir)
    L.append("catalogo   : %d eventos del archivo" % s["n_ref"])
    L.append("generado   : %s" % datetime.now(timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ"))
    L.append("nota       : el catalogo es una REFERENCIA para mirar a ojo, no una "
             "metrica absoluta")

    # 1) eventos del archivo
    L.append("")
    L.append("-" * 100)
    L.append("1. EVENTOS DEL ARCHIVO (la solucion localizada mas parecida)")
    L.append("-" * 100)
    trows = []
    for r in rows:
        ref = r["ref"]
        if ref is None:
            head = ["-", "-", "-", "-"]
        else:
            head = [ref["t0_utc"], fnum(ref["lat"], 3), fnum(ref["lon"], 3),
                    fnum(ref["depth_km"], 1)]
        base_cols = []
        if baseline_dir:
            b = r["base"]
            base_cols = ["-" if b is None else fnum(b.get("depth_km"), 1),
                         fnum(r["d_km_base"], 1), fnum(r["dz_base"], 1)]
        if r["loc"] is None:
            trows.append([r["slug"], r["n"]] + head +
                         ["(sin solucion)", "", "", "", "", "", ""] + base_cols)
            continue
        loc = r["loc"]
        trows.append([r["slug"], r["n"]] + head +
                     [fmt_t0(loc), fnum(loc.get("lat"), 3),
                      fnum(loc.get("lon"), 3), fnum(loc.get("depth_km"), 1),
                      fnum(r["d_km"], 1), fnum(r["dt"], 1), fnum(r["dz"], 1)]
                     + base_cols)
    headers = ["test", "n", "ref_t0", "ref_lat", "ref_lon", "ref_z",
               "loc_t0", "loc_lat", "loc_lon", "loc_z", "d_km", "dt_s", "dz_km"]
    if baseline_dir:
        headers += ["base_z", "base_d_km", "dz_base"]
    L += table(headers, trows)

    # 2) adicionales
    L.append("")
    L.append("-" * 100)
    L.append("2. ADICIONALES - soluciones que no son el evento del archivo "
             "(plus a revisar, %d)" % len(extra))
    L.append("-" * 100)
    erows = []
    for e in sorted(extra, key=lambda e: (slug_key(e["slug"]),
                                          e["rec"].get("event") or 0)):
        r = e["rec"]
        erows.append([e["slug"], r.get("event", "-"), fmt_t0(r),
                      fnum(r.get("lat"), 3), fnum(r.get("lon"), 3),
                      fnum(r.get("depth_km"), 1), r.get("nphases", "-"),
                      fnum(e["d_km"], 1), fnum(e["dt"], 1)])
    L += table(["test", "ev", "t0 (UTC)", "lat", "lon", "z_km", "nph", "d_ref_km",
                "dt_ref_s"], erows)

    # 3) resumen
    L.append("")
    L.append("-" * 100)
    L.append("3. RESUMEN")
    L.append("-" * 100)
    L.append("  eventos del archivo             : %d" % s["n_ref"])
    L.append("  con alguna solucion             : %d" % s["n_con_sol"])
    for b in bands:
        L.append("    - con el evento a <= %3d km  : %d" % (b, s["bands"][b]))
    L.append("  sin ninguna solucion            : %d" % s["n_sin_sol"])
    L.append("  mediana del desvio (km)         : %s" % fnum(s["med_km"], 1))
    L.append("  |dz| vs publicado (km)          : %s  (mediana sobre %d)"
             % (fnum(s.get("med_abs_dz"), 1), s.get("n_dz", 0)))
    L.append("  soluciones totales              : %d" % s["n_sol_total"])
    L.append("  adicionales (plus a revisar)    : %d" % s["n_extra"])
    if baseline_dir:
        L.append("")
        L.append("  --- CRUDO (antes de refinar) ---")
        L.append("  con alguna solucion             : %d" % s["n_con_sol_base"])
        for b in bands:
            L.append("    - con el evento a <= %3d km  : %d"
                     % (b, s["bands_base"][b]))
        L.append("  mediana del desvio (km)         : %s"
                 % fnum(s["med_km_base"], 1))
        L.append("  |dz| vs publicado (km)          : %s  (mediana sobre %d)"
                 % (fnum(s.get("med_abs_dz_base"), 1),
                    s.get("n_dz_base", 0)))
    L.append("")
    return "\n".join(L)


def write_report(vdir, catalog, out=None, T=30.0, D=1.0, bands=BANDS,
                 baseline_dir=None):
    """`catalog` puede ser un dict {slug: Ref} o una ruta al .dat."""
    if not isinstance(catalog, dict):
        catalog = load_catalog(catalog)
    txt = build_text(vdir, catalog, T, D, bands, baseline_dir)
    path = out or os.path.join(vdir, "catalog_report.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(txt)
    return txt, path


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    ap = argparse.ArgumentParser(add_help=False,
                                 description="Cruce de csnloc vs catalogo.")
    ap.add_argument("validation", nargs="?")
    ap.add_argument("--catalog")
    ap.add_argument("--out")
    ap.add_argument("--time-window", type=float, default=30.0)
    ap.add_argument("--dist-deg", type=float, default=1.0)
    ap.add_argument("--bands", default=",".join(str(int(b)) for b in BANDS))
    ap.add_argument("--baseline",
                    help="carpeta con el CRUDO (p. ej. la validacion de csnloc) "
                         "para ponerlo al lado del refinado")
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
        print("catalog_report: ERROR: falta o no existe <validacion>",
              file=sys.stderr)
        return 1
    if not a.catalog or not os.path.isfile(a.catalog):
        print("catalog_report: ERROR: falta --catalog FILE", file=sys.stderr)
        return 1
    bands = tuple(float(x) for x in a.bands.split(",") if x.strip())

    txt, path = write_report(a.validation, a.catalog, a.out, a.time_window,
                             a.dist_deg, bands, a.baseline)
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

    d = tempfile.mkdtemp(prefix="catalog_report_selftest_")
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("ok  : " if cond else "FAIL: ") + msg)
        if not cond:
            ok = False

    cat = os.path.join(d, "cat.dat")
    with open(cat, "w", encoding="utf-8") as fh:
        fh.write("N_test\torigin_time\tlatitude\tlongitude\tdepth[km]\t"
                 "magnitude\tmagnitude_type\n")
        fh.write("1\t2026-03-02T19:00:22Z\t-38.65\t-74.62\t10\t4.2\tMLv\n")
        fh.write("2\t2026-03-02T23:55:54Z\t-28.58\t-71.68\t44.1\t4.4\tMLv\n")
        fh.write("3\t2026-03-03T21:30:29Z\t-32.25\t-72.14\t39.3\t4.5\tMLv\n")
    c = load_catalog(cat)
    check(len(c) == 3 and "test1" in c and "test3" in c,
          "catalogo: 3 filas, cabecera ignorada")

    vdir = os.path.join(d, "val")
    os.makedirs(vdir)
    t0 = c["test1"]["t0"]

    def ev(eid, t0s, lat, lon, z, nph=6):
        return json.dumps({
            "event": eid, "id": eid, "version": 1, "t0": t0s, "t0_utc": "",
            "lat": lat, "lon": lon, "depth_km": z, "nphases": nph,
            "rms_sec": 0.4, "gap_deg": 180.0, "dmin_km": 30.0, "score": 1.0,
            "grid_level": 2, "depth_ctrl": "bien",
            "phases": [{"phase": "P"}],
        })

    # test1: el evento (a ~5 km) + una adicional lejana
    with open(os.path.join(vdir, "test1.jsonl"), "w", encoding="utf-8") as fh:
        fh.write(ev(1, t0 + 2.0, -38.65, -74.62, 12.0) + "\n")
        fh.write(ev(2, t0 + 600.0, -30.0, -70.0, 90.0) + "\n")
    open(os.path.join(vdir, "test2.jsonl"), "w").close()          # sin solucion
    with open(os.path.join(vdir, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump({"tanks": [{"slug": "test1"}, {"slug": "test2"},
                             {"slug": "test3"}]}, fh)

    res = cross_dir(vdir, c, 30.0, 1.0, (25.0, 50.0, 100.0))
    s = res["summary"]
    check(s["n_ref"] == 3 and s["n_con_sol"] == 1, "resumen: 3 ref, 1 con solucion")
    check(s["n_sin_sol"] == 2, "resumen: 2 sin solucion")
    check(s["bands"][25.0] == 1 and s["bands"][100.0] == 1, "resumen: bandas")
    check(s["n_extra"] == 1, "resumen: 1 adicional")
    check(round(s["med_km"], 1) < 10.0, "resumen: mediana pequena")

    txt = build_text(vdir, c, 30.0, 1.0, (25.0, 50.0, 100.0))
    for want in ("1. EVENTOS DEL ARCHIVO", "2. ADICIONALES", "3. RESUMEN"):
        check(want in txt, "bloque presente: %s" % want)
    check("(sin solucion)" in txt, "test2 marcado sin solucion")
    check("con el evento a <=  25 km  : 1" in txt, "banda impresa")

    # --- baseline: el CRUDO al lado del refinado ---
    bdir = os.path.join(d, "base")
    os.makedirs(bdir)
    with open(os.path.join(bdir, "test1.jsonl"), "w", encoding="utf-8") as fh:
        fh.write(ev(0, t0 + 3.0, -38.2, -74.2, 9.0) + "\n")     # ~50 km del ref
    with open(os.path.join(bdir, "test3.jsonl"), "w", encoding="utf-8") as fh:
        fh.write(ev(0, c["test3"]["t0"] + 2.0, -32.25, -72.14, 40.0) + "\n")
    with open(os.path.join(bdir, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump({"tanks": [{"slug": "test1"}, {"slug": "test3"}]}, fh)

    sb = cross_dir(vdir, c, 30.0, 1.0, (25.0, 50.0, 100.0),
                   baseline_dir=bdir)["summary"]
    check(sb.get("n_con_sol_base") == 2, "baseline: 2 con solucion en el crudo")
    check(sb["bands_base"][25.0] == 1 and sb["med_km_base"] is not None,
          "baseline: bandas y mediana del crudo")

    txtb = build_text(vdir, c, 30.0, 1.0, (25.0, 50.0, 100.0), bdir)
    check("base_d_km" in txtb and "base_z" in txtb,
          "baseline: columnas del crudo")
    check("CRUDO (antes de refinar)" in txtb, "baseline: bloque del crudo")
    check("base_d_km" not in txt and "CRUDO" not in txt,
          "sin --baseline la salida no cambia (regresion)")

    # slug con solucion pero SIN fila en el catalogo -> no debe romper
    with open(os.path.join(vdir, "test9.jsonl"), "w", encoding="utf-8") as fh:
        fh.write(ev(0, t0 + 5.0, -38.6, -74.6, 11.0) + "\n")
    txt9 = build_text(vdir, c, 30.0, 1.0, (25.0, 50.0, 100.0))
    check("test9" in txt9, "slug fuera del catalogo: se lista igual")
    s9 = cross_dir(vdir, c, 30.0, 1.0, (25.0, 50.0, 100.0))["summary"]
    check(s9["n_con_sol"] == 2,
          "slug fuera del catalogo: cuenta como con solucion")

    # solucion con lat/lon nulos: el cruce NO debe romperse
    with open(os.path.join(vdir, "test8.jsonl"), "w", encoding="utf-8") as fh:
        fh.write('{"t0": %f, "t0_utc": "", "lat": null, "lon": null, '
                 '"depth_km": 5.0, "nphases": 3, "rms_sec": 0.5, "gap_deg": 200, '
                 '"dmin_km": 30, "event": 0, "id": "0", "phases": [], '
                 '"grid_level": "-", "depth_ctrl": "-"}\n' % t0)
    txt8 = build_text(vdir, c, 30.0, 1.0, (25.0, 50.0, 100.0))
    check("test8" in txt8, "solucion sin lat/lon: no rompe el cruce")

    shutil.rmtree(d, ignore_errors=True)
    print()
    print("%s selftest catalog_report" % ("OK" if ok else "FALLO"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
