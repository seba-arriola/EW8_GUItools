#!/usr/bin/env python3
"""Valida que una imagen sirva como mapa base equirectangular (csnrv / csnstaevdisp).

Contrato impuesto por el codigo (ver ew_gui_tools/common/src/ewgui_geo.c):

    x = W * (lon + 180) / 360
    y = H * (90 - lat) / 180

Es decir, la imagen DEBE cubrir el mundo entero (lon -180..180, lat -90..90),
tener ancho = 2*alto exacto y no tener canal alpha (si lo tuviera, el fondo del
canvas se veria por los oceanos).

Uso:
    check_map_image.py <ruta> [--min-width N] [--json]

Salida (exit):
    0  OK
    2  no existe / ilegible
    3  formato no reconocido (solo PNG/JPEG)
    4  no cumple 2:1
    5  tiene canal alpha
    6  ancho < --min-width

Solo depende de la stdlib (parsea IHDR de PNG y marcadores SOF de JPEG).
"""

import argparse
import json
import struct
import sys
from pathlib import Path

PNG_SIG = b"\x89PNG\r\n\x1a\n"
JPEG_SOI = b"\xff\xd8"

# Marcadores SOF de JPEG (inicio de frame) que llevan dimensiones.
_JPEG_SOF = {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
             0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}


def _png_info(data):
    if not data.startswith(PNG_SIG):
        return None
    off, width, height, bit_depth, color_type, has_trns = 8, None, None, None, None, False
    n = len(data)
    while off + 8 <= n:
        (length,) = struct.unpack(">I", data[off:off + 4])
        ctype = data[off + 4:off + 8]
        body = off + 8
        if ctype == b"IHDR":
            if length < 13 or body + 13 > n:
                return None
            width, height, bit_depth, color_type = struct.unpack(
                ">IIBB", data[body:body + 10])
        elif ctype == b"tRNS":
            has_trns = True
        elif ctype == b"IEND":
            break
        off = body + length + 4
    if width is None:
        return None
    return {"format": "PNG", "width": width, "height": height,
            "bit_depth": bit_depth, "color_type": color_type,
            "has_trns": has_trns, "components": None}


def _jpeg_info(data):
    if not data.startswith(JPEG_SOI):
        return None
    n, i = len(data), 2
    while i + 1 < n:
        if data[i] != 0xFF:
            i += 1
            continue
        while i < n and data[i] == 0xFF:
            i += 1
        if i >= n:
            break
        marker = data[i]
        i += 1
        if marker == 0x00:                     # byte rellenado
            continue
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            continue
        if marker == 0xD9:                     # EOI
            break
        if i + 2 > n:
            break
        seglen = struct.unpack(">H", data[i:i + 2])[0]
        if seglen < 2 or i + seglen > n:
            break
        if marker in _JPEG_SOF:
            body = i + 2
            if body + 6 <= n:
                precision = data[body]
                height, width = struct.unpack(">HH", data[body + 1:body + 5])
                ncomp = data[body + 5]
                return {"format": "JPEG", "width": width, "height": height,
                        "bit_depth": precision, "color_type": None,
                        "has_trns": False, "components": ncomp}
            return None
        i += seglen
    return None


def _channels(info):
    if info["format"] == "JPEG":
        return info.get("components") or 3
    ct = info["color_type"]
    return {0: 1, 2: 3, 3: 3, 4: 2, 6: 4}.get(ct, 3)


def _has_alpha(info):
    if info["format"] == "JPEG":
        return False                            # JPEG nunca tiene alpha (4 comp = CMYK)
    ct = info["color_type"]
    if ct in (4, 6):
        return True
    if ct == 3 and info.get("has_trns"):
        return True
    return False


def _bytes_per_pixel(info):
    bpp = _channels(info)
    depth = info.get("bit_depth") or 8
    if depth in (8, 16):
        bpp *= depth // 8
    return bpp


def inspect(path):
    """Devuelve (info_dict, exit_code). exit_code 0 si todo OK."""
    p = Path(path)
    try:
        data = p.read_bytes()
    except OSError:
        return None, 2
    info = _png_info(data) or _jpeg_info(data)
    if info is None:
        return None, 3
    w, h = info["width"], info["height"]
    info["ratio"] = (w / h) if h else 0.0
    info["alpha"] = _has_alpha(info)
    info["bytes_per_pixel"] = _bytes_per_pixel(info)
    info["ram_mb"] = round(w * h * info["bytes_per_pixel"] / (1024 * 1024), 1)
    return info, 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="imagen a validar")
    ap.add_argument("--min-width", type=int, default=5400,
                    help="ancho minimo en px (default 5400)")
    ap.add_argument("--json", action="store_true", help="salida JSON")
    args = ap.parse_args(argv)

    info, code = inspect(args.path)
    if code != 0:
        reason = {2: "no existe / ilegible", 3: "formato no reconocido (solo PNG/JPEG)"}[code]
        if args.json:
            print(json.dumps({"ok": False, "exit": code, "path": args.path, "error": reason}))
        else:
            print("FAIL %s: %s" % (args.path, reason), file=sys.stderr)
        return code

    w, h = info["width"], info["height"]
    errs = []
    if w != 2 * h:
        errs.append((4, "no es 2:1 (ratio=%.6f, se exige 2.000000)" % info["ratio"]))
    if info["alpha"]:
        errs.append((5, "tiene canal alpha (los oceanos se verian transparentes)"))
    if w < args.min_width:
        errs.append((6, "ancho %d < min-width %d" % (w, args.min_width)))

    ok = not errs
    if args.json:
        out = dict(info)
        out.update({"ok": ok, "path": args.path,
                    "exit": 0 if ok else errs[0][0],
                    "errors": [m for _, m in errs]})
        print(json.dumps(out))
    else:
        status = "OK" if ok else "FAIL"
        print("%s %s: %s %dx%d ratio=%.6f alpha=%s bpp=%d ram~%.1fMB" % (
            status, args.path, info["format"], w, h, info["ratio"],
            str(info["alpha"]).lower(), info["bytes_per_pixel"], info["ram_mb"]))
        for _, m in errs:
            print("  - " + m, file=sys.stderr)
    return 0 if ok else errs[0][0]


if __name__ == "__main__":
    sys.exit(main())
