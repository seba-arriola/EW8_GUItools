#!/usr/bin/env python3
"""Reporte de localizaciones y magnitudes del pipeline EW8_GUItools.

Lee las sesiones que deja `tank_tools/run_events.sh` (carpeta `runs/`) y produce
una tabla de los eventos localizados por `csnloc` junto con las magnitudes
calculadas por `csnmags_toy`, mas un resumen de la corrida.

Uso:
    python3 -m api_report_locs.report --runs runs
    python3 -m api_report_locs.report --runs runs --slug simulacion3_Illapel20150916
    python3 -m api_report_locs.report --runs runs --format all --out /tmp/reporte

Cruce csnloc <-> csnmags: csnloc publica el HYP2000ARC con el ID en los bytes
136-145 como `"%010lu" % (id % 2147000000)` (hypo_out.c:94-95) y loguea ese mismo
`id` en `csnloc: evento N` (csnloc.c:212/228). csnmags lee esos 10 bytes tal cual
(csnmags_toy.c:266). Asi que `int([ID ...])` == `N`. El contador se reinicia en
cada arranque (csnloc.c:300), por eso el cruce se hace SIEMPRE dentro de una
misma sesion, nunca entre sesiones.

Solo biblioteca estandar.
"""

import argparse
import csv
import json
import math
import os
import re
import sys
from datetime import datetime, timedelta, timezone
from typing import Dict, List, Optional, Tuple

try:
    from .model import CSV_COLUMNS, EventRow, Session
    from .parsers import (
        parse_csnloc_file,
        parse_csnmags_file,
        parse_wave_server_file,
    )
except ImportError:  # ejecutado como script suelto
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from api_report_locs.model import CSV_COLUMNS, EventRow, Session
    from api_report_locs.parsers import (
        parse_csnloc_file,
        parse_csnmags_file,
        parse_wave_server_file,
    )

EXIT_OK = 0
EXIT_USAGE = 1
EXIT_NO_EVENTS = 2
EXIT_NO_SESSIONS = 3


# -----------------------------------------------------------------------------
#  Carga de sesiones
# -----------------------------------------------------------------------------
def _read_json(path: str) -> Optional[dict]:
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, json.JSONDecodeError):
        return None


def find_sessions(runs_dir: str, slugs: Optional[List[str]] = None) -> List[str]:
    """Devuelve las rutas de session.json encontradas en <runs>/<slug>/<NNN>/."""
    out: List[str] = []
    if not os.path.isdir(runs_dir):
        return out
    for slug in sorted(os.listdir(runs_dir)):
        sd = os.path.join(runs_dir, slug)
        if not os.path.isdir(sd):
            continue
        if slugs and slug not in slugs:
            continue
        for sub in sorted(os.listdir(sd)):
            p = os.path.join(sd, sub, "session.json")
            if os.path.isfile(p):
                out.append(p)
    return out


def load_session(path: str) -> Optional[Session]:
    meta = _read_json(path)
    if meta is None:
        return None
    sess = Session(
        session_id=str(meta.get("session_id", "")),
        slug=str(meta.get("slug", "")),
        path=os.path.dirname(path),
        source_file=str(meta.get("source_file", "") or ""),
        chunk_index=int(meta.get("chunk_index", 0)),
        chunk_start_utc=str(meta.get("chunk_start_utc", "")),
        mode=str(meta.get("mode", "")),
        wall_s=int(meta.get("wall_s", 0) or 0),
        offset_time_s=float(meta.get("offset_time_s", 0) or 0),
        exit_code=int(meta.get("exit_code", 0) or 0),
        timeout=bool(meta.get("timeout", False)),
        counts=dict(meta.get("counts", {}) or {}),
        logs=dict(meta.get("logs", {}) or {}),
    )

    csl = sess.logs.get("csnloc") or ""
    if csl and os.path.isfile(csl):
        info = parse_csnloc_file(csl)
        sess.events = info["events"]
        sess.csnloc_ready = bool(info["ready"])
        exits = info["exits"]
        if exits:
            sess.csnloc_exit = exits[-1]
        if not sess.csnloc_ready:
            sess.warnings.append("csnloc no registro 'iniciando'")
    else:
        sess.warnings.append("falta el log de csnloc")

    cml = sess.logs.get("csnmags_toy") or ""
    if cml and os.path.isfile(cml):
        info = parse_csnmags_file(cml)
        sess.magnitudes = info["magnitudes"]
        sess.csnmags_ready = bool(info["ready"])
        if not sess.csnmags_ready:
            sess.warnings.append("csnmags_toy no llego a 'Listo. Esperando sismos'")
    else:
        sess.warnings.append("falta el log de csnmags_toy")

    wsv = sess.logs.get("wave_serverV") or ""
    if wsv and os.path.isfile(wsv):
        sess.ws_discards = parse_wave_server_file(wsv)["discards"]
    if sess.timeout:
        sess.warnings.append("sesion agotada por timeout")
    elif sess.exit_code != 0:
        sess.warnings.append("replay salio con codigo %s" % sess.exit_code)

    return sess


# -----------------------------------------------------------------------------
#  Construccion de filas
# -----------------------------------------------------------------------------
def build_rows(sessions: List[Session], slug_to_source: Optional[Dict[str, str]] = None) -> List[EventRow]:
    slug_to_source = slug_to_source or {}
    rows: List[EventRow] = []
    for sess in sessions:
        mags = {m.event_id: m for m in sess.magnitudes}
        for ev in sorted(sess.events, key=lambda e: e.event_id):
            m = mags.get(ev.event_id)
            notes = []
            if m is None:
                notes.append("sin magnitud")
            rows.append(EventRow(
                slug=sess.slug,
                session_id=sess.session_id,
                chunk_index=sess.chunk_index,
                source_file=sess.source_file or slug_to_source.get(sess.slug, ""),
                event_id=ev.event_id,
                log_time_utc=ev.log_time_utc,
                origin_time_utc=ev.origin_time_utc or "",
                origin_hist_utc=shift_iso(ev.origin_time_utc, sess.offset_time_s) if ev.origin_time_utc else "",
                lat=ev.lat, lon=ev.lon, depth_km=ev.depth_km,
                nph=ev.nph, rms=ev.rms, gap=ev.gap,
                ml=m.ml if m else 0.0, n_ml=m.n_ml if m else 0,
                mwp=m.mwp if m else 0.0, n_mwp=m.n_mwp if m else 0,
                pref=m.pref if m else "", pref_val=m.pref_val if m else 0.0,
                notes="; ".join(notes),
            ))
        # magnitudes sin localizacion correlacionada (no deberia pasar)
        ev_ids = {e.event_id for e in sess.events}
        for m in sess.magnitudes:
            if m.event_id not in ev_ids:
                rows.append(EventRow(
                    slug=sess.slug, session_id=sess.session_id,
                    chunk_index=sess.chunk_index,
                    source_file=sess.source_file or slug_to_source.get(sess.slug, ""),
                    event_id=m.event_id, log_time_utc=m.log_time_utc,
                    origin_time_utc="", origin_hist_utc="", lat=0.0, lon=0.0, depth_km=0.0,
                    nph=0, rms=0.0, gap=0,
                    ml=m.ml, n_ml=m.n_ml, mwp=m.mwp, n_mwp=m.n_mwp,
                    pref=m.pref, pref_val=m.pref_val,
                    notes="magnitud sin localizacion correlacionada (ID no visto en csnloc)",
                ))
    # orden estable y legible: por evento y hora historica
    rows.sort(key=lambda r: (r.slug, r.origin_hist_utc or r.log_time_utc, r.event_id))
    return rows


def haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    r = 6371.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(min(1.0, math.sqrt(a)))


def _parse_iso(s: str) -> Optional[datetime]:
    if not s:
        return None
    try:
        return datetime.fromisoformat(s.replace("Z", "+00:00"))
    except ValueError:
        return None


def shift_iso(iso: str, delta_s: float) -> str:
    """Resta `delta_s` a una marca ISO8601.

    El replay re-estampa el dato a "ahora" (tankplayer.c:650), asi que la hora
    origen que calcula csnloc es la del replay. Restando el `offsetTime` que
    publica tankplayer se recupera la hora HISTORICA del sismo.
    """
    dt = _parse_iso(iso)
    if dt is None:
        return ""
    dt2 = dt - timedelta(seconds=float(delta_s or 0))
    return dt2.strftime("%Y-%m-%dT%H:%M:%SZ")


def _same_event(a: EventRow, b: EventRow, tol_deg: float, tol_s: int) -> bool:
    if a.nph == 0 or b.nph == 0:
        return False
    if abs(a.lat - b.lat) > tol_deg or abs(a.lon - b.lon) > tol_deg:
        return False
    ta = _parse_iso(a.origin_hist_utc or a.origin_time_utc or a.log_time_utc)
    tb = _parse_iso(b.origin_hist_utc or b.origin_time_utc or b.log_time_utc)
    if ta and tb:
        return abs((ta - tb).total_seconds()) <= tol_s
    return True


def group_events(rows: List[EventRow], tol_deg: float = 0.5, tol_s: int = 300) -> List[List[EventRow]]:
    """Agrupa filas que probablemente son el MISMO evento (cierre transitivo).

    csnloc emite varias soluciones para el mismo sismo a medida que llegan picks,
    y los trozos solapados lo repiten. Se usa union-find porque las soluciones
    forman cadenas (A~B por posicion, B~C por tiempo) que un agrupado voraz
    partiria en dos.
    """
    idx = [i for i, r in enumerate(rows) if r.nph > 0]
    parent: Dict[int, int] = {i: i for i in idx}

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def union(i: int, j: int) -> None:
        ri, rj = find(i), find(j)
        if ri != rj:
            parent[rj] = ri

    for a in range(len(idx)):
        for b in range(a + 1, len(idx)):
            i, j = idx[a], idx[b]
            if _same_event(rows[i], rows[j], tol_deg, tol_s):
                union(i, j)

    buckets: Dict[int, List[EventRow]] = {}
    for i in idx:
        buckets.setdefault(find(i), []).append(rows[i])
    return list(buckets.values())


def flag_duplicates(rows: List[EventRow], tol_deg: float = 0.5, tol_s: int = 300) -> int:
    """Marca (sin descartar) los eventos repetidos. Devuelve el numero de repetidos."""
    n = 0
    for g in group_events(rows, tol_deg, tol_s):
        if len(g) > 1:
            for r in g:
                r.dup = True
            n += len(g) - 1
    return n


# -----------------------------------------------------------------------------
#  Catalogo opcional (referencia)
# -----------------------------------------------------------------------------
def load_catalog(path: str) -> Dict[str, dict]:
    """CSV con al menos: slug,lat,lon  (y opcionalmente: origin_time_utc, depth_km)."""
    out: Dict[str, dict] = {}
    try:
        with open(path, "r", encoding="utf-8", newline="") as fh:
            for row in csv.DictReader(fh):
                slug = (row.get("slug") or "").strip()
                if not slug:
                    continue
                out[slug] = row
    except OSError:
        return {}
    return out


# -----------------------------------------------------------------------------
#  Salida
# -----------------------------------------------------------------------------
HEADERS = [
    ("slug", 26), ("det_utc", 20), ("origen_hist_utc", 20),
    ("lat", 9), ("lon", 10), ("z_km", 6),
    ("nph", 4), ("rms", 5), ("gap", 4),
    ("ML", 5), ("n", 2), ("Mwp", 5), ("n", 2),
    ("PREF", 5), ("dup", 4),
]


def _fmt_rows(rows: List[EventRow]) -> List[List[str]]:
    out = []
    for r in rows:
        out.append([
            r.slug,
            r.log_time_utc.replace("T", " ").replace("Z", ""),
            (r.origin_hist_utc or "-").replace("T", " ").replace("Z", ""),
            "%8.3f" % r.lat if r.nph else "-",
            "%9.3f" % r.lon if r.nph else "-",
            "%5.1f" % r.depth_km if r.nph else "-",
            str(r.nph),
            "%4.2f" % r.rms,
            str(r.gap),
            "%4.2f" % r.ml if r.n_ml else "-",
            str(r.n_ml) if r.n_ml else "-",
            "%4.2f" % r.mwp if r.n_mwp else "-",
            str(r.n_mwp) if r.n_mwp else "-",
            (r.pref or "-")[:5],
            "SI" if r.dup else "",
        ])
    return out


def print_table(rows: List[EventRow], sessions: List[Session],
                catalog: Optional[Dict[str, dict]] = None,
                tol_deg: float = 0.5, tol_s: int = 300) -> None:
    print("=" * 100)
    print("EVENTOS LOCALIZADOS (csnloc) Y MAGNITUDES (csnmags_toy)")
    print("=" * 100)
    if not rows:
        print("  (ningun evento)")
    else:
        widths = [max(len(h), 2) for h, _ in HEADERS]
        body = _fmt_rows(rows)
        for r in body:
            for i, c in enumerate(r):
                widths[i] = max(widths[i], len(c))
        header = "  ".join(h.ljust(widths[i]) for i, (h, _) in enumerate(HEADERS))
        print(header)
        print("-" * len(header))
        for r in body:
            print("  ".join(c.ljust(widths[i]) for i, c in enumerate(r)))

    if catalog:
        print()
        print("REFERENCIA (catalogo) y desvio")
        print("-" * 100)
        seen = set()
        for r in rows:
            if r.slug in seen or r.slug not in catalog:
                continue
            seen.add(r.slug)
            c = catalog[r.slug]
            try:
                rlat, rlon = float(c.get("lat", "")), float(c.get("lon", ""))
            except ValueError:
                continue
            d = haversine_km(r.lat, r.lon, rlat, rlon) if r.nph else float("nan")
            print("  %-26s ref=(%8.3f,%9.3f)  loc=(%8.3f,%9.3f)  desvio=%.1f km"
                  % (r.slug, rlat, rlon, r.lat, r.lon, d))

    # --- resumen ---
    print()
    print("=" * 100)
    print("RESUMEN")
    print("=" * 100)
    n_ev = len([r for r in rows if r.nph])
    n_mag = len([r for r in rows if r.n_ml or r.n_mwp])
    n_pref = len([r for r in rows if r.pref and r.pref != "None"])
    n_dup = len([r for r in rows if r.dup])
    n_uniq = len(group_events(rows, tol_deg, tol_s))
    print("  sesiones            : %d" % len(sessions))
    print("  eventos localizados : %d" % n_ev)
    print("  eventos unicos      : %d   (agrupando duplicados de un mismo sismo)" % n_uniq)
    print("  con magnitud        : %d" % n_mag)
    print("  con PREF            : %d" % n_pref)
    print("  duplicados (solape) : %d" % n_dup)

    # por slug
    slugs: Dict[str, Dict[str, int]] = {}
    for s in sessions:
        d = slugs.setdefault(s.slug, {"sess": 0, "ev": 0, "mag": 0, "disc": 0})
        d["sess"] += 1
        d["ev"] += len(s.events)
        d["mag"] += len(s.magnitudes)
        d["disc"] += s.ws_discards
    if slugs:
        print()
        print("  %-30s %5s %5s %5s %8s" % ("slug", "ses", "ev", "mag", "desc_ws"))
        for slug in sorted(slugs):
            d = slugs[slug]
            print("  %-30s %5d %5d %5d %8d" % (slug, d["sess"], d["ev"], d["mag"], d["disc"]))

    # diagnosticos
    no_ev = [s for s in sessions if not s.events]
    bad = [s for s in sessions if s.warnings]
    if no_ev:
        print()
        print("  Sesiones SIN eventos (%d):" % len(no_ev))
        for s in no_ev[:12]:
            print("    - %s" % s.session_id)
        if len(no_ev) > 12:
            print("    ... y %d mas" % (len(no_ev) - 12))
    if bad:
        print()
        print("  AVISOS (%d sesiones):" % len(bad))
        for s in bad[:12]:
            print("    - %s: %s" % (s.session_id, "; ".join(s.warnings)))
        if len(bad) > 12:
            print("    ... y %d mas" % (len(bad) - 12))
    print()


def write_csv(path: str, rows: List[EventRow]) -> None:
    with open(path, "w", encoding="utf-8", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=CSV_COLUMNS)
        w.writeheader()
        for r in rows:
            w.writerow(r.as_dict())


def write_json(path: str, rows: List[EventRow], sessions: List[Session], runs_dir: str,
               n_unique: Optional[int] = None) -> None:
    doc = {
        "generated_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "runs_dir": os.path.abspath(runs_dir),
        "summary": {
            "sessions": len(sessions),
            "events": len([r for r in rows if r.nph]),
            "unique_events": n_unique if n_unique is not None else len(group_events(rows)),
            "with_magnitude": len([r for r in rows if r.n_ml or r.n_mwp]),
            "duplicates": len([r for r in rows if r.dup]),
        },
        "sessions": [
            {
                "session_id": s.session_id,
                "slug": s.slug,
                "chunk_index": s.chunk_index,
                "chunk_start_utc": s.chunk_start_utc,
                "mode": s.mode,
                "wall_s": s.wall_s,
                "exit_code": s.exit_code,
                "timeout": s.timeout,
                "events": len(s.events),
                "magnitudes": len(s.magnitudes),
                "ws_discards": s.ws_discards,
                "csnloc_ready": s.csnloc_ready,
                "csnmags_ready": s.csnmags_ready,
                "warnings": s.warnings,
            }
            for s in sessions
        ],
        "events": [r.as_dict() for r in rows],
    }
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=2, ensure_ascii=False)
        fh.write("\n")


# -----------------------------------------------------------------------------
#  CLI
# -----------------------------------------------------------------------------
def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="python3 -m api_report_locs.report",
        description="Reporte de localizaciones (csnloc) y magnitudes (csnmags_toy).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Ejemplo:\n  python3 -m api_report_locs.report --runs runs --format all\n",
    )
    p.add_argument("--runs", default="runs", help="directorio de resultados de run_events.sh (def: runs)")
    p.add_argument("--slug", action="append", default=None,
                   help="filtrar por slug de evento (repetible)")
    p.add_argument("--format", choices=["table", "csv", "json", "all"], default="table",
                   help="que emitir (def: table)")
    p.add_argument("--out", default=None, help="prefijo de salida (def: <runs>/report)")
    p.add_argument("--catalog", default=None,
                   help="CSV opcional de referencia (columnas: slug,lat,lon)")
    p.add_argument("--dup-tol-deg", type=float, default=0.5,
                   help="tolerancia en grados para agrupar soluciones del mismo sismo (def: 0.5)")
    p.add_argument("--dup-tol-s", type=int, default=300,
                   help="tolerancia en segundos para agrupar (def: 300)")
    p.add_argument("--quiet", action="store_true", help="no imprimir la tabla")
    p.add_argument("--version", action="version", version="api_report_locs 1.0.0")
    return p


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)

    paths = find_sessions(args.runs, args.slug)
    if not paths:
        print("ERROR: no hay sesiones en %s (¿corriste run_events.sh run?)" % args.runs,
              file=sys.stderr)
        return EXIT_NO_SESSIONS

    sessions: List[Session] = []
    for p in paths:
        s = load_session(p)
        if s is None:
            print("AVISO: no pude leer %s" % p, file=sys.stderr)
            continue
        sessions.append(s)
    if not sessions:
        print("ERROR: ninguna session.json era legible en %s" % args.runs, file=sys.stderr)
        return EXIT_NO_SESSIONS

    # source_file: viene en session.json; si falta, se busca en el manifest del tank
    slug_to_source: Dict[str, str] = {}
    for s in sessions:
        if s.source_file:
            slug_to_source.setdefault(s.slug, s.source_file)
    for s in sessions:
        if s.slug in slug_to_source:
            continue
        mf = os.path.normpath(os.path.join(args.runs, s.slug, "..", "..", "tank_repo", s.slug, "manifest.json"))
        meta = _read_json(mf) if os.path.isfile(mf) else None
        if meta and meta.get("source"):
            slug_to_source[s.slug] = str(meta["source"])

    rows = build_rows(sessions, slug_to_source)
    flag_duplicates(rows, args.dup_tol_deg, args.dup_tol_s)

    catalog = load_catalog(args.catalog) if args.catalog else None

    if not args.quiet and args.format in ("table", "all"):
        print_table(rows, sessions, catalog, args.dup_tol_deg, args.dup_tol_s)

    out_prefix = args.out or os.path.join(args.runs, "report")
    if args.format in ("csv", "all"):
        path = out_prefix if out_prefix.endswith(".csv") else out_prefix + ".csv"
        write_csv(path, rows)
        print("CSV : %s" % path)
    if args.format in ("json", "all"):
        path = out_prefix if out_prefix.endswith(".json") else out_prefix + ".json"
        write_json(path, rows, sessions, args.runs,
                   len(group_events(rows, args.dup_tol_deg, args.dup_tol_s)))
        print("JSON: %s" % path)

    n_ev = len([r for r in rows if r.nph])
    if n_ev == 0:
        print("AVISO: no se encontro ningun evento localizado.", file=sys.stderr)
        return EXIT_NO_EVENTS
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
