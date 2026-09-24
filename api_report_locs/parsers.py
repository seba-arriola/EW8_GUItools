"""Parsers de los logs que deja el pipeline en modo replay.

Formatos soportados (literales del codigo, con fichero:linea de referencia):

csnloc (`ew_gui_tools/csnloc/csnloc.c`):
  * `:228`  `csnloc: evento %lu lat=%.3f lon=%.3f z=%.1f km nph=%d rms=%.2f gap=%d`
  * `:348`  `csnloc: terminando (insertados=%lu reemplazados=%lu)`
  * `:219`  `csnloc: --- HYP2000ARC evento %lu ---` + el ARC (con DumpHypo 1)
  * `:261`  `csnloc: iniciando (in=%s out=%s threads=%d)`
  * `:273`  `csnloc: %d estaciones cargadas de %s`

csnmags_toy (`ew_gui_tools/csnmags/csnmags_toy.c`):
  * `:214`  `CSNmags_Red: [ID %s] ML=%.2f (%d est) | MWp=%.2f (%d est) -> PREF: %s %.2f`
  * `:106`  `csnmags_toy: Listo. Esperando sismos en el anillo...`

wave_serverV (`earthworm_8.0/src/archiving/wave_serverV/wave_serverV.c`):
  * `:1170` `WARNING: Tracebuf fails validity check, discarding: <SCNL>`

Todas las lineas de disco llevan el prefijo que anade `logit` con flag "t":
`YYYYMMDD_UTC_HH:MM:SS ` (`logit_common.c:945`).
"""

import re
from typing import Dict, List, Optional, Tuple

from .model import Event, Magnitude

# -----------------------------------------------------------------------------
#  Expresiones regulares
# -----------------------------------------------------------------------------
RE_CSNLOC_EVENT = re.compile(
    r"^(?P<date>\d{8})_UTC_(?P<hms>\d{2}:\d{2}:\d{2})\s+csnloc: evento (?P<id>\d+)"
    r"\s+lat=(?P<lat>-?\d+(?:\.\d+)?)"
    r"\s+lon=(?P<lon>-?\d+(?:\.\d+)?)"
    r"\s+z=(?P<z>-?\d+(?:\.\d+)?)\s+km"
    r"\s+nph=(?P<nph>\d+)"
    r"\s+rms=(?P<rms>-?\d+(?:\.\d+)?)"
    r"\s+gap=(?P<gap>-?\d+)\s*$"
)

RE_CSNLOC_EXIT = re.compile(
    r"^(?P<date>\d{8})_UTC_(?P<hms>\d{2}:\d{2}:\d{2})\s+csnloc: terminando "
    r"\(insertados=(?P<ins>\d+)\s+reemplazados=(?P<rep>\d+)\)\s*$"
)

RE_CSNLOC_INIT = re.compile(r"csnloc: iniciando \(in=(?P<in>\S+) out=(?P<out>\S+) threads=(?P<th>\d+)\)")
RE_CSNLOC_STAS = re.compile(r"csnloc: (?P<n>\d+) estaciones cargadas de (?P<f>.+?)\s*$")
RE_CSNLOC_ARC_HEAD = re.compile(r"csnloc: --- HYP2000ARC evento (?P<id>\d+) ---")

RE_CSNMAGS_RED = re.compile(
    r"^(?P<date>\d{8})_UTC_(?P<hms>\d{2}:\d{2}:\d{2})\s+CSNmags_Red: \[ID (?P<id>\S+)\]\s+"
    r"ML=(?P<ml>-?\d+(?:\.\d+)?)\s+\((?P<nml>\d+)\s+est\)\s+\|\s+"
    r"MWp=(?P<mwp>-?\d+(?:\.\d+)?)\s+\((?P<nmwp>\d+)\s+est\)\s+->\s+PREF:\s+"
    r"(?P<pref>ML|Mwp|MWp|None)\s+(?P<prefval>-?\d+(?:\.\d+)?)\s*$"
)

RE_CSNMAGS_READY = re.compile(r"csnmags_toy: Listo\. Esperando sismos")
RE_WS_DISCARD = re.compile(r"fails validity check, discarding")


# -----------------------------------------------------------------------------
#  Utilidades de fecha/hora
# -----------------------------------------------------------------------------
def log_stamp_to_iso(date: str, hms: str) -> str:
    """`20260922` + `16:39:26` -> `2026-09-22T16:39:26Z`."""
    return "%s-%s-%sT%sZ" % (date[0:4], date[4:6], date[6:8], hms)


def arc_stamp_to_iso(stamp: str) -> Optional[str]:
    """16 digitos del ARC `YYYYMMDDHHMMSScc` -> ISO8601 con centesimas.

    El ARC de csnloc escribe la hora origen en los bytes 0-15 como
    `%04d%02d%02d%02d%02d%04d` (year, mon, mday, hour, min, sec*100)
    (`hypo_out.c:102-107`).
    """
    s = stamp.strip()
    if len(s) < 14 or not s[:14].isdigit():
        return None
    year, mon, day = s[0:4], s[4:6], s[6:8]
    hh, mi = s[8:10], s[10:12]
    if len(s) >= 16 and s[12:16].isdigit():
        sec = int(s[12:16]) // 100
        cs = int(s[12:16]) % 100
    else:
        sec = int(s[12:14])
        cs = 0
    return "%s-%s-%sT%s:%s:%02d.%02dZ" % (year, mon, day, hh, mi, sec, cs)


# -----------------------------------------------------------------------------
#  Parsers de linea
# -----------------------------------------------------------------------------
def parse_csnloc_event(line: str) -> Optional[Event]:
    m = RE_CSNLOC_EVENT.match(line.rstrip())
    if not m:
        return None
    return Event(
        event_id=int(m.group("id")),
        log_time_utc=log_stamp_to_iso(m.group("date"), m.group("hms")),
        lat=float(m.group("lat")),
        lon=float(m.group("lon")),
        depth_km=float(m.group("z")),
        nph=int(m.group("nph")),
        rms=float(m.group("rms")),
        gap=int(m.group("gap")),
    )


def parse_csnloc_exit(line: str) -> Optional[Tuple[int, int]]:
    m = RE_CSNLOC_EXIT.match(line.rstrip())
    if not m:
        return None
    return int(m.group("ins")), int(m.group("rep"))


def parse_csnmags_red(line: str) -> Optional[Magnitude]:
    m = RE_CSNMAGS_RED.match(line.rstrip())
    if not m:
        return None
    raw = m.group("id")
    try:
        eid = int(raw)
    except ValueError:
        return None
    return Magnitude(
        event_id=eid,
        id_raw=raw,
        log_time_utc=log_stamp_to_iso(m.group("date"), m.group("hms")),
        ml=float(m.group("ml")),
        n_ml=int(m.group("nml")),
        mwp=float(m.group("mwp")),
        n_mwp=int(m.group("nmwp")),
        pref=m.group("pref"),
        pref_val=float(m.group("prefval")),
    )


# -----------------------------------------------------------------------------
#  Parsers de fichero
# -----------------------------------------------------------------------------
def _iter_lines(path: str):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                yield line
    except OSError:
        return


def parse_csnloc_file(path: str) -> Dict[str, object]:
    """Devuelve events, exits, arc_origins, started, stations, ready."""
    events: List[Event] = []
    exits: List[Tuple[int, int]] = []
    arc_origins: Dict[int, str] = {}
    started = False
    stations: Optional[int] = None

    pending_arc: Optional[int] = None
    lines = list(_iter_lines(path))
    for i, line in enumerate(lines):
        line = line.rstrip("\n")
        if not started and RE_CSNLOC_INIT.search(line):
            started = True
        m = RE_CSNLOC_STAS.search(line)
        if m:
            stations = int(m.group("n"))
        ev = parse_csnloc_event(line)
        if ev is not None:
            events.append(ev)
            continue
        ex = parse_csnloc_exit(line)
        if ex is not None:
            exits.append(ex)
            continue
        m = RE_CSNLOC_ARC_HEAD.search(line)
        if m:
            eid = int(m.group("id"))
            if i + 1 < len(lines):
                iso = arc_stamp_to_iso(lines[i + 1][:16])
                if iso:
                    arc_origins[eid] = iso
            pending_arc = eid
            continue
        if pending_arc is not None and line.strip() == "":
            pending_arc = None

    # La hora origen del ARC es mas fiable que el instante de deteccion.
    for ev in events:
        if ev.event_id in arc_origins:
            ev.origin_time_utc = arc_origins[ev.event_id]

    return {
        "events": events,
        "exits": exits,
        "arc_origins": arc_origins,
        "started": started,
        "stations": stations,
        "ready": started,
    }


def parse_csnmags_file(path: str) -> Dict[str, object]:
    """Devuelve magnitudes (la ULTIMA por ID) y ready."""
    by_id: Dict[int, Magnitude] = {}
    ready = False
    for line in _iter_lines(path):
        if not ready and RE_CSNMAGS_READY.search(line):
            ready = True
        mag = parse_csnmags_red(line.rstrip("\n"))
        if mag is not None:
            # csnmags emite una linea por actualizacion; vale la ultima.
            by_id[mag.event_id] = mag
    return {"magnitudes": list(by_id.values()), "ready": ready}


def parse_wave_server_file(path: str) -> Dict[str, object]:
    """Cuenta los descartes por validez (fecha futura o tiempo que no avanza)."""
    discards = 0
    scnls: List[str] = []
    for line in _iter_lines(path):
        if RE_WS_DISCARD.search(line):
            discards += 1
            m = re.search(r"discarding:\s*<([^>]*)>", line)
            if m:
                scnls.append(m.group(1))
    return {"discards": discards, "scnls": scnls}
