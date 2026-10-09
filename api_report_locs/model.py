"""Modelo de datos del reporte de localizaciones.

Cada `session.json` (una sesion de replay) se convierte en un `Session` con sus
`Event` (localizaciones de csnloc) y `Magnitude` (magnitudes de red de csnmags_toy).
El cruce entre ambos se hace por el ID numerico del HYP2000ARC.
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple


@dataclass
class Event:
    """Una linea `csnloc: evento N lat=... lon=...`."""

    event_id: int
    log_time_utc: str
    lat: float
    lon: float
    depth_km: float
    nph: int
    rms: float
    gap: int
    origin_time_utc: Optional[str] = None   # del volcado HYP2000ARC (DumpHypo 1)


@dataclass
class Magnitude:
    """Una linea `CSNmags_Red: [ID ...] ML=... | MWp=... -> PREF: ...`."""

    event_id: int
    id_raw: str
    log_time_utc: str
    ml: float
    n_ml: int
    mwp: float
    n_mwp: int
    pref: str
    pref_val: float
    mb: float = 0.0
    n_mb: int = 0
    ms: float = 0.0
    n_ms: int = 0


@dataclass
class Session:
    """Una sesion de replay (un trozo de un tank)."""

    session_id: str
    slug: str
    path: str
    source_file: str = ""
    chunk_index: int = 0
    chunk_start_utc: str = ""
    mode: str = ""
    wall_s: int = 0
    offset_time_s: float = 0.0
    exit_code: int = 0
    timeout: bool = False
    counts: Dict[str, int] = field(default_factory=dict)
    logs: Dict[str, str] = field(default_factory=dict)

    events: List[Event] = field(default_factory=list)
    magnitudes: List[Magnitude] = field(default_factory=list)

    csnloc_ready: bool = False
    csnmags_ready: bool = False
    csnloc_exit: Optional[Tuple[int, int]] = None
    ws_discards: int = 0
    warnings: List[str] = field(default_factory=list)


@dataclass
class EventRow:
    """Una fila del reporte: un evento localizado, con su magnitud si la hay."""

    slug: str
    session_id: str
    chunk_index: int
    source_file: str
    event_id: int
    log_time_utc: str
    origin_time_utc: str
    origin_hist_utc: str
    lat: float
    lon: float
    depth_km: float
    nph: int
    rms: float
    gap: int
    ml: float
    n_ml: int
    mwp: float
    n_mwp: int
    pref: str
    pref_val: float
    dup: bool = False
    notes: str = ""

    def as_dict(self) -> Dict[str, object]:
        return {
            "slug": self.slug,
            "session_id": self.session_id,
            "chunk_index": self.chunk_index,
            "source_file": self.source_file,
            "event_id": self.event_id,
            "log_time_utc": self.log_time_utc,
            "origin_time_utc": self.origin_time_utc,
            "origin_hist_utc": self.origin_hist_utc,
            "lat": self.lat,
            "lon": self.lon,
            "depth_km": self.depth_km,
            "nph": self.nph,
            "rms": self.rms,
            "gap": self.gap,
            "ml": self.ml,
            "n_ml": self.n_ml,
            "mwp": self.mwp,
            "n_mwp": self.n_mwp,
            "pref": self.pref,
            "pref_val": self.pref_val,
            "dup": int(self.dup),
            "notes": self.notes,
        }


CSV_COLUMNS = [
    "slug", "session_id", "chunk_index", "source_file", "event_id",
    "log_time_utc", "origin_time_utc", "origin_hist_utc", "lat", "lon", "depth_km",
    "nph", "rms", "gap", "ml", "n_ml", "mwp", "n_mwp",
    "pref", "pref_val", "dup", "notes",
]
