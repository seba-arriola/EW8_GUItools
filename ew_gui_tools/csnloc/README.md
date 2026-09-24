# csnloc — Asociación de fases + localización rápida

Módulo Earthworm nativo (C) que reemplaza **localmente** el camino
`ew2glass → GLASS3 → glass2ew` para asociación de fases y primera
localización.

- **Entrada:** `PICK_RING`, mensajes `TYPE_PICK_SCNL` (tipo 8).
- **Salida:** `HYPO_RING`, mensajes `TYPE_HYP2000ARC` con el **mismo layout
  que `glass2ew`**, de modo que `csnhypodbp`, `csnrv` y `csnmags_toy` lo
  consumen sin cambios.
- **Config:** `run_working_v8/params/csnloc.d`.

## Algoritmo

```
PICK_RING ─► buffer de picks (re-pick reciente reemplaza)
          ─► back-projection 4D sobre grilla (lat, lon, depth, t0)
          ─► DBSCAN sobre nucleaciones (separa eventos)
          ─► asignación exclusiva de fases y refinamiento local
          ─► TYPE_HYP2000ARC ─► HYPO_RING
```

1. **Back-projection**: para cada nodo de la grilla y cada pick se calcula el
   tiempo de viaje esperado (taulib/IASP91) y el origen implícito
   `t0 = t_pick − t_tt`. Se apilan los `t0` en un histograma y el máximo
   (con ventana de tolerancia `T0ToleranceSec`) es el *stack* del nodo.
2. **Nucleación**: máximos locales del stack por encima del umbral.
3. **DBSCAN**: agrupa nucleaciones en el espacio `(x, y, z, v·t0)`; cada
   grupo es una hipótesis. El representante es la nucleación de mayor score.
4. **Asignación exclusiva**: las hipótesis se procesan por score
   descendente y reclaman picks libres dentro de `res_tol`, respetando
   unicidad *estación+fase* (una estación no aporta dos veces la misma fase
   al mismo evento). Así, dos sismos que comparten estaciones no se roban
   fases y los fragmentos espurios de una misma cresta caen bajo
   `MinPhasesPerEvent`.
5. **Refinamiento**: búsqueda local en caja fina alrededor de cada
   hipótesis (`RefineNodeKm`, `RefineIterations`), recalculando residuales,
   RMS y gap azimutal.

### Threading

`taulib` (IASP91) usa estado global y no es reentrante. Por eso la tabla de
tiempos de viaje se **precomputa una sola vez** en el hilo principal
(`ttmodel.c`) y los workers solo interpolan (lectura pura). El
back-projection reparte los nodos de la grilla entre `NumThreads` hilos sin
condiciones de carrera, por lo que el resultado es **determinista**
(verificado con 1 y N hilos).

### Reporte de eventos

Cada evento se publica como `TYPE_HYP2000ARC` en `HYPO_RING` y, además, deja
una línea resumen en el log del módulo (`$EW_LOG/csnloc_YYYYMMDD.log`):

```
csnloc: evento 7 lat=-23.303 lon=-70.065 z=22.5 km nph=22 rms=1.39 gap=180
```

Con `DumpHypo 1` se vuelca también el mensaje `HYP2000ARC` completo (crudo,
multilínea) al log, delimitado por `--- HYP2000ARC evento N ---` /
`--- fin HYP2000ARC evento N ---`. Es la forma recomendada de inspeccionar la
salida durante testeo manual, sin necesidad de `sniffring`.

## Grillas anidadas

La búsqueda corre sobre varias grillas de distinta resolución declaradas en
`csnloc.d` (claves repetibles `GlobalGrid`, `RegionalGrid`, `LocalGrid`, de
gruesa a fina). Cada grilla vive en su propio archivo `.grid`:

```
# run_working_v8/params/grids/tarapaca.grid
Name                Tarapaca
LatMin              -21.653
LatMax              -16.347
LonMin              -71.593
LonMax              -67.408
NodeKm              10.0
DepthLayers         5,10,20,30,40,50,60,70,80,90,100,110,120,130,140,150,160,170,180,190,200,225,250,275,300
# (alternativa homogénea: DepthMin/DepthMax/DepthStep)
StaMaxDistKm        1000     # radio nodo<->estación para el precómputo (0 = todas)
NumStationsPerNode  25       # tope de estaciones por nodo (0 = sin tope)
MinPhases           4        # override opcional de MinPhasesPerEvent
BackProjThreshold   0.0      # override opcional
MaxNodes            200000   # tope de recursos por grilla
```

Comportamiento:

- **Precómputo al arrancar**: por cada grilla se construyen los ejes y, por
  nodo, la lista CSR de estaciones asociadas (radio `StaMaxDistKm`, tope
  `NumStationsPerNode`). La back-projection recorre solo esas estaciones.
- **Activación**: las grillas gruesas (global/regional) se buscan siempre; una
  local se activa si (a) hay al menos `GridActivationMinPicks` picks de sus
  estaciones asociadas, o (b) un evento de un nivel más grueso cae dentro de su
  bbox + `GridActivationMarginDeg`.
- **Ensamblado por grilla + dedup**: cada grilla activa nuclea y ensambla por
  separado (las nucleaciones de resoluciones distintas no son comparables);
  luego los eventos se deduplican por `EventDedupSec`/`EventDedupKm`
  priorizando más fases y la grilla más fina.
- El JSON del modo offline incluye `grid_level` (0 = global, 1 = regional,
  2 = local).

Grillas de producción en `run_working_v8/params/grids/`: `global.grid` (100 km),
`chile_regional.grid` (25 km, cobertura Chile) y las 10 locales réplica de
Glass3 (10 km). `csnloc_legacy.d` + `grids/legacy_chile.grid` reproducen la
configuración anterior de grilla única (regresión).

## Parámetros de `csnloc.d`

| Clave | Default | Descripción |
|---|---|---|
| `MyModuleId` | `MOD_CSNLOC` | ID en `earthworm.d` |
| `InRing` / `OutRing` | `PICK_RING` / `HYPO_RING` | Anillos de entrada/salida |
| `HeartBeatInt` | 30 | Heartbeat (s) |
| `LogFile` | 1 | 0=consola, 1=disco+consola, 2=disco |
| `Debug` | 0 | Verbosidad (loguea picks malformados) |
| `DumpHypo` | 0 | 1 = vuelca el `HYP2000ARC` crudo completo al log |
| `StaFile` | `estaciones_107.txt` | Metadata de estaciones |
| `TauTable` | `iasp91` | Modelo tau (`<modelo>.tbl/.hed` en `$EW_PARAMS`) |
| `GlobalGrid` / `RegionalGrid` / `LocalGrid` | (ninguna) | Archivo `.grid` de una grilla del nivel indicado (clave **repetible**; de gruesa a fina) |
| `GridActivationMinPicks` | 3 | Picks de estaciones asociadas para activar una grilla local |
| `GridActivationMarginDeg` | 1.0 | Margen (grados) para activar una local por cercanía a un evento más grueso |
| `EventDedupSec` | 30.0 | Dedup de eventos entre grillas (s) |
| `EventDedupKm` | 100.0 | Dedup de eventos entre grillas (km) |
| `AssocWindowSec` | 120.0 | Ventana temporal de picks |
| `RePickWindowSec` | 10.0 | Re-pick del mismo SCNL reemplaza al anterior |
| `PickTTLSec` | 300.0 | Expiración de picks |
| `T0ToleranceSec` | 2.0 | Tolerancia del stacking de `t0` (s) |
| `DBSCAN_Eps` | 60.0 | Epsilon de clustering (`(km, km, km, v·s)`) |
| `DBSCAN_MinPts` | 3 | (reservado; se usa 1 en el agrupado de nucleaciones) |
| `BackProjThreshold` | 0.0 | 0 = automático (50 % del máximo); si ∈(0,1], fracción del máximo |
| `MaxEventsPerWindow` | 5 | Máximo de eventos emitidos por ventana |
| `MinPhasesPerEvent` | 3 | Fases mínimas para declarar un evento |
| `MaxRMS` | 2.0 | RMS máximo aceptado (s) |
| `PhaseWeightP` / `PhaseWeightS` | 1.0 / 0.8 | Pesos por fase |
| `NumThreads` | 4 | Hilos de back-projection |
| `RefineIterations` | 3 | Iteraciones del refinamiento |
| `RefineNodeKm` | 5.0 | Paso inicial de la caja de refinamiento (km) |
| `AgencyID` / `Author` | `CL` / `csnloc` | Metadatos de salida |
| `EventTTLSec` | 300.0 | (reservado para dinámica de eventos) |

## Compilación y tests

```bash
source ./ew8_unix.sh
make -C ew_gui_tools/csnloc            # compila e instala en $EW_HOME/$EW_VERSION/bin
make -C ew_gui_tools/csnloc test       # compila los tests standalone
make -C ew_gui_tools/csnloc check      # compila y corre toda la batería
make -C ew_gui_tools/csnloc repeat     # 5 corridas para verificar determinismo
```

El módulo es **autocontenido**: `iasplib.h`, `ttlim.h`, `iasp91.tbl` e
`iasp91.hed` viven en este directorio (no depende de `ew_gui_tools/libsrc`).
En runtime las tablas deben estar en el cwd del proceso (`$EW_PARAMS`; hay
copias en `run_working_v8/params/`).

### Cobertura de los tests

| Test | Criterio |
|---|---|
| `test_pick_parse` | Parseo de `TYPE_PICK_SCNL` |
| `test_repick` | Inserción, re-pick y TTL del buffer |
| `test_dbscan` | DBSCAN separa dos grupos + ruido |
| `test_hypo_format` | Layout `HYP2000ARC` idéntico a `glass2ew` |
| `test_tt` | Tabla de tiempos de viaje P/S (IASP91) |
| `test_synth_locate` | Localización sintética (CA2) y determinismo (CA6) |
| `test_two_events` | Dos sismos simultáneos independientes (CA3) |
| `test_grid_parse` | Parser de `.grid` (profundidad no homogénea, validaciones) |
| `test_gridset_load` | Carga del conjunto de grillas anidadas |
| `test_precompute_equiv` | Precómputo CSR ≡ camino sin CSR |
| `test_grid_activation` | Activación de grillas por picks |
| `test_nested_dedup` | Dos grillas del mismo sismo ⇒ un evento |

## Nota sobre `taulib_csnloc.c`

`taulib_csnloc.c` es una copia local del motor IASP91 (originalmente
`libsrc/taulib.c`, de linaje WC/ATWC) con dos parches *null-safe* en `findtt`
(líneas del `strstr("ab")` y `strchr(' ')`). Sin ellos, la resta de punteros
truncada a `int` en 64 bits genera índices inválidos y corrompe memoria
(segfault no determinista). Es autocontenido: las definiciones que antes
tomaba de `earlybirdlib.h` están en `taulib_compat.h`, y `iasplib.h`/`ttlim.h`
están vendorizados aquí.

## Coexistencia con GLASS3

En `startstop_unix.d` las entradas de `ew2glass`/`glass2ew` quedaron
comentadas y se agregó `csnloc`. Para volver al flujo GLASS3, descomentar
aquellas y comentar la de `csnloc`. No se borró código.

## Pendiente (fase 6, no implementada)

- Re-localización dinámica al ingresar picks nuevos y reemisión de eventos
  activos (`EventTTLSec`).
