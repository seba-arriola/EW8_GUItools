#!/usr/bin/env python3
"""mk_mod_merge.py - Fusiona varias grillas 3D de NLLoc (SLOW_LEN) en una sola.

Las bandas de Potin vienen troceadas por latitud y **cada trozo tiene su propio
`TRANSFORM SIMPLE LatOrig/LongOrig`**, asi que la escala en x de cada trozo es
`111.32*cos(LatOrig)` km/grado y NO estan alineados en una grilla comun de km.
Este script las re-muestrea a una grilla comun (origen y paso elegidos) y
rellena los bordes por **replicacion del borde mas cercano**, para que las
estaciones cercanas al limite queden dentro de la grilla: `Grid2Time` falla con
`Source point is not inside model grid` (docs/REFINADORES_RING.md:125-130).

En la zona de solape entre dos trozos se **promedia** (son el mismo modelo de
fondo, asi que el promedio evita una costura dura).

Uso:
  python3 mk_mod_merge.py --out resources/nlloc/mod3d/CHILE_4k/CHILE_4k.P.mod \\
      --origin -33.0 -70.0 --step 4.0 --pad 3.0 \\
      --bbox -45.90 -17.98 -75.90 -64.46 \\
      --band /mnt/d/nll/time/N18-26_4k/N18-26_4k.P.mod \\
      --band /mnt/d/nll/time/N22-30_4k/N22-30_4k.P.mod ... [--check]
"""
import argparse
import math
import re
import sys

import numpy as np

EARTH_R = 111.32  # km por grado de latitud


def read_hdr(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = [l.rstrip("\n") for l in fh]
    if not lines:
        raise ValueError("cabecera vacia: %s" % path)
    p = lines[0].split()
    if len(p) < 11:
        raise ValueError("cabecera inesperada en %s: %r" % (path, lines[0]))
    h = dict(numx=int(p[0]), numy=int(p[1]), numz=int(p[2]),
             x0=float(p[3]), y0=float(p[4]), z0=float(p[5]),
             dx=float(p[6]), dy=float(p[7]), dz=float(p[8]), gtype=p[9])
    lat0 = lon0 = None
    for l in lines[1:]:
        m = re.match(r"\s*TRANSFORM\s+SIMPLE\s+LatOrig\s+([-\d.]+)\s+"
                     r"LongOrig\s+([-\d.]+)", l)
        if m:
            lat0, lon0 = float(m.group(1)), float(m.group(2))
            break
    if lat0 is None:
        raise ValueError("sin TRANSFORM SIMPLE en %s" % path)
    h["lat0"], h["lon0"] = lat0, lon0
    return h


def read_mod(root):
    h = read_hdr(root + ".hdr")
    if h["gtype"] != "SLOW_LEN":
        raise ValueError("%s: tipo %s (se espera SLOW_LEN)" % (root, h["gtype"]))
    arr = np.fromfile(root + ".buf", dtype="<f4")
    n = h["numx"] * h["numy"] * h["numz"]
    if arr.size != n:
        raise ValueError("%s: %d valores != %d esperados" % (root, arr.size, n))
    # .buf: x mas lento, z mas rapido -> arr[ix*(ny*nz) + iy*nz + iz]
    h["g"] = arr.reshape((h["numx"], h["numy"], h["numz"]))
    return h


def band_bbox(h):
    klon = EARTH_R * math.cos(math.radians(h["lat0"]))
    latmin = h["lat0"] + h["y0"] / EARTH_R
    latmax = h["lat0"] + (h["y0"] + (h["numy"] - 1) * h["dy"]) / EARTH_R
    lonmin = h["lon0"] + h["x0"] / klon
    lonmax = h["lon0"] + (h["x0"] + (h["numx"] - 1) * h["dx"]) / klon
    return latmin, latmax, lonmin, lonmax


def tile_index(h, LAT, LON):
    """Indices (redondeados y recortados) de la grilla h para cada (lat,lon)."""
    klon = EARTH_R * math.cos(math.radians(h["lat0"]))
    it = ((LON - h["lon0"]) * klon - h["x0"]) / h["dx"]
    jt = ((LAT - h["lat0"]) * EARTH_R - h["y0"]) / h["dy"]
    ic = np.clip(np.round(it), 0, h["numx"] - 1).astype(np.int32)
    jc = np.clip(np.round(jt), 0, h["numy"] - 1).astype(np.int32)
    return ic, jc, np.abs(it - ic) + np.abs(jt - jc)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="raiz de salida (sin .hdr/.buf)")
    ap.add_argument("--band", action="append", required=True,
                    help="raiz <...>.P.mod de cada trozo (repetible)")
    ap.add_argument("--origin", nargs=2, type=float, required=True,
                    metavar=("LAT", "LON"), help="origen del TRANSFORM de salida")
    ap.add_argument("--step", type=float, default=4.0, help="paso en km")
    ap.add_argument("--bbox", nargs=4, type=float, required=True,
                    metavar=("LATMIN", "LATMAX", "LONMIN", "LONMAX"),
                    help="extension de DATOS a cubrir (sin el pad)")
    ap.add_argument("--pad", type=float, default=0.0,
                    help="grados de relleno por replicacion del borde")
    ap.add_argument("--check", action="store_true",
                    help="compara el resultado con los trozos y reporta el error")
    a = ap.parse_args(argv[1:])

    lat0o, lon0o = a.origin
    klono = EARTH_R * math.cos(math.radians(lat0o))
    latmin, latmax, lonmin, lonmax = a.bbox
    latmin -= a.pad
    latmax += a.pad
    lonmin -= a.pad
    lonmax += a.pad

    y0o = (latmin - lat0o) * EARTH_R
    x0o = (lonmin - lon0o) * klono
    numy = int(math.ceil(((latmax - latmin) * EARTH_R) / a.step)) + 1
    numx = int(math.ceil(((lonmax - lonmin) * klono) / a.step)) + 1

    tiles = [read_mod(b) for b in a.band]
    numz = min(t["numz"] for t in tiles)
    z0o, dzo = tiles[0]["z0"], tiles[0]["dz"]
    for t in tiles:
        if abs(t["z0"] - z0o) > 1e-6 or abs(t["dz"] - dzo) > 1e-6:
            print("mk_mod_merge: los trozos no comparten z0/dz", file=sys.stderr)
            return 1

    lat = lat0o + (y0o + a.step * np.arange(numy)) / EARTH_R
    lon = lon0o + (x0o + a.step * np.arange(numx)) / klono
    LAT, LON = np.meshgrid(lat, lon, indexing="ij")   # (numy, numx)

    pen = np.full((numy, numx), 1e18)
    idxs = []
    for t in tiles:
        ic, jc, d = tile_index(t, LAT, LON)
        pen = np.minimum(pen, d)
        idxs.append((t, ic, jc, d))

    acc = np.zeros((numy, numx, numz), dtype="f4")
    cnt = np.zeros((numy, numx), dtype="f4")
    for t, ic, jc, d in idxs:
        sel = d <= pen + 0.5           # todos los que alcanzan la distancia minima
        acc[sel] += t["g"][ic[sel], jc[sel], :numz]
        cnt[sel] += 1.0
    out = acc / np.maximum(cnt, 1.0)[:, :, None]

    # .buf: x mas lento -> (ix, iy, iz)
    out.transpose(1, 0, 2).tofile(a.out + ".buf")
    with open(a.out + ".hdr", "w", encoding="utf-8") as fh:
        fh.write("%d %d %d %.6f %.6f %.6f %.6f %.6f %.6f SLOW_LEN FLOAT\n"
                 % (numx, numy, numz, x0o, y0o, z0o, a.step, a.step, dzo))
        fh.write("TRANSFORM  SIMPLE LatOrig %.6f  LongOrig %.6f  RotCW 0.000000\n"
                 % (lat0o, lon0o))

    nout = numx * numy * numz
    print("mk_mod_merge: %s  %dx%dx%d = %.1f M nodos  (%.0f MB)  pad %.2f grados"
          % (a.out, numx, numy, numz, nout / 1e6, nout * 4 / 1e6, a.pad))
    for t in tiles:
        b = band_bbox(t)
        print("  trozo %-28s lat[%.2f,%.2f] lon[%.2f,%.2f]  %dx%dx%d"
              % (t["lat0"], b[0], b[1], b[2], b[3], t["numx"], t["numy"], t["numz"]))

    if a.check:
        # En el interior (penalizacion 0) el resultado debe reproducir el modelo.
        # Se compara Vp = step/valor contra el trozo que gano en cada celda.
        inner = pen <= 0.5
        if not inner.any():
            print("  check: sin celdas interiores")
            return 0
        num = den = 0.0
        for t, ic, jc, d in idxs:
            sel = inner & (d <= 0.5)
            v_o = out[sel]
            v_t = t["g"][ic[sel], jc[sel], :numz]
            ok = (v_o > 0) & (v_t > 0)
            if not ok.any():
                continue
            vp_o = a.step / v_o[ok]
            vp_t = t["dx"] / v_t[ok]
            num += float(np.abs(vp_o - vp_t).sum())
            den += float(vp_t.size)
        print("  check: %d celdas interiores, |dVp| medio = %.4f km/s"
              % (int(inner.sum()), (num / den) if den else float("nan")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
