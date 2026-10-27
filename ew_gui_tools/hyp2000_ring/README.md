# hyp2000_ring

Módulo EarthWorm **de anillo** que refina hipocentros con **HYPOINVERSE-2000**.
A diferencia de `hyp2000_mgr` (que recibe los ARC por *pipe*, dentro de la cadena
*sausage* `eqassemble → eqbuf → eqcoda → eqverify`), este módulo:

- **lee** la semilla `TYPE_HYP2000ARC` de `HYPO_RING` (1.ª solución de `csnloc`);
- **lee** todos los `TYPE_PICK_SCNL` de `PICK_RING` y, si `live_usage=1`, asocia
  cada P/S al hipocentro del seed por **tiempo de viaje** (`t0 + tt`) y rehace el
  ARC solo con las picadas asociadas, cada `time_recalc_live`;
- **escribe** el ARC refinado en `HYPO_RING_REF`;
- **no** usa `pipe`, `eqassemble`, `eqbuf`, `eqcoda` ni `eqverify`.

El seed aporta **solo el hipocentro** (t0/lat/lon/depth); las fases viejas del
seed **no** se copian. La ventana de asociación crece con la distancia y la
profundidad (`LivePickResidualSecP/S`), así que un evento profundo con P-S > 60 s
no pierde la P. La salida se valida (`rms/nph/gap`) antes de publicar: una
solución con `rms=99.99` no se emite ni contamina el seed. Tras `N_retry`
intentos sin picadas nuevas o sin solución válida, deja de localizar el evento.

```
HYPO_RING (semilla) ──┐
PICK_RING (picks) ────┼─► hyp2000_ring ─► HYPO_RING_REF (ARC refinado)
HYPO_RING_REF2 ───────┘        │
                               └─ hypoinverse (librería enlazada)
```

`HYPO_RING_REF2` aporta la solución confirmada/preferida. Si no hay picks
nuevos, tras `N_retry` intentos deja de localizar ese evento.

## Compilar

```bash
source ./ew8_unix.sh
make -C ew_gui_tools/hyp2000_ring        # compila hypoinverse/ y el módulo
```

El Makefile copia el binario a `$(EW_HOME)/$(EW_VERSION)/bin/`.

## Motor vendorizado

`hypoinverse/` contiene HYPOINVERSE-2000 (Klein) tomado de EarthWorm
(`src/seismic_processing/hyp2000` + `hyp2000_mgr/hypoinv.for` y
`hypoinv_wrapper.f90`). Se compila como `libhypoinverse.a` con `gfortran` y se
enlaza con `hypoinv_wrapper(const char*, int*)`.

**Puertos Linux** (los originales eran VMS/Solaris):

| Fichero | Cambio |
|---|---|
| `hytime.for` | `DATE`/`TIME` (VMS) → `DATE_AND_TIME` |
| `hydelt.for` | `LIB$DELETE_FILE` → no-op (solo uso interactivo) |
| `hyedit.for` | `EDT$EDIT`/`SPAWN` → no-op (solo uso interactivo) |

Se excluyen del build los `.f` duplicados del vendorizado original.

## Configuración

`hyp2000_ring.d`:

| Clave | Descripción |
|---|---|
| `MyModuleId` | ID en `earthworm.d` (`MOD_HYP2000_RING`) |
| `InRing` / `OutRing` | `HYPO_RING` / `HYPO_RING_REF` |
| `InPickRing` | `PICK_RING`: solo se usa si `live_usage=1` |
| `HeartBeatInt`, `LogFile`, `Debug`, `SourceCode` | como el resto de módulos |
| `CommandFile` | fichero `.hyp` de arranque de HYPOINVERSE |
| `WorkDir` | carpeta PROPIA del módulo (`../hyp2000_ring`): ahí escribe `arcIn`/`arcOut`; el módulo la crea si falta. Las entradas (`.hyp`/`.sta`/`.crh`) viven en `params/` y se referencian como `../params/...` |
| `GridFile` | `.grid` de `csnloc` (opcional): solo se refinan eventos en su bbox |
| `StaFile` | `estaciones_107.txt` (resuelto contra `$EW_PARAMS`): lat/lon para el tiempo de viaje |
| `TauTable` | `iasp91`: modelo tau (`<EW_PARAMS>/<modelo>.tbl/.hed`) |
| `LivePickLookbackSec` | máximo `t_pick - t0` que se mira hacia adelante (s); el buffer se purga con esto + 60 s |
| `LivePickResidualSecP` / `LivePickResidualSecS` | tolerancia del residual de asociación P/S (s) |
| `LiveMaxIter` | pasadas de refinamiento por corrida: si la salida no es válida se poda el ARC con los residuales del motor y se reintenta (0 = 1 pasada) |
| `LiveEventTTLSec` | ventana para **reabrir** un evento detenido por `N_retry` cuando llega una picada nueva (s); 0 = sin tope |
| `MinPhases`, `MaxRMS`, `MaxGapDeg` | filtros de calidad de la **salida** (`0` = sin filtro): `nph`, `rms`, `gap`. `MinPhases 5` = 1 grado de libertad |
| `FixedDepthKm` | profundidad **fija** (km) de la corrida; `<0` (default) = libre. El módulo emite `ZTR <z> T` después del `.hyp`, así el motor no mueve `z` (`DEPFIXA`). La GUI la pasa por corrida en una copia temporal del `.d`; en modo anillo solo se activa si se escribe en el `.d` |

`hyp2000_ring.hyp` (arranque de HYPOINVERSE): modelo + estaciones + `ZTR` + `DAM`.
El módulo ya fija por evento `PHS`/`ARC`/`SUM`/`COP 5`/`CAR 3`.

- Modelo: `CRH n '<modelo>.crh'` — formato `VEL D` en `(2F5.2)`, V creciente,
  máx. 20 capas (`NLYR=20`).
- **Modelo 1D derivado del 3D** (`mk_crh_from_3d.py`): resume cada banda del modelo
  de Potin promediando `Vp(z)` horizontalmente y escribe un `.crh`. El `.hyp` carga
  las 6 bandas (`CRH 1..6`) y regionaliza con `MUL T 3` + `NOD`; el modelo se elige
  por **epicentro** (`hytra.for`). Convenio de signos: **lat negativa al sur, lon
  positiva al oeste** (`hyphs.for:299-300`); `MUL` se lee como *logical* → `T`, no `1`.
- Estaciones: `STA '<modelo>.sta'` — ancho fijo 82 col. Se genera con
  `python3 mk_sta.py estaciones_107.txt estaciones_hyp.sta`.
- `DAM ... 800.` sube `D2FAR` (default 250 km) para redes de cobertura amplia.

## Probar sin anillos (modo offline del módulo)

```bash
# <workdir> debe tener hyp2000_ring.hyp/.sta/.crh
hyp2000_ring <ruta>/hyp2000_ring.d <arc_entrada> > out.arc
```

Refina y escribe el ARC a stdout, **normalizado** (header 197 / fases 114) y con
el `qid` y la versión de `csnloc` **copiados** al header de salida (para que
`csnhypodbp` agrupe el refinado con el mismo evento). El motor HYPOINVERSE imprime
además su resumen por stdout, así que el ARC queda al final. La salida también se
valida (`rms/nph/gap`): si no pasa, el módulo sale con código 2 y no escribe ARC.

## Log de resultados

Cada refinamiento escribe una línea en `EW_LOG/hyp2000_ring_*.log`:

```
hyp2000_ring: LOC ev=<qid> v=<ver> in{nph=.. lat=.. lon=.. z=.. rms=..} out{nph=.. lat=.. lon=.. z=.. rms=.. gap=..} dt_ms=..
```

## Estado

- Motor: **validado** (137 estaciones). Con el 1D de Husen1999 (`chile_1d.crh`) el
  evento 1382500027 daba `z=12.97 / rms=0.69`; con el 1D derivado del 3D (banda
  N18-26) da `z=3.72 / rms=0.88`. Las 6 bandas + `MUL`/`NOD` cargan sin error.
- Modo offline del módulo: **validado** (ARC normalizado, `qid`/versión copiados, línea `LOC`).
- Integración por anillo: escribe en `HYPO_RING_REF`, el **mismo** anillo que
  `nlloc_ring` (`csnhypodbp` los distingue por módulo origen).
