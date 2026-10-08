#!/usr/bin/env python3
"""offline_report.py - Reporte y comparacion de corridas del modo OFFLINE de csnloc.

Lee las carpetas de validacion producidas por validate_csnloc.sh:

    picks/<captura>/csnlocvalidate_<ts>/<slug>.jsonl   (una solucion por linea)

y hace dos cosas:

  1) UNA carpeta: agrupa las soluciones de un MISMO evento dentro de cada tank
     (la ventana deslizante emite varias soluciones del mismo sismo) y muestra
     un renglon por evento, con el nº de soluciones del set.

  2) DOS carpetas: compara tank contra tank (NUNCA tanks distintos) las
     soluciones de la MISMA captura tras cambiar csnloc. Empareja los eventos
     de cada tank por cercania y los clasifica:

        MISMO                  dt <= T  y  dd <= D
        MISMO_TIEMPO_AMPLIADO  dd <= D  y  T < dt <= F*T
        TEMPORAL_SIN_ESPACIO   dt <= T  y  dd > D
        SOLO_A / SOLO_B        sin pareja

     Si csnloc no cambia, las dos carpetas son identicas byte a byte y lo dice.

El representante de cada set de soluciones es la ULTIMA generada (mayor `event`).

Uso:
    python3 tank_tools/offline_report.py <dir>
    python3 tank_tools/offline_report.py <dirA> <dirB> [--diff]

Opciones:
    --time-window T    ventana temporal en segundos (default 30.0)
    --dist-deg D       distancia espacial en grados (default 1.0; 1 deg ~ 111 km)
    --window-factor F  factor de ventana para "tiempo ampliado" (default 2.0)
    --format FMT       table (default) | csv | json | all
    --out PREFIJO      prefijo de los csv/json (default: <dir>/report o <dirA>/compare)
    --quiet            no imprimir tablas, solo resumen
    --depth            anadir columna 'ctrl' (control de profundidad por evento)
"""
import argparse
import json
import math
import os
import sys
from datetime import datetime, timezone

# Control de profundidad (proxy) compartido con depth_report.py.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from depth_report import depth_control as _depth_control
except Exception:  # pragma: no cover - offline_report sigue funcionando sin el
    def _depth_control(rec):
        return "-"

KM_PER_DEG = 111.195


# --------------------------------------------------------------------------
# Utilidades
# --------------------------------------------------------------------------
def gc_deg(lat1, lon1, lat2, lon2):
    """Distancia angular (grados) entre dos puntos (haversine)."""
    r1 = math.radians(lat1)
    r2 = math.radians(lat2)
    dlat = r2 - r1
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2.0) ** 2 +
         math.cos(r1) * math.cos(r2) * math.sin(dlon / 2.0) ** 2)
    a = min(1.0, max(0.0, a))
    return math.degrees(2.0 * math.asin(math.sqrt(a)))


def has_geo(rec):
    """True si la solucion tiene tiempo y posicion UTILIZABLES (no solo las claves)."""
    return all(rec.get(k) is not None for k in ("t0", "lat", "lon"))


def fmt_t0(rec):
    if rec.get("t0_utc"):
        return rec["t0_utc"]
    if "t0" in rec:
        return datetime.fromtimestamp(rec["t0"], tz=timezone.utc).strftime(
            "%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
    return "?"


def fnum(v, nd=1):
    if v is None:
        return "-"
    try:
        return f"{float(v):.{nd}f}"
    except (TypeError, ValueError):
        return str(v)


# --------------------------------------------------------------------------
# Lectura
# --------------------------------------------------------------------------
def load_dir(d):
    """Devuelve (manifest, by_slug, expected).

    by_slug: {slug: [registros]} (una lista por fichero <slug>.jsonl)
    expected: set de slugs que la corrida debio procesar
    """
    if not os.path.isdir(d):
        raise SystemExit(f"offline_report: no existe el directorio {d}")

    manifest = None
    mp = os.path.join(d, "manifest.json")
    if os.path.isfile(mp):
        try:
            with open(mp, "r", encoding="utf-8") as fh:
                manifest = json.load(fh)
        except (OSError, json.JSONDecodeError):
            manifest = None

    by_slug = {}
    for name in sorted(os.listdir(d)):
        if not name.endswith(".jsonl"):
            continue
        slug = name[: -len(".jsonl")]
        recs = []
        with open(os.path.join(d, name), "r", encoding="utf-8",
                  errors="replace") as fh:
            for ln, line in enumerate(fh, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError as exc:
                    print(f"offline_report: {name}:{ln}: JSON invalido ({exc})",
                          file=sys.stderr)
                    continue
                rec["_slug"] = slug
                rec["_line"] = ln
                recs.append(rec)
        by_slug[slug] = recs

    expected = set(by_slug)
    if manifest and manifest.get("tanks"):
        for t in manifest["tanks"]:
            if t.get("slug"):
                expected.add(t["slug"])
    for name in os.listdir(d):
        if name.endswith(".picks"):
            expected.add(name[: -len(".picks")])

    return manifest, by_slug, expected


# --------------------------------------------------------------------------
# Agrupamiento intra-tank (single-linkage por tiempo y espacio)
# --------------------------------------------------------------------------
def cluster_records(recs, T, D):
    n = len(recs)
    if n == 0:
        return []

    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for i in range(n):
        for j in range(i + 1, n):
            a, b = recs[i], recs[j]
            if not has_geo(a) or not has_geo(b):
                continue
            if abs(a["t0"] - b["t0"]) <= T and \
               gc_deg(a["lat"], a["lon"], b["lat"], b["lon"]) <= D:
                ri, rj = find(i), find(j)
                if ri != rj:
                    parent[rj] = ri

    groups = {}
    for i in range(n):
        groups.setdefault(find(i), []).append(recs[i])
    return list(groups.values())


def representative(cluster):
    """La ULTIMA solucion generada (mayor `event`); desempata por linea."""
    return max(cluster, key=lambda r: (r.get("event") or 0, r.get("_line") or 0))


def cluster_span(cluster):
    t0s = [r["t0"] for r in cluster if "t0" in r]
    span_t = (max(t0s) - min(t0s)) if t0s else 0.0
    span_km = 0.0
    for i in range(len(cluster)):
        for j in range(i + 1, len(cluster)):
            a, b = cluster[i], cluster[j]
            if has_geo(a) and has_geo(b):
                span_km = max(span_km, gc_deg(a["lat"], a["lon"],
                                              b["lat"], b["lon"]) * KM_PER_DEG)
    return span_t, span_km


def events_of(by_slug, T, D):
    """{slug: [ {ev, n_sol, rep, span_s, span_km} ] } con clusters ordenados."""
    out = {}
    for slug, recs in by_slug.items():
        clusters = cluster_records(recs, T, D)
        clusters.sort(key=lambda c: representative(c).get("t0", 0.0))
        out[slug] = [
            {
                "ev": k,
                "n_sol": len(c),
                "rep": representative(c),
                "span_s": cluster_span(c)[0],
                "span_km": cluster_span(c)[1],
            }
            for k, c in enumerate(clusters, 1)
        ]
    return out


# --------------------------------------------------------------------------
# Tabla
# --------------------------------------------------------------------------
def print_table(headers, rows):
    if not rows:
        print("(sin filas)")
        return
    widths = [len(h) for h in headers]
    for r in rows:
        for i, c in enumerate(r):
            widths[i] = max(widths[i], len(str(c)))
    line = "  ".join(h.ljust(widths[i]) for i, h in enumerate(headers))
    print(line)
    print("-" * len(line))
    for r in rows:
        print("  ".join(str(c).ljust(widths[i]) for i, c in enumerate(r)))


# --------------------------------------------------------------------------
# Reporte de UNA corrida
# --------------------------------------------------------------------------
ONE_HEADERS = ["evento", "ev", "id", "ver", "t0 (UTC)", "lat", "lon", "z_km",
               "nph", "rms", "gap", "dmin_km", "score", "n_sol", "span_s",
               "span_km"]


def one_rows(evs, depth=False):
    rows = []
    for slug in sorted(evs):
        for e in evs[slug]:
            r = e["rep"]
            row = [
                slug, str(e["ev"]),
                str(r.get("id", "-")), str(r.get("version", "-")),
                fmt_t0(r),
                fnum(r.get("lat"), 3), fnum(r.get("lon"), 3),
                fnum(r.get("depth_km"), 1), str(r.get("nphases", "?")),
                fnum(r.get("rms_sec"), 2), fnum(r.get("gap_deg"), 0),
                fnum(r.get("dmin_km"), 0), fnum(r.get("score"), 1),
                str(e["n_sol"]), fnum(e["span_s"], 1), fnum(e["span_km"], 1),
            ]
            if depth:
                row.append(_depth_control(r))
            rows.append(row)
    return rows


def report_one(d, T, D, fmt, out, quiet, depth=False):
    _, by_slug, expected = load_dir(d)
    evs = events_of(by_slug, T, D)

    n_raw = sum(len(v) for v in by_slug.values())
    n_events = sum(len(v) for v in evs.values())
    with_hypo = sorted(s for s in by_slug if by_slug[s])
    without = sorted(s for s in expected if not by_slug.get(s))

    headers = ONE_HEADERS + (["ctrl"] if depth else [])
    rows = one_rows(evs, depth)

    if not quiet:
        print_table(headers, rows)
        print()
        print(f"resumen: {len(with_hypo)}/{len(expected)} tanks con al menos "
              f"una solucion; {n_raw} soluciones -> {n_events} eventos")
        print(f"         ventana: T={fnum(T,1)} s  D={fnum(D,2)} deg "
              f"({fnum(D * KM_PER_DEG,0)} km)")
        if without:
            print(f"sin localizar ({len(without)}): {', '.join(without)}")

    if fmt != "table":
        prefix = out or os.path.join(d, "report")
        write_csv_json(prefix, headers, rows,
                       {"events": evs, "n_raw": n_raw, "n_events": n_events}, fmt)
    return 0


# --------------------------------------------------------------------------
# Comparacion de DOS corridas (tank contra tank)
# --------------------------------------------------------------------------
COMPARE_HEADERS = ["evento", "clase", "nA", "nB", "dt_s", "dd_km", "dz_km",
                   "t0_A", "lat_A", "lon_A", "z_A",
                   "t0_B", "lat_B", "lon_B", "z_B"]

CLASES = ["MISMO", "MISMO_TIEMPO_AMPLIADO", "TEMPORAL_SIN_ESPACIO"]


def classify(dt, dd, T, D, F):
    if dd <= D:
        if dt <= T:
            return "MISMO"
        if dt <= F * T:
            return "MISMO_TIEMPO_AMPLIADO"
        return None
    if dt <= T:
        return "TEMPORAL_SIN_ESPACIO"
    return None


def match_events(ea, eb, T, D, F):
    """Empareja dos listas de eventos por cercania de representantes."""
    ra = [e["rep"] for e in ea]
    rb = [e["rep"] for e in eb]
    cand = []
    for i, a in enumerate(ra):
        for j, b in enumerate(rb):
            if not has_geo(a) or not has_geo(b):
                continue
            dt = abs(a["t0"] - b["t0"])
            dd = gc_deg(a["lat"], a["lon"], b["lat"], b["lon"])
            cls = classify(dt, dd, T, D, F)
            if cls:
                cand.append((dd / D + dt / T, i, j, cls, dt, dd))
    cand.sort(key=lambda x: x[0])

    ua, ub = set(), set()
    pairs = []
    for _, i, j, cls, dt, dd in cand:
        if i in ua or j in ub:
            continue
        ua.add(i)
        ub.add(j)
        pairs.append((i, j, cls, dt, dd))
    un_a = [i for i in range(len(ra)) if i not in ua]
    un_b = [j for j in range(len(rb)) if j not in ub]
    return pairs, un_a, un_b


def dirs_identical(a, b):
    na = sorted(f for f in os.listdir(a) if f.endswith(".jsonl"))
    nb = sorted(f for f in os.listdir(b) if f.endswith(".jsonl"))
    if na != nb:
        return False
    for f in na:
        with open(os.path.join(a, f), "rb") as fa, \
             open(os.path.join(b, f), "rb") as fb:
            if fa.read() != fb.read():
                return False
    return True


def _row_side(slug, e, lado):
    r = e["rep"]
    return [slug, "", str(e["n_sol"]) if lado == "A" else "",
            str(e["n_sol"]) if lado == "B" else "",
            "", "", "",
            fmt_t0(r) if lado == "A" else "",
            fnum(r.get("lat"), 3) if lado == "A" else "",
            fnum(r.get("lon"), 3) if lado == "A" else "",
            fnum(r.get("depth_km"), 1) if lado == "A" else "",
            fmt_t0(r) if lado == "B" else "",
            fnum(r.get("lat"), 3) if lado == "B" else "",
            fnum(r.get("lon"), 3) if lado == "B" else "",
            fnum(r.get("depth_km"), 1) if lado == "B" else ""]


def compare(a, b, T, D, F, fmt, out, quiet):
    _, sa, exp_a = load_dir(a)
    _, sb, exp_b = load_dir(b)

    slugs = sorted(set(sa) | set(sb))
    only_a = sorted(set(sa) - set(sb))
    only_b = sorted(set(sb) - set(sa))

    ev_a = events_of(sa, T, D)
    ev_b = events_of(sb, T, D)

    rows = []
    n_cls = {c: 0 for c in CLASES}
    n_only_a = n_only_b = 0

    for slug in slugs:
        if slug not in sa:
            for e in ev_b.get(slug, []):
                r = _row_side(slug, e, "B")
                r[1] = "SOLO_B"
                rows.append(r)
                n_only_b += 1
            continue
        if slug not in sb:
            for e in ev_a.get(slug, []):
                r = _row_side(slug, e, "A")
                r[1] = "SOLO_A"
                rows.append(r)
                n_only_a += 1
            continue

        ea, eb = ev_a.get(slug, []), ev_b.get(slug, [])
        pairs, un_a, un_b = match_events(ea, eb, T, D, F)
        for i, j, cls, dt, dd in pairs:
            ra, rb = ea[i]["rep"], eb[j]["rep"]
            dz = abs((ra.get("depth_km") or 0) - (rb.get("depth_km") or 0))
            rows.append([
                slug, cls, str(ea[i]["n_sol"]), str(eb[j]["n_sol"]),
                fnum(dt, 1), fnum(dd * KM_PER_DEG, 1), fnum(dz, 1),
                fmt_t0(ra), fnum(ra.get("lat"), 3), fnum(ra.get("lon"), 3),
                fnum(ra.get("depth_km"), 1),
                fmt_t0(rb), fnum(rb.get("lat"), 3), fnum(rb.get("lon"), 3),
                fnum(rb.get("depth_km"), 1),
            ])
            n_cls[cls] += 1
        for i in un_a:
            r = _row_side(slug, ea[i], "A")
            r[1] = "SOLO_A"
            rows.append(r)
            n_only_a += 1
        for j in un_b:
            r = _row_side(slug, eb[j], "B")
            r[1] = "SOLO_B"
            rows.append(r)
            n_only_b += 1

    identical = dirs_identical(a, b)

    if not quiet:
        print(f"comparacion: {a}  ->  {b}")
        print(f"             T={fnum(T,1)} s  D={fnum(D,2)} deg "
              f"({fnum(D * KM_PER_DEG,0)} km)  F={fnum(F,2)}")
        if identical:
            print()
            print("*** CORRIDAS IDENTICAS (sin cambios): los .jsonl son "
                  "iguales byte a byte ***")
        else:
            for cls in CLASES:
                sub = [r for r in rows if r[1] == cls]
                print()
                print(f"=== {cls} ({len(sub)}) ===")
                print_table(COMPARE_HEADERS, sub)
            for cls, tag in (("SOLO_A", "SOLO EN A"), ("SOLO_B", "SOLO EN B")):
                sub = [r for r in rows if r[1] == cls]
                if sub:
                    print()
                    print(f"=== {tag} ({len(sub)}) ===")
                    print_table(COMPARE_HEADERS, sub)

        print()
        print(f"resumen: {len(sa)} tanks en A, {len(sb)} en B"
              f"{f' (solo-A: {len(only_a)}, solo-B: {len(only_b)})' if (only_a or only_b) else ''}")
        print(f"         A: {sum(len(v) for v in sa.values())} soluciones -> "
              f"{sum(len(v) for v in ev_a.values())} eventos | "
              f"B: {sum(len(v) for v in sb.values())} soluciones -> "
              f"{sum(len(v) for v in ev_b.values())} eventos")
        print("         " + "  ".join(f"{c}={n_cls[c]}" for c in CLASES)
              + f"  SOLO_A={n_only_a}  SOLO_B={n_only_b}")
        if only_a:
            print(f"         tanks solo en A: {', '.join(only_a)}")
        if only_b:
            print(f"         tanks solo en B: {', '.join(only_b)}")

    if fmt != "table":
        prefix = out or os.path.join(a, "compare")
        payload = {
            "a": a, "b": b, "identical": identical,
            "time_window": T, "dist_deg": D, "window_factor": F,
            "counts": dict(n_cls, SOLO_A=n_only_a, SOLO_B=n_only_b),
            "only_a_tanks": only_a, "only_b_tanks": only_b,
            "rows": [dict(zip(COMPARE_HEADERS, r)) for r in rows],
        }
        write_csv_json(prefix, COMPARE_HEADERS, rows, payload, fmt)
    return 0


# --------------------------------------------------------------------------
# Salidas
# --------------------------------------------------------------------------
def write_csv_json(prefix, headers, rows, payload, fmt):
    if fmt in ("csv", "all"):
        path = prefix + ".csv"
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(",".join(headers) + "\n")
            for r in rows:
                fh.write(",".join(str(c).replace(",", " ") for c in r) + "\n")
        print(f"escrito: {path}", file=sys.stderr)
    if fmt in ("json", "all"):
        path = prefix + ".json"
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, indent=2)
        print(f"escrito: {path}", file=sys.stderr)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def build_parser():
    p = argparse.ArgumentParser(
        description="Reporte y comparacion de corridas offline de csnloc.")
    p.add_argument("dirs", nargs="*",
                   help="1 carpeta (reporte) o 2 carpetas (comparacion)")
    p.add_argument("--diff", action="store_true",
                   help="forzar comparacion (requiere 2 carpetas)")
    p.add_argument("--time-window", type=float, default=30.0,
                   help="ventana temporal en segundos (default 30)")
    p.add_argument("--dist-deg", type=float, default=1.0,
                   help="distancia espacial en grados (default 1.0)")
    p.add_argument("--window-factor", type=float, default=2.0,
                   help="factor de ventana para tiempo ampliado (default 2.0)")
    p.add_argument("--format", choices=["table", "csv", "json", "all"],
                   default="table")
    p.add_argument("--out", metavar="PREFIJO",
                   help="prefijo de los csv/json")
    p.add_argument("--quiet", action="store_true", help="no imprimir tablas")
    p.add_argument("--depth", action="store_true",
                   help="anadir columna 'ctrl' (control de profundidad)")
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)

    if len(args.dirs) == 0:
        raise SystemExit("offline_report: falta el directorio")
    if len(args.dirs) > 2:
        raise SystemExit("offline_report: maximo 2 directorios")

    if len(args.dirs) == 2 or args.diff:
        if len(args.dirs) != 2:
            raise SystemExit("offline_report: --diff requiere 2 directorios")
        return compare(args.dirs[0], args.dirs[1], args.time_window,
                       args.dist_deg, args.window_factor,
                       args.format, args.out, args.quiet)

    return report_one(args.dirs[0], args.time_window, args.dist_deg,
                      args.format, args.out, args.quiet, args.depth)


if __name__ == "__main__":
    sys.exit(main())
