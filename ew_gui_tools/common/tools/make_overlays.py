#!/usr/bin/env python3
"""Genera resources/overlays/*.lines (capa vectorial de csnrv/csnstaevdisp).

Fuentes publicas -> formato propio ".lines" (ver tools/OVERLAYS.md):
  - subduccion : PB2002 plate boundaries (Bird 2003), recortadas a Chile.
  - regiones   : Natural Earth 10m admin-1 lines, filtradas a Chile.
  - fallas     : CHAF (Maldonado et al. 2020, CC-BY-4.0), KMZ.

Solo stdlib (urllib, json, zipfile, xml.etree). Sin GDAL.

Uso:
  make_overlays.py all      # fetch + convert + check
  make_overlays.py fetch     [--src DIR]
  make_overlays.py convert   [--src DIR] [--out DIR]
  make_overlays.py check     [--out DIR]
  make_overlays.py check-lines <archivo.lines>... [--max-jump F]
"""

import argparse
import datetime
import json
import math
import os
import sys
import urllib.request
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_SRC = Path(os.environ.get("OVERLAY_SRC", "/tmp/opencode/overlay_src"))
# Los datos de runtime NO viven en el repo: disco de datos (EW_RESOURCES_ROOT).
RESOURCES_ROOT = Path(os.environ.get("EW_RESOURCES_ROOT", "/mnt/d/resources"))
DEFAULT_OUT = RESOURCES_ROOT / "overlays"

KML_NS = "http://www.opengis.net/kml/2.2"

SOURCES = {
    "pb2002.json":
        "https://raw.githubusercontent.com/fraxen/tectonicplates/master/"
        "GeoJSON/PB2002_boundaries.json",
    "ne_admin1_polys.geojson":
        "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/"
        "geojson/ne_10m_admin_1_states_provinces.geojson",
    "chaf.kmz":
        "https://download.pangaea.de/dataset/922241/files/CHAF_Pangaea_v1.kmz",
}


def fetch(src: Path) -> None:
    src.mkdir(parents=True, exist_ok=True)
    for name, url in SOURCES.items():
        dst = src / name
        if dst.exists() and dst.stat().st_size > 0:
            print(f"  cache  {name} ({dst.stat().st_size} bytes)")
            continue
        print(f"  baja   {name} ...")
        req = urllib.request.Request(url, headers={"User-Agent": "ewgui-overlays/1"})
        with urllib.request.urlopen(req, timeout=180) as r:
            data = r.read()
        dst.write_bytes(data)
        print(f"         {len(data)} bytes -> {dst}")


# --- GeoJSON ---------------------------------------------------------------

def _geometry_lines(geom):
    t = geom.get("type")
    c = geom.get("coordinates")
    if t == "LineString":
        yield c
    elif t == "MultiLineString":
        yield from c
    elif t == "Polygon":
        yield from c
    elif t == "MultiPolygon":
        for poly in c:
            yield from poly


def _prop(props, *names):
    for n in names:
        if n in props and props[n] not in (None, ""):
            return props[n]
    return None


def _clip_bbox(coords, bbox):
    """Recorta una polilínea a `bbox` (corta en los tramos fuera)."""
    lon0, lat0, lon1, lat1 = bbox
    runs, cur = [], []
    for p in coords:
        try:
            lon, lat = float(p[0]), float(p[1])
        except (TypeError, ValueError, IndexError):
            continue
        if lon0 <= lon <= lon1 and lat0 <= lat <= lat1:
            cur.append((lon, lat))
        else:
            if len(cur) >= 2:
                runs.append(cur)
            cur = []
    if len(cur) >= 2:
        runs.append(cur)
    return runs


def _write_polylines(f, polylines) -> int:
    """Escribe las polilineas en `f` (sin duplicados consecutivos y sin
    polilineas de <2 puntos) y devuelve cuantas escribio."""
    n = 0
    for pl in polylines:
        pts = []
        for lon, lat in pl:
            p = (round(lon, 5), round(lat, 5))   # se escribe con %.5f
            if pts and p == pts[-1]:
                continue
            pts.append(p)
        if len(pts) < 2:
            continue
        for lon, lat in pts:
            f.write(f"{lon:.5f} {lat:.5f}\n")
        f.write(">\n")
        n += 1
    return n


def _write_layer(path: Path, name, color, width, polylines) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        f.write(f"#layer name={name} color={color} width={width:.1f}\n")
        return _write_polylines(f, polylines)


def _append_layer(path: Path, name, color, width, polylines) -> int:
    """Agrega otra capa al mismo archivo .lines."""
    with path.open("a") as f:
        f.write(f"#layer name={name} color={color} width={width:.1f}\n")
        return _write_polylines(f, polylines)


def build_subduccion(src: Path, out: Path) -> int:
    """Zanja de subducción: límites PB2002 con placa Nazca/Sudamérica/Altiplano,
    recortados al margen chileno.

    El filtro normal es `Type=subduction`, pero ese campo viene VACÍO en algunas
    versiones de la fuente: si no deja nada, se cae al filtro solo por par de
    placas (el margen chileno ya acota la zona)."""
    obj = json.loads((src / "pb2002.json").read_text())
    bbox = (-85.0, -58.0, -64.0, -14.0)
    keep = {"NZ", "SA", "AP"}
    feats = [ft for ft in obj.get("features", [])
             if ({(ft.get("properties") or {}).get("PlateA"),
                  (ft.get("properties") or {}).get("PlateB")} & keep)]
    sub = [ft for ft in feats
           if str((ft.get("properties") or {}).get("Type", "")).strip().lower() == "subduction"]
    if not sub:
        print("  aviso: PB2002 sin campo Type; se filtra solo por par de placas")
        sub = feats
    polys = []
    for ft in sub:
        for coords in _geometry_lines(ft.get("geometry") or {}):
            polys.extend(_clip_bbox(coords, bbox))
    return _write_layer(out / "subduccion.lines", "subduccion", "CC3300", 1.6, polys)


def _polygon_rings(geom):
    """Rings exteriores de un Polygon/MultiPolygon."""
    t = geom.get("type")
    c = geom.get("coordinates")
    if t == "Polygon" and c:
        yield c[0]
    elif t == "MultiPolygon" and c:
        for poly in c:
            if poly:
                yield poly[0]


def _ring_bbox_area(ring):
    xs = [p[0] for p in ring]
    ys = [p[1] for p in ring]
    return (max(xs) - min(xs)) * (max(ys) - min(ys))


def build_regiones(src: Path, out: Path) -> int:
    """Contorno (polígono) de cada región de Chile (Natural Earth admin-1).

    Conserva el cuerpo principal y las islas grandes (bbox >= 0.1°²) para no
    llenar el mapa de islotes."""
    obj = json.loads((src / "ne_admin1_polys.geojson").read_text())
    area_min = 0.1
    polys = []
    for ft in obj.get("features", []):
        p = ft.get("properties") or {}
        if _prop(p, "adm0_a3", "ADM0_A3", "iso_a2") != "CHL":
            continue
        rings = list(_polygon_rings(ft.get("geometry") or {}))
        if not rings:
            continue
        rings.sort(key=_ring_bbox_area, reverse=True)
        for idx, ring in enumerate(rings):
            if idx > 0 and _ring_bbox_area(ring) < area_min:
                continue
            pts = []
            for pt in ring:
                try:
                    pts.append((float(pt[0]), float(pt[1])))
                except (TypeError, ValueError, IndexError):
                    pass
            if len(pts) >= 3:
                if pts[0] != pts[-1]:
                    pts.append(pts[0])   # cierra el anillo
                polys.append(pts)
    return _write_layer(out / "regiones.lines", "regiones", "4A6FA5", 0.8, polys)


def build_fallas(src: Path, out: Path) -> int:
    with zipfile.ZipFile(src / "chaf.kmz") as z:
        kml_name = next(n for n in z.namelist() if n.lower().endswith(".kml"))
        data = z.read(kml_name)
    root = ET.fromstring(data)
    polys = []
    for ls in root.iter(f"{{{KML_NS}}}LineString"):
        el = ls.find(f"{{{KML_NS}}}coordinates")
        if el is None or not el.text:
            continue
        pts = []
        for tok in el.text.split():
            parts = tok.split(",")
            if len(parts) >= 2:
                try:
                    pts.append((float(parts[0]), float(parts[1])))
                except ValueError:
                    pass
        if len(pts) >= 2:
            polys.append(pts)
    return _write_layer(out / "fallas.lines", "fallas", "8B4513", 1.0, polys)


def build_placas(src: Path, out: Path) -> int:
    """Márgenes de placas de TODO el mundo (PB2002, Bird 2003), sin recorte.

    Dos capas en el mismo archivo:
      - `placas`     : todos los límites (241 segmentos, cobertura mundial).
      - `placas_sub` : solo las zonas de subducción, si la fuente trae el campo
                       `Type` (en algunas versiones viene vacío: entonces se
                       omite la capa y se avisa)."""
    obj = json.loads((src / "pb2002.json").read_text())
    todas, subs = [], []
    for ft in obj.get("features", []):
        p = ft.get("properties") or {}
        es_sub = str(p.get("Type", "")).strip().lower() == "subduction"
        for coords in _geometry_lines(ft.get("geometry") or {}):
            todas.append(coords)
            if es_sub:
                subs.append(coords)
    n_all = _write_layer(out / "placas.lines", "placas", "6E6E6E", 0.9, todas)
    n_sub = 0
    if subs:
        n_sub = _append_layer(out / "placas.lines", "placas_sub", "CC3300", 1.2, subs)
    else:
        print("  aviso: PB2002 sin campo Type; placas.lines queda sin placas_sub")
    print(f"  placas.lines     : {n_all} polilineas (placas) + {n_sub} (placas_sub)")
    return n_all + n_sub


def convert(src: Path, out: Path) -> int:
    out.mkdir(parents=True, exist_ok=True)
    n_sub = build_subduccion(src, out)
    n_reg = build_regiones(src, out)
    n_fal = build_fallas(src, out)
    n_pla = build_placas(src, out)
    prov = out / "PROVENANCE.txt"
    prov.write_text(
        "Overlays de run_working_v8/params/overlays (generados por "
        "ew_gui_tools/common/tools/make_overlays.py)\n"
        f"Fecha: {datetime.date.today().isoformat()}\n\n"
        "subduccion.lines\n"
        "  Limites de placas PB2002 (Bird, P. 2003, G3 4(3), 1027).\n"
        "  Fuente: github.com/fraxen/tectonicplates (GeoJSON/PB2002_boundaries.json).\n"
        "  Filtro: Type=subduction, placas Nazca/Sudamerica/Altiplano; margen chileno.\n\n"
        "regiones.lines\n"
        "  Natural Earth 10m admin-1 states/provinces (poligonos, dominio publico).\n"
        "  Filtrado a Chile (adm0_a3=CHL): un contorno por region.\n\n"
        "fallas.lines\n"
        "  CHAF: Melnick, Maldonado & Contreras (2020), PANGAEA 922241\n"
        "  (CC-BY-4.0). KMZ CHAF_Pangaea_v1.kmz.\n\n"
        "placas.lines\n"
        "  Limites de placas PB2002 (Bird, P. 2003, G3 4(3), 1027) de TODO el\n"
        "  mundo, sin recorte. Fuente: github.com/fraxen/tectonicplates\n"
        "  (GeoJSON/PB2002_boundaries.json, ODC-By 1.0; credito a Hugo Ahlenius /\n"
        "  Nordpil y a Peter Bird).\n"
        "  Capa 'placas' (todos los limites) y capa 'placas_sub' (solo las zonas\n"
        "  de subduccion, campo Type=subduction).\n"
    )
    print(f"  subduccion.lines : {n_sub} polilineas")
    print(f"  regiones.lines   : {n_reg} polilineas")
    print(f"  fallas.lines     : {n_fal} polilineas")
    print(f"  placas.lines     : {n_pla} polilineas")
    return 0


# --- check -----------------------------------------------------------------

def parse_lines(path: Path):
    nlayers = npoly = npts = cur = 0
    if not path.exists():
        return None
    for ln in path.read_text().splitlines():
        s = ln.strip()
        if not s:
            continue
        if s.startswith("#"):
            body = s[1:].strip()
            if body.startswith("layer") and (len(body) == 5 or body[5].isspace()):
                if cur >= 2:
                    npoly += 1
                cur = 0
                nlayers += 1
            continue
        if s == ">":
            if cur >= 2:
                npoly += 1
            cur = 0
            continue
        cur += 1
        npts += 1
    if cur >= 2:
        npoly += 1
    return nlayers, npoly, npts


def check_lines(path: Path, max_jump: float = 2.0) -> int:
    """Valida UN archivo .lines.

    Estructura (rc=1): vertice mal formado, coordenada no numerica o fuera de
    rango, vertice antes de cualquier #layer, capa sin polilineas, sin capas.
    Coherencia (solo avisa, rc=0): saltos entre vertices consecutivos mayores
    que `max_jump` grados, vertices duplicados consecutivos y polilineas de un
    solo vertice (que el dibujo descarta).
    """
    if not path.exists():
        print(f"  FALTA: {path}")
        return 1

    errs = warns = 0
    layers = []
    cur = None
    poly = []

    def flush():
        nonlocal poly
        if cur is None:
            return
        if len(poly) >= 2:
            cur["polys"].append(poly)
            cur["npts"] += len(poly)
        elif poly:
            cur["tiny"] += 1
        poly = []

    for lineno, raw in enumerate(path.read_text().splitlines(), 1):
        s = raw.strip()
        if not s:
            continue
        if s.startswith("#"):
            body = s[1:].strip()
            if body.startswith("layer") and (len(body) == 5 or body[5].isspace()):
                flush()
                hdr = {"name": None, "color": None, "width": None}
                for tok in body.split()[1:]:
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        if k in hdr:
                            hdr[k] = v
                cur = {"name": hdr["name"] or f"layer{len(layers) + 1}",
                       "color": hdr["color"], "width": hdr["width"], "lineno": lineno,
                       "polys": [], "tiny": 0, "npts": 0}
                layers.append(cur)
            continue
        if s == ">":
            flush()
            continue
        parts = s.split()
        if len(parts) < 2:
            print(f"  ERR {path}:{lineno}: vertice mal formado '{s}'")
            errs += 1
            continue
        try:
            lon, lat = float(parts[0]), float(parts[1])
        except ValueError:
            print(f"  ERR {path}:{lineno}: coordenada no numerica '{s}'")
            errs += 1
            continue
        if not (-180.0 <= lon <= 180.0 and -90.0 <= lat <= 90.0):
            print(f"  ERR {path}:{lineno}: coordenada fuera de rango ({lon}, {lat})")
            errs += 1
            continue
        if cur is None:
            print(f"  ERR {path}:{lineno}: vertice antes de cualquier #layer")
            errs += 1
            continue
        poly.append((lon, lat, lineno))
    flush()

    if not layers:
        print(f"  ERR {path}: sin ninguna capa (#layer)")
        errs += 1
    for L in layers:
        if not L["polys"]:
            print(f"  ERR {path}:{L['lineno']}: capa '{L['name']}' sin polilineas")
            errs += 1
        if L["tiny"]:
            print(f"  WARN {path}: capa '{L['name']}': {L['tiny']} polilinea(s) de 1 vertice")
            warns += 1
        jumps = dups = 0
        longest = 0.0
        for pl in L["polys"]:
            for a, b in zip(pl, pl[1:]):
                d = math.hypot(a[0] - b[0], a[1] - b[1])
                if d == 0.0:
                    dups += 1
                if d > longest:
                    longest = d
                if d > max_jump:
                    jumps += 1
                    if jumps <= 3:
                        print(f"  WARN {path}: salto de {d:.2f} grados entre las lineas "
                              f"{a[2]} y {b[2]} (capa '{L['name']}')")
        if jumps:
            print(f"  WARN {path}: capa '{L['name']}': {jumps} salto(s) > {max_jump} grados "
                  f"(max {longest:.2f})")
            warns += 1
        if dups:
            print(f"  WARN {path}: capa '{L['name']}': {dups} vertice(s) duplicado(s) consecutivo(s)")
            warns += 1
        print(f"  OK   {path}: capa '{L['name']}' polilineas={len(L['polys'])} "
              f"vertices={L['npts']} salto_max={longest:.3f}")
    if errs:
        print(f"  ==> {path}: {errs} error(es) estructural(es), {warns} aviso(s)")
        return 1
    return 0


def check(out: Path) -> int:
    rc = 0
    for name in ("subduccion.lines", "regiones.lines", "fallas.lines", "placas.lines"):
        p = out / name
        r = parse_lines(p)
        if r is None:
            print(f"  FALTA: {p}")
            rc = 1
            continue
        nl, np_, npt = r
        status = "OK" if nl > 0 and np_ > 0 else "FAIL"
        if status == "FAIL":
            rc = 1
        print(f"  {status} {name}: capas={nl} polilineas={np_} vertices={npt}")
        # Las capas locales son densas; la mundial (PB2002) tiene segmentos
        # largos legitimos (hasta ~4 grados), asi que se le da mas margen.
        jump = 10.0 if name == "placas.lines" else 2.0
        if check_lines(p, jump) != 0:
            rc = 1
    return rc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["fetch", "convert", "check", "check-lines", "all"])
    ap.add_argument("files", nargs="*", help="archivos .lines para check-lines")
    ap.add_argument("--src", type=Path, default=DEFAULT_SRC)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--max-jump", type=float, default=2.0,
                    help="salto maximo entre vertices consecutivos, en grados (solo avisa)")
    args = ap.parse_args(argv)

    if args.cmd == "check-lines":
        if not args.files:
            print("check-lines: faltan archivos .lines", file=sys.stderr)
            return 2
        rc = 0
        for f in args.files:
            if check_lines(Path(f), args.max_jump) != 0:
                rc = 1
        return rc

    if args.cmd in ("fetch", "all"):
        print("== fetch ==")
        fetch(args.src)
    if args.cmd in ("convert", "all"):
        print("== convert ==")
        convert(args.src, args.out)
    if args.cmd in ("check", "all"):
        print("== check ==")
        return check(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
