#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fetch_responses.py - Genera ficheros SAC PZ (CONSTANT/POLES/ZEROS) a partir del
FDSNWS Station de metadata del CSN, listos para leer con csnmags (respuesta a
desplazamiento en nanómetros).

Es un script MANUAL e independiente: no enlaza ni depende de EarthWorm, y sólo
usa la librería estándar de Python (urllib + xml.etree + math). Corre en esta
máquina sin dependencias nuevas.

Uso:
  ./fetch_responses.py --network C1 --station FAR1 --channel 'HH?' \
      --out-dir ../../run_working_v8/params/responses

  ./fetch_responses.py --inventory local.xml --out-dir responses   # desde XML local

Convierte la respuesta a desplazamiento:
  - InputUnits == M/S  (velocidad): añade 1 cero en el origen
  - InputUnits == M/S**2 (aceleración): añade 2 ceros
  - InputUnits == M    (desplazamiento): no añade ceros
  CONSTANT = A0 * S0 (A0 = NormalizationFactor, S0 = sensibilidad instrumental),
  escalado de metros a nanómetros (÷1e9) salvo --units m.
"""

import argparse
import json
import math
import os
import sys
import urllib.request
import xml.etree.ElementTree as ET

DEFAULT_URL = "http://metadata.lan.csn.uchile.cl/"
NS = "{http://www.fdsn.org/xml/station/1}"


def _txt(el, path, default=None):
    if el is None:
        return default
    c = el.find(path)
    return c.text if c is not None and c.text is not None else default


def _float(el, path, default=None):
    t = _txt(el, path, None)
    try:
        return float(t)
    except (TypeError, ValueError):
        return default


def fetch(url, params, timeout=60):
    q = "&".join("%s=%s" % (k, v) for k, v in params.items())
    full = url.rstrip("/") + "/fdsnws/station/1/query?" + q
    req = urllib.request.Request(full, headers={"User-Agent": "csnmags-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def complex_pair(el):
    re = _float(el, NS + "Real", 0.0)
    im = _float(el, NS + "Imaginary", 0.0)
    return complex(re, im)


def extract_paz(channel):
    """Devuelve dict con constant (counts/m), zeros, poles para un Channel."""
    resp = channel.find(NS + "Response")
    if resp is None:
        return None

    # Sensibilidad instrumental y unidades de entrada
    sens = resp.find(NS + "InstrumentSensitivity")
    s0 = _float(sens, NS + "Value", None)
    f0 = _float(sens, NS + "Frequency", 1.0) or 1.0
    in_units = (_txt(sens, NS + "InputUnits/" + NS + "Name", "") or "").strip()

    # Etapa analógica PolesZeros
    pz_el = None
    for st in resp.findall(NS + "Stage"):
        cand = st.find(NS + "PolesZeros")
        if cand is not None:
            tf = _txt(cand, NS + "PzTransferFunctionType", "")
            if tf.startswith("LAPLACE"):
                pz_el = cand
                break
    if pz_el is None:
        return None

    tf = _txt(pz_el, NS + "PzTransferFunctionType", "")
    fn = _float(pz_el, NS + "NormalizationFrequency", f0) or f0
    a0 = _float(pz_el, NS + "NormalizationFactor", None)

    zeros = [complex_pair(z) for z in pz_el.findall(NS + "Zero")]
    poles = [complex_pair(p) for p in pz_el.findall(NS + "Pole")]

    # rad/s vs Hz
    if "HERTZ" in tf.upper():
        zeros = [z * 2.0 * math.pi for z in zeros]
        poles = [p * 2.0 * math.pi for p in poles]
        if a0 is not None:
            a0 = a0 * (2.0 * math.pi) ** (len(poles) - len(zeros))

    # A0 si falta: A0 = 1 / |prod(sn-z)/prod(sn-p)| en s=j2*pi*fn
    if a0 is None:
        s = complex(0.0, 2.0 * math.pi * fn)
        num = 1.0 + 0j
        den = 1.0 + 0j
        for z in zeros:
            num *= (s - z)
        for p in poles:
            den *= (s - p)
        h = den / num if num != 0 else 1.0 + 0j
        a0 = abs(h)

    if s0 is None:
        return None

    # Conversión a desplazamiento
    iu = in_units.upper().replace(" ", "")
    if iu in ("M/S", "M/SEC", "M/S**1"):
        n_add = 1
    elif iu in ("M/S**2", "M/S2", "M/SEC**2"):
        n_add = 2
    elif iu == "M":
        n_add = 0
    else:
        return {"skip": "unidades no soportadas: %s" % in_units}

    zeros_disp = list(zeros) + [complex(0.0, 0.0)] * n_add
    constant_m = a0 * s0                      # counts/metro
    return {
        "constant_m": constant_m,
        "zeros": zeros_disp,
        "poles": poles,
        "in_units": in_units,
        "sensitivity": s0,
    }


def fmt_complex(c):
    return "%.6e %.6e" % (c.real, c.imag)


def write_pz(path, paz, to_nm=True):
    scale = 1.0e-9 if to_nm else 1.0
    with open(path, "w") as f:
        f.write("* SAC PZ generado por fetch_responses.py\n")
        f.write("* INPUT UNIT: %s   OUTPUT: COUNTS\n" % ("NM" if to_nm else "M"))
        f.write("CONSTANT %.9e\n" % (paz["constant_m"] * scale))
        f.write("ZEROS %d\n" % len(paz["zeros"]))
        for z in paz["zeros"]:
            f.write(fmt_complex(z) + "\n")
        f.write("POLES %d\n" % len(paz["poles"]))
        for p in paz["poles"]:
            f.write(fmt_complex(p) + "\n")


def pattern_name(pattern, net, sta, loc, chan):
    out = pattern
    out = out.replace("%S", sta).replace("%C", chan).replace("%N", net)
    out = out.replace("%L", loc if loc else "--")
    return out


def main():
    ap = argparse.ArgumentParser(description="Genera SAC PZ desde FDSN StationXML")
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("--inventory", help="StationXML local (omite la red)")
    ap.add_argument("--network", default="*")
    ap.add_argument("--station", default="*")
    ap.add_argument("--channel", default="HH?")
    ap.add_argument("--location", default="*")
    ap.add_argument("--starttime", default="")
    ap.add_argument("--endtime", default="")
    ap.add_argument("--out-dir", default="responses")
    ap.add_argument("--pattern", default="%S_%C_%N.pz")
    ap.add_argument("--units", choices=["nm", "m"], default="nm")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.inventory:
        with open(args.inventory, "rb") as f:
            raw = f.read()
    else:
        params = {
            "network": args.network, "station": args.station,
            "channel": args.channel, "location": args.location,
            "level": "response", "format": "xml", "nodata": "404",
        }
        if args.starttime:
            params["starttime"] = args.starttime
        if args.endtime:
            params["endtime"] = args.endtime
        print("GET %s/fdsnws/station/1/query?%s" % (args.url.rstrip('/'),
              "&".join("%s=%s" % kv for kv in params.items())))
        raw = fetch(args.url, params)

    root = ET.fromstring(raw)
    os.makedirs(args.out_dir, exist_ok=True)

    manifest, n_ok, n_skip = {}, 0, 0
    for net_el in root.findall(NS + "Network"):
        net = net_el.get("code", "")
        for sta_el in net_el.findall(NS + "Station"):
            sta = sta_el.get("code", "")
            for ch_el in sta_el.findall(NS + "Channel"):
                chan = ch_el.get("code", "")
                loc = ch_el.get("locationCode", "") or "--"
                paz = extract_paz(ch_el)
                if paz is None:
                    n_skip += 1
                    continue
                if "skip" in paz:
                    n_skip += 1
                    print("skip %s.%s.%s: %s" % (sta, chan, net, paz["skip"]))
                    continue
                name = pattern_name(args.pattern, net, sta, loc, chan)
                path = os.path.join(args.out_dir, name)
                if not args.dry_run:
                    write_pz(path, paz, to_nm=(args.units == "nm"))
                manifest["%s.%s.%s.%s" % (net, sta, loc, chan)] = {
                    "file": name, "input_units": paz["in_units"],
                    "sensitivity": paz["sensitivity"],
                }
                n_ok += 1

    if not args.dry_run:
        with open(os.path.join(args.out_dir, "responses.manifest.json"), "w") as f:
            json.dump({"url": args.url, "pattern": args.pattern,
                       "units": args.units, "n": n_ok, "channels": manifest},
                      f, indent=1)
    print("OK: %d canales escritos, %d omitidos -> %s" % (n_ok, n_skip, args.out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
