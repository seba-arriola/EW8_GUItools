#!/usr/bin/env python3
"""sweep_post.py — barrido offline del post-proceso de `pickml`.

Reimplementa EXACTAMENTE lo que hace el C despues del grafo:

  * `pm_post_peaks`      (pickml_post.c:15)  region contigua >= umbral -> su maximo
  * `pm_gate_s_vs_p`     (pickml_post.c:94)  S a menos de MinSPSec de un P de la ventana
  * `emit_pick`          (pickml_main.c:229) peso W0..W3, compuerta S-P entre ventanas,
                                             dedup por fase (la primera emision gana)

y lo aplica sobre las curvas crudas volcadas con `pickml --dump-curves`, usando el
sidecar `<prefijo>.idx` (una linea por ventana: t0 rate win sta net loc chan).

Asi el ajuste de umbrales y geometria no vuelve a correr el modelo y se puede
barrer en segundos. La salida `--emit` usa el mismo esquema que el JSONL del C.

Uso:
  tools/sweep_post.py --root run_working_v8/pickml/calib \
      --p-thrs 0.10,0.20,0.30,0.40,0.50 --s-thrs 0.10,0.20,0.30,0.40,0.50 \
      --min-pos 2 --grid /tmp/opencode/grid.csv

  tools/sweep_post.py --case run_working_v8/pickml/calib/E794... \
      --p-thr 0.30 --s-thr 0.30 --emit /tmp/opencode/out.jsonl --probe

Solo stdlib. No toca la red ni la BD.
"""

import argparse
import array
import csv
import json
import math
import os
import struct
import sys
from datetime import datetime, timezone
from itertools import product

PHASES = ("P", "S")
MAX_EMIT = 64          # PM_MAX_EMIT
DEDUP_SLOTS = 256      # PM_DEDUP_SLOTS


# ------------------------------------------------------------------ utilidades

def f32(x):
    """Redondeo a float32, como el cast del C al comparar umbrales."""
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def read_f32(path):
    a = array.array("f")
    n = os.path.getsize(path) // 4
    with open(path, "rb") as fh:
        a.fromfile(fh, n)
    return a


def read_idx(path):
    rows = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = line.split()
            if len(p) != 7:
                raise SystemExit(f"{path}: linea con {len(p)} campos: {line!r}")
            rows.append({
                "t0": float(p[0]),
                "rate": float(p[1]),
                "win": int(p[2]),
                "sta": p[3], "net": p[4], "loc": norm_loc(p[5]), "chan": p[6],
            })
    return rows


def read_ref(path):
    """picks_ref.csv: net,sta,loc,chan,phase,t_ref,... (DictReader)."""
    out = []
    with open(path, newline="", encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            ph = (r.get("phase") or "").strip()
            if ph not in PHASES:
                continue
            try:
                t = float(r["t_ref"])
            except (KeyError, ValueError):
                continue
            out.append({
                "net": (r.get("net") or "").strip(),
                "sta": (r.get("sta") or "").strip(),
                "loc": norm_loc(r.get("loc")),
                "phase": ph,
                "t": t,
            })
    return out


def norm_loc(v):
    v = (v or "").strip()
    return "" if v in ("--", "-", "nan", "None") else v


def iso_utc(t):
    dt = datetime.fromtimestamp(t, tz=timezone.utc)
    return dt.strftime("%Y-%m-%d %H:%M:%S.") + f"{int(round((t % 1) * 1000)) % 1000:03d}"


# ------------------------------------------------------- post-proceso (espejo)

def scan_window(x, i0, i1, floor):
    """Maximos locales de x[i0..i1] con valor >= floor y sus saddles.

    `saddles[k]` es el minimo entre los maximos k y k+1: el nivel al que dos
    regiones se fusionan. Con eso, `regions_at` reproduce para CUALQUIER umbral
    la particion en regiones contiguas de `pm_post_peaks`.
    """
    peaks = []
    i = i0
    while i <= i1:
        v = x[i]
        if v < floor:
            i += 1
            continue
        left = (i == i0) or (x[i] > x[i - 1])
        right = (i == i1) or (x[i] >= x[i + 1])
        if left and right:
            peaks.append(i)
        i += 1

    vals = [x[p] for p in peaks]
    saddles = []
    for a, b in zip(peaks, peaks[1:]):
        m = x[a]
        for k in range(a + 1, b + 1):
            v = x[k]
            if v < m:
                m = v
        saddles.append(m)
    return peaks, vals, saddles


def regions_at(peaks, vals, saddles, thr):
    """[(idx, val)] de las regiones >= thr, con el maximo (primera aparicion)."""
    out = []
    last = None
    for k in range(len(peaks)):
        if vals[k] < thr:
            if last is not None:
                out.append(last)
                last = None
            continue
        if last is not None and saddles[k - 1] >= thr:
            if vals[k] > last[1]:
                last = (peaks[k], vals[k])
        else:
            if last is not None:
                out.append(last)
            last = (peaks[k], vals[k])
    if last is not None:
        out.append(last)
    return out


def weight_for(prob, wprob):
    for i in range(4):
        if prob >= f32(wprob[i]):
            return i
    return None


class StationState:
    """Estado por estacion: dedup (ring de 256) y ultimo P aceptado."""

    __slots__ = ("dedup", "last_p_t", "have_p")

    def __init__(self):
        self.dedup = []          # [(t, phase)]
        self.last_p_t = 0.0
        self.have_p = False

    def accept_dedup(self, t, phase, dedup_sec):
        if dedup_sec <= 0.0:
            return True
        for (tt, pp) in self.dedup:
            if pp == phase and abs(tt - t) <= dedup_sec:
                return False
        if len(self.dedup) < DEDUP_SLOTS:
            self.dedup.append((t, phase))
        else:
            oldest = min(range(len(self.dedup)), key=lambda i: self.dedup[i][0])
            self.dedup[oldest] = (t, phase)
        return True


def emit_pick(state, phase, t, prob, win_t0, cfg, sta, net, loc, chan_p, chan_s):
    weight = weight_for(prob, cfg["wprob"])
    if weight is None:
        return None
    if (phase == "S" and cfg["min_sp"] > 0.0 and state.have_p
            and t >= state.last_p_t and (t - state.last_p_t) < cfg["min_sp"]):
        return None
    if not state.accept_dedup(t, phase, cfg["dedup"]):
        return None
    if phase == "P":
        state.last_p_t = t
        state.have_p = True
    return {
        "sta": sta, "net": net, "loc": loc,
        "chan": chan_p if phase == "P" else chan_s,
        "phase": phase, "t_epoch": t, "t_utc": iso_utc(t),
        "prob": prob, "weight": weight, "win_t0": win_t0,
    }


def run_case(windows, curves_p, curves_s, cfg, model="phasenetwc"):
    """Aplica el post-proceso a un caso completo; devuelve la lista de picks."""
    states = {}
    picks = []
    win = cfg["win"]
    rate = cfg["rate"]

    for wi, w in enumerate(windows):
        key = (w["net"], w["sta"], w["loc"])
        st = states.get(key)
        if st is None:
            st = states[key] = StationState()

        base = wi * win
        cp = curves_p[base:base + win]
        cs = curves_s[base:base + win]

        i0 = int(math.ceil(cfg["min_pos"] * rate))
        i1 = int(math.floor(cfg["max_pos"] * rate)) if cfg["max_pos"] > 0.0 else win - 1
        if i0 < 0:
            i0 = 0
        if i1 >= win:
            i1 = win - 1
        if i0 > i1:
            continue

        pp = regions_at(*cfg["_peaks_p"][wi], cfg["p_thr"])[:MAX_EMIT]
        ss = regions_at(*cfg["_peaks_s"][wi], cfg["s_thr"])[:MAX_EMIT]
        # `scan_window` trabaja sobre el array completo: los indices vienen
        # absolutos y hay que pasarlos a la ventana antes de armar el tiempo.
        pp = [(i - base, v) for (i, v) in pp]
        ss = [(i - base, v) for (i, v) in ss]

        tp = [(w["t0"] + i / rate) for (i, _) in pp]
        ts = [(w["t0"] + i / rate) for (i, _) in ss]

        # pm_gate_s_vs_p (intra-ventana)
        if tp and cfg["min_sp"] > 0.0:
            kept = []
            for (i, v), t in zip(ss, ts):
                drop = False
                for p in tp:
                    dt = t - p
                    if 0.0 <= dt < cfg["min_sp"]:
                        drop = True
                        break
                if not drop:
                    kept.append((i, v, t))
            ss = [(i, v) for (i, v, _) in kept]
            ts = [t for (_, _, t) in kept]

        chp = w["chan"]
        chs = chp[:-1] + "N" if chp.endswith("Z") else chp

        for (i, v), t in zip(pp, tp):
            r = emit_pick(st, "P", t, v, w["t0"], cfg, w["sta"], w["net"],
                          w["loc"], chp, chs)
            if r:
                r["model"] = model
                picks.append(r)
        for (i, v), t in zip(ss, ts):
            r = emit_pick(st, "S", t, v, w["t0"], cfg, w["sta"], w["net"],
                          w["loc"], chp, chs)
            if r:
                r["model"] = model
                picks.append(r)
    return picks


# --------------------------------------------------------------------- metricas

def match_picks(refs, picks, tol_p, tol_s):
    """1-a-1 greedy por |dt| sobre (net,sta,loc,phase); canal ignorado."""
    from collections import defaultdict

    idx_ref = defaultdict(list)
    for i, r in enumerate(refs):
        idx_ref[(r["net"], r["sta"], r["loc"], r["phase"])].append(i)
    idx_pick = defaultdict(list)
    for i, p in enumerate(picks):
        idx_pick[(p["net"], p["sta"], p["loc"], p["phase"])].append(i)

    pairs = []
    for key, ris in idx_ref.items():
        pis = idx_pick.get(key)
        if not pis:
            continue
        tol = tol_p if key[3] == "P" else tol_s
        for ri in ris:
            for pi in pis:
                dt = abs(refs[ri]["t"] - picks[pi]["t_epoch"])
                if dt <= tol:
                    pairs.append((dt, ri, pi))
    pairs.sort(key=lambda x: x[0])
    used_r, used_p = set(), set()
    out = {}
    for dt, ri, pi in pairs:
        if ri in used_r or pi in used_p:
            continue
        used_r.add(ri)
        used_p.add(pi)
        out[ri] = (pi, dt)
    return out, len(used_p)


def stats_for(refs, picks, tol_p, tol_s):
    matched, nused = match_picks(refs, picks, tol_p, tol_s)
    res = {}
    for ph in PHASES:
        nref = sum(1 for r in refs if r["phase"] == ph)
        tp = sum(1 for ri in matched if refs[ri]["phase"] == ph)
        res[ph] = {"ref": nref, "tp": tp, "fn": nref - tp,
                   "recall": (tp / nref) if nref else float("nan")}
    res["picks"] = len(picks)
    res["extra"] = len(picks) - nused
    return res


# ------------------------------------------------------------------- casos

def collect_cases(root, explicit):
    dirs = []
    if root:
        for name in sorted(os.listdir(root)):
            d = os.path.join(root, name)
            if os.path.isdir(d) and os.path.exists(os.path.join(d, "curve.idx")):
                dirs.append(d)
    dirs.extend(explicit or [])
    return dirs


def load_case(d):
    idx = os.path.join(d, "curve.idx")
    windows = read_idx(idx)
    if not windows:
        raise SystemExit(f"{idx}: sin ventanas")
    win = windows[0]["win"]
    rate = windows[0]["rate"]
    cp = read_f32(os.path.join(d, "curve.p.f32"))
    cs = read_f32(os.path.join(d, "curve.s.f32"))
    if len(cp) != len(windows) * win or len(cs) != len(windows) * win:
        raise SystemExit(
            f"{d}: curvas inconsistentes con el .idx "
            f"(P {len(cp)} S {len(cs)}, ventanas {len(windows)} x {win})")
    refp = os.path.join(d, "picks_ref.csv")
    refs = read_ref(refp) if os.path.exists(refp) else []
    return {"dir": d, "windows": windows, "p": cp, "s": cs,
            "refs": refs, "win": win, "rate": rate}


def probe_refs(case, floor=0.02):
    """¿El modelo 'vio' la picada de referencia? max prob en +-1 s, por fase."""
    buckets = {ph: {"n": 0, "ge50": 0, "ge30": 0, "ge20": 0, "ge10": 0, "lt10": 0}
               for ph in PHASES}
    win, rate = case["win"], case["rate"]
    by_sta = {}
    for wi, w in enumerate(case["windows"]):
        by_sta.setdefault((w["net"], w["sta"], w["loc"]), []).append(wi)

    for r in case["refs"]:
        wis = by_sta.get((r["net"], r["sta"], r["loc"]))
        if not wis:
            continue
        curve = case["p"] if r["phase"] == "P" else case["s"]
        best = 0.0
        for wi in wis:
            w = case["windows"][wi]
            span = win / rate
            if not (w["t0"] <= r["t"] <= w["t0"] + span):
                continue
            c = int(round((r["t"] - w["t0"]) * rate))
            lo = max(0, c - int(round(rate)))
            hi = min(win - 1, c + int(round(rate)))
            base = wi * win
            for k in range(lo, hi + 1):
                v = curve[base + k]
                if v > best:
                    best = v
        b = buckets[r["phase"]]
        b["n"] += 1
        if best >= 0.50:
            b["ge50"] += 1
        elif best >= 0.30:
            b["ge30"] += 1
        elif best >= 0.20:
            b["ge20"] += 1
        elif best >= 0.10:
            b["ge10"] += 1
        else:
            b["lt10"] += 1
    return buckets


# ---------------------------------------------------------------------- main

def parse_list(s):
    return [float(x) for x in s.split(",") if x.strip() != ""]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", help="directorio con un subdirectorio por caso")
    ap.add_argument("--case", action="append", default=[], help="caso explicito")
    ap.add_argument("--p-thrs", default="0.30", help="lista de umbrales P")
    ap.add_argument("--s-thrs", default="0.30", help="lista de umbrales S")
    ap.add_argument("--p-thr", type=float, default=None, help="umbral P unico")
    ap.add_argument("--s-thr", type=float, default=None, help="umbral S unico")
    ap.add_argument("--w0", type=float, default=0.90)
    ap.add_argument("--w1", type=float, default=0.70)
    ap.add_argument("--w2", type=float, default=0.50)
    ap.add_argument("--w3", type=float, default=0.30)
    ap.add_argument("--dedups", default="2.0", help="lista de DedupSec")
    ap.add_argument("--min-sps", default="2.0", help="lista de MinSPSec")
    ap.add_argument("--min-pos", type=float, default=2.0)
    ap.add_argument("--max-pos", type=float, default=0.0)
    ap.add_argument("--tol-p", type=float, default=2.0)
    ap.add_argument("--tol-s", type=float, default=3.0)
    ap.add_argument("--grid", help="CSV de salida del barrido")
    ap.add_argument("--emit", help="directorio donde escribir el JSONL del 1.er config")
    ap.add_argument("--probe", action="store_true",
                    help="tabla de prob del modelo en las referencias")
    ap.add_argument("--no-ref", action="store_true",
                    help="no exige picks_ref.csv (solo emitir picks)")
    args = ap.parse_args(argv)

    if args.p_thr is not None:
        p_thrs = [args.p_thr]
    else:
        p_thrs = parse_list(args.p_thrs)
    if args.s_thr is not None:
        s_thrs = [args.s_thr]
    else:
        s_thrs = parse_list(args.s_thrs)
    dedups = parse_list(args.dedups)
    min_sps = parse_list(args.min_sps)

    cases = collect_cases(args.root, args.case)
    if not cases:
        raise SystemExit("sin casos: usa --root <dir> o --case <dir>")

    cfgs = []
    for (pt, st, dd, ms) in product(p_thrs, s_thrs, dedups, min_sps):
        cfgs.append({
            "p_thr": f32(pt), "s_thr": f32(st),
            "wprob": [args.w0, args.w1, args.w2, args.w3],
            "dedup": dd, "min_sp": ms,
            "min_pos": args.min_pos, "max_pos": args.max_pos,
        })

    tot = [{"p_thr": c["p_thr"], "s_thr": c["s_thr"], "dedup": c["dedup"],
            "min_sp": c["min_sp"], "refP": 0, "tpP": 0, "refS": 0, "tpS": 0,
            "picks": 0, "extra": 0, "span": 0.0} for c in cfgs]
    probe = {ph: {"n": 0, "ge50": 0, "ge30": 0, "ge20": 0, "ge10": 0, "lt10": 0}
             for ph in PHASES}

    for d in cases:
        case = load_case(d)
        win, rate = case["win"], case["rate"]
        if not args.no_ref and not case["refs"]:
            print(f"aviso: {d} sin picks_ref.csv (se ignora en el recall)",
                  file=sys.stderr)
        if args.probe:
            pb = probe_refs(case)
            for ph in PHASES:
                for k in probe[ph]:
                    probe[ph][k] += pb[ph][k]

        for c in cfgs:
            c["win"] = win
            c["rate"] = rate

        # La geometria es fija en esta corrida: el barrido de maximos/saddles se
        # hace UNA vez por caso y lo comparten todos los umbrales.
        i0 = int(math.ceil(args.min_pos * rate))
        i1 = int(math.floor(args.max_pos * rate)) if args.max_pos > 0.0 else win - 1
        if i0 < 0:
            i0 = 0
        if i1 >= win:
            i1 = win - 1
        if i0 > i1:
            i0, i1 = 0, win - 1
        floor = min(min(p_thrs), min(s_thrs), 0.02)
        pre_p, pre_s = [], []
        for wi in range(len(case["windows"])):
            base = wi * win
            pre_p.append(scan_window(case["p"], base + i0, base + i1, floor))
            pre_s.append(scan_window(case["s"], base + i0, base + i1, floor))
        for c in cfgs:
            c["_peaks_p"] = pre_p
            c["_peaks_s"] = pre_s

        span = 0.0
        if case["windows"]:
            first = min(w["t0"] for w in case["windows"])
            last = max(w["t0"] for w in case["windows"])
            span = (last + win / rate) - first

        for c, acc in zip(cfgs, tot):
            picks = run_case(case["windows"], case["p"], case["s"], c)
            st = stats_for(case["refs"], picks, args.tol_p, args.tol_s)
            acc["refP"] += st["P"]["ref"]
            acc["tpP"] += st["P"]["tp"]
            acc["refS"] += st["S"]["ref"]
            acc["tpS"] += st["S"]["tp"]
            acc["picks"] += st["picks"]
            acc["extra"] += st["extra"]
            acc["span"] += span

        if args.emit and cfgs:
            os.makedirs(args.emit, exist_ok=True)
            name = os.path.basename(d.rstrip("/")) + ".jsonl"
            with open(os.path.join(args.emit, name), "w", encoding="utf-8") as fh:
                for p in run_case(case["windows"], case["p"], case["s"], cfgs[0]):
                    # el JSONL del C escribe "--" para la localizacion nula
                    p["loc"] = p.get("loc") or "--"
                    fh.write(json.dumps(p, ensure_ascii=False) + "\n")

    # tabla
    def rec(tp, ref):
        return (100.0 * tp / ref) if ref else float("nan")

    hdr = ["p_thr", "s_thr", "dedup", "min_sp",
           "recallP%", "recallS%", "picks", "picks/h", "extra"]
    rows = []
    for a in tot:
        rows.append([
            f"{a['p_thr']:.2f}", f"{a['s_thr']:.2f}", f"{a['dedup']:.1f}",
            f"{a['min_sp']:.1f}",
            f"{rec(a['tpP'], a['refP']):.1f}", f"{rec(a['tpS'], a['refS']):.1f}",
            str(a["picks"]),
            f"{a['picks'] / (a['span'] / 3600.0):.1f}" if a["span"] > 0 else "-",
            str(a["extra"]),
        ])

    w = [max(len(h), *(len(r[i]) for r in rows)) for i, h in enumerate(hdr)]
    print("  ".join(h.ljust(w[i]) for i, h in enumerate(hdr)))
    print("  ".join("-" * w[i] for i in range(len(hdr))))
    for r in rows:
        print("  ".join(r[i].ljust(w[i]) for i in range(len(hdr))))

    if args.grid:
        with open(args.grid, "w", newline="", encoding="utf-8") as fh:
            wr = csv.writer(fh)
            wr.writerow(hdr)
            wr.writerows(rows)
        print(f"\nescrito {args.grid}", file=sys.stderr)

    if args.probe:
        print("\n# prob del modelo en las referencias (max en +-1 s)")
        print(f"{'fase':5} {'n':>5} {'>=.50':>6} {'>=.30':>6} {'>=.20':>6} "
              f"{'>=.10':>6} {'<.10':>6}")
        for ph in PHASES:
            b = probe[ph]
            print(f"{ph:5} {b['n']:5d} {b['ge50']:6d} {b['ge30']:6d} "
                  f"{b['ge20']:6d} {b['ge10']:6d} {b['lt10']:6d}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
