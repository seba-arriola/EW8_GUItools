#!/usr/bin/env python3
"""eval_picks.py — recall de un picker contra picadas MANUALES de la BD.

Criterio acordado: para cada picada manual, ¿el modelo encontró algo en la
MISMA estación y fase, y coincide en tiempo dentro de tolerancia? Lo que el
modelo encuentre de más NO es falso positivo y NO penaliza: sólo suma.

    tools/eval_picks.py --ref picks_ref.csv \
        --pick eqt.jsonl [--pick phasenet.jsonl ...] \
        [--tol-p 2.0] [--tol-s 3.0] [--show-extras]

Emparejamiento por (net, sta, loc, phase) IGNORANDO canal: el modelo reporta
la S en HHN y la referencia puede estar en HHZ. La location `--` y la vacía son
la misma.
"""

import argparse
import csv
import json
import statistics
import sys


def norm_loc(loc):
    return "" if loc in ("--", "-", None) else loc


def load_ref(path):
    out = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            phase = (row.get("phase") or "").strip().upper()[:1]
            if phase not in ("P", "S"):
                continue
            out.append({
                "net": row["net"].strip(),
                "sta": row["sta"].strip(),
                "loc": norm_loc(row["loc"].strip()),
                "phase": phase,
                "t": float(row["t_ref"]),
                "weight": row.get("weight", ""),
            })
    return out


def load_picks(path):
    out = []
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        try:
            o = json.loads(line)
        except json.JSONDecodeError:
            continue
        phase = (o.get("phase") or "")[:1].upper()
        if phase not in ("P", "S"):
            continue
        out.append({
            "net": o["net"].strip(),
            "sta": o["sta"].strip(),
            "loc": norm_loc(o["loc"].strip()),
            "phase": phase,
            "t": float(o["t_epoch"]),
            "prob": o.get("prob"),
            "model": o.get("model", "?"),
        })
    return out


def evaluate(ref, picks, tol_p, tol_s, show_extras, minp):
    used = set()
    stats = {"P": {"n": 0, "match": 0, "dt": []},
             "S": {"n": 0, "match": 0, "dt": []}}
    extras = []

    for r in ref:
        tol = tol_p if r["phase"] == "P" else tol_s
        stats[r["phase"]]["n"] += 1

        best, best_dt = None, None
        for i, p in enumerate(picks):
            if i in used:
                continue
            if p["prob"] is not None and p["prob"] < minp[p["phase"]]:
                continue
            if (p["net"], p["sta"], p["loc"], p["phase"]) != \
               (r["net"], r["sta"], r["loc"], r["phase"]):
                continue
            dt = abs(p["t"] - r["t"])
            if dt <= tol and (best_dt is None or dt < best_dt):
                best, best_dt = i, dt

        if best is not None:
            used.add(best)
            stats[r["phase"]]["match"] += 1
            stats[r["phase"]]["dt"].append(best_dt)

    n_used = 0
    for i, p in enumerate(picks):
        if i in used:
            n_used += 1
            continue
        if p["prob"] is not None and p["prob"] < minp[p["phase"]]:
            continue
        extras.append(p)

    return stats, extras, n_used


def fmt(v):
    return "n/a" if not v else f"{v:.2f}"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ref", required=True, help="picks_ref.csv de pickml_fetch")
    ap.add_argument("--pick", action="append", required=True,
                    help="JSONL de un picker (repetible: uno por modelo)")
    ap.add_argument("--tol-p", type=float, default=2.0, help="tolerancia P [s]")
    ap.add_argument("--tol-s", type=float, default=3.0, help="tolerancia S [s]")
    ap.add_argument("--show-extras", action="store_true")
    ap.add_argument("--min-prob", type=float, default=0.0,
                    help="descarta picks con prob menor (ambas fases)")
    ap.add_argument("--min-prob-p", type=float, default=None)
    ap.add_argument("--min-prob-s", type=float, default=None)
    args = ap.parse_args()

    minp = {
        "P": args.min_prob_p if args.min_prob_p is not None else args.min_prob,
        "S": args.min_prob_s if args.min_prob_s is not None else args.min_prob,
    }

    ref = load_ref(args.ref)
    if not ref:
        sys.exit(f"eval_picks: {args.ref} sin picadas P/S")

    print(f"referencia: {len(ref)} picadas manuales "
          f"(P {sum(1 for r in ref if r['phase']=='P')}, "
          f"S {sum(1 for r in ref if r['phase']=='S')})")
    print(f"tolerancias: P ±{args.tol_p:g} s, S ±{args.tol_s:g} s")
    print()

    rc = 0
    for path in args.pick:
        picks = load_picks(path)
        model = picks[0]["model"] if picks else "?"
        stats, extras, n_used = evaluate(ref, picks, args.tol_p, args.tol_s,
                                         args.show_extras, minp)

        print(f"== {path}  (modelo {model}, {len(picks)} picks) ==")
        for phase in ("P", "S"):
            s = stats[phase]
            if s["n"] == 0:
                continue
            rec = 100.0 * s["match"] / s["n"]
            dt = sorted(s["dt"])
            med = statistics.median(dt) if dt else None
            p90 = dt[min(len(dt) - 1, int(0.9 * len(dt)))] if dt else None
            print(f"  {phase}: recall {s['match']}/{s['n']} = {rec:5.1f} %  "
                  f"|dt| mediana {fmt(med)} s  P90 {fmt(p90)} s")
        print(f"  extras (no penalizan): {len(extras)}"
              f"  [min_prob P {minp['P']:.2f} / S {minp['S']:.2f}]")
        if args.show_extras:
            for p in extras[:20]:
                print(f"    {p['sta']} {p['phase']} {p['t']:.3f} "
                      f"prob={p['prob']}")
        print()
        if stats["P"]["match"] == 0 and stats["S"]["match"] == 0:
            rc = 1

    return rc


if __name__ == "__main__":
    sys.exit(main())
