#!/usr/bin/env python3
"""refine_arcs.py - Suelo de ARC y arnes OFFLINE de los refinadores.

Dos trabajos, siempre OFFLINE (no arranca anillos ni servicios):

  1) --baseline   congela el CRUDO: corre `csnloc` offline con `DumpHypo 1` sobre
     una captura de picks, extrae los HYP2000ARC del log y los deja como
     <out>/arcs/<slug>_<NN>.arc  +  <out>/manifest.json (sha256 del csnloc.d).
     Ese conjunto es el insumo IDENTICO de todas las variantes.

  2) --refiner    corre un refinador sobre CADA ARC del suelo:
     <out>/arcs_ref/<slug>_<NN>.arc   ARC refinado (salida del binario)
     <out>/json/<slug>.jsonl          una solucion por linea (esquema offline_report)
     <out>/manifest.json              config, filtros y conteos

El layout del ARC es el de `ew_gui_tools/csnloc/hypo_out.c` (y el que parsea
`hyp2000_ring.c:169-198`):

    [0:16]  tiempo origen YYYYMMDDHHMMSS + centisegundos
    [16:18] lat grados   [18] N/S   [19:23] lat min*100
    [23:26] lon grados   [26] E/W   [27:31] lon min*100
    [31:36] profundidad*100          [39:42] nph
    [42:45] gap          [45:48] dmin km   [48:52] rms*100
    [136:146] qid        [161]/[178:182] version
    fase:   [0:5] sta [5:7] net [9:12] chan [14] P|S [17:34] tiempo

Ojo: en modo OFFLINE `hyp2000_ring` **no** aplica `MinPhases`/`MaxRMS` (viven en
el bucle de anillo, hyp2000_ring.c:508-528) mientras que `nlloc_ring` si
(locate_arc, nlloc_ring.c:455-457). El arnes emula el filtro sobre el ARC de
ENTRADA (como hace el anillo) para que las variantes sean comparables.

Uso:
    python3 tank_tools/refine_arcs.py --baseline \
        --csnloc ew_gui_tools/csnloc/csnloc \
        --csnloc-d run_working_v8/params/csnloc.d \
        --picks-dir picks_ps/<ts> --out tmp/refine [--only 'test[1-40]']

    python3 tank_tools/refine_arcs.py --refiner hyp2000 \
        --refiner-d tmp/refine/hyp2000/cfg/base.d \
        --arcs-dir tmp/refine/arcs --out tmp/refine/hyp2000/val/base

    python3 tank_tools/refine_arcs.py --preflight --refiner nlloc \
        --refiner-d ... --arcs-dir ...
"""
import argparse
import fnmatch
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RE_START = re.compile(r"csnloc: --- HYP2000ARC evento \d+-\d+ ---\s*$")
RE_END = re.compile(r"csnloc: --- fin HYP2000ARC evento \d+-\d+ ---\s*$")

ARC_HEADER_MIN = 190
ARC_HEADER_LEN = 197
ARC_PHASE_LEN = 114
ARC_TIMEOUT_SEC = 180.0


# --------------------------------------------------------------------------
# Lectura de ARC
# --------------------------------------------------------------------------
def _num(s):
    s = s.strip()
    if not s:
        return None
    try:
        return float(s)
    except ValueError:
        return None


def _sha256(path):
    if not path or not os.path.isfile(path):
        return None
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for blk in iter(lambda: fh.read(65536), b""):
            h.update(blk)
    return h.hexdigest()


def extract_arcs(logpath):
    """ARCs crudos volcados por DumpHypo, entre marcadores."""
    arcs, cur = [], None
    with open(logpath, "r", encoding="latin-1", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if RE_START.search(line):
                cur = []
                continue
            if cur is not None and RE_END.search(line):
                arcs.append("\n".join(cur) + "\n")
                cur = None
                continue
            if cur is not None:
                cur.append(line)
    return arcs


def header_line(buf):
    """La cabecera ARC: primera linea de >= 190 chars (no la de 'NN STATIONS')."""
    if not buf:
        return None
    if isinstance(buf, bytes):
        parts = buf.split(b"\n")
    else:
        parts = buf.encode("latin-1", "replace").split(b"\n")
    for raw in parts:
        if len(raw) >= ARC_HEADER_MIN:
            return raw.decode("latin-1", "replace")
    return None


def arc_header(buf):
    """Cabecera del ARC -> dict. None si no hay cabecera reconocible."""
    line = header_line(buf)
    if line is None:
        return None
    out = {"t0": None, "t0_utc": None, "lat": None, "lon": None,
           "depth_km": None, "nphases": 0, "gap_deg": None, "dmin_km": None,
           "rms_sec": None, "qid": "", "version": ""}

    sec = _num(line[12:16])
    try:
        if sec is None:
            raise ValueError
        t0 = datetime(int(line[0:4]), int(line[4:6]), int(line[6:8]),
                      int(line[8:10]), int(line[10:12]), 0,
                      tzinfo=timezone.utc).timestamp() + sec / 100.0
        out["t0"] = t0
        out["t0_utc"] = datetime.fromtimestamp(
            t0, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%S") + "Z"
    except (ValueError, TypeError):
        out["t0"] = None

    dlat, mlat = _num(line[16:18]), _num(line[19:23])
    if dlat is not None and mlat is not None:
        lat = dlat + mlat / 100.0 / 60.0
        out["lat"] = round(-lat if line[18:19] == "S" else lat, 5)
    dlon, mlon = _num(line[23:26]), _num(line[27:31])
    if dlon is not None and mlon is not None:
        lon = dlon + mlon / 100.0 / 60.0
        out["lon"] = round(-lon if line[26:27] == "W" else lon, 5)

    z = _num(line[31:36])
    out["depth_km"] = None if z is None else round(z / 100.0, 2)
    nph = _num(line[39:42])
    out["nphases"] = int(nph) if nph is not None else 0
    out["gap_deg"] = _num(line[42:45])
    out["dmin_km"] = _num(line[45:48])
    rms = _num(line[48:52])
    out["rms_sec"] = None if rms is None else round(rms / 100.0, 2)

    if len(line) >= 146:
        out["qid"] = line[136:146].strip()
    if len(line) >= 182:
        out["version"] = line[178:182].strip()
    elif len(line) >= 162:
        out["version"] = line[161:162].strip()
    return out


def arc_phases(buf):
    """Lineas de fase primarias (no las de continuacion con '$1' en [104:106])."""
    out = []
    if isinstance(buf, str):
        buf = buf.encode("latin-1", "replace")
    for raw in buf.split(b"\n"):
        if len(raw) < 34:
            continue
        line = raw.decode("latin-1", "replace")
        if line[14] not in ("P", "S"):
            continue
        if line[104:106] == "$1":
            continue
        out.append({"sta": line[0:5].strip(), "net": line[5:7].strip(),
                    "chan": line[9:12].strip(), "phase": line[14]})
    return out


def arc_to_record(buf):
    """ARC -> registro con el esquema que consume offline_report.load_dir."""
    h = arc_header(buf)
    if h is None:
        return None
    rec = dict(h)
    rec["event"] = 0
    rec["id"] = h["qid"] or "0"
    rec["phases"] = arc_phases(buf)
    rec["grid_level"] = "-"
    rec["depth_ctrl"] = "-"
    return rec


def arc_slug(path):
    """<slug>_<NN>.arc -> slug."""
    return os.path.basename(path).rsplit("_", 1)[0]


def read_d(path):
    """[(clave, valor)] de las lineas activas de un .d."""
    out = []
    if not path or not os.path.isfile(path):
        return out
    with open(path, "r", encoding="latin-1", errors="replace") as fh:
        for line in fh:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            f = s.split(None, 1)
            if len(f) == 2:
                out.append((f[0], f[1].split("#")[0].strip()))
    return out


# --------------------------------------------------------------------------
# Suelo (ARC crudos de csnloc)
# --------------------------------------------------------------------------
def dump_config(src, dst):
    """Copia el .d de csnloc forzando DumpHypo 1."""
    with open(src, "r", encoding="latin-1", errors="replace") as fi, \
         open(dst, "w", encoding="latin-1") as fo:
        seen = False
        for line in fi:
            if line.lstrip().startswith("DumpHypo"):
                fo.write("DumpHypo          1\n")
                seen = True
            else:
                fo.write(line)
        if not seen:
            fo.write("DumpHypo          1\n")
    return dst


def run_baseline(csnloc, csnloc_d, picks_dir, out_dir, params_dir=None,
                 only="*", limit=0):
    # OJO: csnloc corre con cwd=params_dir, asi que todo lo que le pasemos
    # (EW_LOG incluido) tiene que ser absoluto.
    out_dir = os.path.abspath(out_dir)
    csnloc = os.path.abspath(csnloc)
    csnloc_d = os.path.abspath(csnloc_d)
    arcs_dir = os.path.join(out_dir, "arcs")
    logs_dir = os.path.join(out_dir, "logs")
    os.makedirs(arcs_dir, exist_ok=True)
    os.makedirs(logs_dir, exist_ok=True)
    cfg = dump_config(csnloc_d, os.path.join(out_dir, "csnloc_dump.d"))
    if not params_dir:
        params_dir = os.path.dirname(os.path.abspath(csnloc_d))

    picks = sorted(glob.glob(os.path.join(picks_dir, "*.picks")))
    picks = [p for p in picks
             if only == "*"
             or fnmatch.fnmatch(os.path.basename(p)[:-len(".picks")], only)]
    if limit:
        picks = picks[:limit]

    slugs, n_arc, n_fail = [], 0, 0
    for p in picks:
        slug = os.path.basename(p)[:-len(".picks")]
        logdir = os.path.join(logs_dir, slug)
        os.makedirs(logdir, exist_ok=True)
        env = dict(os.environ, EW_LOG=logdir)
        r = subprocess.run([os.path.abspath(csnloc), os.path.abspath(cfg),
                            os.path.abspath(p)],
                           cwd=params_dir, capture_output=True, env=env)
        if r.returncode != 0:
            n_fail += 1
        arcs = []
        for lg in sorted(glob.glob(os.path.join(logdir, "*.log"))):
            arcs += extract_arcs(lg)
        slugs.append(slug)
        recs = []
        for k, arc in enumerate(arcs):
            with open(os.path.join(arcs_dir, "%s_%02d.arc" % (slug, k)), "w",
                      encoding="latin-1") as fh:
                fh.write(arc)
            n_arc += 1
            rec = arc_to_record(arc)
            if rec is not None:
                rec["event"] = k
                recs.append(rec)
        # jsonl del crudo: asi catalog_report --baseline puede leerlo con load_dir
        with open(os.path.join(out_dir, slug + ".jsonl"), "w",
                  encoding="utf-8") as fh:
            for rec in recs:
                fh.write(json.dumps(rec) + "\n")

    manifest = {
        "kind": "baseline",
        "capture_dir": os.path.abspath(picks_dir),
        "csnloc": os.path.abspath(csnloc),
        "config": os.path.abspath(csnloc_d),
        "config_sha256": _sha256(csnloc_d),
        "params_dir": os.path.abspath(params_dir),
        "only": only,
        "n_tanks": len(slugs), "n_arcs": n_arc, "n_failed": n_fail,
        "tanks": [{"slug": s} for s in slugs],
    }
    with open(os.path.join(out_dir, "manifest.json"), "w",
              encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2)
    return manifest


# --------------------------------------------------------------------------
# Refinador
# --------------------------------------------------------------------------
def _rewrite_d(src, dst, over):
    """Copia un .d reemplazando claves (o anadiendolas si no estaban)."""
    seen = set()
    with open(src, "r", encoding="latin-1", errors="replace") as fi, \
         open(dst, "w", encoding="latin-1") as fo:
        for line in fi:
            s = line.strip()
            k = s.split(None, 1)[0] if s and s[0] not in "#*" else ""
            if k and k in over:
                fo.write("%-16s%s\n" % (k, over[k]))
                seen.add(k)
            else:
                fo.write(line)
        for k, v in over.items():
            if k not in seen:
                fo.write("%-16s%s\n" % (k, v))
    return dst


def replicate_cfg(refiner_d, out_dir, i):
    """Un .d por worker, con su propio WorkDir (y OutRoot), para paralelizar.

    Los refinadores hacen chdir(WorkDir) y escriben ahi (arcIn/arcOut en
    hyp2000; obs.nll y <OutRoot>.sum.grid0.loc.arc en nlloc), asi que dos
    corridas del MISMO .d no pueden solaparse.
    """
    keys = {k for k, _ in read_d(refiner_d)}
    kind = "nlloc" if "OutRoot" in keys else "hyp2000"
    dmap = {k: v for k, v in read_d(refiner_d)}
    wd = os.path.join(os.path.abspath(out_dir), "wd%d" % i)
    os.makedirs(wd, exist_ok=True)
    if kind == "hyp2000":
        src = dmap.get("WorkDir", "")
        if src and os.path.isdir(src):
            for f in sorted(os.listdir(src)):
                p = os.path.join(src, f)
                if os.path.isfile(p) and f not in ("arcIn", "arcOut"):
                    shutil.copy2(p, os.path.join(wd, f))
    else:
        os.makedirs(os.path.join(wd, "loc"), exist_ok=True)
    over = {"WorkDir": wd}
    if kind == "nlloc":
        over["OutRoot"] = os.path.join(wd, "loc", "ev")
    dst = os.path.join(wd, "worker.d")
    _rewrite_d(refiner_d, dst, over)
    return dst


def run_refiner(refiner_bin, refiner_d, arcs_dir, out_dir, only="*",
                minphases=0, maxrms=0.0, timeout=ARC_TIMEOUT_SEC, arc_jobs=1):
    out_dir = os.path.abspath(out_dir)
    refiner_bin = os.path.abspath(refiner_bin)
    refiner_d = os.path.abspath(refiner_d)
    ref_dir = os.path.join(out_dir, "arcs_ref")
    json_dir = out_dir
    logs_dir = os.path.join(out_dir, "logs")
    for d in (ref_dir, json_dir, logs_dir):
        os.makedirs(d, exist_ok=True)

    jobs = max(1, int(arc_jobs))
    cfgs = [refiner_d]
    if jobs > 1:
        cfgs = [replicate_cfg(refiner_d, out_dir, i) for i in range(jobs)]
    for i in range(jobs):
        os.makedirs(os.path.join(logs_dir, "wd%d" % i), exist_ok=True)

    targets = []
    for ap in sorted(glob.glob(os.path.join(arcs_dir, "*.arc"))):
        if only != "*" and not fnmatch.fnmatch(arc_slug(ap), only):
            continue
        targets.append(ap)

    def one(item):
        idx, ap = item
        base = os.path.basename(ap)
        with open(ap, "r", encoding="latin-1", errors="replace") as fh:
            hdr = arc_header(fh.read())
        # El anillo filtra ANTES de refinar (hyp2000_ring.c:508-528); el modo
        # offline de hyp2000 no lo hace, asi que lo emulamos aqui.
        if hdr is not None:
            if minphases > 0 and hdr["nphases"] < minphases:
                return (base, "drop", None, "")
            if maxrms > 0.0 and hdr["rms_sec"] is not None \
                    and hdr["rms_sec"] > maxrms:
                return (base, "drop", None, "")
        w = idx % jobs
        env = dict(os.environ, EW_LOG=os.path.join(logs_dir, "wd%d" % w))
        try:
            r = subprocess.run([refiner_bin, cfgs[w], ap],
                               capture_output=True, env=env, timeout=timeout)
        except subprocess.TimeoutExpired:
            return (base, "err", None, "timeout de %s" % refiner_bin)
        if r.returncode == 0 and r.stdout:
            return (base, "ok", r.stdout, "")
        err = (r.stderr or b"").decode("latin-1", "replace").strip()
        if r.returncode == 2:
            return (base, "noloc", None, err)
        return (base, "err", None, err or ("rc=%d" % r.returncode))

    items = list(enumerate(targets))
    if jobs > 1:
        with ThreadPoolExecutor(max_workers=jobs) as ex:
            results = list(ex.map(one, items))
    else:
        results = [one(it) for it in items]

    n_ok = n_drop = n_noloc = n_err = 0
    per_slug, seen = {}, []
    errs = {}
    for base, status, out, err in results:
        slug = base.rsplit("_", 1)[0]
        if slug not in seen:
            seen.append(slug)
        if status == "ok":
            with open(os.path.join(ref_dir, base), "wb") as fh:
                fh.write(out)
            rec = arc_to_record(out)
            if rec is not None:
                recs = per_slug.setdefault(slug, [])
                rec["event"] = len(recs)
                recs.append(rec)
            n_ok += 1
        elif status == "drop":
            n_drop += 1
        elif status == "noloc":
            n_noloc += 1
            if err:
                errs[err] = errs.get(err, 0) + 1
        else:
            n_err += 1
            if err:
                errs[err] = errs.get(err, 0) + 1
    n_in = len(targets)

    if errs:
        with open(os.path.join(out_dir, "errors.txt"), "w",
                  encoding="utf-8") as fh:
            for msg, cnt in sorted(errs.items(), key=lambda kv: -kv[1]):
                fh.write("%6d  %s\n" % (cnt, msg.replace("\n", " | ")))

    for slug in seen:
        path = os.path.join(json_dir, slug + ".jsonl")
        with open(path, "w", encoding="utf-8") as fh:
            for rec in per_slug.get(slug, []):
                fh.write(json.dumps(rec) + "\n")

    d_keys = read_d(refiner_d)
    dmap = {k: v for k, v in d_keys}
    manifest = {
        "kind": "refined",
        "refiner": os.path.abspath(refiner_bin),
        "refiner_sha256": _sha256(refiner_bin),
        "config": os.path.abspath(refiner_d),
        "config_sha256": _sha256(refiner_d),
        "d_keys": [[k, v] for k, v in d_keys],
        "workdir": dmap.get("WorkDir", ""),
        "command_file": dmap.get("CommandFile", ""),
        "baseline_dir": os.path.abspath(arcs_dir),
        "only": only,
        "filters": {"MinPhases": minphases, "MaxRMS": maxrms},
        "n_in": n_in, "n_ok": n_ok, "n_dropped": n_drop,
        "n_noloc": n_noloc, "n_error": n_err, "arc_jobs": jobs,
        "tanks": [{"slug": s} for s in seen],
    }
    with open(os.path.join(out_dir, "manifest.json"), "w",
              encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2)
    return manifest


# --------------------------------------------------------------------------
# Preflight
# --------------------------------------------------------------------------
def preflight(refiner_bin, refiner_d, arcs_dir, out_dir=None, only="*"):
    """Chequeos baratos antes de gastar una corrida larga."""
    fails = []
    if not os.path.isfile(refiner_bin):
        fails.append("binario inexistente: %s" % refiner_bin)
    if not os.path.isfile(refiner_d):
        fails.append("config inexistente: %s" % refiner_d)
    arcs = sorted(glob.glob(os.path.join(arcs_dir, "*.arc")))
    if not arcs:
        fails.append("sin ARC en %s" % arcs_dir)
    if fails:
        return fails, None

    dmap = {k: v for k, v in read_d(refiner_d)}
    wd = dmap.get("WorkDir", "")
    cmd = dmap.get("CommandFile", "")
    if wd and not os.path.isdir(wd):
        fails.append("WorkDir inexistente: %s" % wd)
    if wd and cmd and not os.path.isfile(os.path.join(wd, cmd)):
        fails.append("CommandFile no esta en el WorkDir: %s/%s" % (wd, cmd))
    if fails:
        return fails, None

    probe = arcs[0]
    env = dict(os.environ)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
        env["EW_LOG"] = os.path.abspath(out_dir)
    try:
        r = subprocess.run([os.path.abspath(refiner_bin), os.path.abspath(refiner_d),
                            os.path.abspath(probe)],
                           capture_output=True, env=env, timeout=ARC_TIMEOUT_SEC)
    except subprocess.TimeoutExpired:
        fails.append("timeout refinando %s" % os.path.basename(probe))
        return fails, None
    info = {"probe": os.path.basename(probe), "rc": r.returncode,
            "stdout_bytes": len(r.stdout),
            "stderr": (r.stderr or b"").decode("latin-1", "replace").strip()[-200:]}
    if r.returncode not in (0, 2):
        fails.append("rc=%d refinando %s" % (r.returncode, os.path.basename(probe)))
    if r.returncode == 0 and arc_header(r.stdout) is None:
        fails.append("la salida no tiene cabecera ARC reconocible")
    return fails, info


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    ap = argparse.ArgumentParser(add_help=False, description=__doc__.split("\n")[0])
    ap.add_argument("--baseline", action="store_true")
    ap.add_argument("--refiner", choices=["hyp2000", "nlloc"])
    ap.add_argument("--preflight", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("-h", "--help", action="store_true")

    ap.add_argument("--csnloc", default=os.path.join(REPO, "ew_gui_tools", "csnloc", "csnloc"))
    ap.add_argument("--csnloc-d", default=os.path.join(REPO, "run_working_v8", "params", "csnloc.d"))
    ap.add_argument("--picks-dir")
    ap.add_argument("--params-dir")
    ap.add_argument("--refiner-bin")
    ap.add_argument("--refiner-d")
    ap.add_argument("--arcs-dir")
    ap.add_argument("--out", default=os.path.join(REPO, "tmp", "refine"))
    ap.add_argument("--only", default="*")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--minphases", type=int, default=0)
    ap.add_argument("--maxrms", type=float, default=0.0)
    ap.add_argument("--arc-jobs", type=int, default=1,
                    help="ARC en paralelo dentro de la corrida (WorkDir por worker)")
    ap.add_argument("--timeout", type=float, default=ARC_TIMEOUT_SEC)
    a = ap.parse_args(argv)

    if a.selftest:
        return selftest()
    if a.help:
        print(__doc__.strip())
        return 0

    if a.preflight:
        if not (a.refiner_bin and a.refiner_d and a.arcs_dir):
            print("refine_arcs: --preflight necesita --refiner-bin, --refiner-d y --arcs-dir",
                  file=sys.stderr)
            return 1
        fails, info = preflight(a.refiner_bin, a.refiner_d, a.arcs_dir, a.out)
        print("preflight: %s" % ("OK" if not fails else "FALLO"))
        if info:
            print("  prueba: %s rc=%s stdout=%d bytes" % (
                info["probe"], info["rc"], info["stdout_bytes"]))
            if info["stderr"]:
                print("  stderr: %s" % info["stderr"])
        for f in fails:
            print("  - %s" % f)
        return 0 if not fails else 1

    if a.baseline:
        if not a.picks_dir:
            print("refine_arcs: --baseline necesita --picks-dir", file=sys.stderr)
            return 1
        m = run_baseline(a.csnloc, a.csnloc_d, a.picks_dir, a.out,
                         a.params_dir, a.only, a.limit)
        print("baseline: %d tanks, %d ARC -> %s" % (
            m["n_tanks"], m["n_arcs"], os.path.join(a.out, "arcs")))
        print("  csnloc.d sha256 %s" % (m["config_sha256"] or "?")[:16])
        if m["n_failed"]:
            print("  OJO: %d capturas fallaron" % m["n_failed"])
        return 0

    if a.refiner:
        if not (a.refiner_bin and a.refiner_d and a.arcs_dir):
            print("refine_arcs: --refiner necesita --refiner-bin, --refiner-d y --arcs-dir",
                  file=sys.stderr)
            return 1
        m = run_refiner(a.refiner_bin, a.refiner_d, a.arcs_dir, a.out,
                        a.only, a.minphases, a.maxrms, a.timeout, a.arc_jobs)
        print("%s: in=%d ok=%d dropped=%d noloc=%d err=%d -> %s" % (
            a.refiner, m["n_in"], m["n_ok"], m["n_dropped"], m["n_noloc"],
            m["n_error"], os.path.join(a.out, "arcs_ref")))
        return 0

    print("refine_arcs: nada que hacer (usa --baseline, --refiner o --preflight)",
          file=sys.stderr)
    return 1


# --------------------------------------------------------------------------
# Selftest
# --------------------------------------------------------------------------
def _mk_arc(y, mo, d, h, mi, sec100, lat, lon, z100, nph, gap, dmin, rms100,
            phases, qid="0000000001", version="0001"):
    lat_dir = "S" if lat < 0 else "N"
    lon_dir = "W" if lon < 0 else "E"
    alat, alon = abs(lat), abs(lon)
    ldeg, lmin = int(alat), int(round((alat - int(alat)) * 60 * 100))
    odeg, omin = int(alon), int(round((alon - int(alon)) * 60 * 100))
    head = ("%04d%02d%02d%02d%02d%04d%02d%c%04d%03d%c%04d%05d"
            % (y, mo, d, h, mi, sec100, ldeg, lat_dir, lmin, odeg, lon_dir,
               omin, z100))
    head += " " * (39 - len(head))
    head += "%3d%3d%3d%4d" % (nph, gap, dmin, rms100)
    head += " " * (ARC_HEADER_LEN - len(head))
    head = head[:136] + qid.ljust(10) + head[146:]
    head = head[:161] + version[-1] + head[162:]
    head = head[:178] + version.rjust(4) + head[182:]
    out = [head, "$1"]
    for sta, net, chan, ph in phases:
        t = ("%04d%02d%02d%02d%02d%05.2f" % (y, mo, d, h, mi, 0.0))
        pre = (sta.ljust(5) + net.ljust(2) + "  " + chan.ljust(3) + "  "
               + ph + " 0")
        line = pre + t
        line += " " * (ARC_PHASE_LEN - len(line))
        out.append(line)
        out.append((sta.ljust(5) + net.ljust(2) + "  "
                    + chan.ljust(3)).ljust(104) + "$1")
    out.append("")
    return "\n".join(out) + "\n"


def selftest():
    import tempfile

    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("ok  : " if cond else "FAIL: ") + msg)
        if not cond:
            ok = False

    arc = _mk_arc(2026, 7, 22, 2, 3, 4900, -19.36, -70.3185, 2750, 15, 178,
                  33, 121,
                  [("PSGCX", "CX", "HHZ", "P"), ("PB23", "CX", "HHZ", "P"),
                   ("TA02", "C1", "HHZ", "S")])
    h = arc_header(arc)
    check(h is not None, "arc_header: cabecera reconocida")
    check(abs(h["lat"] + 19.36) < 0.01, "arc_header: lat (%.5f)" % h["lat"])
    check(abs(h["lon"] + 70.3185) < 0.01, "arc_header: lon (%.5f)" % h["lon"])
    check(abs(h["depth_km"] - 27.50) < 0.01, "arc_header: z=%.2f" % h["depth_km"])
    check(h["nphases"] == 15, "arc_header: nph=%d" % h["nphases"])
    check(abs(h["gap_deg"] - 178) < 0.5, "arc_header: gap=%.0f" % h["gap_deg"])
    check(abs(h["dmin_km"] - 33) < 0.5, "arc_header: dmin=%.0f" % h["dmin_km"])
    check(abs(h["rms_sec"] - 1.21) < 0.01, "arc_header: rms=%.2f" % h["rms_sec"])
    check(h["t0_utc"] == "2026-07-22T02:03:49Z", "arc_header: t0=%s" % h["t0_utc"])
    check(h["qid"] == "0000000001" and h["version"] == "0001",
          "arc_header: qid/version")

    ph = arc_phases(arc)
    check(len(ph) == 3, "arc_phases: 3 primarias (sin continuaciones)")
    check(ph[0]["sta"] == "PSGCX" and ph[0]["net"] == "CX"
          and ph[0]["phase"] == "P", "arc_phases: primera fase")
    check(ph[2]["phase"] == "S", "arc_phases: fase S")

    rec = arc_to_record(arc)
    check(rec["t0"] is not None and rec["lat"] is not None,
          "arc_to_record: esquema offline_report")

    # --- extract_arcs + run_refiner con un refinador falso ---
    tmp = tempfile.mkdtemp(prefix="refine_arcs_selftest_")
    try:
        logs = os.path.join(tmp, "logs")
        os.makedirs(logs)
        with open(os.path.join(logs, "x.log"), "w", encoding="latin-1") as fh:
            fh.write("ruido previo\n")
            fh.write("csnloc: --- HYP2000ARC evento 1-1 ---\n")
            fh.write(arc)
            fh.write("csnloc: --- fin HYP2000ARC evento 1-1 ---\n")
        got = extract_arcs(os.path.join(logs, "x.log"))
        check(len(got) == 1 and arc_header(got[0])["nphases"] == 15,
              "extract_arcs: 1 ARC con 15 fases")

        arcs_dir = os.path.join(tmp, "arcs")
        os.makedirs(arcs_dir)
        with open(os.path.join(arcs_dir, "test1_00.arc"), "w",
                  encoding="latin-1") as fh:
            fh.write(arc)
        with open(os.path.join(arcs_dir, "test1_01.arc"), "w",
                  encoding="latin-1") as fh:
            fh.write(arc)

        fake = os.path.join(tmp, "fake_refiner.sh")
        with open(fake, "w") as fh:
            fh.write("#!/bin/sh\ncat \"$2\"\n")
        os.chmod(fake, 0o755)

        dpath = os.path.join(tmp, "fake.d")
        with open(os.path.join(tmp, "fake.hyp"), "w") as fh:
            fh.write("* fake command file\n")
        with open(dpath, "w") as fh:
            fh.write("MyModuleId MOD_FAKE\nCommandFile fake.hyp\n"
                     "WorkDir %s\nMinPhases 0\n" % tmp)

        m = run_refiner(fake, dpath, arcs_dir, os.path.join(tmp, "val"))
        check(m["n_in"] == 2 and m["n_ok"] == 2 and m["n_dropped"] == 0,
              "run_refiner: 2 in, 2 ok")
        check(os.path.isfile(os.path.join(tmp, "val", "test1.jsonl")),
              "run_refiner: jsonl por slug")
        m2 = run_refiner(fake, dpath, arcs_dir, os.path.join(tmp, "val4"),
                         minphases=16)
        check(m2["n_in"] == 2 and m2["n_ok"] == 0 and m2["n_dropped"] == 2,
              "run_refiner: MinPhases filtra (emula el anillo)")

        rep = replicate_cfg(dpath, os.path.join(tmp, "rep"), 0)
        dm2 = dict(read_d(rep))
        check(os.path.isabs(dm2["WorkDir"]) and rep.endswith("worker.d"),
              "replicate_cfg: WorkDir propio por worker")
        check(os.path.isfile(os.path.join(dm2["WorkDir"], "fake.hyp")),
              "replicate_cfg: copia el WorkDir de hyp2000")
        m3 = run_refiner(fake, dpath, arcs_dir, os.path.join(tmp, "val5"),
                         arc_jobs=2)
        check(m3["n_ok"] == 2 and m3["arc_jobs"] == 2,
              "run_refiner: arc_jobs=2 refina igual")

        # consumible por offline_report
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from offline_report import load_dir
        _, by_slug, expected = load_dir(os.path.join(tmp, "val"))
        check("test1" in by_slug and len(by_slug["test1"]) == 2,
              "load_dir lee el jsonl refinado")
        check(by_slug["test1"][0]["nphases"] == 15, "jsonl: nphases")

        fails, info = preflight(fake, dpath, arcs_dir, os.path.join(tmp, "pre"))
        check(not fails and info and info["rc"] == 0,
              "preflight: OK con refinador falso")
        bad = os.path.join(tmp, "bad.d")
        with open(bad, "w") as fh:
            fh.write("WorkDir %s\nCommandFile no_existe.hyp\n" % tmp)
        fails2, _ = preflight(fake, bad, arcs_dir)
        check(any("CommandFile" in f for f in fails2),
              "preflight: detecta WorkDir/CommandFile incompleto")
        fails3, _ = preflight(os.path.join(tmp, "nope"), dpath, arcs_dir)
        check(any("binario" in f for f in fails3),
              "preflight: detecta binario ausente")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print()
    print("%s selftest refine_arcs" % ("OK" if ok else "FALLO"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
