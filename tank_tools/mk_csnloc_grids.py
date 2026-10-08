#!/usr/bin/env python3
"""mk_csnloc_grids.py - Genera los `.grid` de `csnloc` desde una tabla declarativa.

Las grillas de `csnloc` (`GlobalGrid`/`RegionalGrid`/`LocalGrid`) definen la
**nucleacion por retroproyeccion**: son el primer paso de la localizacion, antes
del refinamiento. Hasta ahora eran ficheros hechos a mano (una transcripcion de
la tiling de Glass3), sin generador y sin forma de comparar alternativas.

Aqui viven las definiciones como tablas y el script las materializa. Incluye las
dos tilings para poder contrastarlas:

  current : las 10 cajas de produccion, tal cual (sirve para validar el script).
  A       : mismo numero de cajas, pero el rango de longitud sigue el ancho del
            modelo 3D de Potin en cada latitud (cierra los huecos al oeste y al
            este), se tapa el agujero austral (-56..-54.65) y la regional pasa a
            tener las mismas 31 capas de profundidad que las locales.

OJO con `MaxNodes`: no recorta, **aborta el arranque** (`grid.c:127-130` devuelve
-2 y `csnloc.c:652-654` mata el modulo). Por eso el `--check` avisa.

Uso:
    python3 mk_csnloc_grids.py --tiling A --out-dir tmp/csnloc_grids/A --check
    python3 mk_csnloc_grids.py --tiling current --out-dir /tmp/x --check
"""
import argparse
import math
import os
import sys

KM_PER_DEG = 111.195

# 31 capas 5..300 km: finas en la corteza (donde esta la sismicidad superficial)
# y mas gruesas abajo. Es la que ya usan las locales de produccion.
DEPTHS_LOCAL = [5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70,
                80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190,
                200, 225, 250, 275, 300]
DEPTHS_GLOBAL = [10, 30, 50, 100, 200, 300, 400, 500, 600, 750]

# Bboxes del modelo 3D de Potin (nlloc_ring.d:34-39). Se usan como ancla para el
# borde oeste (el modelo se construyo cubriendo la subduccion) y para el --check.
POTIN = [
    ("N18-26", -25.2192, -17.9833, -71.5456, -65.1549),
    ("N22-30", -30.0514, -21.8992, -72.1779, -65.2067),
    ("N26-34", -34.0516, -26.0611, -72.8184, -64.4555),
    ("N30-38", -38.0434, -29.9451, -74.4971, -66.8734),
    ("N34-42", -42.0155, -33.9172, -75.3937, -68.1264),
    ("N38-46", -45.8956, -38.0129, -75.8988, -70.2262),
]

# (name, latmin, latmax, lonmin, lonmax) -- produccion, tal cual.
LOCALS_CURRENT = [
    ("tarapaca",    -21.653, -16.347, -71.593, -67.408),
    ("antofagasta", -25.653, -20.347, -71.150, -66.851),
    ("atacama",     -29.653, -24.347, -72.221, -67.780),
    ("coquimbo",    -33.153, -27.847, -73.296, -68.704),
    ("centro",      -36.153, -30.847, -73.373, -68.627),
    ("biobio",      -39.153, -33.847, -74.461, -69.539),
    ("araucania",   -42.153, -36.847, -75.064, -69.936),
    ("los_lagos",   -45.153, -39.847, -75.684, -70.316),
    ("aysen",       -49.153, -43.847, -76.874, -71.126),
    ("magallanes",  -54.653, -49.347, -75.214, -68.786),
]

# Tiling A: se CONSERVAN las franjas de latitud de produccion (ya cubren Chile,
# -16.35..-54.65) y solo se cambia el rango de LONGITUD de cada una: pasa a ser
# la UNION de (la caja actual) y (los bboxes de Potin que solapan esa latitud).
#
# El principio: la grilla de nucleacion debe cubrir el MISMO pie que el modelo
# 3D, para que cualquier evento que se nuclee tenga modelo disponible (y no caiga
# al fallback 1D), y para no perder nada de lo que ya se cubria. Se calcula, no se
# escribe a mano: asi el "cero regresion" es por construccion.
# Ademas magallanes se extiende hasta -56.0 (tapa el agujero austral).
def _union_lon(la0, la1):
    """Longitud a cubrir en la franja de latitud [la0, la1]."""
    los = []
    for l in LOCALS_CURRENT:
        if not (l[2] < la0 or l[1] > la1):
            los.append((l[3], l[4]))
    for p in POTIN:
        if not (p[2] < la0 or p[1] > la1):
            los.append((p[3], p[4]))
    if not los:
        return None
    return min(x[0] for x in los), max(x[1] for x in los)


def _build_a():
    out = []
    for (nm, la0, la1, lo0, lo1) in LOCALS_CURRENT:
        if nm == "magallanes":
            la0 = -56.0
        u = _union_lon(la0, la1)
        if u:
            lo0, lo1 = u
        out.append((nm, la0, la1, lo0, lo1))
    return out


LOCALS_A = _build_a()

REGIONAL = {
    "current": ("chile_regional", -56.0, -16.0, -76.0, -66.0, 25.0,
                None, 0.0, 300.0, 25.0, 0.0, 0, 200000),
    # A: mas ancha (cubre fosa y altiplano, que es adonde caen los eventos que
    # se escapan de las locales) y con las 31 capas finas, para no nuclear con
    # la profundidad cuantizada a 25 km.
    "A": ("chile_regional", -56.0, -16.0, -80.0, -64.0, 25.0,
          None, 0.0, 300.0, 25.0, 0.0, 0, 500000),
}
# En A la regional usa las capas finas (se rellena abajo).
REGIONAL_A_DEPTHS = DEPTHS_LOCAL

GLOBAL = ("global", -90.0, 90.0, -180.0, 180.0, 100.0,
          DEPTHS_GLOBAL, 0.0, 0.0, 0.0, 1500.0, 20, 1000000)


def nodes(latmin, latmax, lonmin, lonmax, node_km, n_depth):
    """Numero de nodos como lo calcula Grid_BuildAxes (grid.c:105-124)."""
    lat_med = 0.5 * (latmin + latmax)
    lat_step = node_km / KM_PER_DEG
    lon_step = node_km / (KM_PER_DEG * max(0.1, math.cos(math.radians(lat_med))))
    ny = int(math.floor((latmax - latmin) / lat_step)) + 1
    nx = int(math.floor((lonmax - lonmin) / lon_step)) + 1
    return nx, ny, n_depth, nx * ny * n_depth


def grid_text(name, latmin, latmax, lonmin, lonmax, node_km,
              depths=None, dmin=0.0, dmax=0.0, dstep=0.0,
              sta_max=0.0, nsta=0, max_nodes=200000, comment="",
              min_phases=None, backproj=None):
    """`min_phases`/`backproj` son OPCIONES POR GRILLA (grid.c los lee): si se
    fijan, sustituyen a `MinPhasesPerEvent`/`BackProjThreshold` del `.d` solo en
    esa grilla. Es la via para ampliar cobertura sin diluir la seleccion."""
    L = []
    if comment:
        L.append("# %s" % comment)
    L.append("%-20s%s" % ("Name", name))
    L.append("%-20s%.3f" % ("LatMin", latmin))
    L.append("%-20s%.3f" % ("LatMax", latmax))
    L.append("%-20s%.3f" % ("LonMin", lonmin))
    L.append("%-20s%.3f" % ("LonMax", lonmax))
    L.append("%-20s%.1f" % ("NodeKm", node_km))
    if depths:
        L.append("%-20s%s" % ("DepthLayers", ",".join("%g" % d for d in depths)))
    else:
        L.append("%-20s%.1f" % ("DepthMin", dmin))
        L.append("%-20s%.1f" % ("DepthMax", dmax))
        L.append("%-20s%.1f" % ("DepthStep", dstep))
    L.append("%-20s%g" % ("StaMaxDistKm", sta_max))
    L.append("%-20s%d" % ("NumStationsPerNode", nsta))
    if min_phases is not None:
        L.append("%-20s%d" % ("MinPhases", min_phases))
    if backproj is not None:
        L.append("%-20s%g" % ("BackProjThreshold", backproj))
    L.append("%-20s%d" % ("MaxNodes", max_nodes))
    return "\n".join(L) + "\n"


def in_box(lat, lon, b):
    """b = (latmin, latmax, lonmin, lonmax)."""
    return b[0] <= lat <= b[1] and b[2] <= lon <= b[3]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--tiling", choices=["current", "reg31", "A"], default="A")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--cells", type=float, default=0.5,
                    help="paso de la retícula del --check (grados)")
    ap.add_argument("--node-km", type=float, default=10.0,
                    help="NodeKm de las grillas locales (default 10)")
    ap.add_argument("--local-max-nodes", type=int, default=200000,
                    help="MaxNodes de las locales; hay que subirlo si NodeKm baja")
    ap.add_argument("--local-min-phases", type=int, default=None,
                    help="MinPhases POR GRILLA en las locales (sustituye al del .d)")
    ap.add_argument("--local-backproj", type=float, default=None,
                    help="BackProjThreshold POR GRILLA en las locales (0..1)")
    ap.add_argument("--local-nsta", type=int, default=25,
                    help="NumStationsPerNode de las locales (default 25)")
    ap.add_argument("--local-sta-max", type=float, default=1000.0,
                    help="StaMaxDistKm de las locales (default 1000)")
    ap.add_argument("--env", nargs=4, type=float,
                    default=[-56.0, -16.0, -82.0, -64.0],
                    metavar=("LATMIN", "LATMAX", "LONMIN", "LONMAX"),
                    help="sobre donde se mide la cobertura")
    a = ap.parse_args(argv[1:])

    os.makedirs(a.out_dir, exist_ok=True)
    if a.tiling == "current":
        locals_, reg, reg_depths = LOCALS_CURRENT, REGIONAL["current"], None
    elif a.tiling == "reg31":
        # Solo el cambio de la regional (locales de produccion): aisla cuanto
        # aporta la regional con capas finas y mas ancha.
        locals_, reg, reg_depths = LOCALS_CURRENT, REGIONAL["A"], REGIONAL_A_DEPTHS
    else:                                   # "A"
        locals_, reg, reg_depths = LOCALS_A, REGIONAL["A"], REGIONAL_A_DEPTHS

    files = []
    # global
    g = GLOBAL
    p = os.path.join(a.out_dir, "global.grid")
    open(p, "w").write(grid_text(
        g[0], g[1], g[2], g[3], g[4], g[5], depths=g[6],
        sta_max=g[10], nsta=g[11], max_nodes=g[12],
        comment="Replica de la grilla global de Glass3 (global_grid.d): 100 km, 10 capas."))
    files.append((g[0], g[1], g[2], g[3], g[4], g[5], len(g[6]), g[12], "global"))

    # regional
    p = os.path.join(a.out_dir, "chile_regional.grid")
    open(p, "w").write(grid_text(
        reg[0], reg[1], reg[2], reg[3], reg[4], reg[5],
        depths=reg_depths, dmin=reg[7], dmax=reg[8], dstep=reg[9],
        sta_max=reg[10], nsta=reg[11], max_nodes=reg[12],
        comment="Grilla regional de Chile (nivel 1): cobertura continua para los "
                "sismos que caen fuera de las locales."))
    nz_reg = len(reg_depths) if reg_depths else int((reg[8] - reg[7]) / reg[9]) + 1
    files.append((reg[0], reg[1], reg[2], reg[3], reg[4], reg[5], nz_reg, reg[12], "regional"))

    # locales
    for (nm, la0, la1, lo0, lo1) in locals_:
        p = os.path.join(a.out_dir, nm + ".grid")
        extra = ""
        if a.local_min_phases is not None:
            extra += ", MinPhases %d" % a.local_min_phases
        if a.local_backproj is not None:
            extra += ", BackProjThreshold %g" % a.local_backproj
        open(p, "w").write(grid_text(
            nm, la0, la1, lo0, lo1, a.node_km, depths=DEPTHS_LOCAL,
            sta_max=a.local_sta_max, nsta=a.local_nsta,
            max_nodes=a.local_max_nodes,
            min_phases=a.local_min_phases, backproj=a.local_backproj,
            comment="Grilla local %s (%g km, %d capas%s)."
                    % (nm, a.node_km, len(DEPTHS_LOCAL), extra)))
        files.append((nm, la0, la1, lo0, lo1, a.node_km, len(DEPTHS_LOCAL),
                      a.local_max_nodes, "local"))

    print("mk_csnloc_grids: tiling %s -> %s  (%d ficheros)"
          % (a.tiling, a.out_dir, len(files)))
    print("  %-14s %-7s %-7s %-9s %8s %9s  %s"
          % ("nombre", "nivel", "NodeKm", "capas", "nodos", "MaxNodes", "estado"))
    bad = 0
    for (nm, la0, la1, lo0, lo1, nk, nz, mx, lvl) in files:
        nx, ny, _, n = nodes(la0, la1, lo0, lo1, nk, nz)
        ok = "OK" if n <= mx else "ABORTA (>MaxNodes)"
        if n > mx:
            bad += 1
        print("  %-14s %-7s %-7.1f %-9d %8d %9d  %s"
              % (nm, lvl, nk, nz, n, mx, ok))
    if bad:
        print("  AVISO: %d grillas superan MaxNodes y harian ABORTAR el arranque" % bad)

    if a.check:
        la0, la1, lo0, lo1 = a.env
        st = a.cells
        nlat = int(round((la1 - la0) / st))
        nlon = int(round((lo1 - lo0) / st))
        # "debe cubrirse" = las locales de PRODUCCION (no regresion) + los
        # bboxes de Potin (mismo pie que el modelo 3D).
        must_old = [(l[1], l[2], l[3], l[4]) for l in LOCALS_CURRENT]
        must_pot = [(p[1], p[2], p[3], p[4]) for p in POTIN]
        new = [(l[1], l[2], l[3], l[4]) for l in locals_]
        tot = cov_new = cov_old = cov_pot = reg_old = reg_pot = 0
        huecos = {}
        for i in range(nlat):
            la = la0 + (i + 0.5) * st
            for j in range(nlon):
                lo = lo0 + (j + 0.5) * st
                tot += 1
                cn = any(in_box(la, lo, b) for b in new)
                co = any(in_box(la, lo, b) for b in must_old)
                cp = any(in_box(la, lo, b) for b in must_pot)
                cov_new += 1 if cn else 0
                cov_old += 1 if co else 0
                cov_pot += 1 if cp else 0
                if co and not cn:
                    reg_old += 1
                    huecos[int(la)] = huecos.get(int(la), 0) + 1
                if cp and not cn:
                    reg_pot += 1
        print()
        print("  --- cobertura (%g grados, sobre lat[%.1f,%.1f] lon[%.1f,%.1f]) ---"
              % (st, la0, la1, lo0, lo1))
        print("  celdas totales                        : %d" % tot)
        print("  cubiertas por >=1 local nueva         : %d (%.1f %%)"
              % (cov_new, 100.0 * cov_new / tot))
        print("  locales de PRODUCCION cubiertas       : %d" % cov_old)
        print("  REGRESION vs produccion (debe ser 0)  : %d" % reg_old)
        print("  bboxes de Potin cubiertos             : %d de %d (%.1f %%)"
              % (cov_pot, cov_pot, 100.0))
        print("  Potin NO cubierto por ninguna local   : %d" % reg_pot)
        if huecos:
            print("  franjas con regresion: %s"
                  % ", ".join("%d:%d" % kv for kv in sorted(huecos.items())))

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
