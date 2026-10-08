#!/usr/bin/env python3
"""mk_crh_from_3d.py - Deriva un modelo 1D por capas (Vp(z)) desde una grilla de
slowness 3D de NLLoc (formato .mod.hdr/.buf, tipo SLOW_LEN) para HYPOINVERSE
(CRH n '<modelo>.crh'). HYPOINVERSE solo admite 1D por capas, asi que el modelo
3D de Potin se resume promediando horizontalmente una region.

El valor de la grilla es SLOW_LEN = dx/vel (segundos por celda) -> vel = dx/valor.
Se impone Vp y profundidad monotonas crecientes (lo exige hycrh.for) y se
remuestrea a <= 20 capas (NLYR=20 en common_data.inc).

Uso:
  python3 mk_crh_from_3d.py --mod /mnt/d/nll/time/N22-30_1.5k/N22-30_1.5k.P.mod \
      --out chile_22_30.crh --name CHILE_POTIN_N22-30 \
      [--region LATMIN LATMAX LONMIN LONMAX] [--min-vp 2.0] [--zmax 300] [--dump]
"""
import argparse
import math
import os
import re
import sys

import numpy as np

EARTH_R = 111.32  # km por grado de latitud

# Niveles de profundidad por defecto (<= 20 capas: NLYR=20 en hypoinverse).
DEPTHS = [0.0, 1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 15.0, 20.0, 25.0,
          30.0, 35.0, 40.0, 50.0, 60.0, 80.0, 100.0, 150.0, 200.0, 300.0]


def read_mod_hdr(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = [l.rstrip("\n") for l in fh]
    if not lines:
        raise ValueError("cabecera vacia: %s" % path)
    p = lines[0].split()
    if len(p) < 11:
        raise ValueError("cabecera inesperada en %s: %r" % (path, lines[0]))
    numx, numy, numz = int(p[0]), int(p[1]), int(p[2])
    x0, y0, z0 = float(p[3]), float(p[4]), float(p[5])
    dx, dy, dz = float(p[6]), float(p[7]), float(p[8])
    gtype = p[9]
    lat0 = lon0 = None
    for l in lines[1:]:
        m = re.match(r"\s*TRANSFORM\s+SIMPLE\s+LatOrig\s+([-\d.]+)\s+"
                     r"LongOrig\s+([-\d.]+)", l)
        if m:
            lat0, lon0 = float(m.group(1)), float(m.group(2))
            break
    if lat0 is None:
        raise ValueError("sin TRANSFORM SIMPLE en %s" % path)
    return numx, numy, numz, x0, y0, z0, dx, dy, dz, gtype, lat0, lon0


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--mod", required=True, help="raiz sin extension: <...>.P.mod")
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", default="")
    ap.add_argument("--zmax", type=float, default=300.0)
    ap.add_argument("--region", nargs=4, type=float, default=None,
                    metavar=("LATMIN", "LATMAX", "LONMIN", "LONMAX"))
    ap.add_argument("--min-vp", type=float, default=0.0,
                    help="descarta celdas con Vp menor (p.ej. agua marina)")
    ap.add_argument("--dump", action="store_true")
    a = ap.parse_args(argv[1:])

    hdr, buf = a.mod + ".hdr", a.mod + ".buf"
    (numx, numy, numz, x0, y0, z0, dx, dy, dz,
     gtype, lat0, lon0) = read_mod_hdr(hdr)
    if gtype != "SLOW_LEN":
        print("mk_crh_from_3d: tipo %s (se espera SLOW_LEN)" % gtype, file=sys.stderr)
        return 1

    arr = np.fromfile(buf, dtype="<f4")
    if arr.size != numx * numy * numz:
        print("mk_crh_from_3d: %d valores != %d esperados"
              % (arr.size, numx * numy * numz), file=sys.stderr)
        return 1
    # .buf: x mas lento, z mas rapido -> arr[ix*(ny*nz) + iy*nz + iz]
    g = arr.reshape((numx, numy, numz))

    ix0, ix1, iy0, iy1 = 0, numx, 0, numy
    if a.region:
        latmin, latmax, lonmin, lonmax = a.region
        klon = EARTH_R * math.cos(math.radians(lat0))

        def ixof(lon):
            return int(round(((lon - lon0) * klon - x0) / dx))

        def iyof(lat):
            return int(round(((lat - lat0) * EARTH_R - y0) / dy))

        ix0 = max(0, min(ixof(lonmin), ixof(lonmax)))
        ix1 = min(numx, max(ixof(lonmin), ixof(lonmax)) + 1)
        iy0 = max(0, min(iyof(latmin), iyof(latmax)))
        iy1 = min(numy, max(iyof(latmin), iyof(latmax)) + 1)
        if ix1 - ix0 < 1 or iy1 - iy0 < 1:
            print("mk_crh_from_3d: region fuera de la grilla", file=sys.stderr)
            return 1

    sub = g[ix0:ix1, iy0:iy1, :].astype("f8")
    with np.errstate(divide="ignore", invalid="ignore"):
        vel = np.where(sub > 0, dx / sub, np.nan)   # km/s
    if a.min_vp > 0:
        vel = np.where(vel >= a.min_vp, vel, np.nan)
    prof = np.nanmean(vel.reshape(-1, numz), axis=0)
    zs = z0 + dz * np.arange(numz)

    # perfil fino valido (z>=0) e interpolacion a los niveles objetivo
    good = np.isfinite(prof) & (zs >= 0.0)
    if not good.any():
        print("mk_crh_from_3d: sin celdas validas", file=sys.stderr)
        return 1
    zg, vg = zs[good], prof[good]

    depths = [d for d in DEPTHS if d <= a.zmax]
    if depths[0] > 0.0:
        depths.insert(0, 0.0)
    vals = np.interp(depths, zg, vg)   # np.interp recorta a los extremos

    # monotonia estricta en V y D (hycrh.for)
    out = []
    vmax = -1.0
    for d, v in zip(depths, vals):
        if v <= vmax:
            v = vmax + 0.01
        vmax = v
        out.append((v, d))

    if a.dump:
        for v, d in out:
            print("%6.2f %8.2f" % (v, d))

    name = a.name or ("CHILE_POTIN_" + os.path.basename(a.mod).replace(".P.mod", ""))
    with open(a.out, "w") as f:
        f.write(name[:30] + "\n")
        for v, d in out:
            f.write("%5.2f %5.2f\n" % (v, d))
    print("mk_crh_from_3d: %s -> %d capas (Vp %.2f..%.2f km/s, z %.1f..%.1f km)"
          % (a.out, len(out), out[0][0], out[-1][0], out[0][1], out[-1][1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
