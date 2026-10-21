# Refinadores por anillo (hyp2000_ring + nlloc_ring) e integración con csnhypodbp

Dos módulos EarthWorm **de anillo** (sin la cadena *sausage* `eq*`) que refinan
los hipocentros de `csnloc` y los publican en **un mismo anillo**, donde
`csnhypodbp` los muestra como filas independientes por módulo.

```
PICK_RING ─csnloc─► HYPO_RING (ARC de csnloc)
                        │
                        ├─► hyp2000_ring ─► HYPO_RING_REF ─┐
                        │       (HYPOINVERSE, barato)      │
                        │                                  ├─► csnhypodbp
                        └─► nlloc_ring   ─► HYPO_RING_REF ─┘   (una instancia)
                                (NonLinLoc, rate-limited)
```

## Automatismo

- `csnloc` emite un ARC a `HYPO_RING` **solo cuando la solución mejora**
  (`event_registry.c`), con un `qid` de evento y una versión creciente.
- `hyp2000_ring` reacciona a **cada** versión y refina (es barato).
- `nlloc_ring` **no** recalcula en cada versión: guarda el último ARC por `qid` y
  relanza NLLoc como mucho cada `NllIntervalSec` (por defecto 300 s), siempre
  sobre la versión más reciente. El bucle corre cada ~200 ms, así que dispara por
  temporizador aunque no lleguen mensajes. `NllTTLSec` (1800 s) descarta eventos
  sin novedades. `NllIntervalSec 0` = procesar cada versión.

## Etiquetado del mensaje

El ARC refinado conserva el **`qid`** (offset 136) y la **versión** (offset
178/161) del ARC de `csnloc`. El **módulo origen** viaja en el `reclogo.mod` del
mensaje de anillo (163 = `hyp2000_ring`, 164 = `nlloc_ring`). Así, para un mismo
sismo, `csnhypodbp` muestra una fila por módulo, con el mismo `ID` pero distinto
`Mod`.

Además, el ARC se **normaliza** para que `csnhypodbp` lo parsee:

- header a 197 y líneas de fase a 114;
- **longitud**: NLLoc escribe `'E'`/blanco; se normaliza a `'W'` para oeste;
- **tiempo de fase**: el estándar Y2K usa centésimas enteras (`SSCCC`, sin punto),
  que es lo que emiten HYPOINVERSE/NLLoc; `csnloc`/`csnhypodbp` usan `SS.ss`, así
  que se convierte (offset 29-33) para que el overlay de picks caiga bien.

## csnhypodbp

- Clave de evento: **`(qid, módulo)`** (`csnhypodbp_ws.c`), no solo `qid`.
- Columnas nuevas visibles: **`Mod`** (etiqueta del módulo) y **`Ver`**
  (versión). El `PickCache` también se indexa por `(qid, módulo)` para no mezclar
  las fases de un módulo con las del otro.
- Etiquetas configurables en `csnhypodbp.d`:
  ```
  InRing HYPO_RING_REF
  ModuleLabel 162 csnloc
  ModuleLabel 163 hyp2000
  ModuleLabel 164 nlloc
  ```

## Logs de análisis

Cada refinamiento deja **una línea** en el log del módulo
(`EW_LOG/hyp2000_ring_*.log`, `EW_LOG/nlloc_ring_*.log`):

```
<modulo>: LOC ev=<qid> v=<ver> in{nph=.. lat=.. lon=.. z=.. rms=..} out{nph=.. lat=.. lon=.. z=.. rms=.. gap=..} dt_ms=..
```

Ejemplo real (evento `1382500027`, con el modelo 3D de bandas):

```
hyp2000_ring: LOC ev=1382500027 v=0001 in{nph=10 lat=-21.653 lon=-67.411 z=5.00 rms=1.23} out{nph=9  lat=-21.676 lon=-67.313 z=3.72 rms=0.88 gap=304} dt_ms=..
nlloc_ring:   LOC ev=1382500027 v=0001 in{nph=10 lat=-21.653 lon=-67.411 z=5.00 rms=1.23} out{nph=10 lat=-21.481 lon=-68.264 z=130.83 rms=0.15 gap=276} dt_ms=..
```

(Con la plantilla 1D anterior, NLLoc daba `lat=-21.540 lon=-67.796 z=149.41`,
pegado al borde de su caja de ±40 km; ver "Volumen de búsqueda y fallback".)

Para analizar: `grep ' LOC ' run_working_v8/log/hyp2000_ring_*.log`.

## Configuración

| Módulo | `.d` | Claves relevantes |
|---|---|---|
| `hyp2000_ring` | `hyp2000_ring.d` | `InRing HYPO_RING`, `OutRing HYPO_RING_REF`, `CommandFile`, `WorkDir`, `GridFile`, `MinPhases`, `MaxRMS` |
| `nlloc_ring` | `nlloc_ring.d` | `InRing HYPO_RING`, `OutRing HYPO_RING_REF`, `OutRoot`, `WorkDir`, `GridFile`, `ModelBand` (repetible), `ModelFallback`, `TransOrigin`, `LocGrid`, `NllIntervalSec`, `NllTTLSec`, `NllMaxPending` |
| `csnhypodbp` | `csnhypodbp.d` | `InRing HYPO_RING_REF`, `ConfRing HYPO_RING_REF2`, `ModuleLabel` |

`HYPO_RING_REF2` es el **anillo de confirmaciones**: `csnhypodbp` publica ahí el ARC de cada solución
confirmada a mano y `hypodb_ingest` lo consume (`hypodb.d`: `InRingConf HYPO_RING_REF2`) para
guardarla como fila `origin` propia (`<event>:<mod>:<version>`). Los refinadores no lo usan.

## Puesta en marcha

`startstop_unix.d` tiene las dos entradas activas:

```
Process  "hyp2000_ring hyp2000_ring.d"
Process  "nlloc_ring nlloc_ring.d"
```

## Requisitos de `WorkDir`

`WorkDir` es la carpeta PROPIA de cada módulo (relativa a `$EW_PARAMS`:
`../hyp2000_ring`, `../nlloc_ring`), donde el motor escribe sus ficheros de
intercambio; el módulo la crea al arrancar (`mkdir -p`) y `.gitignore` la ignora.

Las ENTRADAS (configuración versionada) NO viven ahí: se referencian desde el
`WorkDir` y viven en `params/`.

- `hyp2000_ring`: `CommandFile ../params/hyp2000_ring.hyp` y, dentro del `.hyp`,
  `STA '../params/estaciones_hyp.sta'` + los 6 `CRH '../params/N18-26_1.5k.crh'` …
  `'../params/N38-46_1.5k.crh'` (derivadas del 3D de Potin; `chile_1d.crh` sigue
  disponible para volver al modelo único).
- `nlloc_ring`: las grillas de tiempos 3D (`time3d/<banda>/time/<banda>`), las
  plantillas de control (`ctrl/<banda>.in`) y el fallback 1D (`time/model`,
  `nlloc.in`) van **ABSOLUTOS al disco de datos** en `ModelBand`
  (`TtimeRoot`/`ControlFile`) y en `ControlFile`/`TtimeRoot`/`ModelFallback`
  globales. Son sólo lectura; el módulo no escribe ahí.

Las SALIDAS del motor van a la carpeta del módulo: `run_working_v8/hyp2000_ring/`
(`arcIn`, `arcOut`) y `run_working_v8/nlloc_ring/` (`obs.nll`, `nll_ring.in` y
`loc/ev.*` bajo `OutRoot`, que es relativo al `WorkDir`).

## Modelos de velocidad

- **`nlloc_ring` (3D)**: grillas de tiempos 3D por banda de latitud, derivadas del
  modelo 3D de B. Potin (formato NLLoc `.mod.hdr/.buf`, `SLOW_LEN`; `vel = dx/valor`).
  Generación (una vez, ~10 min y ~12 GB por banda a 1.5 km):
  ```
  python3 ew_gui_tools/nlloc_ring/mk_nll_grids_3d.py \
      --mod-dir /mnt/d/nll/time --outdir resources/nlloc/time3d \
      --ctrl-dir resources/nlloc/ctrl \
      --estaciones run_working_v8/params/estaciones_107.txt \
      --band N18-26_1.5k --band N22-30_1.5k --band N26-34_1.5k \
      --band N30-38_1.5k --band N34-42_1.5k --band N38-46_1.5k \
      --grid2time ew_gui_tools/nlloc_ring/nlloc/Grid2Time --run
  ```
  Las grillas de tiempos se calculan para **nuestras** estaciones (no las de Potin).

  **Cobertura de estaciones:** `Grid2Time` falla con `Source point is not inside
  model grid` para estaciones fuera del bbox del modelo, y NLLoc luego las
  descarta (warning). Ocurre en los **bordes de banda**: p. ej. N34-42 pierde 14
  de 34 (MT01…MT19, LMEL, VA05 están a lat ≈ −33.5, justo al norte del modelo;
  LL01/LL06 a lat ≈ −42.3). Es una limitación del modelo de Potin, no del
  módulo. Los eventos afectados se refinan igual, con menos estaciones.
- **`hyp2000_ring` (1D derivado)**: HYPOINVERSE solo admite 1D por capas, así que el
  3D se resume promediando horizontalmente cada banda:
  ```
  python3 ew_gui_tools/hyp2000_ring/mk_crh_from_3d.py \
      --mod /mnt/d/nll/time/N22-30_1.5k/N22-30_1.5k.P.mod --out N22-30_1.5k.crh
  ```
  El `.hyp` carga `CRH 1..6` y regionaliza con `MUL T 3` + `NOD` (el modelo se elige
  por **epicentro**, `hytra.for`). Convenio de signos: **lat negativa al sur, lon
  positiva al oeste** (`hyphs.for:299-300`). `MUL` se lee como *logical*: usar `T`,
  no `1`. Máximo 20 capas por modelo (`NLYR=20`).

## Volumen de búsqueda y fallback

Si la caja de búsqueda (`LOCGRID`) es pequeña o está mal centrada, NLLoc devuelve la
solución **pegada al borde** (caso real: `lon=-67.796`, `z=148.83` repetidos). Con las
bandas 3D el `LOCGRID` es el de la banda y cabe en su grilla de tiempos
(`NLLocLib.c:1904`). Para el fallback 1D (`GRID2D`) el `LOCGRID` debe caber en el
alcance de distancias de la grilla de tiempos (`NLLocLib.c:1949 IsGrid2DBigEnough`),
así que **no** puede ser enorme y un evento lejos del origen puede quedar en el borde.
Claves globales `TransOrigin <lat> <lon>` y
`LocGrid <nx> <ny> <nz> <x0> <y0> <z0> <dx> <dy> <dz>` reescriben `TRANS`/`LOCGRID`
de la plantilla **solo cuando el modelo elegido no es una banda** (si no, pisarían la
banda). Sin `ModelFallback`, los eventos fuera de banda no se refinan.

## Robustez del motor (crash de HYPOINVERSE / NLLoc)

El motor se invoca en un **proceso hijo** (`fork`): HYPOINVERSE puede llamar
`exit()`/`abort()` con eventos degenerados (p. ej. un ARC con solo 3 lecturas →
`hyphs.for:222`, `Sequential READ ... after EOF marker`). Con el hijo aislado el
módulo **no muere**: registra `...: LOC termino anormalmente ...; evento
descartado` y continúa con el siguiente. Además, antes de cada llamada se vacía el
ARC de salida, de modo que si el motor no lo reescribe no se reutiliza el
resultado del evento anterior (y `hyp2000_ring` añade una línea en blanco al final
de `arcIn`).

## Pendiente

- Magnitudes: `csnmags` lee `HYPO_RING` (crudo), así que las columnas Ml/Mwp
  quedarán vacías para los eventos refinados salvo que se apunte también a
  `HYPO_RING_REF`.
- `csnrv`/`csnstaevdisp` siguen viendo el crudo de `HYPO_RING`.
