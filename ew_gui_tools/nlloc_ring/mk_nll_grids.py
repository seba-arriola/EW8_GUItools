#!/usr/bin/env python3
"""mk_nll_grids.py - Genera los control files de NonLinLoc (Vel2Grid, Grid2Time,
NLLoc) para un conjunto de estaciones y un modelo 1D, centrados en una region.

Modelo 1D por capas (VEL D) igual que chile_1d.crh. Se usan grillas GRID2D
(distancia, profundidad), validas para cualquier azimut.

Uso:
    python3 mk_nll_grids.py <arcfile|--sta L1 L2 ...> <estaciones.txt> <outdir> \
            <lat_c> <lon_c> [--dmax KM] [--zmax KM] [--model FILE]
"""
import argparse
import os
import re
import sys

LAYERS = [
    (5.50, 0.0), (6.10, 10.0), (6.90, 35.0), (8.00, 55.0),
]


def sta_from_arc(path):
    stas = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if len(line) >= 114 and not line.startswith("$"):
                s = line[0:5].strip()
                if s and s not in stas:
                    stas.append(s)
    return stas


def load_coords(path):
    coords = {}
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            p = line.split()
            if len(p) >= 6:
                try:
                    coords[p[0]] = (float(p[4]), float(p[5]))
                except ValueError:
                    pass
    return coords


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("arcfile", nargs="?")
    ap.add_argument("--sta", nargs="*", default=[])
    ap.add_argument("estaciones")
    ap.add_argument("outdir")
    ap.add_argument("lat_c", type=float)
    ap.add_argument("lon_c", type=float)
    ap.add_argument("--dmax", type=float, default=250.0)
    ap.add_argument("--zmax", type=float, default=150.0)
    a = ap.parse_args(argv[1:])

    stas = a.sta[:] if a.sta else (sta_from_arc(a.arcfile) if a.arcfile else [])
    coords = load_coords(a.estaciones)
    stas = [s for s in stas if s in coords]
    if not stas:
        print("mk_nll_grids: sin estaciones validas", file=sys.stderr)
        return 1

    os.makedirs(a.outdir, exist_ok=True)
    os.makedirs(os.path.join(a.outdir, "time"), exist_ok=True)
    os.makedirs(os.path.join(a.outdir, "loc"), exist_ok=True)

    nz = int(a.zmax) + 1
    nd = int(a.dmax) + 1

    # --- Vel2Grid ---
    with open(os.path.join(a.outdir, "vel2grid.in"), "w") as f:
        f.write("CONTROL 0 0\n")
        f.write(f"TRANS SIMPLE {a.lat_c} {a.lon_c} 0.0\n")
        f.write("VGTYPE P\n")
        f.write(f"VGGRID 2 {nd} {nz} 0.0 0.0 0.0 1.0 1.0 1.0 SLOW_LEN\n")
        f.write(f"VGOUT {a.outdir}/time/model\n")
        for v, d in LAYERS:
            f.write(f"LAYER {d:6.1f} {v:6.2f} 0.00 {v/1.78:6.2f} 0.00 2.70 0.0\n")

    # --- Grid2Time ---
    with open(os.path.join(a.outdir, "grid2time.in"), "w") as f:
        f.write("CONTROL 0 0\n")
        f.write(f"TRANS SIMPLE {a.lat_c} {a.lon_c} 0.0\n")
        f.write(f"GTFILES {a.outdir}/time/model {a.outdir}/time/model P\n")
        f.write("GTMODE GRID2D ANGLES_NO\n")
        for s in stas:
            lat, lon = coords[s]
            f.write(f"GTSRCE {s} LATLON {lat:.5f} {lon:.5f} 0.0 0.0\n")
        f.write("GT_PLFD 1.0e-3 0\n")

    # --- NLLoc (plantilla; el modulo reemplaza LOCFILES) ---
    with open(os.path.join(a.outdir, "nlloc.in"), "w") as f:
        f.write("CONTROL 0 0\n")
        f.write(f"TRANS SIMPLE {a.lat_c} {a.lon_c} 0.0\n")
        f.write(f"LOCFILES obs.nll NLLOC_OBS {a.outdir}/time/model {a.outdir}/loc/ev\n")
        f.write("LOCHYPOUT SAVE_HYPOINVERSE_Y2000_ARC\n")
        f.write("LOCSEARCH OCT 10 10 4 0.5 20000 1000 0 1\n")
        f.write("LOCGRID 81 81 31 -40.0 -40.0 0.0 1.0 1.0 5.0 PROB_DENSITY SAVE\n")
        f.write("LOCMETH EDT_OT_WT 9999.0 4 -1 -1 1.78 6 -1.0 1\n")
        f.write("LOCGAU 0.2 0.0\n")
        f.write("LOCGAU2 0.01 0.05 2.0\n")
        f.write("LOCPHASEID P P p G PN PG\n")
        f.write("LOCPHASEID S S s G SN SG\n")
        f.write("LOCQUAL2ERR 0.1 0.5 1.0 2.0 99999.9\n")

    print(f"generado en {a.outdir}: {len(stas)} estaciones: {' '.join(stas)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
