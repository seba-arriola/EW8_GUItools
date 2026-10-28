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
| `event_id` | `E<qid>-<yyyymmddhhmmss>` (UTC de la hora origen de la 1.ª solución del grupo) | `ewgui/db_ids.c:20`, `hypodb_ingest.c:335` |
| `event_type` | `'EARTHQUAKE'` fijo | `hypodb_ingest.c:535` |
| `felt` / `published` | `false` / `false` fijos | `hypodb_ingest.c:533` |
| `creation_time` | DEFAULT `CURRENT_TIMESTAMP` (nadie la escribe) | `schema.sql:16`, `ewgui_db.c:266-269` |
| `preferred_origin_id` | `UPDATE` posterior, nunca en el INSERT; la confirmación manual **más reciente** manda (§1.2) | `hypodb_ingest.c:451-540`, `ewgui_db.c:483-509` |
| `preferred_magnitude_id` | idem, pero **no se borra** si no hay candidata | `hypodb_ingest.c:530-536`, `ewgui_db.c:483-495` |

### 1.2 `origin` (fila viva del asociador + una por iteración de refinador vivo + una por confirmación manual)

| Columna | Valor | Fuente |
|---|---|---|
| `origin_id` | `<event_id>:<mod>` (fila viva de `csnloc`) · `<event_id>:<mod>:<version>:I` (**iteración** de un localizador `live_usage=1`) · `<event_id>:<mod>:<version>` (**confirmación manual**: snapshot inmutable) | `ewgui/db_ids.c:48-110` |
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
| `status` | manual (byte 174 `M`) → `REVIEWED`; 162 → `PRELIMINARY`; 163/164 → `REVISED` | `hypodb_ingest.c:169-174`, `:612` |
| `creation_time` | DEFAULT `CURRENT_TIMESTAMP`; **no se refresca** en el UPSERT | `schema.sql:45`, `ewgui_db.c:286-299` |
| `latitude_uncertainty`, `longitude_uncertainty`, `depth_uncertainty`, `time_uncertainty`, `error_ellipse` | **nunca escritas** (§3) | `ewgui_db.c:286-299` |

### 1.3 `pick` (fases del ARC, cuelgan de `origin`)

| Columna | Valor | Fuente |
|---|---|---|
| `pick_id` | `<origin_id>/pick/<STA>.<FASE>` | `hypodb_ingest.c:568`, `ewgui/db_ids.c:70` |
| `channel` | NSLCO `NET.STA.LOC.CHAN`, location `--`/vacío → `""` (`C1.GO01..HHZ`) | `ewgui/db_ids.c:79-91` |
| `pick_time` | ARC línea de fase `17..33` | `csnhypodbp_group.c:170` |
| `phase` | `'P'` / `'S'` | `hypodb_ingest.c:576` |
| `weight` | `1 − LPWT/4` (P off 16, S off 49); 1.0 si el código no está en 0..4 | `csnhypodbp_group.c:176-179` |
| `pick_status` | `manual` si cruza un pick de `PICK_RING` (misma estación+fase, ≤ `PickMatchTolSec` 0.05 s); si no `automatic` | `hypodb_ingest.c:581-582`, `:489-502` |
| `uncertainty` | residual del localizador (P `34..37` / S `50..53` ÷ 100); NULL si blanco | `hypodb_ingest.c:580`, `csnhypodbp_group.c:188` |
| `polarity`, `probability` | **nunca escritas** (§3) | `ewgui_db.c:355-357` |

### 1.4 `magnitude` y `station_magnitude`

| Columna | Valor | Fuente |
|---|---|---|
| `magnitude_id` | `<origin_id>/mag/<TIPO>` (el UPSERT **pisa** el mismo tipo) | `ewgui/db_ids.c:45`, `hypodb_ingest.c:415` |
| `magnitude_value` / `magnitude_type` | `mag.mag` / `mag.szmagtype` | `hypodb_ingest.c:697` |
| `mag_uncertainty` | `mag.error` o NULL si < 0 | `hypodb_ingest.c:426` |
| `calculation_method` | `MAG_INFO.algorithm` del mensaje (`CSNNet` automática, `CSNMan` manual) | `hypodb_ingest.c:427` |
| `station_count` | `mag.nstations` | `hypodb_ingest.c:428` |
| `station_magnitude_id` | `<magnitude_id>/<NSLCO>` | `ewgui/db_ids.c:61` |
| `station_magnitude.channel` | NSLCO (mismo formato que `pick.channel`) | `hypodb_ingest.c:389` |
| `read_amplitude` / `calculated_magnitude` | `MAG_CHAN_INFO.Amp1` / `.mag` | `hypodb_ingest.c:390-394` |
| `amplitude_type` | `ML→AML`, `Mb→AMb`, `Ms→AMS`, otro → `AMP` | `hypodb_ingest.c:176-182` |
| **`Mww` (GUI)** | `<origin_id>/mag/Mww`, `magnitude_type=Mww`, `calculation_method=WPHASE`, `station_count` = estaciones usadas | `csnhypodbp_mww_persist` (`csnhypodbp_mww.c`), al **confirmar** la solución con el radio `Mww` marcado, vía `ewguidb_upsert_magnitude` |

### 1.5 `tensor`

Lo escribe la GUI con el mecanismo focal de la inversión de fase W, **al confirmar la
solución** con el radio `Mww` del preview (`csnhypodbp_mww_persist` →
`ewguidb_upsert_tensor`, `common/src/ewgui_db.c`):

| Columna | Valor | Fuente |
|---|---|---|
| `tensor_id` | `<origin_id>/tensor/WPHASE` (re-guardar es idempotente) | `csnhypodbp_mww.c` |
| `origin_id` | el de la **solución confirmada** (NO el centroide de la fase W). Si ese origen todavía no está en la base, la GUI **crea** `event` + `origin` con los ids de `ewgui/db_ids.h` y no pisa uno existente (`ewguidb_upsert_origin_if_absent`) | `csnhypodbp_mww.c` |
| `method` / `mechanism` | `WPHASE` / `WCMT` | `csnhypodbp_mww.c` |
| `strike1/dip1/rake1`, `strike2/dip2/rake2` | planos nodales `W_bestnodal`; NULL si el log no los trae | `CsnwphaseOut.have_planes` |
| `m_rr,m_tt,m_pp,m_rt,m_rp,m_tp` | `WCMTSOLUTION` (N·m, base `r`=arriba, `t`=sur, `p`=este) | `csnwphase_parse` |
| `scalar_moment` | `Wmom` | `CsnwphaseOut.m0` |

El **centroide** de la inversión (lat/lon/prof y el `time shift`) **no** se escribe en
`origin`: el origen es el de la solución confirmada, y de la fase W sólo se rescatan la
magnitud y el mecanismo. La **preferida del evento** pasa a ser esa solución con su `Mww`
(`ewguidb_set_preferred`), porque el `Mww` no viaja en el ARC (ver `docs/pendienteMww.md`).
Ver `docs/WPHASE.md`.

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
| `Mod` | `ewgui_origin_id_parse(origin_id)` → `mod` → `ModLabel` | `csnhypodbp_ws.c:714,740` |
| `Ver` | versión del `origin_id`: real para confirmadas (`<event>:<mod>:<version>`) y para iteraciones (`<event>:<mod>:<version>:I`); `0` en la fila viva del asociador | `csnhypodbp_ws.c:716,752` |
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
2. **Qué esperar**: `csnhypodbp_confirm_manual` publica las magnitudes a `HYPO_RING_REF` (con
   `algorithm="CSNMan"`), los picks con el 12.º token `M` a `PICK_RING` y, además, el **ARC de la
   solución confirmada** en el anillo de confirmaciones (`ConfRing`, p. ej. `HYPO_RING_REF2`) con el
   `mod` real del candidato (162/163/164). `hypodb_ingest` lo ingiere por `InRingConf` como
   **snapshot**: escribe una fila `origin` propia con `origin_id = <event>:<mod>:<version>`,
   `status='REVIEWED'`, sus picadas, y recalcula `event.preferred_origin_id` = la confirmación
   **más reciente** (`creation_time`). Las confirmaciones anteriores **no** se pisan: quedan como
   orígenes manuales históricos. La fila viva `<event>:<mod>` la mantiene `csnloc` (162). Los
   refinadores con `live_usage=1` (`hyp2000_ring` 163) escriben una fila
   `<event>:<mod>:<version>:I` por iteración válida, así que *Origins* conserva el historial completo
   de soluciones.
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
