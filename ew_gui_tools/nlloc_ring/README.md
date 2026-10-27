# nlloc_ring

Módulo EarthWorm **de anillo** que refina hipocentros con **NonLinLoc (NLLoc)**.
Lee los `TYPE_HYP2000ARC` de `HYPO_RING` (salida de `csnloc`), los convierte a
observaciones `NLLOC_OBS`, llama a `NLLoc()` **enlazado como librería**, y
escribe el ARC refinado en `HYPO_RING_REF` (el **mismo** anillo que
`hyp2000_ring`; `csnhypodbp` los distingue por módulo origen). **No** usa la
cadena *sausage*.

Con `live_usage=0` (valor por defecto en `csnhypodbpqt.d`) el proceso vivo queda
inactivo: se usa **sólo** por el `locate` manual/offline de la GUI. Para
activarlo en tiempo real habría que implementar el mismo modo de picks que
`hyp2000_ring`; hoy no se hace.

```
HYPO_RING (ARC de csnloc) ──► nlloc_ring ──► HYPO_RING_REF (ARC refinado)
                                   │
                                   └─ NLLoc() (librería enlazada, GPL-3.0)
```

## Compilar

```bash
source ./ew8_unix.sh
make -C ew_gui_tools/nlloc_ring      # compila nlloc/ (libnlloc.a) y el módulo
```

## Motor vendorizado

`nlloc/` contiene NonLinLoc (github.com/ut-beg-texnet/NonLinLoc, **GPL-3.0**) y
se compila como `libnlloc.a` a partir de los mismos objetos que usa
`NLLoc_func_test` en su CMake: `GRID_LIB_OBJS` + `NLLOC_LIB_OBJS` + `NLLoc1.c`.

- `Time_3d_NLL_multiSource.c` se compila con `-DNO_IEEE_PROTOCOL` (como el
  CMake original; si no, referencia la función Fortran `nint`).
- `Vel2Grid` y `Grid2Time` se construyen con `make -C nlloc grids` y se usan
  **una vez** para precalcular las grillas (no en runtime).

## Configuración

`nlloc_ring.d`:

| Clave | Descripción |
|---|---|
| `MyModuleId` | `MOD_NLLOC_RING` (en `earthworm.d`) |
| `InRing` / `OutRing` | `HYPO_RING` / `HYPO_RING_REF` |
| `HeartBeatInt`, `LogFile`, `Debug`, `SourceCode` | como el resto de módulos |
| `ControlFile` | plantilla de control de NLLoc (el módulo reemplaza su `LOCFILES`) |
| `TtimeRoot` | raíz de las grillas de tiempos |
| `OutRoot` | raíz de salida de NLLoc |
| `WorkDir` | carpeta PROPIA del módulo (`../nlloc_ring`): ahí van `obs.nll`, `nll_ring.in` y `<OutRoot>.*` (crea `loc/` si falta). Las grillas de tiempos y las plantillas de control se referencian por ruta ABSOLUTA en `ModelBand`/`TtimeRoot`/`ControlFile` (sólo lectura) |
| `GridFile` | `.grid` de `csnloc` (opcional): solo refina eventos en su bbox |
| `MinPhases`, `MaxRMS`, `MaxGapDeg` | filtros de calidad de entrada **y** de la salida: una solución con `rms=99.99` (centinela de NLLoc), `nph < MinPhases` o `gap > MaxGapDeg` no se publica |
| `FixedDepthKm` | profundidad **fija** (km); `<0` (default) = libre. NLLoc no la soporta nativa: se emula con `LOCGRID` de 2 nodos en z centrados en el valor y `LOCSEARCH OCT … init_num_cells_z=1` (receta oficial). La solución queda dentro de ±`FixDepthSpacingKm`/2, **no** en un valor exacto |
| `FixDepthSpacingKm` | separación entre los 2 nodos z de la emulación (km, default 0.1 ⇒ ±0.05 km). `<=0` se sustituye por 0.1 con aviso |
| `NllIntervalSec` | rate-limit del modo vivo (no usado con `live_usage=0`; `0` = cada versión) |
| `NllTTLSec` | olvida un evento sin versiones nuevas tras N s |
| `NllMaxPending` | máximo de eventos pendientes en la tabla |

La plantilla de control debe traer `LOCHYPOUT SAVE_HYPOINVERSE_Y2000_ARC` y
`LOCMETH ... 1.78` (VpVsRatio: NLLoc deriva S de las grillas P).

## Grillas (una vez)

```bash
python3 ew_gui_tools/nlloc_ring/mk_nll_grids.py <arc|--sta ...> \
    run_working_v8/params/estaciones_107.txt <outdir> <lat_c> <lon_c> \
    [--dmax KM] [--zmax KM]
make -C ew_gui_tools/nlloc_ring/nlloc grids
<nlloc>/Vel2Grid <outdir>/vel2grid.in
<nlloc>/Grid2Time <outdir>/grid2time.in
```

## Modelos 3D de B. Potin (bandas)

`mk_nll_grids_3d.py` genera, por banda de latitud, un `grid2time.in` que toma el
**modelo 3D ya existente** de Potin (`<banda>.P.mod`/`.S.mod`, formato NLLoc
`SLOW_LEN`) y calcula las grillas de tiempo para **nuestras** estaciones
(`GTMODE GRID3D ANGLES_NO`), más un `nlloc.in` con el `TRANS`/`LOCGRID` de la banda:

```bash
python3 mk_nll_grids_3d.py --mod-dir /mnt/d/nll/time \
    --outdir <workdir>/time3d --ctrl-dir <workdir>/ctrl \
    --estaciones ../../run_working_v8/params/estaciones_107.txt \
    --band N18-26_1.5k --band N22-30_1.5k --band N26-34_1.5k \
    --band N30-38_1.5k --band N34-42_1.5k --band N38-46_1.5k \
    --grid2time nlloc/Grid2Time --run
```

`nlloc_ring.d` declara una `ModelBand <nombre> <latmin> <latmax> <lonmin> <lonmax>
<TtimeRoot> <ControlFile>` por banda; el módulo elige la que contiene el hipocentro
de entrada y, si ninguna, usa `ModelFallback`.

Puntos verificados (importantes):

- **Genera P y S** (dos pasadas de `Grid2Time`: `grid2time.in` y `grid2time_S.in`).
  Con el modelo 1D antiguo solo se generaba P y NLLoc derivaba S con `VpVsRatio`.
- **`LOCGRID` con origen explícito** (no `-1.0e30`): el chequeo previo
  `IsGrid2DBigEnough` usa el centro del grid; con el origen "auto" calcula una
  distancia absurda y descarta todas las fases (`istat=-2`). Con grillas 3D, en
  cambio, `LOCGRID` debe estar **dentro** de la grilla de tiempos
  (`NLLocLib.c:1904`); el `LOCGRID` de cada banda sale de su cabecera.
- **`--dmax`** debe cubrir la distancia estación-evento (p. ej. 600 km para
  redes regionales) en el modo 1D.

## Rate-limit por evento

Con `live_usage=0` (la tabla actual) este camino **no se usa**: el módulo no
procesa `HYPO_RING` en vivo y se invoca solo por el `locate` manual/offline.
El código conserva la tabla de pendientes y `NllIntervalSec`/`NllTTLSec` para
una eventual habilitación de `live_usage=1`, que **no está implementada**: si se
pusiera en 1, el módulo avisa y fuerza 0. En ese modo hipotético guardaría el
último ARC por `qid` y relanzaría NLLoc como mucho cada `NllIntervalSec`.

## Log de resultados

Cada refinamiento escribe una línea en `EW_LOG/nlloc_ring_*.log`:

```
nlloc_ring: LOC ev=<qid> v=<ver> in{nph=.. lat=.. lon=.. z=.. rms=..} out{nph=.. lat=.. lon=.. z=.. rms=.. gap=..} dt_ms=..
```

## Probar sin anillos

```bash
cd <workdir>
nlloc_ring <ruta>/nlloc_ring.d <arc_entrada> > out.arc   # ARC refinado a stdout
```

NLLoc imprime su traza por stdout, así que el motor corre con su stdout redirigido a
`/dev/null` (`freopen`) y por stdout sale **solo** el ARC (normalizado a header 197 /
fases 114, con el `qid` y la versión de `csnloc` copiados).

**Longitud**: NLLoc escribe `'E'` para el este y **blanco** para el oeste
(`NLLocLib.c:12965`), mientras que `csnloc`/`csnhypodbp` usan `'W'`. El módulo lo
normaliza al reescribir el header.

**SCNL de las fases**: `arc_rewrite_id` copia además **red, COMP3 y LOC de cada
línea de fase del ARC de entrada**. NLLoc escribe la línea de fase como
`comp`(1 letra)+`inst`(3) y deja `inst` en blanco (el OBS sólo lleva el canal en
el campo COMP: `NLLocLib.c:13171-13177`), así que su ARC salía **sin red y sin
canal**; `csnhypodbp` y HYPOINVERSE emparejan las fases por `STA+NET+COMP3+LOC`
(`LET 5 2 3 0 0`) y con `--`/blancos las rechazan **todas**. El OBS también
escribe `?` cuando el canal viene vacío, para no quedarse en 13 campos.

## Estado

- `libnlloc.a` **validada** (el demo `NLLoc_func_test` localiza eventos).
- `nlloc_ring` **validado offline**: un ARC real de `csnloc` se refina y produce
  un ARC Y2K normalizado, con `qid`/versión copiados y longitud corregida.
- Rate-limit y log de resultados implementados.
- **Modelos 3D de Potin integrados**: `mk_nll_grids_3d.py` + `ModelBand` +
  `ModelFallback`; verificado que un evento del norte (banda N18-26_1.5k) da una
  solución **interior** (`z=130.83`, `rms=0.15`) en vez del borde de la caja 1D.
- Integración por anillo: escribe en `HYPO_RING_REF` (el mismo que `hyp2000_ring`).
