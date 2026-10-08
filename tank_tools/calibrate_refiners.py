#!/usr/bin/env python3
"""calibrate_refiners.py - Barrido de parametros de hyp2000_ring / nlloc_ring.

Corre cada variante (un .d + su .hyp o su plantilla de control, en copias
AISLADAS) sobre el MISMO suelo de ARC y deja, en <work>/val/<tag>/:

    refine_report.txt    parametros del refinador + todos los eventos + Δ vs crudo
    catalog_report.txt   cruce con el catalogo (referencia) + el crudo al lado

Al final imprime una linea por variante para discriminar cual le apunta a mas.
Nunca toca los .d/.hyp/plantillas de produccion (verificado por sha256).

Uso:
    python3 tank_tools/calibrate_refiners.py --refiner hyp2000 \
        --baseline tmp/refine --catalog tests_soluciones_publicadas.dat \
        --work tmp/refine/hyp2000 --jobs 8
    python3 tank_tools/calibrate_refiners.py --refiner nlloc --dry-run ...

Opciones:
    --refiner hyp2000|nlloc   refinador a barrer (obligatorio)
    --baseline DIR            raiz del suelo (con arcs/ y <slug>.jsonl)
    --catalog FILE            (default tests_soluciones_publicadas.dat)
    --variants FILE           (default tank_tools/calib/<refiner>_variants.json)
    --work DIR                (default tmp/refine/<refiner>)
    --tags LIST               solo variantes cuyo tag case con algun glob (coma)
    --only GLOB               evaluar solo slugs que casen (default *)
    --jobs N                  variantes en paralelo (default 1)
    --time-window T           (default 30.0 s)   --dist-deg D  (default 1.0)
    --bands L                 bandas de distancia (default 25,50,100)
    --force                   rehacer las validaciones existentes
    --no-reports              no escribir refine_report/catalog_report
    --dry-run                 solo lista las variantes; no escribe nada
    --selftest                pruebas internas
    -h, --help                esta ayuda
"""
import argparse
import fnmatch
import json
import os
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import catalog_report  # noqa: E402
import refine_arcs  # noqa: E402
import refine_report  # noqa: E402
from offline_report import print_table  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PARAMS = os.path.join(REPO, "run_working_v8", "params")

BIN = {
    "hyp2000": os.path.join(REPO, "ew_gui_tools", "hyp2000_ring", "hyp2000_ring"),
    "nlloc": os.path.join(REPO, "ew_gui_tools", "nlloc_ring", "nlloc_ring"),
}
BASE_D = {
    "hyp2000": os.path.join(PARAMS, "hyp2000_ring.d"),
    "nlloc": os.path.join(PARAMS, "nlloc_ring.d"),
}
BASE_HYP = os.path.join(PARAMS, "hyp2000_ring.hyp")
SRC_WORKDIR = {
    "hyp2000": os.path.join(REPO, "tmp", "hyp2000_ring"),
    "nlloc": os.path.join(REPO, "tmp", "nlloc_ring"),
}
DEFAULT_CATALOG = os.path.join(REPO, "tests_soluciones_publicadas.dat")

_BAND_CRH = ["N18-26_1.5k", "N22-30_1.5k", "N26-34_1.5k",
             "N30-38_1.5k", "N34-42_1.5k", "N38-46_1.5k"]
_BAND_CRH_2DEG = [b + "_2deg" for b in _BAND_CRH]
# Regionalizacion por circulos NOD: se mezclan hasta 3 modelos con taper coseno
# (hytra.for). RAD1=450 km / DRAD=150 km: sin justificar por ningun analisis.
_BAND_TAIL = ("MUL T 3\n"
              "NOD -21.601 0 68.350 0 450. 150. 1\n"
              "NOD -25.975 0 68.692 0 450. 150. 2\n"
              "NOD -30.056 0 68.637 0 450. 150. 3\n"
              "NOD -33.994 0 70.685 0 450. 150. 4\n"
              "NOD -37.968 0 71.769 0 450. 150. 5\n"
              "NOD -41.954 0 73.065 0 450. 150. 6\n")


def _crh_block(files):
    return "".join("CRH %d '%s.crh'\n" % (i + 1, f) for i, f in enumerate(files))


MODELS = {
    "husen": "CRH 1 'chile_1d.crh'\n",
    "ak135": "CRH 1 'ak135.crh'\n",
    "bandas": _crh_block(_BAND_CRH) + _BAND_TAIL,
    # Mismos centros y radios, pero cada perfil promediado sobre +-2 grados en
    # vez de sobre toda la caja del modelo (~8x6 grados): region mas homogenea.
    "bandas2deg": _crh_block(_BAND_CRH_2DEG) + _BAND_TAIL,
}


# --------------------------------------------------------------------------
# Materializacion aislada
# --------------------------------------------------------------------------
def load_variants(path):
    """{"refiner": [...variantes]} o [variantes]. Cada una: {tag, d, hyp, ctrl}."""
    with open(path, "r", encoding="utf-8") as fh:
        data = json.load(fh)
    if isinstance(data, dict):
        for key in ("variants", "hyp2000", "nlloc"):
            if key in data:
                data = data[key]
                break
    if not isinstance(data, list):
        raise ValueError("variants: se esperaba una lista")
    for v in data:
        if not v.get("tag"):
            raise ValueError("variants: falta 'tag'")
    return data


def filter_tags(variants, tags):
    if not tags or tags == "*":
        return variants
    pats = [t.strip() for t in tags.split(",") if t.strip()]
    return [v for v in variants
            if any(fnmatch.fnmatch(v["tag"], p) for p in pats)]


def _key_of(line):
    s = line.strip()
    if not s or s[0] in "#*":
        return ""
    return s.split(None, 1)[0]


def rewrite_d(src, dst, overrides, drop=()):
    """Copia un .d reemplazando claves; `drop` elimina esas claves."""
    seen = set()
    with open(src, "r", encoding="latin-1", errors="replace") as fi, \
         open(dst, "w", encoding="latin-1") as fo:
        for line in fi:
            k = _key_of(line)
            if k and k in drop:
                continue
            if k and k in overrides:
                fo.write("%-16s%s\n" % (k, overrides[k]))
                seen.add(k)
                continue
            fo.write(line)
        for k, v in overrides.items():
            if k not in seen and k not in drop:
                fo.write("%-16s%s\n" % (k, v))
    return dst


def write_hyp(src_hyp, dst, variant):
    """Genera el .hyp a partir del de produccion, con modelo/escalares del set."""
    hyp = variant.get("hyp") or {}
    model = hyp.get("model")
    scalars = dict(hyp.get("scalars") or {})
    drop = {"CRH", "MUL", "NOD"} if model else set()

    src = []
    with open(src_hyp, "r", encoding="latin-1", errors="replace") as fh:
        for line in fh:
            src.append(line.rstrip("\n"))
    src_keys = set(_key_of(l) for l in src)

    out, inserted = [], False
    for line in src:
        k = _key_of(line)
        if k and k in drop:
            continue
        if model and not inserted and k == "STA":
            out.append(line)
            out += MODELS[model].rstrip("\n").split("\n")
            inserted = True
            continue
        if k and k in scalars:
            out.append("%s %s" % (k, scalars[k]))
            continue
        out.append(line)
    if model and not inserted:
        out += MODELS[model].rstrip("\n").split("\n")
    # Los escalares que NO estaban en el .hyp de origen van al FINAL, despues del
    # bloque de modelo. HYPOINVERSE despacha `CRH` en linea (hypoinv.for:287-291)
    # y `hycmd.for:425` (POS) hace `DO I=1,LM`: emitido ANTES del CRH, `LM` sigue
    # en 0 y POS no tiene ningun efecto. Medido: POS 1.65/1.85 antes del CRH dan
    # resultados identicos byte a byte.
    for k2, v2 in scalars.items():
        if k2 not in src_keys:
            out.append("%s %s" % (k2, v2))
    with open(dst, "w", encoding="latin-1") as fh:
        fh.write("\n".join(out) + "\n")
    return dst


def scale_locgrid(line, k):
    """Engrosa la grilla de densidad de NLLoc por factor k, CONSERVANDO el centro.

    LOCGRID <nx> <ny> <nz> <x0> <y0> <z0> <dx> <dy> <dz> PROB_DENSITY SAVE
    """
    f = line.split()
    if len(f) < 10:
        return line
    try:
        nx, ny, nz = int(f[1]), int(f[2]), int(f[3])
        x0, y0, z0 = float(f[4]), float(f[5]), float(f[6])
        dx, dy, dz = float(f[7]), float(f[8]), float(f[9])
    except ValueError:
        return line
    rest = f[10:]
    out = []
    for n, o, d in ((nx, x0, dx), (ny, y0, dy), (nz, z0, dz)):
        n2 = max(1, (n + k - 1) // k)
        c = o + (n - 1) * d / 2.0
        out.append((n2, c - (n2 - 1) * (d * k) / 2.0, d * k))
    (nx2, x2, dx2), (ny2, y2, dy2), (nz2, z2, dz2) = out
    return ("LOCGRID %d %d %d %g %g %g %g %g %g %s"
            % (nx2, ny2, nz2, x2, y2, z2, dx2, dy2, dz2, " ".join(rest)))


def apply_ctrl(src, dst, overrides, grid_scale=0):
    with open(src, "r", encoding="latin-1", errors="replace") as fi, \
         open(dst, "w", encoding="latin-1") as fo:
        for line in fi:
            k = _key_of(line)
            if k == "LOCGRID" and grid_scale > 1:
                fo.write(scale_locgrid(line, grid_scale) + "\n")
            elif k and k in overrides:
                fo.write("%s %s\n" % (k, overrides[k]))
            else:
                fo.write(line)
    return dst


def materialize_hyp2000(variant, cfg_dir):
    """WorkDir aislado (.hyp + .crh + .sta) y .d con WorkDir apuntando ahi."""
    tag = variant["tag"]
    vdir = os.path.join(cfg_dir, tag)
    wd = os.path.join(vdir, "wd")
    os.makedirs(wd, exist_ok=True)
    src = SRC_WORKDIR["hyp2000"]
    if not os.path.isdir(src):
        raise OSError("no existe el WorkDir origen %s" % src)
    import glob as _glob
    import shutil
    # Los .crh/estaciones viven en el WorkDir, pero ak135.crh solo esta en
    # params/: copiamos de ambos para que todas las variantes sean posibles.
    for pat in ("*.crh", "estaciones_hyp.sta"):
        for src_dir in (src, PARAMS):
            for f in _glob.glob(os.path.join(src_dir, pat)):
                dstf = os.path.join(wd, os.path.basename(f))
                if not os.path.exists(dstf):
                    shutil.copy2(f, dstf)
    if not _glob.glob(os.path.join(wd, "*.crh")):
        raise OSError("el WorkDir origen no tiene .crh: %s" % src)
    write_hyp(BASE_HYP, os.path.join(wd, "hyp2000_ring.hyp"), variant)
    over = dict(variant.get("d") or {})
    over["WorkDir"] = wd
    over["CommandFile"] = "hyp2000_ring.hyp"
    return rewrite_d(BASE_D["hyp2000"], os.path.join(vdir, tag + ".d"), over)


def materialize_nlloc(variant, cfg_dir):
    """WorkDir/OutRoot aislados y, si el set cambia la plantilla, copia del ctrl."""
    tag = variant["tag"]
    vdir = os.path.join(cfg_dir, tag)
    wd = os.path.join(vdir, "wd")
    os.makedirs(os.path.join(wd, "loc"), exist_ok=True)

    over = dict(variant.get("d") or {})
    bands = over.pop("bands", True)
    over["WorkDir"] = wd
    over["OutRoot"] = os.path.join(wd, "loc", "ev")
    drop = () if bands else ("ModelBand",)
    ctrl_over = dict(variant.get("ctrl") or {})
    grid_scale = int(variant.get("ctrl_grid_scale") or 0)

    src_ctls = []
    for k, v in refine_arcs.read_d(BASE_D["nlloc"]):
        if k == "ControlFile":
            src_ctls.append(v)
        elif k == "ModelBand":
            parts = v.split()
            if len(parts) >= 7:
                src_ctls.append(parts[-1])

    ctl_map = {}
    if ctrl_over or grid_scale > 1:
        cdir = os.path.join(vdir, "ctrl")
        os.makedirs(cdir, exist_ok=True)
        for i, c in enumerate(dict.fromkeys(src_ctls)):
            if not os.path.isfile(c):
                continue
            dst = os.path.join(cdir, "%02d_%s" % (i, os.path.basename(c)))
            apply_ctrl(c, dst, ctrl_over, grid_scale)
            ctl_map[c] = dst

    out = os.path.join(vdir, tag + ".d")
    seen = set()
    with open(BASE_D["nlloc"], "r", encoding="latin-1", errors="replace") as fi, \
         open(out, "w", encoding="latin-1") as fo:
        for line in fi:
            k = _key_of(line)
            if k and k in drop:
                continue
            if k == "ModelBand" and ctl_map:
                parts = line.strip().split()
                if len(parts) >= 7 and parts[-1] in ctl_map:
                    parts[-1] = ctl_map[parts[-1]]
                    fo.write(" ".join(parts) + "\n")
                    seen.add(k)
                    continue
            if k and k in over:
                fo.write("%-16s%s\n" % (k, over[k]))
                seen.add(k)
                continue
            fo.write(line)
        for k, v in over.items():
            if k not in seen and k not in drop:
                fo.write("%-16s%s\n" % (k, v))
    return out


def materialize(refiner, variant, cfg_dir):
    # El refiner hace chdir(WorkDir): WorkDir/OutRoot/CommandFile tienen que ser
    # ABSOLUTOS o el resto de rutas se resuelven contra el cwd equivocado.
    cfg_dir = os.path.abspath(cfg_dir)
    if refiner == "hyp2000":
        return materialize_hyp2000(variant, cfg_dir)
    return materialize_nlloc(variant, cfg_dir)


# --------------------------------------------------------------------------
# Barrido
# --------------------------------------------------------------------------
def sweep(refiner, variants, baseline, work, catalog_path, only="*", jobs=1,
          force=False, reports=True, T=30.0, D=1.0, bands=(25.0, 50.0, 100.0),
          arc_jobs=1):
    arcs_dir = os.path.join(baseline, "arcs")
    cfg_dir = os.path.join(work, "cfg")
    os.makedirs(cfg_dir, exist_ok=True)
    catalog = catalog_report.load_catalog(catalog_path)
    if only != "*":
        catalog = {k: v for k, v in catalog.items()
                   if fnmatch.fnmatch(k, only)}

    def one(v):
        tag = v["tag"]
        res = {"tag": tag, "overrides": {
            "d": v.get("d") or {}, "hyp": v.get("hyp") or {},
            "ctrl": v.get("ctrl") or {}}}
        try:
            cfg = materialize(refiner, v, cfg_dir)
        except OSError as exc:
            res["error"] = str(exc)
            return res
        val = os.path.join(work, "val", tag)
        dmap = {k: x for k, x in refine_arcs.read_d(cfg)}
        minph = int(float(dmap.get("MinPhases", 0) or 0))
        maxrms = float(dmap.get("MaxRMS", 0.0) or 0.0)
        if force or not os.path.isfile(os.path.join(val, "manifest.json")):
            refine_arcs.run_refiner(BIN[refiner], cfg, arcs_dir, val, only,
                                    minph, maxrms, arc_jobs=arc_jobs)
        try:
            with open(os.path.join(val, "manifest.json"), "r",
                      encoding="utf-8") as fh:
                res["refine"] = json.load(fh)
        except (OSError, json.JSONDecodeError):
            res["refine"] = {}
        if reports:
            refine_report.build_report(val, arcs_dir,
                                       os.path.join(val, "refine_report.txt"))
            catalog_report.write_report(
                val, catalog, os.path.join(val, "catalog_report.txt"),
                T=T, D=D, bands=bands, baseline_dir=baseline)
        res["summary"] = catalog_report.summarize(val, catalog, T, D,
                                                  baseline_dir=baseline)
        return res

    if jobs > 1:
        with ThreadPoolExecutor(max_workers=jobs) as ex:
            return list(ex.map(one, variants))
    return [one(v) for v in variants]


def print_comparison(refiner, results, bands, base_summary=None):
    good = [r for r in results if r.get("summary")]
    bad = [r for r in results if not r.get("summary")]
    top = max(bands)

    def key(r):
        s = r["summary"]
        return (-s["bands"].get(top, 0),
                s["med_km"] if s["med_km"] is not None else 1e9)

    good.sort(key=key)
    headers = (["variante", "con_sol"] + ["<=%dkm" % b for b in bands] +
               ["sin_sol", "mediana_km", "adicionales", "soluciones",
                "refinados", "descart", "sin_loc"])
    rows = []
    for r in good:
        s = r["summary"]
        m = r.get("refine") or {}
        rows.append([r["tag"], s["n_con_sol"]]
                    + [s["bands"].get(b, 0) for b in bands]
                    + [s["n_sin_sol"], catalog_report.fnum(s["med_km"], 1),
                       s["n_extra"], s["n_sol_total"],
                       m.get("n_ok", "-"), m.get("n_dropped", "-"),
                       m.get("n_noloc", "-")])
    print()
    print("=== comparacion de %s ===" % refiner)
    print("(ordenado por eventos del archivo a <=%d km, descendente)" % top)
    if base_summary:
        print("CRUDO (sin refinar): con_sol %d  <=%dkm %d  mediana %s km"
              % (base_summary["n_con_sol"], top,
                 base_summary["bands"].get(top, 0),
                 catalog_report.fnum(base_summary["med_km"], 1)))
    print_table(headers, rows)
    for r in bad:
        print("ERROR %s: %s" % (r["tag"], r.get("error", "sin resumen")))
    return 0 if not bad else 2


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    ap = argparse.ArgumentParser(add_help=False, description=__doc__.split("\n")[0])
    ap.add_argument("--refiner", choices=["hyp2000", "nlloc"])
    ap.add_argument("--baseline", default=os.path.join(REPO, "tmp", "refine"))
    ap.add_argument("--catalog", default=DEFAULT_CATALOG)
    ap.add_argument("--variants")
    ap.add_argument("--work")
    ap.add_argument("--tags", default="")
    ap.add_argument("--only", default="*")
    ap.add_argument("--jobs", type=int, default=1,
                    help="variantes en paralelo")
    ap.add_argument("--arc-jobs", type=int, default=1,
                    help="ARC en paralelo dentro de cada variante")
    ap.add_argument("--time-window", type=float, default=30.0)
    ap.add_argument("--dist-deg", type=float, default=1.0)
    ap.add_argument("--bands", default="25,50,100")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--no-reports", dest="reports", action="store_false")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("-h", "--help", action="store_true")
    a = ap.parse_args(argv)

    if a.selftest:
        return selftest()
    if a.help:
        print(__doc__.strip())
        return 0
    if not a.refiner:
        print("calibrate_refiners: falta --refiner hyp2000|nlloc", file=sys.stderr)
        return 1

    vpath = a.variants or os.path.join(REPO, "tank_tools", "calib",
                                       "%s_variants.json" % a.refiner)
    if not os.path.isfile(vpath):
        print("calibrate_refiners: no existe %s" % vpath, file=sys.stderr)
        return 1
    variants = filter_tags(load_variants(vpath), a.tags)
    work = a.work or os.path.join(REPO, "tmp", "refine", a.refiner)
    bands = tuple(float(x) for x in a.bands.split(",") if x.strip())

    print("calibrate_refiners: %s  %d variantes  baseline=%s"
          % (a.refiner, len(variants), a.baseline))
    if a.refiner == "nlloc" and (a.jobs > 1 or a.arc_jobs > 1):
        print("AVISO: nlloc_ring descarta ~15% de los eventos por si solo y la "
              "concurrencia lo agrava\n       (medido: 15% con --arc-jobs 1, 38% "
              "con 4). Para numeros fiables usa --jobs 1 --arc-jobs 1.")
    for v in variants:
        bits = []
        for k in ("d", "hyp", "ctrl"):
            if v.get(k):
                bits.append("%s=%s" % (k, json.dumps(v[k])))
        print("  %-14s %s" % (v["tag"], " ".join(bits)))
    if a.dry_run:
        print("(--dry-run: no escribo nada)")
        return 0

    if not os.path.isdir(os.path.join(a.baseline, "arcs")):
        print("calibrate_refiners: el baseline no tiene arcs/: %s" % a.baseline,
              file=sys.stderr)
        return 1
    if not os.path.isfile(a.catalog):
        print("calibrate_refiners: no existe el catalogo %s" % a.catalog,
              file=sys.stderr)
        return 1

    base_summary = None
    try:
        base_summary = catalog_report.summarize(
            a.baseline, catalog_report.load_catalog(a.catalog),
            a.time_window, a.dist_deg)
    except SystemExit:
        base_summary = None

    results = sweep(a.refiner, variants, a.baseline, work, a.catalog, a.only,
                    max(1, a.jobs), a.force, a.reports, a.time_window,
                    a.dist_deg, bands, max(1, a.arc_jobs))

    out = os.path.join(work, "calibration.json")
    os.makedirs(work, exist_ok=True)
    with open(out, "w", encoding="utf-8") as fh:
        json.dump({"refiner": a.refiner, "baseline": a.baseline,
                   "catalog": a.catalog, "only": a.only,
                   "time_window": a.time_window, "dist_deg": a.dist_deg,
                   "bands": list(bands), "results": results}, fh, indent=2)
    print("escrito: %s" % out)
    return print_comparison(a.refiner, results, bands, base_summary)


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

    d = tempfile.mkdtemp(prefix="calibrate_refiners_selftest_")
    try:
        vj = os.path.join(d, "v.json")
        with open(vj, "w", encoding="utf-8") as fh:
            json.dump({"refiner": "hyp2000", "variants": [
                {"tag": "a", "hyp": {"model": "husen"}},
                {"tag": "b", "hyp": {"model": "bandas", "scalars": {"ZTR": "0.0 F"}}},
            ]}, fh)
        vs = load_variants(vj)
        check(len(vs) == 2 and vs[0]["tag"] == "a", "load_variants: dict+lista")
        check([v["tag"] for v in filter_tags(vs, "a*")] == ["a"],
              "filter_tags: glob")
        check(len(filter_tags(vs, "")) == 2, "filter_tags: vacio = todas")

        # .d de mentira con WorkDir absoluto
        base_d = os.path.join(d, "base.d")
        with open(base_d, "w") as fh:
            fh.write("MyModuleId MOD_HYP2000_RING\nWorkDir /tmp/viejo\n"
                     "CommandFile hyp2000_ring.hyp\nMinPhases 4\n")
        out = rewrite_d(base_d, os.path.join(d, "out.d"), {"WorkDir": "/tmp/nuevo"})
        with open(out) as fh:
            txt = fh.read()
        check("/tmp/nuevo" in txt and "/tmp/viejo" not in txt,
              "rewrite_d: reemplaza WorkDir")
        out2 = rewrite_d(base_d, os.path.join(d, "out2.d"),
                         {"MaxRMS": "1.0"}, drop=("CommandFile",))
        with open(out2) as fh:
            txt2 = fh.read()
        check("MaxRMS" in txt2 and "CommandFile" not in txt2,
              "rewrite_d: agrega claves y borra las de drop")

        # .hyp: modelo y escalares
        src_hyp = os.path.join(d, "src.hyp")
        with open(src_hyp, "w") as fh:
            fh.write("* comentario\n200 T 1900 0\nSTA 'estaciones_hyp.sta'\n"
                     "CRH 1 'N18-26_1.5k.crh'\nMUL T 3\nZTR 10.0 F\n"
                     "DAM 7. 30. 0.5\n")
        hp = write_hyp(src_hyp, os.path.join(d, "h.hyp"),
                       {"tag": "a", "hyp": {"model": "husen",
                                            "scalars": {"ZTR": "0.0 F"}}})
        with open(hp) as fh:
            htxt = fh.read()
        check("chile_1d.crh" in htxt and "N18-26" not in htxt and "MUL" not in htxt,
              "write_hyp: cambia el bloque de modelo")
        check("ZTR 0.0 F" in htxt and "ZTR 10.0 F" not in htxt,
              "write_hyp: escalares")
        check("* comentario" in htxt and "200 T 1900 0" in htxt,
              "write_hyp: conserva cabecera y comentarios")

        # ctrl de nlloc
        src_ctl = os.path.join(d, "c.in")
        with open(src_ctl, "w") as fh:
            fh.write("CONTROL 0 0\nLOCSEARCH OCT 96 48 6\nLOCMETH EDT 1.78\n")
        cc = apply_ctrl(src_ctl, os.path.join(d, "c2.in"),
                        {"LOCSEARCH": "OCT 10 10 4"})
        with open(cc) as fh:
            ctxt = fh.read()
        check("LOCSEARCH OCT 10 10 4" in ctxt and "LOCMETH EDT 1.78" in ctxt,
              "apply_ctrl: reemplaza solo la clave del set")

        lg = "LOCGRID 442 538 221 -367 -404 -9.25 1.5 1.5 1.5 PROB_DENSITY SAVE"
        lg2 = scale_locgrid(lg, 2)

        def _ctr(s):
            f = s.split()
            return [float(f[4 + i]) + (int(f[1 + i]) - 1) * float(f[7 + i]) / 2.0
                    for i in range(3)]

        check(all(abs(a - b) < 1e-6 for a, b in zip(_ctr(lg), _ctr(lg2))),
              "scale_locgrid: conserva el centro")
        check(lg2.split()[1:4] == ["221", "269", "111"],
              "scale_locgrid: dims ~/k (%s)" % " ".join(lg2.split()[1:4]))
        check(lg2.split()[7:10] == ["3", "3", "3"],
              "scale_locgrid: paso xk")
        check(scale_locgrid(lg, 1) == lg, "scale_locgrid: factor 1 no cambia")

        check(os.path.isfile(BASE_D["hyp2000"]) and os.path.isfile(BASE_D["nlloc"]),
              "los .d de produccion existen")

        # materializar con un cfg_dir RELATIVO debe dejar rutas absolutas
        rel = os.path.relpath(d, os.getcwd())
        cfn = materialize("nlloc", {"tag": "p", "d": {}}, rel)
        check(os.path.isabs(cfn), "materialize: devuelve ruta absoluta")
        dm = dict(refine_arcs.read_d(cfn))
        check(os.path.isabs(dm.get("WorkDir", ""))
              and os.path.isabs(dm.get("OutRoot", "")),
              "materialize: WorkDir/OutRoot absolutos (el refiner hace chdir)")
    finally:
        shutil.rmtree(d, ignore_errors=True)

    print()
    print("%s selftest calibrate_refiners" % ("OK" if ok else "FALLO"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
