# Mapeo de datos: módulos ↔ PostgreSQL

Auditoría del mapeo campo a campo en los dos sentidos, con `archivo:línea` por celda.
Cubre la cadena `ARC/mensajes de anillo → hypodb → PostgreSQL` (ida) y
`PostgreSQL → ewguidb_* → csnhypodbp` (vuelta), más el contrato de orden de las tablas.

- Verificado: 2026-10-12 sobre `db/schema.sql` (copia literal de `test_db`), `ew_gui_tools/hypodb/`,
  `ew_gui_tools/common/src/ewgui_db.c` y `ew_gui_tools/csnhypodbp/`.
- Lo que queda sin decidir o sin resolver está en `docs/DATA-MAPPING-PENDIENTES.md`.
- Invariante automático de cobertura del esquema: `common/tests/test_schema_coverage.sh`.

## 0. Cómo se verifica

```bash
source ./ew8_unix.sh
make -C ew_gui_tools/hypodb check            # unitarios headless (sin BD)
make -C ew_gui_tools/hypodb mapping          # tabla esperado | escrito | releído (requiere PG)
make -C ew_gui_tools/common check            # headless + invariantes de fuente
# integración real (si no hay EWGUI_DB_TEST_ENV, los tests de BD hacen SKIP):
EWGUI_DB_TEST_ENV=run_working_v8/params/db.env make -C ew_gui_tools/hypodb check
```

Prerrequisito de PostgreSQL: `db/docker-compose.yml` (puerto 5435).

## 1. Ida: módulo → base de datos

`mod` = logo del mensaje: **158** `csnhypodbp`, **162** `csnloc`, **163** `hyp2000_ring`,
**164** `nlloc_ring` (`run_working_v8/params/earthworm.d:223-229`).

### 1.1 `event`

| Columna | Valor | Fuente |
|---|---|---|
| `event_id` | `E<qid>-<yyyymmddhhmmss>` (UTC de la hora origen de la 1.ª solución del grupo) | `hypodb_ids.c:15`, `hypodb_ingest.c:335` |
| `event_type` | `'EARTHQUAKE'` fijo | `hypodb_ingest.c:535` |
| `felt` / `published` | `false` / `false` fijos | `hypodb_ingest.c:533` |
| `creation_time` | DEFAULT `CURRENT_TIMESTAMP` (nadie la escribe) | `schema.sql:16`, `ewgui_db.c:266-269` |
| `preferred_origin_id` | `UPDATE` posterior, nunca en el INSERT | `hypodb_ingest.c:486`, `ewgui_db.c:419` |
| `preferred_magnitude_id` | idem | `hypodb_ingest.c:465`, `ewgui_db.c:419` |

### 1.2 `origin` (una fila por `<event>:<mod>`)

| Columna | Valor | Fuente |
|---|---|---|
| `origin_id` | `<event_id>:<mod>` | `hypodb_ids.c:22` |
| `origin_time` | ARC línea 1 `[0:16]` (`YYYYMMDDHHMMSS`+frac) | `csnhypodbp_group.c:101` |
| `latitude` / `longitude` | ARC `16..22` (+`S`) / `23..30` (+`W`) | `csnhypodbp_group.c:106-116` |
| `depth` | ARC `31..35` ÷ 100 (km) | `csnhypodbp_group.c:118-120` |
| `author` | `'csnhypodbp'` (constante: el módulo que revisa/produce el registro) | `hypodb_ingest.c:547` |
| `software` | `getenv("EW_VERSION")` en el arranque; fallback `"ew_8.0"` | `hypodb_ingest.c:548` |
| `location_algorithm` | `csnloc` \| `HYPOINVERSE` \| `NLLoc` (por `mod`) | `hypodb_ingest.c:161-167`, `:549` |
| `velocity_model` | ARC `182..194` (13 chars) o NULL si vacío | `hypodb_ingest.c:505-517`, `arc_layout.h:44` |
| `azimuthal_gap` | ARC `42..44`; **NULL si 0** | `hypodb_ingest.c:553` |
| `station_count` | ARC `39..41` | `hypodb_ingest.c:554` |
| `standard_error` | ARC `48..51` ÷ 100 (RMS); **NULL si 0** | `hypodb_ingest.c:555` |
| `status` | manual (byte 174 `M`) → `REVIEWED`; 162 → `PRELIMINARY`; 163/164 → `REVISED` | `hypodb_ingest.c:169-174`, `:556` |
| `creation_time` | DEFAULT `CURRENT_TIMESTAMP`; **no se refresca** en el UPSERT | `schema.sql:45`, `ewgui_db.c:286-299` |
| `latitude_uncertainty`, `longitude_uncertainty`, `depth_uncertainty`, `time_uncertainty`, `error_ellipse` | **nunca escritas** (§3) | `ewgui_db.c:286-299` |

### 1.3 `pick` (fases del ARC, cuelgan de `origin`)

| Columna | Valor | Fuente |
|---|---|---|
| `pick_id` | `<origin_id>/pick/<STA>.<FASE>` | `hypodb_ingest.c:568`, `hypodb_ids.c:48` |
| `channel` | NSLCO `NET.STA.LOC.CHAN`, location `--`/vacío → `""` (`C1.GO01..HHZ`) | `hypodb_ids.c:61-63` |
| `pick_time` | ARC línea de fase `17..33` | `csnhypodbp_group.c:170` |
| `phase` | `'P'` / `'S'` | `hypodb_ingest.c:576` |
| `weight` | `1 − LPWT/4` (P off 16, S off 49); 1.0 si el código no está en 0..4 | `csnhypodbp_group.c:176-179` |
| `pick_status` | `manual` si cruza un pick de `PICK_RING` (misma estación+fase, ≤ `PickMatchTolSec` 0.05 s); si no `automatic` | `hypodb_ingest.c:581-582`, `:489-502` |
| `uncertainty` | residual del localizador (P `34..37` / S `50..53` ÷ 100); NULL si blanco | `hypodb_ingest.c:580`, `csnhypodbp_group.c:188` |
| `polarity`, `probability` | **nunca escritas** (§3) | `ewgui_db.c:355-357` |

### 1.4 `magnitude` y `station_magnitude`

| Columna | Valor | Fuente |
|---|---|---|
| `magnitude_id` | `<origin_id>/mag/<TIPO>` (el UPSERT **pisa** el mismo tipo) | `hypodb_ids.c:30`, `hypodb_ingest.c:415` |
| `magnitude_value` / `magnitude_type` | `mag.mag` / `mag.szmagtype` | `hypodb_ingest.c:697` |
| `mag_uncertainty` | `mag.error` o NULL si < 0 | `hypodb_ingest.c:426` |
| `calculation_method` | `MAG_INFO.algorithm` del mensaje (`CSNNet` automática, `CSNMan` manual) | `hypodb_ingest.c:427` |
| `station_count` | `mag.nstations` | `hypodb_ingest.c:428` |
| `station_magnitude_id` | `<magnitude_id>/<NSLCO>` | `hypodb_ids.c:39` |
| `station_magnitude.channel` | NSLCO (mismo formato que `pick.channel`) | `hypodb_ingest.c:389` |
| `read_amplitude` / `calculated_magnitude` | `MAG_CHAN_INFO.Amp1` / `.mag` | `hypodb_ingest.c:390-394` |
| `amplitude_type` | `ML→AML`, `Mb→AMb`, `Ms→AMS`, otro → `AMP` | `hypodb_ingest.c:176-182` |
| **`Mww` (GUI)** | `<origin_id>/mag/Mww`, `magnitude_type=Mww`, `calculation_method=WPHASE`, `station_count` = estaciones usadas | panel `Mww` → `Save to DB` (`csnhypodbp_mww.c`), vía `ewguidb_upsert_magnitude` |

### 1.5 `tensor`

Lo escribe el panel **`Mww`** con el mecanismo focal de la inversión de fase W
(`Save to DB` → `ewguidb_upsert_tensor`, `common/src/ewgui_db.c`):

| Columna | Valor | Fuente |
|---|---|---|
| `tensor_id` | `<origin_id>/tensor/WPHASE` (re-guardar es idempotente) | `csnhypodbp_mww.c` |
| `origin_id` | el de la **solución analizada** (NO el centroide de la fase W) | `selected_id` |
| `method` / `mechanism` | `WPHASE` / `WCMT` | `csnhypodbp_mww.c` |
| `strike1/dip1/rake1`, `strike2/dip2/rake2` | planos nodales `W_bestnodal`; NULL si el log no los trae | `CsnwphaseOut.have_planes` |
| `m_rr,m_tt,m_pp,m_rt,m_rp,m_tp` | `WCMTSOLUTION` (N·m, base `r`=arriba, `t`=sur, `p`=este) | `csnwphase_parse` |
| `scalar_moment` | `Wmom` | `CsnwphaseOut.m0` |

El **centroide** de la inversión (lat/lon/prof y el `time shift`) **no** se escribe en
`origin`: el origen es el de la solución elegida, y de la fase W sólo se rescatan la
magnitud y el mecanismo. Ver `docs/WPHASE.md`.

## 2. Vuelta: base de datos → GUI

Ruta: `ewguidb_query_recent_events` → `ewguidb_query_origins` (1 por evento) →
`ewguidb_query_magnitudes` + `ewguidb_query_picks` (1 por origen) → `cargar_grupos_db`
(`csnhypodbp_ws.c:665-806`) → el **mismo** `g_store_events` que las filas vivas del anillo.

### 2.1 Tabla *Events* (maestra) y *Origins* (detalle)

| Columna UI | Origen en historial | Fuente |
|---|---|---|
| `Date` | `origin.origin_time` | `csnhypodbp_ws.c:686,698` |
| `Lat.` / `Lon.` | `origin.latitude` / `longitude` (`%.2fN/S`, `%.2fE/W`) | `csnhypodbp_ws.c:703-706` |
| `Dep` | `origin.depth` | `csnhypodbp_ws.c:707` |
| `Res` | `origin.standard_error` (NAN → 0.0) | `csnhypodbp_ws.c:708` |
| `Azm` | `origin.azimuthal_gap` (NAN → 0.0); solo en la maestra | `csnhypodbp_ws.c:710` |
| `#Stn` | `origin.station_count` (< 0 → 0) | `csnhypodbp_ws.c:712` |
| `ID` | `atoi(event_id+1)` → `%010d` (el sufijo no molesta: `atoi` corta en `-`) | `csnhypodbp_ws.c:676,714` |
| `Ml/Mwp/Mb/Ms` | `magnitude.magnitude_value`-`station_count`, por tipo exacto (`"%.1f-%d"`) | `csnhypodbp_ws.c:718-732` |
| `Mod` | tras el último `:` del `origin_id` → `ModLabel` | `csnhypodbp_ws.c:684,715` |
| `Ver` | constante `"0"` (la versión no está en la BD, ver P2) | `csnhypodbp_ws.c:716,752` |
| `Est.` | `origin.status == "REVIEWED" ? "M" : "A"` | `csnhypodbp_ws.c:741` |
| `Created` | `origin.creation_time` (historial) / hora de llegada a la GUI (fila viva) | `csnhypodbp_ws.c:701`, `:961` |
| `N` | nº de soluciones del grupo | `csnhypodbp_master.c:37` |
| (oculta) `origin-id` | `origin.origin_id`, para pedir `station_magnitude` | `csnhypodbp_ws.c:740` |

### 2.2 Tabla de estaciones (*Ondas*) y pestaña *magnitudes*

| Columna UI | Origen | Fuente |
|---|---|---|
| `station` / `dist[km]` | `StaArray` + distancia al hipocentro | `csnhypodbp_gui.c:110-135` |
| `pick` | `P` / `S` según la fila | `csnhypodbp_staitem.c:123` |
| `stat` | `M` > `A` > `Teo` (`statable_build`) | `csnhypodbp_statable.c:101` |
| `res` | `resid_for(sol)` → `%+.3f`; de la BD, `pick.uncertainty` | `csnhypodbp_staitem.c:117`, `csnhypodbp_ws.c:769` |
| *magnitudes*: `dist`, ancla, `t0`/`t1`, `mag`, `res`, `stat` | `magrow_build` desde la tabla de estaciones + `station_magnitude` de la BD | `csnhypodbp_gui.c:292`, `csnhypodbp_ws.c:1160-1188` |

## 3. Columnas del esquema que ningún código escribe (siempre NULL/vacías)

| Columnas | Estado | Motivo |
|---|---|---|
| `origin.latitude_uncertainty`, `longitude_uncertainty`, `depth_uncertainty`, `time_uncertainty` | vacías por diseño (pendiente P4) | el ARC no las transporta |
| `origin.error_ellipse` | vacía por diseño (pendiente P4) | idem |
| `pick.polarity` | vacía por diseño | el ARC que circula no lleva primer movimiento (`csnloc/hypo_out.c:208-219`) |
| `pick.probability` | vacía por diseño | no hay picker probabilístico publicando |
| `event.creation_time`, `origin.creation_time` | DEFAULT de PostgreSQL | hora de inserción (ver P3) |

## 4. Campos leídos de la BD que la GUI no usa

`origin.author`, `origin.software`, `origin.location_algorithm`, `origin.velocity_model`,
`magnitude.mag_uncertainty`, `station_magnitude.amplitude_type`, `pick.weight`,
`pick.pick_status`, `pick.pick_id`. Las columnas de incertidumbre y `pick.polarity/probability`
ni se seleccionan (`ewgui_db.c:649-653`, `:704-706`).

## 5. Contrato de orden de las tablas

El orden **visible** lo impone el `GtkSortListModel` de cada tabla (cliente); el `ORDER BY` de la
consulta sólo fija el orden de inserción en el store, que es el desempate cuando el criterio
visible empata. Para que el resultado sea determinista y coherente con el default visible:

| Tabla | Default visible | `ORDER BY` de la consulta | Desempate |
|---|---|---|---|
| *Events* (maestra) | `Date` (`otime`) DESC | `recent_events`: `creation_time DESC, event_id` | `Created` implícito (orden del store) |
| *Origins* (detalle) | `Created` (`creation_time`) DESC | `origins`: `creation_time DESC, origin_time DESC, origin_id` | alineado al default visible |
| Mapa / búsqueda | — | `map_events`/`search`: `origin_time DESC, event_id` | id |
| Picadas (BD) | — | `picks`: `pick_time, pick_id` | id |
| Magnitudes (BD) | — | `magnitudes`: `magnitude_type, magnitude_id` | id |
| `station_magnitude` | — | `channel, station_magnitude_id` | id |
| Tabla de estaciones (*Ondas*) | `station` ASC | — (orden local) | `phase` |
| Pestaña *magnitudes* | sin orden inicial | — (orden local) | — |

Nota: la columna `Created` de una fila **viva** es la hora de llegada a la GUI (`time(NULL)`,
`csnhypodbp_ws.c:961`) y la de una fila de **historial** es `origin.creation_time` (hora de
inserción en la BD, `csnhypodbp_ws.c:701`): son fuentes distintas para la misma columna (ver P3).

## 6. Cambios de esta iteración

| Defecto | Antes | Ahora |
|---|---|---|
| D2 · `author`/`software` | `author == software == mod_author(mod)` (`csnloc`/`hyp2000`/`nlloc`) | `author = 'csnhypodbp'`; `software = $EW_VERSION` (fallback `ew_8.0`); `location_algorithm` sin cambios |
| D3 · `calculation_method` | `"CSNNet"` fijo (ignoraba el mensaje) | `MAG_INFO.algorithm` (`CSNNet`/`CSNMan`), así la magnitud ajustada a mano se reconoce al releer |
| D5 · `event_id` | `E<qid>` (el rango se repetía cada ~27,7 h y colisionaba entre sesiones) | `E<qid>-<yyyymmddhhmmss>`; además el atajo de agrupación por qid exige `|Δotime| ≤ EventMaxDtSec` |
| D10 · orden | `ORDER BY` sin desempate; default de *Origins* desalineado | desempates explícitos por tabla (§5) |

## 7. Guion de verificación manual (confirmar → BD)

1. Con EarthWorm arriba y `hypodb_ingest` consumiendo los anillos, confirma una solución en
   `csnhypodbp` (`Confirm`).
2. **Qué esperar hoy** (ver P1 en `docs/DATA-MAPPING-PENDIENTES.md`): `csnhypodbp_confirm_manual`
   **no** publica el ARC; publica las magnitudes a `HYPO_RING_REF` (con `algorithm="CSNMan"`) y los
   picks con el 12.º token `M` a `PICK_RING`. La fila `origin` la crea `csnloc` (162) al
   re-localizar esos picks — y `hyp2000_ring`/`nlloc_ring` con 163/164 — con `status='REVIEWED'`
   por el byte 174. Por eso `origin.author` sigue siendo el módulo que la escribió.
3. Consultas para inspeccionar el resultado (con los datos de conexión de
   `run_working_v8/params/db.env`):

```sql
-- Ultimos eventos con su solucion preferida
SELECT e.event_id, e.preferred_origin_id, o.author, o.software, o.location_algorithm,
       o.velocity_model, o.status, o.creation_time
FROM event e JOIN origin o ON o.origin_id = e.preferred_origin_id
ORDER BY e.creation_time DESC LIMIT 10;

-- Soluciones de un evento (el mismo orden que la tabla Origins)
SELECT origin_id, origin_time, latitude, longitude, depth, author, software,
       location_algorithm, velocity_model, azimuthal_gap, station_count,
       standard_error, status, creation_time
FROM origin WHERE event_id = 'E...'
ORDER BY creation_time DESC, origin_time DESC, origin_id;

-- Picadas y magnitudes de una solucion
SELECT pick_id, channel, pick_time, phase, weight, pick_status, uncertainty
FROM pick WHERE origin_id = 'E...:162' ORDER BY pick_time, pick_id;

SELECT magnitude_id, magnitude_value, magnitude_type, mag_uncertainty,
       calculation_method, station_count
FROM magnitude WHERE origin_id = 'E...:162';

SELECT channel, read_amplitude, calculated_magnitude, amplitude_type
FROM station_magnitude WHERE magnitude_id = 'E...:162/mag/ML';
```

4. Qué mirar: que `origin_time`/`pick_time` sean el instante UTC correcto (`+00`), que `channel` sea
   `NET.STA.LOC.CHAN` (`--` → vacío), que `pick_status='manual'` solo en las picadas que pasaron por
   `PICK_RING`, y que `calculation_method` sea `CSNMan` en la magnitud ajustada a mano.
