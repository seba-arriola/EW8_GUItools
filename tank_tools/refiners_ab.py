#!/usr/bin/env python3
"""refiners_ab.py - A/B del modelo 1D de hyp2000_ring sobre los tanks.

Para cada captura de picks de `picks/<cap>/`:
  1) corre `csnloc` OFFLINE con `DumpHypo 1` y extrae los HYP2000ARC del log,
  2) corre `hyp2000_ring` OFFLINE sobre cada ARC con dos modelos:
       A = chile_1d.crh                       (Husen et al. 1999)
       B = las 6 bandas derivadas del 3D      (MUL T 3 + NOD)
  3) compara profundidad / nº de fases / RMS y escribe un CSV + resumen.

Todo es OFFLINE: no arranca anillos ni servicios.

Uso:
    python3 tank_tools/refiners_ab.py --picks-dir picks/20260925-084632 \
        --out tmp/ab [--limit N]
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RE_START = re.compile(r"csnloc: --- HYP2000ARC evento \d+-\d+ ---\s*$")
RE_END = re.compile(r"csnloc: --- fin HYP2000ARC evento \d+-\d+ ---\s*$")

# --- .hyp: cabecera comun + bloque de modelos ---
HYP_TAIL = ("ZTR 10.0 F\n"
            "DAM 7. 30. 0.5 0.9 0.012 0.02 0.6 50. 800.\n")
HYP_HEAD = ("200 T 1900 0\n"
            "LET 5 2 3 0 0\n"
            "H71 2 3 3\n"
            "STA 'estaciones_hyp.sta'\n")

MODELS_A = "CRH 1 'chile_1d.crh'\n"

MODELS_B = ("CRH 1 'N18-26_1.5k.crh'\n"
            "CRH 2 'N22-30_1.5k.crh'\n"
            "CRH 3 'N26-34_1.5k.crh'\n"
            "CRH 4 'N30-38_1.5k.crh'\n"
            "CRH 5 'N34-42_1.5k.crh'\n"
            "CRH 6 'N38-46_1.5k.crh'\n"
            "MUL T 3\n"
            "NOD -21.601 0 68.350 0 450. 150. 1\n"
            "NOD -25.975 0 68.692 0 450. 150. 2\n"
            "NOD -30.056 0 68.637 0 450. 150. 3\n"
            "NOD -33.994 0 70.685 0 450. 150. 4\n"
            "NOD -37.968 0 71.769 0 450. 150. 5\n"
            "NOD -41.954 0 73.065 0 450. 150. 6\n")

D_TPL = ("MyModuleId      MOD_HYP2000_RING\n"
         "InRing          HYPO_RING\n"
         "OutRing         HYPO_RING_REF\n"
         "HeartBeatInt    30\n"
         "LogFile         1\n"
         "Debug           1\n"
         "SourceCode      W\n"
         "CommandFile     hyp2000_ring.hyp\n"
         "WorkDir         {wd}\n"
         "MinPhases       4\n"
         "MaxRMS          2.0\n")


def extract_arcs(logpath):
    """Devuelve la lista de ARCs crudos volcados por DumpHypo entre marcadores."""
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


def parse_arc(buf):
    """Parsea la cabecera (197 chars) de un ARC HYP2000: lat/lon/z/nph/rms.

    El motor HYPOINVERSE escribe antes '  NNN STATIONS READ IN.', asi que se
    toma la primera linea de >= 190 chars (la cabecera ARC) y no la primera.
    """
    if not buf:
        return None
    line = None
    for raw in buf.split(b"\n"):
        if len(raw) >= 190:
            line = raw.decode("latin-1")
            break
    if line is None:
        return None

    def num(a, b):
        try:
            return float(line[a:b])
        except ValueError:
            return 0.0

    lat = int(num(16, 18)) + num(19, 23) / 100.0 / 60.0
    if line[18:19] == "S":
        lat = -lat
    lon = int(num(23, 26)) + num(27, 31) / 100.0 / 60.0
    if line[26:27] == "W":
        lon = -lon
    return {"lat": round(lat, 4), "lon": round(lon, 4),
            "z": num(31, 36) / 100.0, "nph": int(num(39, 42)),
            "rms": num(48, 52) / 100.0}


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)


def build_workdirs(out, src_workdir, hypA, hypB):
    """Crea dos WorkDir (A y B) copiando .sta y .crh y escribiendo el .hyp."""
    for name, models, hyp in (("hypA", MODELS_A, hypA), ("hypB", MODELS_B, hypB)):
        wd = os.path.join(out, name)
        os.makedirs(wd, exist_ok=True)
        for pat in ("*.crh", "estaciones_hyp.sta"):
            for f in glob.glob(os.path.join(src_workdir, pat)):
                shutil.copy2(f, wd)
        with open(os.path.join(wd, "hyp2000_ring.hyp"), "w") as fh:
            fh.write(HYP_HEAD + models + HYP_TAIL)
        with open(hyp, "w") as fh:
            fh.write(D_TPL.format(wd=wd))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--picks-dir", required=True)
    ap.add_argument("--out", default=os.path.join(REPO, "tmp", "ab"))
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--csnloc", default=os.path.join(REPO, "ew_gui_tools", "csnloc", "csnloc"))
    ap.add_argument("--csnloc-d", default=os.path.join(REPO, "run_working_v8", "params", "csnloc.d"))
    ap.add_argument("--params-dir", default=os.path.join(REPO, "run_working_v8", "params"))
    ap.add_argument("--hyp", default=os.path.join(REPO, "ew_gui_tools", "hyp2000_ring", "hyp2000_ring"))
    ap.add_argument("--src-workdir", default=os.path.join(REPO, "tmp", "hyp2000_ring"))
    a = ap.parse_args(argv[1:])

    out = os.path.abspath(a.out)
    for sub in ("arcs", "logs", "json"):
        os.makedirs(os.path.join(out, sub), exist_ok=True)

    # config de csnloc con DumpHypo 1 (copia de la de produccion)
    cfg = os.path.join(out, "csnloc_ab.d")
    with open(a.csnloc_d, "r", encoding="latin-1", errors="replace") as fi, \
         open(cfg, "w") as fo:
        for line in fi:
            fo.write("DumpHypo          1\n" if line.startswith("DumpHypo") else line)

    hypA = os.path.join(out, "hypA.d")
    hypB = os.path.join(out, "hypB.d")
    build_workdirs(out, os.path.abspath(a.src_workdir), hypA, hypB)

    picks = sorted(glob.glob(os.path.join(a.picks_dir, "*.picks")))
    if a.limit:
        picks = picks[:a.limit]
    print("refiners_ab: %d capturas de %s" % (len(picks), a.picks_dir))
    sys.stdout.flush()

    rows = []
    n_arc = 0
    for i, p in enumerate(picks, 1):
        slug = os.path.basename(p)[:-len(".picks")]
        logdir = os.path.join(out, "logs", slug)
        os.makedirs(logdir, exist_ok=True)
        env = dict(os.environ, EW_LOG=logdir)
        r = run([a.csnloc, cfg, os.path.abspath(p)],
                cwd=os.path.abspath(a.params_dir), env=env)
        with open(os.path.join(out, "json", slug + ".jsonl"), "wb") as fh:
            fh.write(r.stdout)

        arcs = []
        for lg in glob.glob(os.path.join(logdir, "*.log")):
            arcs += extract_arcs(lg)
        for k, arc in enumerate(arcs):
            n_arc += 1
            apath = os.path.join(out, "arcs", "%s_%02d.arc" % (slug, k))
            with open(apath, "w") as fh:
                fh.write(arc)
            res = {}
            for tag, dpath in (("A", hypA), ("B", hypB)):
                h = run([a.hyp, dpath, apath], env=env)
                rec = parse_arc(h.stdout)
                if rec is None:
                    rec = {"lat": None, "lon": None, "z": None,
                           "nph": 0, "rms": None, "rc": h.returncode}
                rec["rc"] = h.returncode
                res[tag] = rec
            rows.append((slug, k, res["A"], res["B"]))

        if i % 25 == 0 or i == len(picks):
            print("  %d/%d  (%d ARCs)" % (i, len(picks), n_arc))
            sys.stdout.flush()

    # --- CSV ---
    csv = os.path.join(out, "results.csv")
    with open(csv, "w") as fh:
        fh.write("slug,ev,lat,lon,zA,nphA,rmsA,zB,nphB,rmsB,dz\n")
        for slug, k, A, B in rows:
            dz = "" if (A["z"] is None or B["z"] is None) else "%.2f" % (B["z"] - A["z"])
            fh.write("%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s\n"
                     % (slug, k, A["lat"], A["lon"], A["z"], A["nph"], A["rms"],
                        B["z"], B["nph"], B["rms"], dz))

    # --- resumen ---
    both = [(A, B) for _, _, A, B in rows if A["z"] is not None and B["z"] is not None]
    print("\n=== resumen ===")
    print("capturas: %d   ARCs: %d   con solucion A y B: %d"
          % (len(picks), n_arc, len(both)))
    if both:
        dzs = [B["z"] - A["z"] for A, B in both]
        absd = sorted(abs(d) for d in dzs)
        n = len(dzs)
        mean = sum(dzs) / n
        med = sorted(dzs)[n // 2]
        print("dz = zB - zA  (km):  media %+.2f   mediana %+.2f   mediana|dz| %.2f"
              % (mean, med, absd[n // 2]))
        print("  |dz| <= 5 km: %d (%.0f%%)   <= 15 km: %d (%.0f%%)   > 50 km: %d"
              % (sum(1 for d in absd if d <= 5), 100.0 * sum(1 for d in absd if d <= 5) / n,
                 sum(1 for d in absd if d <= 15), 100.0 * sum(1 for d in absd if d <= 15) / n,
                 sum(1 for d in absd if d > 50)))
        zmA = sorted(A["z"] for A, _ in both)
        zmB = sorted(B["z"] for A, B in both)
        print("  zA: min %.1f  mediana %.1f  max %.1f" % (zmA[0], zmA[n // 2], zmA[-1]))
        print("  zB: min %.1f  mediana %.1f  max %.1f" % (zmB[0], zmB[n // 2], zmB[-1]))
        # cuantos quedan pegados al fondo de la caja DAM (DZMAX=800) o al tope
        print("  zA <= 0.1 km: %d   zB <= 0.1 km: %d"
              % (sum(1 for A, _ in both if A["z"] <= 0.1),
                 sum(1 for _, B in both if B["z"] <= 0.1)))
        rmsA = [A["rms"] for A, _ in both if A["rms"] is not None]
        rmsB = [B["rms"] for A, B in both if B["rms"] is not None]
        if rmsA and rmsB:
            print("  rms medio: A %.3f   B %.3f" % (sum(rmsA) / len(rmsA), sum(rmsB) / len(rmsB)))
    print("CSV: %s" % csv)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
