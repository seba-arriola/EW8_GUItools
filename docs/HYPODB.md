# HYPODB — base de datos hipocentral

Almacén **PostgreSQL** de la información hipocentral del sistema, importado tal cual del
proyecto `test_db` (simplificación QuakeML para el CSN). Sustituye el flujo hipocentral por
archivos (`csnhypodbp_hist.txt`, el CSV de `csnstaevdisp`): los **anillos siguen siendo el
transporte en vivo** y la BD es el **store persistente** que consultan los visores.

## Esquema

6 tablas (`db/schema.sql`, copia literal; extensión `uuid-ossp`):

| Tabla | Contenido |
|---|---|
| `event` | agrupador; `preferred_origin_id`/`preferred_magnitude_id` (FK diferidas) |
| `origin` | una solución (t0, lat, lon, depth, gap, `station_count`, `standard_error`=RMS, `status`, `velocity_model`) |
| `pick` | fases del ARC (cuelga de `origin`): `channel` NSLCO, `phase`, `weight`, `pick_status` |
| `magnitude` | cuelga de `origin`: value, type (ML/Mwp/Mb/Ms), error, `calculation_method` |
| `station_magnitude` | por estación (NSLCO + amplitud + magnitud) |
| `tensor` | mecanismos focales — **vacía** (no hay productor todavía) |

## Requisitos

- **Docker** + plugin **compose** (para la BD).
- **`libpq-dev`** solo para **compilar** los módulos C (`common`, `hypodb`, visores). En runtime
  basta **`libpq5`** (normalmente ya instalado).
- Puerto host **5435** libre para la instancia 0 (5433/5434 los ocupan otros contenedores de
  desarrollo; las instancias de worktree usan 5435+N).

## Configuración (docker)

`db/docker-compose.yml`: `postgres:16`, contenedor **`ew8_hypodb`**, puerto `5435:5432`, volumen
**`ew8_hypodb_ew8_db_data`**, `restart: unless-stopped`, `healthcheck` con `pg_isready`, y el
esquema autoaplicado al crear el volumen (`/docker-entrypoint-initdb.d/01-schema.sql`).

El nombre del proyecto, el del contenedor, el puerto y el `restart` se interpolan
(`${COMPOSE_PROJECT_NAME:-ew8_hypodb}`, `${DB_PORT:-5435}`, `${DB_RESTART:-unless-stopped}`), que es
lo que permite tener **una instancia por worktree** (ver "Instancias" abajo).

Credenciales en **`run_working_v8/params/db.env`** (no versionado; versionado `db.env.example`):

```
DB_HOST / DB_PORT / DB_NAME / DB_USER / DB_PASSWORD          # conexión de los módulos (libpq)
POSTGRES_USER / POSTGRES_PASSWORD / POSTGRES_DB              # inicialización del contenedor
```

Las variables de entorno tienen precedencia sobre el archivo al conectar desde los módulos.

## Ciclo de vida

```bash
# Arrancar (una vez, si falta db.env)
cp run_working_v8/params/db.env.example run_working_v8/params/db.env
./ew8_db.sh up                         # equivale a `up -d --wait` y muestra el estado
db/test_schema.sh -i 0                 # verifica las 6 tablas (AC1)

# Parar (conserva los datos) / estado / logs / psql
./ew8_db.sh down
./ew8_db.sh status
./ew8_db.sh logs
./ew8_db.sh psql

# Reset total (BORRA los datos y reaplica el esquema, con confirmación)
./ew8_db.sh reset

# Borrado definitivo de una instancia (contenedor + volumen + red, sin recrear)
./ew8_db.sh -i 2 destroy --yes
./ew8_db.sh instances                  # qué instancias hay (y cuáles quedaron huérfanas)
```

### Instancias (worktrees)

Cada worktree puede tener su **propia** base, aislada: el mismo `docker-compose.yml` con proyecto,
contenedor y volumen propios, y otro puerto.

```bash
./ew8_db.sh -i 2 up      # proyecto/contenedor ew8_hypodb_2, puerto 5437, volumen propio
```

- Requiere que el `db.env` de ese worktree declare `DB_PORT=5437`; el script **aborta** si no
  coincide (si no, los módulos apuntarían a otra base). Solo para `up`/`psql`/`reset`: mirar o
  borrar la base de otra instancia —o la de un worktree ya borrado, que no tiene `db.env`— no lo
  exige (avisa por stderr y sigue).
- Las instancias ≥1 usan `restart: no`: no se auto-arrancan al bootear.
- Se gestionan por separado: `./ew8_db.sh -i 2 status|down|reset|destroy`, y `./ew8_db.sh instances`
  lista lo que hay (marca las huérfanas). `down` **detiene** (conserva el volumen); `destroy [--yes]`
  **borra** contenedor+volumen+red sin recrear.
- Por debajo son `COMPOSE_PROJECT_NAME`/`DB_PORT`/`DB_RESTART`; también vale
  `EWGUI_INSTANCE=2 ./ew8_db.sh up`.

- **Autoarranque**: la instancia 0 (`restart: unless-stopped`) arranca sola con Docker (salvo que la
  hayas parado explícitamente con `down`). Las instancias de worktree usan `restart: no`.
- **Independencia de EarthWorm**: reiniciar EarthWorm **no** afecta a la BD; sus datos siguen en el
  volumen. `hypodb_ingest` se reconecta solo (reintenta cada 10 s si no encuentra la BD).

## Monitoreo

```bash
docker compose -f db/docker-compose.yml ps           # estado + healthcheck
docker compose -f db/docker-compose.yml logs -f db   # logs del contenedor
docker exec ew8_hypodb pg_isready -U csn -d csn_hypo
docker compose -f db/docker-compose.yml exec db psql -U csn -d csn_hypo

# Consultas útiles
docker compose -f db/docker-compose.yml exec -T db psql -U csn -d csn_hypo -c \
  "SELECT count(*) AS events FROM event;"
docker compose -f db/docker-compose.yml exec -T db psql -U csn -d csn_hypo -c \
  "SELECT e.event_id,o.origin_time,o.latitude,o.longitude,o.depth,o.velocity_model
     FROM event e JOIN origin o ON o.origin_id=e.preferred_origin_id
    ORDER BY o.origin_time DESC LIMIT 10;"
```

## Troubleshooting

- **`Bind for 0.0.0.0:5433 failed`**: otro contenedor usa ese puerto → la BD usa **5435** (ya
  configurado); si hace falta otro, cámbialo en `docker-compose.yml` y en `db.env`.
- **`Text file busy` al `make`**: un binario del módulo está corriendo; `rm -f
  ew_8.0/bin/<modulo>` antes de recompilar.
- **El esquema no aparece**: se aplica solo al crear el volumen → `down -v` + `up`.
- **Los módulos no ven la BD**: revisa `run_working_v8/params/db.env` y que `DbEnvFile` apunte a
  él en los `.d`; los módulos registran el error de conexión en su log.
- **`hypodb_ingest` moría con `SIGABRT` (signal 6) entre 9 y 77 minutos después de arrancar, sin
  log**: el array de canales de `TYPE_MAGNITUDE` se dimensionaba contando `'\n'`, y `rd_chan_mag`
  (EarthWorm) **no acota** sus escrituras contra el tamaño que se le pasa ⇒ escribía fuera del heap
  y glibc lo detectaba minutos después. Ahora el dimensionado es **lo que el motor escribe de
  verdad** (`csnhypodbp_magchan.c`, compartido con `csnhypodbp`), sobre una copia **con holgura a
  ceros** porque el motor además parsea in-place (destruye el mensaje) y lee 1 byte de más.
  Cobertura: `test_magchan` + `make -C ew_gui_tools/hypodb check-valgrind`.
- **Al reiniciar el módulo, `startstop` espera 5 s y lo mata** (`did not self-terminate in 5 sec`):
  la parada se pide con `TERMINATE` **o** con el PID del módulo escrito en el anillo, y hay que
  mirar **las dos** y **dentro** del drenado (el cuerpo puede procesar 3×1000 mensajes con
  escrituras a la BD; es lo que hacen `csnloc`, `csnmags`, `hyp2000_ring` y `nlloc_ring`).
- **¿Dónde está el log del módulo?** `logit_init` escribe `EW_LOG/hypodb_<fecha>.log` (se **añade**,
  no se trunca). Con `Debug 1` en `hypodb.d` se registran además los mensajes descartados con su
  longitud (`ARC ilegible…`, `TYPE_MAGNITUDE ilegible…`).

## Mapeo de datos (ingester `hypodb_ingest`)

- **IDs** (VARCHAR(64)): `event_id = E<qid>-<yyyymmddhhmmss>` · `origin_id = <event_id>:<mod>` (fila
  viva) **o** `<event_id>:<mod>:<version>` (**confirmación manual**; `version` = campo 178..181 del
  ARC, 1..9999) · `magnitude_id = <origin_id>/mag/<TIPO>` ·
  `station_magnitude_id = <magnitude_id>/<NSLCO>` · `pick_id = <origin_id>/pick/<STA>.<FASE>`.
- **`event`**: agrupación por firma de picadas (reutiliza `csnhypodbp_group.c`); un `event` por
  sismo físico aunque cambie el qid (el atajo por qid exige además
  `|Δotime| <= EventMaxDtSec`, clave de `hypodb.d`, default 300 s, para no fusionar dos sismos de
  sesiones distintas de csnloc). Al arrancar reconstruye los grupos desde la BD
  (`EventLoadHours`) para conservar los `event_id`.
- **`origin`**: una fila viva por `(event, mod)` con la **última versión** que publica el pipeline
  (se actualiza en sitio), **más una fila por cada confirmación manual** (`csnhypodbp` → `ConfRing`
  → `InRingConf`) con `origin_id = <event>:<mod>:<version>`; esas filas son **inmutables** y quedan
  como orígenes manuales históricos. `status` = `REVIEWED` si la solución es manual (byte 174), si no
  `PRELIMINARY`(162)/`REVISED`(163/164). `author` es constante (`csnhypodbp`, el módulo que
  revisa/produce el registro), `software` sale de `$EW_VERSION` y el localizador real queda en
  `location_algorithm` (`csnloc`/`HYPOINVERSE`/`NLLoc`). Las confirmaciones **no** pasan por la
  guarda de versión (su `version` es la del candidato, no la del pipeline).
- **`pick`**: fases del ARC; `pick_status` = `manual` si correlaciona con un pick de `PICK_RING`
  (12.º token `M`), si no `automatic`.
- **`magnitude`**: `TYPE_MAGNITUDE`; `station_magnitude` desde las líneas por estación
  (`wr_mag_scnl` en `csnmags`). `calculation_method` copia el `algorithm` del mensaje (`CSNNet`
  automática, `CSNMan` ajustada a mano), que es la marca que `csnhypodbp` busca al releer de la BD.
- `preferred_*` se recalculan **leyendo los `origin` del evento en la BD**: si hay confirmaciones
  manuales manda la **más reciente** (`creation_time`; desempate `version` y hora origen), si no la
  manual más reciente y si no más fases / menor RMS. La magnitud preferida es
  `Mwp>Ms>Mb>ML` (o el tipo que pidió el ARC) y **no se borra** cuando aún no hay candidata (p. ej.
  tras reiniciar, antes del primer `TYPE_MAGNITUDE`).

## Mapeo de velocidad (`origin.velocity_model`)

`origin.velocity_model` guarda el **modelo de velocidades / curvas de tiempos de llegada** con el
que cada localizador calculó su solución. **Cada localizador lo aporta** escribiéndolo en el ARC:

- **Dónde viaja**: ARC, **línea‑1, offsets 182..194** (13 chars), campo
  `EWGUI_ARC_VMODEL_OFF`/`EWGUI_ARC_VMODEL_LEN` en `common/include/ewgui/arc_layout.h`.
  **No puede ir en 122..135**: HYPOINVERSE lee la línea‑1 como su resumen
  (`hypoinverse/hyphs.for`, `FORMAT 1212 = T123,2(A1,F3.2,F3.1)`) y en ese tramo espera las
  magnitudes (`BMTYPI/BMAGI/TEMPI`); un nombre como `iasp91` en 123 hace abortar LOC con
  `Bad value during floating point read`, y `hyp2000_ring` le pasa el ARC de entrada **verbatim** a
  LOC, así que pierde el evento. 182..194 queda fuera de la línea de HYPOINVERSE (179 chars; el
  `FORMAT` no lee más allá de 163) y de los campos 136..145 (qid), 161, 174 (marca manual) y
  178..181 (versión); la línea‑1 mide 197 bytes y se normaliza a 197 en `hyp2000_ring`/`nlloc_ring`.
  `hyp2000_ring` además **limpia 122..135** antes de escribir `arcIn`, como red de seguridad para
  ARCs viejos o ya presentes en el anillo.
- **Quién lo escribe**:

| mod | Localizador | Modelo que aporta | Origen del nombre |
|---|---|---|---|
| 162 | `csnloc` | `iasp91` | clave `TauTable` de `csnloc.d` |
| 163 | `hyp2000_ring` | `CHILE_POTIN` (o la banda) | clave `VelocityModel`; los `.crh` se derivan del **3D de Potin 2019** (`mk_crh_from_3d.py`); alternativas `chile_1d` (Husen1999), `ak135` |
| 164 | `nlloc_ring` | banda seleccionada (`N18-26_1.5k` … `CHILE_4k`) o el fallback 1D | `ModelBand <nombre>` de `nlloc_ring.d` (grillas 3D de **Potin 2019**); `ModelFallback` para el 1D |

- **Quién lo lee**: `hypodb_ingest` copia los offsets 182..194 a `origin.velocity_model`.

Modelos presentes en el repo: **Potin 2019** (3D, bandas `N18-26_1.5k`…`N38-46_1.5k` y `CHILE_4k`;
`resources/nlloc/time3d/`), **iasp91** (1D, `csnloc/iasp91.tbl`), **ak135** y **chile_1d**
(Husen1999) (`.crh`).

## Tests

```bash
make -C ew_gui_tools/hypodb check                                   # ids + agrupación (headless)
EWGUI_DB_TEST_ENV=run_working_v8/params/db.env \
    ./ew_gui_tools/hypodb/test_ingest_db                            # integración con la BD real
make -C ew_gui_tools/common check                                   # capa ewgui_db (config + SQL)
```
