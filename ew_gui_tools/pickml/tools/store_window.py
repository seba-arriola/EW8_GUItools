#!/usr/bin/env python3
"""store_window.py — ventana 3C del store en un trace, y búsqueda de eventos reales.

Dos usos, ambos OFFLINE (no tocan el sistema en marcha):

1) Materializar una ventana 3C para pickml / el fixture dorado:

       python3 tools/store_window.py --root ../wstore \
           --scnl CX.PB15.--.HHZ --t0 1792473299.558 --dur 60 \
           --out /tmp/win.trace

   Equivale a tres `wstore_export` + `cat`, pero alinea por TIEMPO: en un stream
   real cada componente puede empezar en un instante distinto (se han medido
   6.7 s) y concatenar por componente desplaza unas respecto de otras.

2) Encontrar sismos de verdad en el store:

       python3 tools/store_window.py --root ../wstore --scan

   Ordena por peak-to-peak de 1 s, NO por max|x|: hay estaciones con offset DC
   de millones de counts que ganarían ese ranking sin ser sismos (medido: un
   "máximo" de 7.7M counts era offset puro).

El formato leído es el del store documentado en ew_gui_tools/wavestore/README.md:
<root>/<NET>.<STA>.<LOC>.<CHAN>/<YYYYMMDD>.trace con TRACE2_HEADER + int32.
"""
import argparse
import glob
import os
import struct
import sys

try:
    import numpy as np
except ImportError:
    np = None

HDR = struct.Struct("<iiddd7s9s4s3s2s3s2s2s")


def read_records(path):
    with open(path, "rb") as f:
        while True:
            h = f.read(HDR.size)
            if len(h) < HDR.size:
                return
            pin, ns, t0, t1, rate, sta, net, chan, loc, v, dt, q, pad = HDR.unpack(h)
            raw = f.read(ns * 4)
            if len(raw) < ns * 4:
                return
            yield {
                "t0": t0, "rate": rate, "nsamp": ns,
                "sta": sta.rstrip(b"\0").decode(),
                "net": net.rstrip(b"\0").decode(),
                "chan": chan.rstrip(b"\0").decode(),
                "loc": loc.rstrip(b"\0").decode(),
                "x": struct.unpack(f"<{ns}i", raw),
            }


def write_traces(path, recs, rate):
    with open(path, "wb") as f:
        for r in recs:
            n = len(r["x"])
            hdr = HDR.pack(0, n, r["t0"], r["t0"] + (n - 1) / rate, rate,
                           r["sta"].encode(), r["net"].encode(), r["chan"].encode(),
                           r["loc"].encode(), b"20", b"i4", b"", b"")
            f.write(hdr)
            f.write(struct.pack(f"<{n}i", *r["x"]))


def scnl_dir(root, scnl):
    d = os.path.join(root, scnl)
    if not os.path.isdir(d):
        sys.exit(f"store_window: no existe {d}")
    return d


def materialize(args):
    net, sta, loc, chan = args.scnl.split(".")
    # Las 3 componentes: se derivan del canal pedido (HH?/BH?).
    prefix, comp = chan[:-1], chan[-1]
    comps = [prefix + c for c in ("Z", "N", "E")]
    nsamp = int(round(args.dur * 100.0))
    grid = {c: [None] * nsamp for c in comps}
    got = {c: 0 for c in comps}
    rate_seen = None

    for c in comps:
        d = scnl_dir(args.root, f"{net}.{sta}.{loc}.{c}")
        for trace in sorted(glob.glob(os.path.join(d, "*.trace"))):
            for r in read_records(trace):
                rate = r["rate"] or 100.0
                rate_seen = rate_seen or rate
                idx0 = int(round((r["t0"] - args.t0) * rate))
                for i, v in enumerate(r["x"]):
                    k = idx0 + i
                    if 0 <= k < nsamp and grid[c][k] is None:
                        grid[c][k] = v
                        got[c] += 1

    missing = [c for c in comps if got[c] < nsamp]
    if missing:
        sys.exit(f"store_window: la ventana no cubre {nsamp} muestras de "
                 f"{missing} (cobertura {got})")

    recs = [{"t0": args.t0, "sta": sta, "net": net, "loc": loc,
             "chan": c, "x": grid[c]} for c in comps]
    write_traces(args.out, recs, rate_seen or 100.0)
    print(f"store_window: {args.out} ({nsamp} muestras x 3, {args.dur:.0f} s, "
          f"rate {rate_seen:.0f} Hz)")


def scan(args):
    if np is None:
        sys.exit("store_window --scan necesita numpy")
    res = []
    for p in sorted(glob.glob(os.path.join(args.root, "*.HHZ", "*.trace"))):
        scnl = os.path.basename(os.path.dirname(p))
        vmax, tmax = 0, 0.0
        for r in read_records(p):
            x = np.asarray(r["x"], dtype=np.int64)
            nb = len(x) // 100
            if not nb:
                continue
            b = x[: nb * 100].reshape(nb, 100)
            ptp = b.max(axis=1) - b.min(axis=1)
            k = int(ptp.argmax())
            if ptp[k] > vmax:
                vmax, tmax = int(ptp[k]), r["t0"] + k / (r["rate"] or 100.0)
        res.append((vmax, scnl, tmax))
    res.sort(reverse=True)
    print(f"estaciones con HHZ: {len(res)}")
    for vmax, scnl, tmax in res[:args.top]:
        print(f"{vmax:9d}  {scnl:24s} t_ptp={tmax:.3f}")


def main():
    ap = argparse.ArgumentParser(description="Ventana 3C del store / búsqueda de eventos")
    ap.add_argument("--root", required=True, help="raíz del store (p. ej. ../wstore)")
    ap.add_argument("--scnl", help="NET.STA.LOC.CHAN de la componente Z")
    ap.add_argument("--t0", type=float, help="epoch de inicio de la ventana")
    ap.add_argument("--dur", type=float, default=60.0, help="segundos (default 60)")
    ap.add_argument("--out", help="trace de salida")
    ap.add_argument("--scan", action="store_true", help="rankear transitorios del store")
    ap.add_argument("--top", type=int, default=12)
    args = ap.parse_args()

    if args.scan:
        scan(args)
        return
    if not (args.scnl and args.t0 is not None and args.out):
        ap.error("sin --scan hacen falta --scnl, --t0 y --out")
    materialize(args)


if __name__ == "__main__":
    main()
