#!/usr/bin/env python3
"""Test hermetico de build_placas (make_overlays.py): capas, cobertura mundial y
capa de subduccion, sin red y sin tocar resources/."""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import make_overlays as mo   # noqa: E402

fails = 0


def ck(cond, msg):
    global fails
    if cond:
        print(f"ok  : {msg}")
    else:
        print(f"FAIL: {msg}")
        fails += 1


def feat(lon0, lat0, lon1, lat1, tipo="", pa="NZ", pb="SA"):
    return {"type": "Feature",
            "properties": {"PlateA": pa, "PlateB": pb, "Type": tipo},
            "geometry": {"type": "LineString",
                         "coordinates": [[lon0, lat0],
                                         [(lon0 + lon1) / 2.0, (lat0 + lat1) / 2.0],
                                         [lon1, lat1]]}}


def layers_of(txt):
    layers = {}
    cur = None
    for ln in txt.splitlines():
        s = ln.strip()
        if s.startswith("#layer"):
            for tok in s.split():
                if tok.startswith("name="):
                    cur = tok.split("=", 1)[1]
                    layers[cur] = []
        elif s and s != ">" and cur:
            layers[cur].append(tuple(float(v) for v in s.split()))
    return layers


with tempfile.TemporaryDirectory() as td:
    src, out = Path(td), Path(td) / "out"

    # Un limite al oeste, otro sin Type al este y la fosa chilena.
    (src / "pb2002.json").write_text(json.dumps({
        "type": "FeatureCollection",
        "features": [feat(-120, 40, -100, 30, "subduction"),
                     feat(20, -20, 40, -30, ""),
                     feat(-75, -20, -70, -30, "subduction")]}))

    n = mo.build_placas(src, out)
    ck(n == 5, f"build_placas devuelve 5 polilineas (3 + 2, devuelve {n})")
    txt = (out / "placas.lines").read_text()
    ck("name=placas " in txt, "capa 'placas' presente")
    ck("name=placas_sub " in txt, "capa 'placas_sub' presente")

    L = layers_of(txt)
    ck(len(L.get("placas", [])) == 9, f"capa placas con 9 vertices (tiene {len(L.get('placas', []))})")
    ck(len(L.get("placas_sub", [])) == 6,
       f"capa placas_sub con 6 vertices (tiene {len(L.get('placas_sub', []))})")
    ck(all(-180.0 <= lon <= 180.0 and -90.0 <= lat <= 90.0 for lon, lat in L["placas"]),
       "coordenadas dentro de rango")
    ck(any(lon > 0 for lon, _ in L["placas"]), "placas cubre tambien el hemisferio este")
    ck(any(-76 <= lon <= -70 and -31 <= lat <= -19 for lon, lat in L["placas_sub"]),
       "placas_sub incluye la fosa chilena")
    ck(not any(-76 <= lon <= -70 and -31 <= lat <= -19 for lon, lat in
               [p for p in L["placas"] if p not in L["placas_sub"]]),
       "la capa placas incluye ademas los limites no-subduccion")

    # Sin campo Type: se omite placas_sub y placas sigue existiendo.
    (src / "pb2002.json").write_text(json.dumps({
        "type": "FeatureCollection",
        "features": [feat(-120, 40, -100, 30, ""), feat(20, -20, 40, -30, "")]}))
    mo.build_placas(src, out)
    txt2 = (out / "placas.lines").read_text()
    ck("name=placas_sub" not in txt2, "sin Type: no se crea placas_sub")
    ck("name=placas " in txt2, "sin Type: placas sigue existiendo")

if fails:
    print(f"FALLOS: {fails}")
    sys.exit(1)
print("OK test_make_overlays")
