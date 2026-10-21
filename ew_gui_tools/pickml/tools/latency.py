#!/usr/bin/env python3
"""latency.py — latencia de los picks en vivo (JSONL de `pickml --ring`).

Cada línea del JSONL trae, además del tiempo de la picada:

  gen_t_epoch  hora de pared en que se escribió la picada
  lag_s        gen_t - t          (muestra -> picada escrita)
  proc_s       gen_t - fin_ventana (fin de la ventana -> escrita)

`lag_s` es lo que ve el consumidor (incluye la espera del algoritmo: la picada
no puede salir antes de que su ventana esté completa). `proc_s` aísla el coste
del proceso: si crece con el tiempo, el módulo no va al día.

    python3 tools/latency.py run_working_v8/pickml_live/*.jsonl
    python3 tools/latency.py pn.jsonl --bucket-min 10
"""

import argparse
import glob
import json
from datetime import datetime, timezone


def pct(v, q):
    """Percentil por rango más cercano (suficiente para diagnóstico)."""
    if not v:
        return 0.0
    v = sorted(v)
    i = min(len(v) - 1, max(0, int(round(q * (len(v) - 1)))))
    return v[i]


def fmt(v):
    return " ".join(f"{pct(v, q):7.2f}" for q in (0.5, 0.9, 0.99)) + \
           f" {max(v) if v else 0:7.2f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--bucket-min", type=int, default=10,
                    help="tamaño del bloque del resumen temporal (0 = no)")
    args = ap.parse_args()

    paths = []
    for pat in args.files:
        paths.extend(sorted(glob.glob(pat)) or [pat])

    for path in paths:
        picks = []
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line:
                    picks.append(json.loads(line))

        print(f"\n== {path}  ({len(picks)} picks)")
        if not picks:
            continue
        if "lag_s" not in picks[0]:
            print("   sin métricas de latencia (salida offline)")
            continue

        print(f"   {'fase':4s} {'n':>5s}  {'lag_s p50    p90    p99    max'}"
              f"   {'proc_s p50    p90    p99    max'}")
        for ph in ("P", "S"):
            sub = [p for p in picks if p["phase"] == ph]
            if not sub:
                continue
            lag = [p["lag_s"] for p in sub]
            proc = [p.get("proc_s", 0.0) for p in sub]
            print(f"   {ph:4s} {len(sub):5d}  {fmt(lag)}   {fmt(proc)}")

        if args.bucket_min <= 0:
            continue

        buckets = {}
        for p in picks:
            w = args.bucket_min * 60
            key = int(p["gen_t_epoch"] / w) * w
            buckets.setdefault(key, []).append(p)

        print(f"   por bloque de {args.bucket_min} min (hora de generación):")
        for key in sorted(buckets):
            sub = buckets[key]
            lag = [p["lag_s"] for p in sub]
            proc = [p.get("proc_s", 0.0) for p in sub]
            hh = datetime.fromtimestamp(key, timezone.utc).strftime("%H:%M")
            print(f"     {hh}  n={len(sub):4d}  lag_p50={pct(lag, 0.5):7.2f}  "
                  f"proc_p50={pct(proc, 0.5):6.2f}  proc_max={max(proc):6.2f}")


if __name__ == "__main__":
    main()
