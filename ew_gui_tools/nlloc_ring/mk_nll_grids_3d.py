#!/usr/bin/env python3
"""mk_nll_grids_3d.py - Genera control files de NonLinLoc para usar los modelos
3D de B. Potin (formato NLLoc nativo, <banda>.P.mod.hdr/.buf) con nuestras
estaciones, y opcionalmente corre Grid2Time para materializar las grillas de
tiempo por estacion.

Por cada banda (p.ej. N22-30_1.5k) escribe:
  <outdir>/<banda>/grid2time.in     GTFILES al .mod de Potin, GTMODE GRID3D (P)
  <outdir>/<banda>/grid2time_S.in   idem para S
  <ctrl-dir>/<banda>.in             plantilla NLLoc con TRANS y LOCGRID de la banda

y, con --run, ejecuta Grid2Time (--grid2time PATH) para cada una.

Uso:
  python3 mk_nll_grids_3d.py --mod-dir /mnt/d/nll/time \
      --outdir resources/nlloc/time3d --ctrl-dir resources/nlloc/ctrl \
      --estaciones run_working_v8/params/estaciones_107.txt \
      --band N18-26_1.5k --band N22-30_1.5k \
      --grid2time ew_gui_tools/nlloc_ring/nlloc/Grid2Time --run
"""
import argparse
import math
import os
import re
import subprocess
import sys

EARTH_R = 111.32  # km por grado de latitud


def read_mod_hdr(path):
    """Lee <banda>.P.mod.hdr.

    Devuelve (numx,numy,numz,x0,y0,z0,dx,dy,dz,gtype,lat0,lon0). Exige
    TRANSFORM SIMPLE (las bandas de Potin 1.5k/4k la usan).
    """
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


def band_bbox(numx, numy, x0, y0, dx, dy, lat0, lon0):
    """Bbox geografico (lat_min,lat_max,lon_min,lon_max) de la grilla."""
    xmax = x0 + dx * (numx - 1)
    ymax = y0 + dy * (numy - 1)
    klon = EARTH_R * math.cos(math.radians(lat0))
    return (lat0 + y0 / EARTH_R, lat0 + ymax / EARTH_R,
            lon0 + x0 / klon, lon0 + xmax / klon)


def load_stations(path):
    """estaciones_107.txt: STA NET CHAN LOC lat lon elev cal."""
    stas = {}
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if line.startswith("#") or not line.strip():
                continue
            p = line.split()
            if len(p) >= 6:
                try:
                    stas[p[0]] = (float(p[4]), float(p[5]))
                except ValueError:
                    pass
    return stas


def write_grid2time(path, lat0, lon0, modroot, outroot, phase, stas):
    with open(path, "w") as f:
        f.write("CONTROL 0 0\n")
        f.write("TRANS SIMPLE %s %s 0.0\n" % (lat0, lon0))
        f.write("GTFILES %s %s %s 0\n" % (modroot, outroot, phase))
        f.write("GTMODE GRID3D ANGLES_NO\n")
        for s, (lat, lon) in stas:
            f.write("GTSRCE %s LATLON %.5f %.5f 0.0 0.0\n" % (s, lat, lon))
        f.write("GT_PLFD 1.0e-3 0\n")


def write_nlloc_ctrl(path, lat0, lon0, dims, ttroot, outroot):
    numx, numy, numz, x0, y0, z0, dx, dy, dz = dims
    with open(path, "w") as f:
        f.write("CONTROL 0 0\n")
        f.write("TRANS SIMPLE %s %s 0.0\n" % (lat0, lon0))
        f.write("LOCFILES obs.nll NLLOC_OBS %s %s 0\n" % (ttroot, outroot))
        f.write("LOCHYPOUT SAVE_HYPOINVERSE_Y2000_ARC\n")
        f.write("LOCSEARCH OCT 96 48 6 0.05 50000 10000 4 0\n")
        f.write("LOCGRID %d %d %d %g %g %g %g %g %g PROB_DENSITY SAVE\n"
                % (numx, numy, numz, x0, y0, z0, dx, dy, dz))
        f.write("LOCMETH EDT_OT_WT 9999.0 4 -1 -1 1.78 6 -1.0 1\n")
        f.write("LOCGAU 0.2 0.0\n")
        f.write("LOCGAU2 0.01 0.05 2.0\n")
        f.write("LOCPHASEID P P p G PN PG\n")
        f.write("LOCPHASEID S S s G SN SG\n")
        f.write("LOCQUAL2ERR 0.1 0.5 1.0 2.0 99999.9\n")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--mod-dir", required=True, help="dir con <banda>/<banda>.P.mod.hdr")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--ctrl-dir", required=True)
    ap.add_argument("--estaciones", required=True)
    ap.add_argument("--band", action="append", default=[],
                    help="nombre de banda, p.ej. N22-30_1.5k (repetible)")
    ap.add_argument("--margin", type=float, default=0.5,
                    help="margen en grados para incluir estaciones de borde")
    ap.add_argument("--grid2time", default="", help="ruta al binario Grid2Time")
    ap.add_argument("--run", action="store_true", help="ejecutar Grid2Time")
    a = ap.parse_args(argv[1:])

    # nlloc_ring hace chdir(WorkDir): las rutas de las configs deben ser absolutas.
    a.mod_dir = os.path.abspath(a.mod_dir)
    a.outdir = os.path.abspath(a.outdir)
    a.ctrl_dir = os.path.abspath(a.ctrl_dir)

    if not a.band:
        print("mk_nll_grids_3d: falta --band", file=sys.stderr)
        return 1

    stas_all = load_stations(a.estaciones)
    if not stas_all:
        print("mk_nll_grids_3d: sin estaciones en %s" % a.estaciones, file=sys.stderr)
        return 1

    os.makedirs(a.ctrl_dir, exist_ok=True)
    for band in a.band:
        modroot = os.path.join(a.mod_dir, band, band)
        hdr = modroot + ".P.mod.hdr"
        if not os.path.exists(hdr):
            print("mk_nll_grids_3d: falta %s" % hdr, file=sys.stderr)
            return 1
        (numx, numy, numz, x0, y0, z0, dx, dy, dz,
         gtype, lat0, lon0) = read_mod_hdr(hdr)
        if gtype != "SLOW_LEN":
            print("mk_nll_grids_3d: %s: tipo %s (se espera SLOW_LEN)"
                  % (band, gtype), file=sys.stderr)
            return 1

        lat_min, lat_max, lon_min, lon_max = band_bbox(
            numx, numy, x0, y0, dx, dy, lat0, lon0)
        sel = sorted((s, c) for s, c in stas_all.items()
                     if lat_min - a.margin <= c[0] <= lat_max + a.margin and
                        lon_min - a.margin <= c[1] <= lon_max + a.margin)
        if not sel:
            print("mk_nll_grids_3d: %s: ninguna estacion dentro del bbox"
                  % band, file=sys.stderr)
            return 1

        bdir = os.path.join(a.outdir, band)
        os.makedirs(os.path.join(bdir, "time"), exist_ok=True)
        os.makedirs(os.path.join(bdir, "loc"), exist_ok=True)
        ttroot = os.path.join(bdir, "time", band)
        outroot = os.path.join(bdir, "loc", "ev")
        ctl = os.path.join(a.ctrl_dir, band + ".in")

        write_grid2time(os.path.join(bdir, "grid2time.in"),
                        lat0, lon0, modroot, ttroot, "P", sel)
        write_grid2time(os.path.join(bdir, "grid2time_S.in"),
                        lat0, lon0, modroot, ttroot, "S", sel)
        write_nlloc_ctrl(ctl, lat0, lon0,
                         (numx, numy, numz, x0, y0, z0, dx, dy, dz),
                         ttroot, outroot)

        print("# %s: %d estaciones  lat[%.3f,%.3f] lon[%.3f,%.3f]"
              % (band, len(sel), lat_min, lat_max, lon_min, lon_max))
        print("ModelBand %s %.4f %.4f %.4f %.4f %s %s"
              % (band, lat_min, lat_max, lon_min, lon_max, ttroot, ctl))

        if a.run:
            if not a.grid2time:
                print("mk_nll_grids_3d: --run requiere --grid2time", file=sys.stderr)
                return 1
            for inp in ("grid2time.in", "grid2time_S.in"):
                cmd = [a.grid2time, os.path.join(bdir, inp)]
                print("+ " + " ".join(cmd))
                sys.stdout.flush()
                r = subprocess.run(cmd, cwd=bdir)
                if r.returncode != 0:
                    print("mk_nll_grids_3d: Grid2Time rc=%d en %s"
                          % (r.returncode, inp), file=sys.stderr)
                    return r.returncode

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
