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
"""

import argparse
import datetime
import json
import os
import sys
import urllib.request
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_SRC = Path(os.environ.get("OVERLAY_SRC", "/tmp/opencode/overlay_src"))
DEFAULT_OUT = ROOT / "resources" / "overlays"

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


def _write_layer(path: Path, name, color, width, polylines) -> int:
    n = 0
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        f.write(f"#layer name={name} color={color} width={width:.1f}\n")
        for pl in polylines:
            for lon, lat in pl:
                f.write(f"{lon:.5f} {lat:.5f}\n")
            f.write(">\n")
            n += 1
    return n


def build_subduccion(src: Path, out: Path) -> int:
    """Zanja de subducción: solo límites PB2002 Type=subduction con placa
    Nazca/Sudamérica/Altiplano, recortados al margen chileno."""
    obj = json.loads((src / "pb2002.json").read_text())
    bbox = (-85.0, -58.0, -64.0, -14.0)
    keep = {"NZ", "SA", "AP"}
    polys = []
    for ft in obj.get("features", []):
        p = ft.get("properties") or {}
        if str(p.get("Type", "")).strip().lower() != "subduction":
            continue
        if not ({p.get("PlateA"), p.get("PlateB")} & keep):
            continue
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


def convert(src: Path, out: Path) -> int:
    out.mkdir(parents=True, exist_ok=True)
    n_sub = build_subduccion(src, out)
    n_reg = build_regiones(src, out)
    n_fal = build_fallas(src, out)
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
        "  (CC-BY-4.0). KMZ CHAF_Pangaea_v1.kmz.\n"
    )
    print(f"  subduccion.lines : {n_sub} polilineas")
    print(f"  regiones.lines   : {n_reg} polilineas")
    print(f"  fallas.lines     : {n_fal} polilineas")
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


def check(out: Path) -> int:
    rc = 0
    for name in ("subduccion.lines", "regiones.lines", "fallas.lines"):
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
    return rc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["fetch", "convert", "check", "all"])
    ap.add_argument("--src", type=Path, default=DEFAULT_SRC)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args(argv)

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
