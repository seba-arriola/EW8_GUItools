# wavestore — store de formas de onda indexado

Librería headless + ingester para almacenar y servir formas de onda de forma
**más eficiente que los tanks planos**, sin dependencias
nuevas.

## Idea

Un **tank indexado**: los datos siguen siendo una secuencia `TRACE2_HEADER` +
`int32` (idéntica a un tank), pero cada segmento lleva al lado un **índice**
que permite leer una ventana por SCNL en O(log n) sin barrer todo el fichero.
Así:

- `pick_FP`, `pickS` y `csnmags` siguen leyendo el `.tank` **sin cambios**.
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
| `ChannelFilterFile` | — (sin filtro: se guarda todo) | lista SCNL de canales a guardar (ver abajo) |

### Filtro de canales (`ChannelFilterFile`)

`ChannelFilterFile wstore.sta` apunta a un archivo con **una línea por patrón**
`STA NET CHAN LOC` (separados por espacios o tabs; `#` comenta el resto de la
línea; las vacías y los comentarios se ignoran):

```
# ST01 vertical + horizontales, con localizacion nula
ST01  CX  HH?  --
# cualquier estacion de la red C1_AC01, cualquier canal y loc
*     C1_AC01  *  *
# todo (lo mismo que no poner la clave)
* * * *
```

- `*` = cualquier secuencia (incluida vacía), `?` = exactamente 1 carácter; el
  resto es literal y **case-sensitive**.
- `LOC`: `--` (o vacío) es la localización nula de EarthWorm; `*` casa con
  cualquier localización.
- El canal es el del **store** (`HHZ`, `HHN`, …), **no** el selector de
  `slink2ew` (que se escribe `HH?.D`).
- Sin la clave ⇒ se guarda todo. Archivo ilegible o línea mal formada ⇒ el
  ingester **no arranca** (mensaje `archivo:línea` en stderr); archivo sin
  patrones ⇒ aviso y se guarda todo.
- Los descartes por filtro se cuentan aparte de `dropped`
  (`wstore_ingest_filtered()`), porque no son un fallo: son política.
- El filtro sólo puede **reducir** lo que `slink2ew` ya entrega: un canal que no
  esté en los selectores de `slink2ew_*.d` no se puede recuperar aquí.
- No afecta a `tank2store`, `store2tank`, `wstore_query` ni a la API del store
  (una conversión o una consulta explícitas no se filtran).
- La antigua clave `ChannelFilter` (que ningún código leía) se retiró: su lugar
  lo ocupa `ChannelFilterFile`.

Para arrancarlo con EarthWorm, añade en `startstop_unix.d`:

```
Process           "wstore_ingest wavestore.d"
Class/Priority    OTHER 0
```

## Integración en los módulos

- **`csnhypodbp`**: claves `StoreRoot` y `FetchThreads`. Las ondas salen del store (y del SDS para
  lo antiguo).
- **`csnmags`**: modo `store` (offline) sobre el mismo store.

## API

```c
#include "wstore.h"
WStore *s; wstore_open(&s, root, NULL);
WStoreTrace tr; wstore_get(s, sta, net, chan, loc, t0, t1, &tr);  /* 0 datos, 1 sin datos */
wstore_trace_free(&tr); wstore_close(s);
```

`WaveSource` (`wavesource.h`) unifica los backends `WSRC_TANK` y `WSRC_STORE`.

## Tests

```bash
source ./ew8_unix.sh
make -C ew_gui_tools/wavestore check   # headless: store, concurrencia, ingester, filtro, config, wavesource, conversores
```
