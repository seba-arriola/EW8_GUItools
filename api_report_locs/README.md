# api_report_locs

Reporte de **localizaciones** (`csnloc`) y **magnitudes** (`csnmags_toy`) a partir
de los logs que deja `tank_tools/run_events.sh` en la carpeta `runs/`.

Solo usa la **biblioteca estándar** de Python 3 (no requiere `numpy`, `pandas`
ni instalar nada).

## Uso

```bash
# tabla por stdout
python3 -m api_report_locs.report --runs runs

# un evento concreto
python3 -m api_report_locs.report --runs runs --slug simulacion3_Illapel20150916

# tabla + CSV + JSON
python3 -m api_report_locs.report --runs runs --format all --out /tmp/reporte

# comparar con un catálogo de referencia
python3 -m api_report_locs.report --runs runs --catalog referencias.csv
```

### Opciones

| opción | efecto |
|---|---|
| `--runs DIR` | directorio de resultados de `run_events.sh` (def: `runs`) |
| `--slug S` | filtra por evento (repetible) |
| `--format table\|csv\|json\|all` | qué emitir (def: `table`) |
| `--out PREFIJO` | prefijo de salida (def: `<runs>/report`) |
| `--catalog CSV` | CSV de referencia con columnas `slug,lat,lon` (opcional) |
| `--dup-tol-deg G` | tolerancia en grados para agrupar soluciones (def: 0.5) |
| `--dup-tol-s S` | tolerancia en segundos para agrupar (def: 300) |
| `--quiet` | no imprimir la tabla |

### Códigos de salida

| código | significado |
|---|---|
| 0 | ok, con al menos un evento localizado |
| 1 | error de uso |
| 2 | no se encontró ningún evento localizado |
| 3 | no hay sesiones en `--runs` |

## Qué lee

`runs/<slug>/<NNN>/session.json` (manifiesto que escribe el runner) y los logs a
los que apunta:

| log | qué se extrae |
|---|---|
| `logs/csnloc_YYYYMMDD.log` | eventos (`csnloc: evento N lat=… lon=…`), resumen de salida, y la hora origen real del volcado `HYP2000ARC` |
| `logs/csnmags_toy_YYYYMMDD.log` | magnitudes de red (`CSNmags_Red: [ID …] ML=… \| MWp=… -> PREF: …`) |
| `logs/wave_serverV_YYYYMMDD.log` | descartes por validez (`fails validity check, discarding`) |

## Cómo se cruzan localización y magnitud

`csnloc` escribe el ID del evento en los bytes 136-145 del HYP2000ARC como
`"%010lu" % (id % 2147000000)` (`hypo_out.c:94-95`) y loguea **ese mismo** `id` en
`csnloc: evento N` (`csnloc.c:212/228`). `csnmags_toy` lee esos 10 bytes tal cual
(`csnmags_toy.c:266`). Por tanto `int([ID …]) == N` y el cruce es por igualdad
numérica.

**Importante:** el contador se reinicia en cada arranque de `csnloc`
(`csnloc.c:300`), así que el cruce se hace **siempre dentro de una misma sesión**;
nunca entre sesiones.

## Hora histórica del evento

El replay **re-estampa** el dato a "ahora" (`tankplayer.c:650`), así que la hora
origen que calcula `csnloc` es la del replay (año actual), no la del sismo. El
runner guarda el `offsetTime` que publica `tankplayer` en `session.json`, y el
reporte lo resta para dar la columna **`origin_hist_utc`**, que es la hora real
del evento (la que se compara con un catálogo).

## Duplicados

`csnloc` emite **varias soluciones** del mismo sismo a medida que llegan picks, y
los trozos solapados lo repiten. El reporte:

- **no descarta** ninguna fila,
- marca `dup = 1` las que pertenecen al mismo grupo,
- cuenta **eventos únicos** agrupando por posición (±`--dup-tol-deg`) y hora
  (±`--dup-tol-s`) con cierre transitivo (union-find, porque las soluciones
  forman cadenas que un agrupado voraz partiría en dos).

## Columnas de la tabla

| columna | significado |
|---|---|
| `slug` | evento (nombre del tank) |
| `det_utc` | instante (de replay) en que `csnloc` registró la solución |
| `origen_hist_utc` | **hora origen histórica** del sismo (del ARC menos `offsetTime`) |
| `lat`, `lon`, `z_km` | hipocentro |
| `nph`, `rms`, `gap` | número de fases, residuo RMS, hueco azimutal |
| `ML`, `n` | magnitud local y nº de estaciones |
| `Mwp`, `n` | magnitud de onda P y nº de estaciones |
| `PREF` | magnitud preferida (`ML`, `Mwp` o `None`) |
| `dup` | `SI` si es una solución repetida del mismo sismo |

El CSV/JSON añaden `session_id`, `chunk_index`, `source_file`, `event_id`,
`origin_time_utc` (tiempo de replay) y `notes` (p. ej. `sin magnitud`).

## Pruebas

```bash
python3 -m unittest discover -s api_report_locs/tests -t . -v
```

Cubren los formatos literales de ambos logs (con líneas reales de
`run_working_v8/log/`), el volcado del ARC, el cruce por ID, la agrupación de
duplicados y el CLI de extremo a extremo.
