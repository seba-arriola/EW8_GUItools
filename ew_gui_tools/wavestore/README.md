# wavestore — store de formas de onda indexado

Librería headless + ingester para almacenar y servir formas de onda de forma
**más eficiente que los tanks planos** y que `wave_serverV`, sin dependencias
nuevas.

## Idea

Un **tank indexado**: los datos siguen siendo una secuencia `TRACE2_HEADER` +
`int32` (idéntica a un tank), pero cada segmento lleva al lado un **índice**
que permite leer una ventana por SCNL en O(log n) sin barrer todo el fichero.
Así:

- `pick_FP`, `pickS`, `csnmags` y `tankplayer` siguen leyendo el `.tank` **sin cambios**.
- El lector nuevo usa el `.idx` para acceso aleatorio y paralelo.
- El ingester escribe en paralelo (un hilo por shard; cada canal en un solo shard).

## Formato en disco

```
<StoreRoot>/<NET>.<STA>.<LOC>.<CHAN>/<YYYYMMDD>.tank   # TRACE2_HEADER + int32 (i4)
<StoreRoot>/<NET>.<STA>.<LOC>.<CHAN>/<YYYYMMDD>.idx    # índice lateral
```

- `.tank`: tank válido, en orden creciente de `starttime` (si llega desorden,
  el índice se marca "no ordenado" y la lectura cae a barrido; no se pierde nada).
- `.idx`: cabecera `WSTIDX1` + entradas `{start, end, rate, offset, nsamp}`.
  La escritura publica `count` **después** de los datos (lectores nunca ven un
  registro parcial).
- Rollover diario UTC; retención configurable.

## Herramientas

| binario | para qué |
|---|---|
| `wstore_ingest <wavestore.d>` | lee `SLINK_RING` y escribe el store en vivo |
| `tank2store <tank> --root <store>` | carga un tank (histórico) al store |
| `store2tank --root <store> --scnl NET.STA.LOC.CHAN --start <e> --end <e> --out f.tank` | materializa una ventana en un tank para `pick_FP`/`pickS` |
| `wstore_query --root <store>` | lista SCNL, nº de registros y span |

## Config (`wavestore.d`)

| clave | default | significado |
|---|---|---|
| `StoreRoot` | — (obligatoria) | raíz del store |
| `RingName` | `SLINK_RING` | anillo de entrada |
| `RetentionDays` | 7 | borra segmentos más viejos |
| `IngestShards` | 4 | hilos de escritura (sharding por SCNL) |
| `HeartbeatInt` | 30 | latido |
| `LogFile` | 1 | log de EarthWorm |

Para arrancarlo con EarthWorm, añade en `startstop_unix.d`:

```
Process           "wstore_ingest wavestore.d"
Class/Priority    OTHER 0
```

## Integración en los módulos

- **`csnhypodbp`**: claves `WaveBackend store|ws|auto` (default `auto`),
  `StoreRoot`, `FetchThreads`. En `auto`, intenta el store y cae a
  `wave_serverV` si no hay datos.
- **`csnmags`**: modo `store` (offline) sobre el mismo store.

## API

```c
#include "wstore.h"
WStore *s; wstore_open(&s, root, NULL);
WStoreTrace tr; wstore_get(s, sta, net, chan, loc, t0, t1, &tr);  /* 0 datos, 1 sin datos */
wstore_trace_free(&tr); wstore_close(s);
```

`WaveSource` (`wavesource.h`) unifica los backends `WSRC_WS` (wave_serverV),
`WSRC_TANK`, `WSRC_STORE` y `WSRC_AUTO`.

## Tests

```bash
source ./ew8_unix.sh
make -C ew_gui_tools/wavestore check   # headless: store, concurrencia, ingester, wavesource, conversores
```
